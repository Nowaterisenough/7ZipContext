#include "ArchiveController.h"
#include "WindowsPath.h"
#include "ArchivePasswords.h"
#include "NestedArchives.h"
#include "ArchiveCleanup.h"
#include "MediaOrganizer.h"
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <filesystem>
#include <deque>
#include <QFile>

namespace fs = std::filesystem;
namespace {
QString Q(const std::wstring& text) { return QString::fromStdWString(text); }
QString PreferencesFile() {
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/7ZipContext";
    QDir().mkpath(directory);
    return directory + "/desktop.ini";
}
QString ArchiveKey(const std::wstring& path) {
    return QDir::cleanPath(QFileInfo(Q(path)).absoluteFilePath()).toCaseFolded();
}
PasswordList& SavedPasswords(QList<PasswordList>& lists, bool enabled) {
    const QString base = enabled ? QStringLiteral("saved-passwords") : QStringLiteral("disabled-passwords");
    auto id = base;
    for (int suffix = 2;; ++suffix) {
        auto existing = std::find_if(lists.begin(), lists.end(), [&](const auto& list) { return list.id == id; });
        if (existing == lists.end()) {
            lists.append({id, QStringLiteral("密码列表"), enabled, {}, {}}); return lists.last();
        }
        if (existing->enabled == enabled) return *existing;
        id = base + '-' + QString::number(suffix);
    }
}
}

ArchiveController::ArchiveController(QObject *parent, const QString& passwordStoragePath) : QObject(parent), passwords_(passwordStoragePath), updates_(this) {
    QSettings preferences(PreferencesFile(), QSettings::IniFormat);
    // Earlier versions saved auto-hide as a default, including when unrelated settings changed.
    hideOnBlur = preferences.value("Window/TrayAutoHide", false).toBool();
    rememberGeometry = preferences.value("Window/RememberGeometry", true).toBool();
    autoPasswords = preferences.value("Passwords/AutoTry", true).toBool();
    autoNested = preferences.value("Extraction/AutoNested", true).toBool();
    organizeMedia = preferences.value("Extraction/OrganizeMedia", true).toBool();
    if (!passwords_.load(passwordError_)) status_ = passwordError_;
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, [this] {
        auto now = std::chrono::steady_clock::now();
        const auto seconds = std::chrono::duration<double>(now - sampledAt_).count();
        const auto done = completed_.load();
        speed_ = !paused_ && done >= lastBytes_ && seconds > 0 ? (done - lastBytes_) / seconds : 0;
        lastBytes_ = done; sampledAt_ = now;
        speeds_.append(speed_);
        if (speeds_.size() > 80) speeds_.removeFirst();
        if (!paused_ && !cancelled_) {
            QString message;
            switch (phase_.load()) {
            case Phase::InputScanning: message = QStringLiteral("正在扫描文件和目录…"); break;
            case Phase::Opening: message = QStringLiteral("正在读取压缩包…"); break;
            case Phase::Passwords: message = QStringLiteral("正在匹配密码 · %1 / %2").arg(passwordAttempt_.load()).arg(passwordTotal_.load()); break;
            case Phase::Extracting: message = QStringLiteral("正在解压…"); break;
            case Phase::Scanning: message = QStringLiteral("正在识别内层压缩包及视频…"); break;
            case Phase::Cleaning: message = QStringLiteral("正在清理已完成的中间压缩包…"); break;
            case Phase::Organizing: message = QStringLiteral("正在整理视频和图片…"); break;
            case Phase::CopyingMedia: message = QStringLiteral("正在复制并校验媒体文件…"); break;
            case Phase::Testing: message = QStringLiteral("正在检测完整性…"); break;
            case Phase::Correcting: message = QStringLiteral("正在修正名称…"); break;
            case Phase::Compressing: message = QStringLiteral("正在压缩…"); break;
            default: break;
            }
            if (!message.isEmpty() && extractionDepth_ > 0 && phase_ != Phase::Cleaning && phase_ != Phase::Organizing) message += QStringLiteral(" · 第 %1 层").arg(extractionDepth_.load());
            if (!message.isEmpty() && jobCount_ > 1) message += QStringLiteral(" · 第 %1 / %2 组").arg(jobIndex_.load()).arg(jobCount_.load());
            if (!message.isEmpty() && message != status_) setStatus(message);
        }
        emit progressChanged();
    });
}

ArchiveController::~ArchiveController() {
    cancelled_ = true; scanCancelled_ = true; paused_ = false;
    if (scanWorker_.joinable()) scanWorker_.join();
    if (worker_.joinable()) worker_.join();
}

void ArchiveController::setStatus(const QString& text) {
    status_ = text; emit statusChanged(text);
}
void ArchiveController::notifyOptions() { emit optionsChanged(); }
bool ArchiveController::installUpdate() {
    if (busy()) { setStatus(QStringLiteral("请等待当前任务结束后再安装更新")); return false; }
    QString error;
    if (!updates_.install(error)) { setStatus(error); return false; }
    emit quitReady(); return true;
}
void ArchiveController::savePreferences() {
    QSettings preferences(PreferencesFile(), QSettings::IniFormat);
    preferences.setValue("Window/TrayAutoHide", hideOnBlur);
    preferences.remove("Window/HideOnBlur");
    preferences.setValue("Window/RememberGeometry", rememberGeometry);
    preferences.setValue("Passwords/AutoTry", autoPasswords);
    preferences.setValue("Extraction/AutoNested", autoNested);
    preferences.setValue("Extraction/OrganizeMedia", organizeMedia);
}
QByteArray ArchiveController::windowGeometry() const {
    return QSettings(PreferencesFile(), QSettings::IniFormat).value("Window/Geometry").toByteArray();
}
void ArchiveController::saveWindowGeometry(const QByteArray& geometry) {
    if (rememberGeometry) QSettings(PreferencesFile(), QSettings::IniFormat).setValue("Window/Geometry", geometry);
}

