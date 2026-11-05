#include "NestedArchives.h"
#include "MediaOrganizer.h"
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
QString VolumeGroup(const QString& name) {
    static const QRegularExpression numeric("^(.+)\\.([0-9]{3,6})$");
    static const QRegularExpression part("^(.+)\\.part[0-9]{1,6}(\\.[^.]+)?$");
    static const QRegularExpression rar("^(.+)\\.(?:rar|r[0-9]{2,3})$");
    static const QRegularExpression zip("^(.+)\\.(?:zip|z[0-9]{2,3})$");
    for (const auto& entry : {std::pair{&numeric, "numeric:"}, {&part, "part:"}, {&rar, "rar:"}, {&zip, "zip:"}}) {
        const auto match = entry.first->match(name);
        if (match.hasMatch()) return QString::fromLatin1(entry.second) + match.captured(1);
    }
    return {};
}
bool ArchiveName(const QString& name) {
    static const QRegularExpression archive("\\.(?:7z|zip|rar|tar|gz|bz2|xz|cab|iso|tgz|tbz2)(?:\\.[0-9]{3,6})?$|\\.[rz][0-9]{2,3}$|\\.part[0-9]{1,6}(?:\\.[^.]+)?$");
    return archive.match(name).hasMatch();
}
bool FinishedZipContainer(const QFileInfo& file, const std::wstring& signature) {
    // These formats use ZIP internally but are final documents or installable packages.
    static const QSet<QString> extensions{
        "apk", "aab", "apks", "xapk", "ipa", "appx", "msix", "appxbundle", "msixbundle",
        "jar", "war", "ear", "vsix", "nupkg", "whl", "epub", "cbz",
        "docx", "docm", "dotx", "dotm", "xlsx", "xlsm", "xlsb", "xltx", "xltm",
        "pptx", "pptm", "potx", "potm", "ppsx", "ppsm", "odt", "ods", "odp", "odg"};
    return signature == L"zip" && extensions.contains(file.suffix().toLower());
}
}

QString ArchiveVolumeGroup(const QString& filename) { return VolumeGroup(filename.toLower()); }

NestedArchiveScan FindNestedArchives(const QStringList& files, ProgressCallback progress, const QStringList& relatedFiles, bool probePdfs) {
    NestedArchiveScan result;
    QMap<QString, QStringList> groups;
    QSet<QString> candidates;
    std::vector<std::wstring> context;
    for (const auto& file : files + relatedFiles) context.push_back(file.toStdWString());
    for (const auto& path : files) {
        if (progress && !progress(0, 0)) { result.cancelled = true; return result; }
        const QFileInfo info(path);
        if (!info.isFile() || info.isSymLink()) continue;
        const auto name = info.fileName().toLower();
        const auto signature = DetectArchiveSignature(path.toStdWString());
        const auto group = VolumeGroup(name);
        if (FinishedZipContainer(info, signature)) {
            if (!group.startsWith("part:")) { result.ignored.append(path); continue; }
        }
        if (!group.isEmpty()) {
            groups[group].append(path);
            if (!signature.empty() || ArchiveName(name)) candidates.insert(group);
        } else if (!signature.empty() || ArchiveName(name)) {
            result.archives.push_back(InspectArchiveFiles(path.toStdWString(), context));
        } else if (probePdfs && info.suffix().compare("pdf", Qt::CaseInsensitive) == 0) {
            ArchivePlan probe; probe.input = probe.firstVolume = path.toStdWString(); probe.volumes.push_back(probe.input);
            probe.format = L"probe"; probe.probePdf = true; probe.size = static_cast<uint64_t>(info.size()); result.archives.push_back(std::move(probe));
        } else {
            const auto media = DetectMediaFile(path);
            if (media.kind == MediaKind::Video) result.videos.append(path);
            if (media.kind == MediaKind::Image) result.images.append(path);
            if (media.kind == MediaKind::None) result.ignored.append(path);
        }
    }
    for (auto group = groups.cbegin(); group != groups.cend(); ++group) {
        if (progress && !progress(0, 0)) { result.cancelled = true; return result; }
        if (!candidates.contains(group.key())) {
            for (const auto& path : group.value()) {
                const auto media = DetectMediaFile(path);
                if (media.kind == MediaKind::Video) result.videos.append(path);
                if (media.kind == MediaKind::Image) result.images.append(path);
                if (media.kind == MediaKind::None) result.ignored.append(path);
            }
            continue;
        }
        const auto inspectBundle = [&](const std::vector<std::wstring>& bundle) {
            auto plan = InspectArchiveFiles(bundle.front(), bundle);
            if (plan.format == L"zip" && !plan.concatenated) {
                auto native = InspectArchiveFiles(plan.firstVolume, context);
                if (!native.volumeName.empty()) { native.input = plan.input; plan = std::move(native); }
            }
            result.archives.push_back(std::move(plan));
        };
        if (group.key().startsWith("part:")) {
            std::vector<std::wstring> paths;
            for (const auto& path : group.value()) paths.push_back(path.toStdWString());
            for (const auto& bundle : GroupNamedArchiveParts(paths)) inspectBundle(bundle);
            continue;
        }
        bool duplicate = false; QSet<QString> names;
        for (const auto& path : group.value()) {
            const auto name = QFileInfo(path).fileName().toLower();
            if (names.contains(name)) duplicate = true;
            names.insert(name);
        }
        // Identical part names in separate bundles must never be combined.
        QMap<QString, std::vector<std::wstring>> bundles;
        for (const auto& path : group.value()) bundles[duplicate ? QFileInfo(path).absolutePath().toCaseFolded() : QString{}].push_back(path.toStdWString());
        for (const auto& bundle : bundles) inspectBundle(bundle);
    }
    QSet<QString> attached;
    auto key = [](const QString& path) { return QFileInfo(path).absoluteFilePath().toCaseFolded(); };
    for (const auto& plan : result.archives) if (plan.error.empty() && plan.format == L"zip" && !plan.volumeAliases.empty()) {
        for (const auto& part : plan.volumes) if (part != plan.firstVolume) attached.insert(key(QString::fromStdWString(part)));
    }
    std::erase_if(result.archives, [&](const auto& plan) { return attached.contains(key(QString::fromStdWString(plan.input))); });
    for (auto* files : {&result.videos, &result.images, &result.ignored}) {
        files->removeIf([&](const auto& path) { return attached.contains(key(path)); });
    }
    return result;
}
