#include "ArchivePasswords.h"
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <QRegularExpression>

QList<PasswordList> ReadPasswordNotes(const QStringList& files) {
    QList<PasswordList> notes;
    static const QRegularExpression label(QStringLiteral("(?:解压密码|解压口令|密码|password|pwd)\\s*[:：=]\\s*(.+)$"), QRegularExpression::CaseInsensitiveOption);
    for (const auto& path : files) {
        const QFileInfo info(path); const auto name = info.completeBaseName().toLower();
        if (info.suffix().compare("txt", Qt::CaseInsensitive) != 0 || info.size() > 65536 ||
            !(name.contains(QStringLiteral("密码")) || name.contains("password") || name == "pwd")) continue;
        QString text, error; if (!PasswordLibrary::readTextFile(path, text, error)) continue;
        QStringList entries; QSet<QString> seen;
        for (auto line : text.split('\n')) {
            if (line.endsWith('\r')) line.chop(1);
            if (line.startsWith(QChar::ByteOrderMark)) line.remove(0, 1);
            const auto match = label.match(line);
            const auto candidate = match.hasMatch() ? match.captured(1).trimmed() : line;
            if (candidate.isEmpty() || candidate.size() > 1024 || candidate.contains(QChar::Null) || seen.contains(candidate)) continue;
            seen.insert(candidate); entries.append(candidate); if (entries.size() == 32) break;
        }
        if (!entries.isEmpty()) notes.append({"note:" + info.absoluteFilePath(), QStringLiteral("随包密码说明"), true, entries});
    }
    return notes;
}

QList<PasswordCandidate> BuildPasswordCandidates(const QString& manual, const QList<PasswordList>& lists,
                                               bool enabled, const QString& cached, const QString& recent) {
    QList<PasswordCandidate> result;
    QSet<QString> seen;
    QHash<QString, QString> sources;
    if (enabled) for (const auto& list : lists) if (list.enabled) for (const auto& entry : list.entries) {
        if (!sources.contains(entry)) sources.insert(entry, list.id.startsWith("note:") ? QStringLiteral("随包密码说明") : QStringLiteral("密码单「%1」").arg(list.name));
    }
    auto add = [&](const QString& value, const QString& source) {
        if (!value.isEmpty() && !seen.contains(value)) { seen.insert(value); result.append({value, source}); }
    };
    add(manual, QStringLiteral("手动密码"));
    if (sources.contains(cached)) add(cached, sources.value(cached));
    if (sources.contains(recent)) add(recent, sources.value(recent));
    if (enabled) for (const auto& list : lists) if (list.enabled) for (const auto& entry : list.entries) add(entry, sources.value(entry));
    return result;
}

ArchivePasswordResult ResolveArchivePassword(SevenZipCore& core, const std::wstring& path,
    const QList<PasswordCandidate>& candidates, ProgressCallback progress, std::function<void(int, int)> attempting) {
    return ResolveArchivePassword(core, InspectArchive(path), candidates, progress, attempting);
}

ArchivePasswordResult ResolveArchivePassword(SevenZipCore& core, const ArchivePlan& plan,
    const QList<PasswordCandidate>& candidates, ProgressCallback progress, std::function<void(int, int)> attempting) {
    ArchivePasswordResult result;
    auto alive = [&] { return !progress || progress(0, 0); };
    auto cancelled = [&] {
        core.CloseArchive(); result.state = ArchivePasswordResult::State::Cancelled;
        result.error = QStringLiteral("已取消"); return result;
    };
    if (!alive()) return cancelled();
    const bool opened = core.OpenArchive(plan, L"", progress);
    if (!alive()) return cancelled();
    if (!opened && !core.NeedsPassword()) {
        result.error = QString::fromStdWString(core.LastError()); return result;
    }
    if (opened && !core.NeedsPassword()) { result.state = ArchivePasswordResult::State::Ready; return result; }
    if (opened && core.TestArchive(L"", progress)) {
        result.state = ArchivePasswordResult::State::Ready; result.verified = true; return result;
    }
    for (const auto& candidate : candidates) {
        if (!alive()) return cancelled();
        ++result.attempts;
        if (attempting) attempting(result.attempts, static_cast<int>(candidates.size()));
        if (!core.OpenArchive(plan, candidate.value.toStdWString(), progress)) {
            if (!alive()) return cancelled();
            if (!core.NeedsPassword()) { result.error = QString::fromStdWString(core.LastError()); return result; }
            continue;
        }
        if (core.TestArchive(candidate.value.toStdWString(), progress)) {
            if (!alive()) return cancelled();
            result.state = ArchivePasswordResult::State::Ready;
            result.password = candidate.value; result.source = candidate.source; result.verified = true;
            return result;
        }
    }
    if (!alive()) return cancelled();
    core.CloseArchive(); result.state = ArchivePasswordResult::State::NoMatch;
    result.error = candidates.isEmpty() ? QStringLiteral("需要密码：请填写解压密码，或添加并启用密码单")
        : QStringLiteral("已尝试 %1 个密码，未找到可用密码（也可能是压缩包已损坏）").arg(result.attempts);
    return result;
}