bool ArchiveController::savePasswordLists(const QList<PasswordList>& lists) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可修改密码")); return false; }
    QString error;
    if (!passwords_.replace(lists, error)) { passwordError_ = error; setStatus(error); emit passwordsChanged(); return false; }
    passwordError_.clear(); passwordMatches_.clear(); emit passwordsChanged(); return true;
}
bool ArchiveController::addPasswordList(const QString& name, const QString& text, const QHash<QString, QString>* comments) {
    QStringList entries; QString error;
    if (!PasswordLibrary::parse(text, entries, error)) { setStatus(error); return false; }
    auto lists = passwords_.lists();
    auto title = name.trimmed();
    if (title.isEmpty()) title = QStringLiteral("常用密码");
    const auto base = title;
    int suffix = 2;
    while (std::any_of(lists.begin(), lists.end(), [&](const auto& item) { return item.name == title; })) title = base + QStringLiteral(" (%1)").arg(suffix++);
    lists.append({QUuid::createUuid().toString(QUuid::WithoutBraces), title, true, entries, comments ? *comments : QHash<QString, QString>{}});
    if (!savePasswordLists(lists)) return false;
    setStatus(QStringLiteral("已添加密码单「%1」· %2 个密码").arg(title).arg(entries.size())); return true;
}
bool ArchiveController::importPasswordList(const QString& path) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可导入密码单")); return false; }
    QString text, error;
    if (!PasswordLibrary::readTextFile(path, text, error)) { setStatus(error); return false; }
    QStringList entries;
    if (!PasswordLibrary::parse(text, entries, error)) { setStatus(error); return false; }
    auto title = QFileInfo(path).completeBaseName().trimmed();
    if (title.isEmpty()) title = QStringLiteral("常用密码");
    const QSet<QString> imported(entries.cbegin(), entries.cend());
    for (const auto& list : passwords_.lists()) {
        if (list.name != title || QSet<QString>(list.entries.cbegin(), list.entries.cend()) != imported) continue;
        const auto id = list.id;
        if (!list.enabled && !setPasswordListEnabled(id, true)) return false;
        setStatus(QStringLiteral("密码单已存在，已保留原有注释并启用，未重复添加")); return true;
    }
    return addPasswordList(QFileInfo(path).completeBaseName(), text);
}
bool ArchiveController::editPasswordList(const QString& id, const QString& name, const QString& text, const QHash<QString, QString>* comments) {
    QStringList entries; QString error;
    if (!PasswordLibrary::parse(text, entries, error)) { setStatus(error); return false; }
    auto lists = passwords_.lists();
    for (auto& list : lists) if (list.id == id) {
        list.name = name.trimmed(); list.entries = entries;
        if (comments) list.comments = *comments;
        else list.comments.removeIf([&](QHash<QString, QString>::iterator note) { return !entries.contains(note.key()); });
        if (!savePasswordLists(lists)) return false;
        setStatus(QStringLiteral("密码单已保存")); return true;
    }
    setStatus(QStringLiteral("密码单不存在")); return false;
}
bool ArchiveController::setPasswordListEnabled(const QString& id, bool enabled) {
    auto lists = passwords_.lists();
    for (auto& list : lists) if (list.id == id) {
        list.enabled = enabled;
        if (!savePasswordLists(lists)) return false;
        setStatus(enabled ? QStringLiteral("密码单已启用") : QStringLiteral("密码单已停用")); return true;
    }
    return false;
}
bool ArchiveController::removePasswordList(const QString& id) {
    auto lists = passwords_.lists();
    const auto count = lists.removeIf([&](const auto& list) { return list.id == id; });
    if (!count || !savePasswordLists(lists)) return false;
    setStatus(QStringLiteral("密码单已移除，原始导入文件未更改")); return true;
}

bool ArchiveController::addPasswords(const QString& text) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可添加密码")); return false; }
    QStringList values; QString error;
    if (!PasswordLibrary::parse(text, values, error)) { setStatus(error); return false; }
    const auto& active = passwords_.enabledPasswords(); const QSet<QString> enabled(active.cbegin(), active.cend());
    values.removeIf([&](const auto& value) { return enabled.contains(value); });
    if (values.isEmpty()) { setStatus(QStringLiteral("这些密码已在列表中")); return true; }
    const QSet<QString> added(values.cbegin(), values.cend()); QSet<QString> restored;
    auto lists = passwords_.lists(); QList<PasswordList> separated;
    // Re-adding a disabled password enables only that value, not its old source list.
    for (auto& list : lists) {
        if (list.enabled) continue;
        QStringList selected;
        for (const auto& value : list.entries) if (added.contains(value)) { selected.append(value); restored.insert(value); }
        if (selected.isEmpty()) continue;
        if (selected.size() == list.entries.size()) { list.enabled = true; continue; }
        PasswordList enabledList{QUuid::createUuid().toString(QUuid::WithoutBraces), list.name, true, selected, {}};
        for (const auto& value : selected) if (list.comments.contains(value)) enabledList.comments.insert(value, list.comments.take(value));
        list.entries.removeIf([&](const auto& value) { return added.contains(value); });
        separated.append(std::move(enabledList));
    }
    lists.append(separated);
    const auto count = values.size(); values.removeIf([&](const auto& value) { return restored.contains(value); });
    if (!values.isEmpty()) SavedPasswords(lists, true).entries.append(values);
    if (!savePasswordLists(lists)) return false;
    setStatus(QStringLiteral("已保存 %1 个密码").arg(count)); return true;
}

bool ArchiveController::importPasswords(const QString& path) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可导入密码")); return false; }
    QString text, error;
    if (!PasswordLibrary::readTextFile(path, text, error)) { setStatus(error); return false; }
    return addPasswords(text);
}

bool ArchiveController::editPassword(const QString& value, const QString& replacement) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可编辑密码")); return false; }
    if (replacement.isEmpty() || replacement.size() > 1024 || replacement.contains(QChar::Null) || replacement.contains('\n') || replacement.contains('\r')) {
        setStatus(QStringLiteral("密码不能为空或包含换行，最多 1024 个字符")); return false;
    }
    auto lists = passwords_.lists(); QList<PasswordList> separated; bool found = false;
    for (auto& list : lists) {
        if (!list.entries.contains(value)) continue;
        found = true;
        if (value == replacement) continue;
        const auto note = list.comments.take(value);
        for (auto& entry : list.entries) if (entry == value) entry = replacement;
        list.entries.removeDuplicates();
        if (!note.isEmpty()) {
            const auto existingNote = list.comments.value(replacement);
            if (existingNote.isEmpty()) list.comments.insert(replacement, note);
            else if (existingNote != note) separated.append({QUuid::createUuid().toString(QUuid::WithoutBraces), list.name,
                list.enabled, {replacement}, {{replacement, note}}});
        }
    }
    if (!found) { setStatus(QStringLiteral("该密码已不存在")); return false; }
    lists.append(separated);
    if (!savePasswordLists(lists)) return false;
    setStatus(QStringLiteral("密码已保存")); return true;
}

bool ArchiveController::removePasswords(const QStringList& values) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可删除密码")); return false; }
    const QSet<QString> selected(values.cbegin(), values.cend()); QSet<QString> removed;
    auto lists = passwords_.lists();
    for (auto& list : lists) {
        list.entries.removeIf([&](const auto& value) { if (!selected.contains(value)) return false; removed.insert(value); return true; });
        list.comments.removeIf([&](auto it) { return selected.contains(it.key()); });
    }
    if (removed.isEmpty()) return false;
    lists.removeIf([](const auto& list) { return list.entries.isEmpty(); });
    if (!savePasswordLists(lists)) return false;
    setStatus(QStringLiteral("已删除 %1 个密码").arg(removed.size())); return true;
}

bool ArchiveController::setPasswordEnabled(const QString& value, bool enabled) {
    if (busy()) { setStatus(QStringLiteral("任务结束后可修改密码")); return false; }
    const auto& entries = passwords_.uniquePasswords();
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.value == value; });
    if (entry == entries.end()) return false;
    if (entry->enabled == enabled) return true;
    auto lists = passwords_.lists(); QList<PasswordList> separated;
    for (auto& list : lists) {
        if (list.enabled == enabled || !list.entries.contains(value)) continue;
        if (list.entries.size() == 1) { list.enabled = enabled; continue; }
        PasswordList single{QUuid::createUuid().toString(QUuid::WithoutBraces), list.name, enabled, {value}, {}};
        if (list.comments.contains(value)) single.comments.insert(value, list.comments.take(value));
        list.entries.removeAll(value); separated.append(std::move(single));
    }
    lists.append(separated);
    if (!savePasswordLists(lists)) return false;
    setStatus(enabled ? QStringLiteral("密码已启用") : QStringLiteral("密码已停用")); return true;
}

void ArchiveController::addPaths(const QStringList& paths, bool compress) {
    if (busy()) { setStatus(QStringLiteral("当前任务结束后可继续添加文件")); return; }
    if (paths.isEmpty()) return;
    if (!compress) { applyDiscoveredInputs(DiscoverInputs(queuedInputPaths() + paths)); return; }
    for (const auto& input : paths) {
        const auto path = QDir::toNativeSeparators(QFileInfo(input).absoluteFilePath());
        if (compress) {
            bool duplicate = false;
            for (const auto& source : sources) duplicate |= source.compare(path, Qt::CaseInsensitive) == 0;
            if (!duplicate) {
                sources.append(path);
                if (sources.size() == 1) archiveName = QFileInfo(path).completeBaseName();
            }
        }
    }
    emit queueChanged(); emit optionsChanged();
    setStatus(compress ? QStringLiteral("已添加 %1 个输入项目").arg(sources.size())
                       : QStringLiteral("已添加 %1 组压缩包，可先检测完整性").arg(archives.size()));
}

QStringList ArchiveController::queuedInputPaths() const {
    QStringList paths = ignoredInputFiles;
    for (const auto& plan : archives) {
        paths.append(Q(plan.input));
        for (const auto& volume : plan.volumes) paths.append(Q(volume));
    }
    for (const auto& group : mediaInputs) paths.append(group.files);
    QSet<QString> seen;
    paths.removeIf([&](const auto& path) {
        const auto key = ArchiveKey(path.toStdWString()); if (seen.contains(key)) return true; seen.insert(key); return false;
    });
    return paths;
}

void ArchiveController::applyDiscoveredInputs(DiscoveredInputs inputs) {
    if (inputs.cancelled) { setStatus(QStringLiteral("已取消文件扫描")); return; }
    // Rediscover the active queue together with new inputs, so a later drop can
    // complete a cross-directory bundle and replace its earlier missing-volume row.
    archives = std::move(inputs.archives); mediaInputs = std::move(inputs.media);
    ignoredInputFiles = std::move(inputs.ignored); discoveredInputFiles = std::move(inputs.files);
    associatedInputFiles.clear();
    emit queueChanged(); emit optionsChanged();
    setStatus(QStringLiteral("已识别 %1 组压缩包、%2 组媒体 · 忽略 %3 个辅助文件")
        .arg(archives.size()).arg(mediaInputs.size()).arg(ignoredInputFiles.size()));
}

void ArchiveController::extractPaths(const QStringList& paths) {
    if (busy() || paths.isEmpty()) return;
    queuePaths(paths, false, true, true);
}

void ArchiveController::queuePaths(const QStringList& paths, bool compress, bool replace, bool startAfterScan) {
    if (compress) { addPaths(paths, true); return; }
    if (busy()) { setStatus(QStringLiteral("当前任务结束后可继续添加文件")); return; }
    if (paths.isEmpty()) return;
    if (scanWorker_.joinable()) scanWorker_.join();
    const auto scanPaths = replace ? paths : queuedInputPaths() + paths;
    scanning_ = true; scanCancelled_ = false; cancelled_ = false; paused_ = false;
    completed_ = 0; total_ = 0; extractionDepth_ = 0; jobIndex_ = 0; jobCount_ = 0; speed_ = 0;
    phase_ = Phase::InputScanning; lastBytes_ = 0; sampledAt_ = std::chrono::steady_clock::now();
    setStatus(QStringLiteral("正在扫描文件和目录…")); emit busyChanged(); timer_.start();
    scanWorker_ = std::thread([this, scanPaths, startAfterScan] {
        auto inputs = DiscoverInputs(scanPaths, [this](uint64_t, uint64_t) { return !scanCancelled_; });
        QMetaObject::invokeMethod(this, [this, startAfterScan, inputs = std::move(inputs)]() mutable {
            if (scanWorker_.joinable()) scanWorker_.join();
            inputs.cancelled = inputs.cancelled || scanCancelled_;
            const bool launch = startAfterScan && !inputs.cancelled && !quitAfterJob_;
            timer_.stop(); scanning_ = false; phase_ = Phase::Idle; cancelled_ = false;
            applyDiscoveredInputs(std::move(inputs));
            // Keep one busy interval from intake through extraction so callers cannot mistake scan completion for job completion.
            if (launch && start(Operation::Extract)) return;
            emit busyChanged(); emit progressChanged();
            if (quitAfterJob_) emit quitReady();
        }, Qt::QueuedConnection);
    });
}
void ArchiveController::remove(int index, bool compress) {
    if (busy() || index < 0) return;
    if (compress && index < sources.size()) sources.removeAt(index);
    if (!compress) {
        if (static_cast<size_t>(index) < archives.size()) archives.erase(archives.begin() + index);
        else if (static_cast<size_t>(index) - archives.size() < static_cast<size_t>(mediaInputs.size())) mediaInputs.removeAt(index - static_cast<int>(archives.size()));
    }
    emit queueChanged();
}
void ArchiveController::clear(bool compress) {
    if (busy()) return;
    if (compress) sources.clear(); else { archives.clear(); mediaInputs.clear(); ignoredInputFiles.clear(); discoveredInputFiles.clear(); associatedInputFiles.clear(); }
    emit queueChanged();
    setStatus(QStringLiteral("队列已清空"));
}
void ArchiveController::rescan() {
    if (busy()) return;
    queuePaths(queuedInputPaths(), false, true);
}

bool ArchiveController::start(Operation operation, int selected) {
    if (busy()) return false;
    const bool compress = operation == Operation::Compress;
    if (compress ? sources.isEmpty() : archives.empty() && mediaInputs.isEmpty()) {
        setStatus(ignoredInputFiles.isEmpty() ? QStringLiteral("请先添加文件") : QStringLiteral("这些文件没有可处理的压缩包或媒体，辅助文件已忽略")); return false;
    }
    if (operation == Operation::Correct && (selected < 0 || static_cast<size_t>(selected) >= archives.size())) {
        setStatus(QStringLiteral("请先选中需要修正名称的压缩包")); return false;
    }
    auto settings = compression;
    settings.password = compressPassword.toStdWString();
    if (compress && !ParseVolumeSize(volumeSize.toStdWString(), settings.volumeSize)) {
        setStatus(QStringLiteral("分卷大小无效，例如 100M、1G 或留空不分卷")); return false;
    }
    if (compress && (archiveName.trimmed().isEmpty() || archiveName.contains(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]"))) ||
        archiveName.endsWith('.') || archiveName.endsWith(' '))) {
        setStatus(QStringLiteral("请输入有效的压缩包名称")); return false;
    }
    const auto destination = compress ? compressDestination.trimmed() : extractDestination.trimmed();
    if (!destination.isEmpty() && !QDir::isAbsolutePath(destination)) {
        setStatus(QStringLiteral("保存位置需要使用完整路径")); return false;
    }
    auto plans = archives; auto inputs = sources; auto name = archiveName;
    const auto media = mediaInputs;
    const auto relatedInputs = discoveredInputFiles;
    auto secret = extractPassword; const bool separate = subfolder || organizeMedia;
    const bool sortMedia = organizeMedia, nested = autoNested || organizeMedia;
    const auto lists = passwords_.lists(); const auto matches = passwordMatches_; const bool useLists = autoPasswords;
    busy_ = true; paused_ = false; cancelled_ = false;
    completed_ = 0; total_ = 0; jobIndex_ = 0;
    extractionDepth_ = 0;
    if (operation == Operation::Extract) {
        videoFiles.clear(); imageFiles.clear(); cleanedIntermediateFiles = 0; cleanedIntermediateBytes = 0;
        associatedInputFiles.clear();
        cleanedOtherFiles = 0; cleanedOtherBytes = 0;
    }
    passwordAttempt_ = 0; passwordTotal_ = 0; phase_ = compress ? Phase::Compressing : Phase::Opening;
    jobCount_ = compress || operation == Operation::Correct ? 1 : static_cast<unsigned>(plans.size() + media.size());
    lastBytes_ = 0; speed_ = 0; speeds_.clear(); sampledAt_ = std::chrono::steady_clock::now();
    setStatus(operation == Operation::Test ? QStringLiteral("正在检测完整性…") : operation == Operation::Correct ? QStringLiteral("正在验证真实格式…")
        : compress ? QStringLiteral("正在压缩…") : QStringLiteral("正在解压…"));
    emit busyChanged(); emit progressChanged(); timer_.start();
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread([this, operation, selected, compress, plans, inputs, settings, destination, name, secret, separate, lists, matches, useLists, nested, sortMedia, media, relatedInputs] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Result result;
        result.extraction = operation == Operation::Extract;
        auto progress = [this](uint64_t completed, uint64_t total) {
            if (total > 0) { completed_ = completed; total_ = total; }
            while (paused_ && !cancelled_) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return !cancelled_;
        };
        try {
            SevenZipCore core;
            if (compress) {
                jobIndex_ = 1;
                fs::path folder = destination.isEmpty() ? fs::path(inputs.first().toStdWString()).parent_path() : fs::path(destination.toStdWString());
                fs::path filename(name.toStdWString());
                if (_wcsicmp(filename.extension().c_str(), (L"." + settings.format).c_str()) != 0) filename += L"." + settings.format;
                std::vector<std::wstring> sources;
                for (const auto& path : inputs) sources.push_back(path.toStdWString());
                bool ok = core.Compress(sources, (folder / filename).wstring(), settings, progress);
                result.tasks.append({Q(filename.wstring()), ok ? settings.volumeSize ? QStringLiteral("分卷压缩完成 · %1 / 卷").arg(Q(FormatBytes(settings.volumeSize)))
                    : QStringLiteral("压缩完成") : Q(core.LastError()), Q(folder.wstring()), ok});
            } else {
                QString recent;
                auto taskLists = lists;
                if (useLists) taskLists.append(ReadPasswordNotes(relatedInputs));
                auto relatedFiles = relatedInputs;
                struct Job {
                    ArchivePlan plan; QString parentOutput; unsigned depth = 1;
                    int parent = -1, task = -1; bool complete = false;
                    IntermediateArchive intermediate;
                    size_t root = 0;
                    bool consumed = false, keepAuxiliary = false;
                };
                struct Output { QString path; IntermediateArchive generated; QStringList directories; };
                std::vector<Job> jobs;
                std::deque<size_t> pending, deferred;
                std::vector<Output> outputs(plans.size());
                QStringList originals;
                for (const auto& plan : plans) {
                    jobs.push_back({plan, {}, 1});
                    jobs.back().root = jobs.size() - 1;
                    (plan.awaitingVolume || !plan.error.empty() ? deferred : pending).push_back(jobs.size() - 1);
                    originals.append(Q(plan.input)); originals.append(Q(plan.firstVolume));
                    for (const auto& file : plan.volumes) originals.append(Q(file));
                }
                for (const auto& group : media) originals.append(group.files);
                auto updateCount = [&] {
                    const auto attached = std::count_if(jobs.begin(), jobs.end(), [](const auto& queued) { return queued.consumed && queued.task < 0; });
                    jobCount_ = static_cast<unsigned>(jobs.size() - attached + media.size());
                };
                auto attachParts = [&](const ArchivePlan& combined, size_t index, const QString& output) {
                    if (!combined.error.empty() || combined.format != L"zip" || combined.volumeAliases.empty()) return;
                    for (size_t part = 0; part < plans.size(); ++part) {
                        if (part == index) continue;
                        const bool attached = std::any_of(combined.volumes.begin(), combined.volumes.end(), [&](const auto& path) {
                            return ArchiveKey(path) == ArchiveKey(jobs[part].plan.input) && ArchiveKey(path) != ArchiveKey(combined.firstVolume);
                        });
                        if (!attached) continue;
                        if (!jobs[part].consumed) result.associatedInputs.append(Q(jobs[part].plan.input));
                        jobs[part].consumed = true; jobs[part].complete = true;
                        if (jobs[part].task >= 0) {
                            auto& attachment = result.tasks[jobs[part].task]; attachment.success = true;
                            attachment.detail = QStringLiteral("已关联到内层 ZIP 分卷组，由该组统一校验");
                            if (!output.isEmpty()) attachment.output = output;
                        }
                    }
                    updateCount();
                };
                while ((!pending.empty() || !deferred.empty()) && !cancelled_) {
                    const bool delayed = pending.empty(); auto& queue = delayed ? deferred : pending;
                    const auto index = queue.front(); queue.pop_front();
                    if (operation == Operation::Correct && index != static_cast<size_t>(selected)) continue;
                    if (jobs[index].consumed) continue;
                    if (delayed && QFileInfo(Q(jobs[index].plan.input)).isFile()) {
                        std::vector<std::wstring> context; for (const auto& file : relatedFiles) context.push_back(file.toStdWString());
                        jobs[index].plan = InspectArchiveFiles(jobs[index].plan.input, context);
                        if (jobs[index].plan.awaitingVolume) jobs[index].plan.error = L"ZIP 分卷缺少主目录所在卷，请一并添加尾卷";
                    }
                    const auto job = jobs[index]; const auto& plan = job.plan;
                    attachParts(plan, index, outputs[job.root].path);
                    ++jobIndex_; completed_ = 0; total_ = 0;
                    extractionDepth_ = result.extraction ? job.depth : 0;
                    phase_ = Phase::Opening; passwordAttempt_ = 0; passwordTotal_ = 0;
                    ArchiveTask task{Q(fs::path(plan.input).filename().wstring()), Q(plan.error), {}, false};
                    task.input = Q(plan.input); task.rootInput = Q(jobs[job.root].plan.input); task.depth = job.depth;
                    for (const auto& file : plan.volumes) task.inputFiles.append(Q(file));
                    ArchivePasswordResult resolved;
                    if (plan.error.empty()) {
                        const auto candidates = BuildPasswordCandidates(secret, taskLists, useLists, matches.value(ArchiveKey(plan.firstVolume)), recent);
                        resolved = ResolveArchivePassword(core, plan, candidates, progress, [this](int attempt, int total) {
                            passwordAttempt_ = attempt; passwordTotal_ = total; phase_ = Phase::Passwords;
                        });
                    }
                    if (plan.error.empty() && resolved.state == ArchivePasswordResult::State::Ready) {
                        if (resolved.verified && !resolved.password.isEmpty()) {
                            recent = resolved.password; result.matches.insert(ArchiveKey(plan.firstVolume), resolved.password);
                        }
                        if (operation == Operation::Test) {
                            phase_ = Phase::Testing;
                            task.success = resolved.verified || core.TestArchive(resolved.password.toStdWString(), progress);
                            task.detail = task.success ? QStringLiteral("完整性检测通过") : Q(core.LastError());
                        } else if (operation == Operation::Correct) {
                            phase_ = Phase::Correcting;
                            core.CloseArchive(); auto corrected = plan; std::wstring error;
                            task.success = !plan.needsRename || CorrectArchiveName(corrected, error);
                            task.detail = !plan.needsRename ? QStringLiteral("真实格式与扩展名一致，无需修正") : task.success
                                ? QStringLiteral("已修正为 %1").arg(Q(fs::path(corrected.input).filename().wstring())) : Q(error);
                            if (task.success) result.corrections.emplace_back(index, std::move(corrected));
                        } else {
                            phase_ = Phase::Extracting;
                            const bool independent = separate || job.depth > 1;
                            auto folder = !job.parentOutput.isEmpty() ? fs::path(job.parentOutput.toStdWString())
                                : destination.isEmpty() ? fs::path(plan.firstVolume).parent_path() : fs::path(destination.toStdWString());
                            if (independent) folder /= ArchiveOutputName(plan);
                            auto available = folder; unsigned suffix = 2; std::error_code ec;
                            while (independent && fs::exists(NativePath(available.wstring()), ec)) available = folder.wstring() + L" (" + std::to_wstring(suffix++) + L")";
                            fs::create_directories(NativePath(available.wstring()), ec);
                            if (ec) task.detail = QStringLiteral("无法创建输出文件夹");
                            else {
                                task.output = Q(available.wstring());
                                if (job.depth == 1) outputs[job.root].path = task.output;
                                attachParts(plan, index, outputs[job.root].path);
                                task.success = core.Extract(available.wstring(), resolved.password.toStdWString(), progress);
                                task.detail = task.success ? QStringLiteral("解压完成 · %1 个项目").arg(core.GetItemCount())
                                    : Q(core.LastError()) + QStringLiteral(" · 部分文件保留在输出目录");
                                if (task.success) {
                                    QStringList written;
                                    auto& output = outputs[job.root];
                                    for (const auto& item : core.GetItems()) {
                                        const auto path = (available / fs::path(item.path.empty() ? L"content" : item.path)).lexically_normal();
                                        if (!item.isDir) written.append(Q(path.wstring()));
                                        if (sortMedia) {
                                            auto directory = item.isDir ? path : path.parent_path();
                                            while (ArchiveKey(directory.wstring()).startsWith(ArchiveKey(output.path.toStdWString()) + '/')) {
                                                output.directories.append(Q(directory.wstring()));
                                                const auto parent = directory.parent_path(); if (parent == directory) break; directory = parent;
                                            }
                                        }
                                    }
                                    if (sortMedia) {
                                        const auto tracked = TrackGeneratedFiles(written, output.path, originals);
                                        output.generated.files.append(tracked.files);
                                        if (!tracked.error.isEmpty()) output.generated.error = tracked.error;
                                    }
                                    phase_ = Phase::Scanning;
                                    relatedFiles.append(written);
                                    if (useLists) taskLists.append(ReadPasswordNotes(written));
                                    const auto found = FindNestedArchives(written, progress, relatedFiles, true);
                                    result.videos.append(found.videos);
                                    result.images.append(found.images);
                                    if (!found.videos.isEmpty()) task.detail += QStringLiteral(" · 找到 %1 个视频").arg(found.videos.size());
                                    if (!found.images.isEmpty()) task.detail += QStringLiteral(" · %1 张图片").arg(found.images.size());
                                    if (found.cancelled) { task.success = false; task.detail += QStringLiteral(" · 已取消后续识别"); }
                                    else if (nested && !found.archives.empty()) {
                                        if (job.depth >= 32 || jobs.size() + found.archives.size() > 1000) {
                                            task.success = false; task.detail += QStringLiteral(" · 内层解压达到层数或任务数限制，剩余压缩包已保留");
                                        } else {
                                            for (const auto& inner : found.archives) {
                                                Job child{inner, task.output, job.depth + 1}; child.parent = static_cast<int>(index);
                                                child.root = job.root;
                                                auto cleanable = inner;
                                                QSet<QString> produced;
                                                for (const auto& path : written) produced.insert(ArchiveKey(path.toStdWString()));
                                                std::erase_if(cleanable.volumes, [&](const auto& file) { return !produced.contains(ArchiveKey(file)); });
                                                child.intermediate = TrackIntermediateArchive(cleanable, written, task.output, originals);
                                                attachParts(inner, index, task.output);
                                                jobs.push_back(std::move(child));
                                                (inner.awaitingVolume || !inner.error.empty() ? deferred : pending).push_back(jobs.size() - 1);
                                            }
                                            updateCount();
                                            task.detail += QStringLiteral(" · 继续解压 %1 组内层压缩包").arg(found.archives.size());
                                        }
                                    }
                                }
                            }
                        }
                        if (!resolved.source.isEmpty()) task.detail += QStringLiteral(" · %1命中").arg(resolved.source);
                    } else if (plan.error.empty()) {
                        task.detail = resolved.error;
                        if (plan.probePdf && resolved.state == ArchivePasswordResult::State::Invalid) {
                            QFile file(Q(plan.input));
                            if (file.open(QIODevice::ReadOnly) && file.read(5) == "%PDF-") {
                                task.success = true; jobs[index].keepAuxiliary = true; task.output = job.parentOutput;
                                task.detail = QStringLiteral("已尝试解压 PDF，未识别到压缩数据，作为辅助文件处理");
                            }
                        }
                    }
                    if (result.extraction && nested) task.detail.prepend(QStringLiteral("第 %1 层 · ").arg(job.depth));
                    core.CloseArchive();
                    jobs[index].complete = task.success; jobs[index].task = static_cast<int>(result.tasks.size());
                    result.tasks.append(std::move(task));
                }
                if (result.extraction && nested && !cancelled_) {
                    phase_ = Phase::Cleaning;
                    // Children are queued after parents. Walking backwards preserves ancestors of failed branches.
                    for (size_t remaining = jobs.size(); remaining > 0; --remaining) {
                        auto& job = jobs[remaining - 1];
                        if (job.depth > 1 && job.task >= 0 && !job.keepAuxiliary) {
                            auto& task = result.tasks[job.task];
                            if (job.complete && progress(0, 0)) {
                                const auto cleanup = DeleteIntermediateArchive(job.intermediate, progress);
                                if (cleanup.success) {
                                    result.cleanedFiles += cleanup.files; result.cleanedBytes += cleanup.bytes;
                                    task.detail += QStringLiteral(" · 已清理 %1 个中间文件（%2）").arg(cleanup.files).arg(Q(FormatBytes(cleanup.bytes)));
                                    result.matches.remove(ArchiveKey(job.plan.firstVolume));
                                    QSet<QString> removed;
                                    for (const auto& file : job.intermediate.files) removed.insert(ArchiveKey(file.path.toStdWString()));
                                    outputs[job.root].generated.files.removeIf([&](const auto& file) { return removed.contains(ArchiveKey(file.path.toStdWString())); });
                                } else {
                                    job.complete = false; task.success = false; task.detail += QStringLiteral(" · ") + cleanup.error;
                                }
                            } else task.detail += QStringLiteral(" · 后续未全部完成，中间包已保留");
                        }
                        if (!job.complete && job.parent >= 0) jobs[static_cast<size_t>(job.parent)].complete = false;
                        if (cancelled_) break;
                    }
                }
                if (result.extraction && sortMedia && !cancelled_) {
                    phase_ = Phase::Organizing;
                    for (size_t root = 0; root < plans.size() && !cancelled_; ++root) {
                        auto& job = jobs[root]; auto& output = outputs[root];
                        if (job.task < 0 || output.path.isEmpty()) continue;
                        auto& task = result.tasks[job.task];
                        if (!job.complete) { task.detail += QStringLiteral(" · 内层未全部成功，保留原布局及其他文件"); continue; }
                        const auto organized = OrganizeExtractedMedia(output.path, output.generated, output.directories, progress);
                        auto relocate = [&](QStringList& paths) {
                            for (auto& path : paths) if (organized.relocated.contains(path)) path = organized.relocated.value(path);
                        };
                        relocate(result.videos); relocate(result.images);
                        result.removedOtherFiles += organized.removedFiles; result.removedOtherBytes += organized.removedBytes;
                        if (!organized.relocated.isEmpty() || (organized.success && !organized.noMedia)) {
                            for (const auto& child : jobs) if (child.root == root && child.task >= 0) result.tasks[child.task].output = output.path;
                        }
                        if (!organized.success) { task.success = false; task.detail += QStringLiteral(" · ") + organized.error; }
                        else if (organized.noMedia) task.detail += QStringLiteral(" · 未发现视频或图片，解压内容已保留");
                        else {
                            task.detail += QStringLiteral(" · 已整理 %1 个视频、%2 张图片，清理 %3 个其他文件")
                                .arg(organized.videos.size()).arg(organized.images.size()).arg(organized.removedFiles);
                        }
                    }
                }
                if (operation != Operation::Correct) for (const auto& group : media) {
                    if (cancelled_) break;
                    ++jobIndex_; extractionDepth_ = 0; completed_ = 0; total_ = 0;
                    phase_ = operation == Operation::Test ? Phase::Testing : Phase::CopyingMedia;
                    const auto copied = ProcessMediaInput(group, destination, operation == Operation::Test, progress);
                    ArchiveTask task{group.name, copied.detail, copied.output, copied.success};
                    task.input = task.rootInput = group.directory; task.inputFiles = group.files; task.mediaInput = true;
                    result.tasks.append(std::move(task));
                    result.videos.append(copied.videos); result.images.append(copied.images);
                }
            }
        } catch (const std::exception&) {
            result.tasks.append({QStringLiteral("任务失败"), QStringLiteral("文件访问失败，请检查路径、权限和可用空间"), {}, false});
        }
        CoUninitialize();
        QMetaObject::invokeMethod(this, [this, result = std::move(result)]() mutable { finish(std::move(result)); }, Qt::QueuedConnection);
    });
    return true;
}

void ArchiveController::finish(Result result) {
    if (worker_.joinable()) worker_.join();
    timer_.stop();
    int success = 0;
    for (const auto& task : result.tasks) if (task.success) ++success;
    tasks.append(result.tasks);
    if (result.extraction) {
        videoFiles = result.videos; imageFiles = result.images;
        associatedInputFiles = result.associatedInputs;
        cleanedIntermediateFiles = result.cleanedFiles; cleanedIntermediateBytes = result.cleanedBytes;
        cleanedOtherFiles = result.removedOtherFiles; cleanedOtherBytes = result.removedOtherBytes;
    }
    for (auto it = result.matches.cbegin(); it != result.matches.cend(); ++it) passwordMatches_.insert(it.key(), it.value());
    for (auto& [index, plan] : result.corrections) if (index < archives.size()) archives[index] = std::move(plan);
    busy_ = false; paused_ = false; speed_ = 0; phase_ = Phase::Idle;
    auto message = cancelled_ ? QStringLiteral("任务已取消") : QStringLiteral("任务结束 · %1 成功 · %2 失败").arg(success).arg(result.tasks.size() - success);
    if (result.extraction) message += QStringLiteral(" · %1 个视频 · %2 张图片").arg(videoFiles.size()).arg(imageFiles.size());
    if (result.cleanedFiles) message += QStringLiteral(" · 已清理 %1 个中间文件").arg(result.cleanedFiles);
    if (result.removedOtherFiles) message += QStringLiteral(" · 已清理 %1 个其他文件").arg(result.removedOtherFiles);
    setStatus(message);
    emit queueChanged(); emit tasksChanged(); emit progressChanged(); emit busyChanged();
    if (quitAfterJob_) emit quitReady();
}
void ArchiveController::togglePause() {
    if (!busy_ || cancelled_) return;
    paused_ = !paused_;
    setStatus(paused_ ? QStringLiteral("任务已暂停") : QStringLiteral("任务继续执行"));
    emit progressChanged();
}
void ArchiveController::cancel() {
    if (scanning_) { scanCancelled_ = true; cancelled_ = true; setStatus(QStringLiteral("正在取消文件扫描…")); emit progressChanged(); return; }
    if (!busy_) return;
    cancelled_ = true; paused_ = false;
    setStatus(QStringLiteral("正在取消…")); emit progressChanged();
}
void ArchiveController::requestQuit() {
    if (busy()) { quitAfterJob_ = true; cancel(); }
    else emit quitReady();
}
