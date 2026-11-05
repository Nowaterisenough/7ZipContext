#include "InputDiscovery.h"
#include "WindowsPath.h"
#include "NestedArchives.h"
#include "MediaOrganizer.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QHash>
#include <QSet>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;
namespace {
QString Key(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath())).toCaseFolded(); }
bool Auxiliary(const QString& path) {
    static const QSet<QString> extensions{"txt", "pdf", "md", "html", "htm", "url", "lnk", "nfo", "srt", "ass", "ssa", "json", "xml", "db", "ini",
        "exe", "dll", "bat", "cmd", "ps1", "apk", "ipa", "msix", "appx", "jar", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "epub",
        "mp3", "flac", "aac", "wav", "m4a", "ogg", "opus", "ape"};
    const auto extension = QFileInfo(path).suffix().toLower();
    if (extension == "pdf") { QFile file(path); return file.open(QIODevice::ReadOnly) && file.read(5) == "%PDF-"; }
    return extensions.contains(extension);
}
}

DiscoveredInputs DiscoverInputs(const QStringList& paths, ProgressCallback progress) {
    DiscoveredInputs result;
    QSet<QString> seenFiles, seenDirectories, siblingGroups;
    QStringList pending = paths;
    std::reverse(pending.begin(), pending.end());
    auto error = [&](const QString& path, const QString& detail) {
        ArchivePlan plan; plan.input = plan.firstVolume = QFileInfo(path).absoluteFilePath().toStdWString(); plan.error = detail.toStdWString();
        result.archives.push_back(std::move(plan));
    };
    auto addFile = [&](const QString& path) {
        const auto absolute = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        if (!seenFiles.contains(Key(absolute))) { seenFiles.insert(Key(absolute)); result.files.append(absolute); }
    };
    while (!pending.isEmpty()) {
        if (progress && !progress(0, 0)) { result.cancelled = true; return result; }
        const auto path = pending.takeLast(); const QFileInfo info(path);
        const auto attributes = GetFileAttributesW(NativePath(info.absoluteFilePath().toStdWString()).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) { error(path, QStringLiteral("文件或目录不存在，或无法读取")); continue; }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) { error(path, QStringLiteral("请添加链接指向的实际文件或目录")); continue; }
        if (info.isDir()) {
            const auto key = Key(path); if (seenDirectories.contains(key)) continue; seenDirectories.insert(key);
            QStringList children;
            const auto listing = ReadWindowsDirectory(info.absoluteFilePath().toStdWString());
            for (const auto& child : listing.entries) children.append(QString::fromStdWString(child.path));
            children.sort(Qt::CaseInsensitive); std::reverse(children.begin(), children.end()); pending.append(children);
            if (listing.error != ERROR_SUCCESS) error(path, QStringLiteral("无法完整扫描目录，请检查访问权限"));
        } else if (info.isFile()) addFile(path);
        if (result.files.size() + pending.size() > 100000) { error(path, QStringLiteral("单次扫描超过 100000 个项目，请分批添加")); break; }
    }
    // Selecting one volume still discovers its siblings; explicitly supplied folders can also span subdirectories.
    for (qsizetype index = 0; index < result.files.size(); ++index) {
        if (progress && !progress(0, 0)) { result.cancelled = true; return result; }
        const QFileInfo info(result.files[index]); const auto family = ArchiveVolumeGroup(info.fileName());
        if (family.isEmpty()) continue;
        const auto key = Key(info.absolutePath()) + '|' + family;
        if (siblingGroups.contains(key)) continue;
        const auto siblings = info.dir().entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
        std::vector<std::wstring> candidates;
        for (const auto& sibling : siblings) if (ArchiveVolumeGroup(sibling.fileName()) == family) candidates.push_back(sibling.absoluteFilePath().toStdWString());
        if (family.startsWith("part:")) {
            for (const auto& group : GroupNamedArchiveParts(candidates)) {
                if (std::none_of(group.begin(), group.end(), [&](const auto& p) { return seenFiles.contains(Key(QString::fromStdWString(p))); })) continue;
                for (const auto& p : group) addFile(QString::fromStdWString(p));
            }
        } else for (const auto& p : candidates) addFile(QString::fromStdWString(p));
        siblingGroups.insert(key);
    }
    auto scan = FindNestedArchives(result.files, progress);
    if (scan.cancelled) { result.cancelled = true; return result; }
    result.archives.insert(result.archives.end(), scan.archives.begin(), scan.archives.end());
    QSet<QString> attached;
    for (const auto& plan : result.archives) if (plan.error.empty() && plan.format == L"zip" && !plan.volumeAliases.empty()) {
        for (const auto& part : plan.volumes) if (part != plan.firstVolume) attached.insert(Key(QString::fromStdWString(part)));
    }
    std::erase_if(result.archives, [&](const auto& plan) { return attached.contains(Key(QString::fromStdWString(plan.input))); });
    for (const auto& plan : result.archives) for (const auto& part : plan.volumes) addFile(QString::fromStdWString(part));
    QHash<QString, qsizetype> positions;
    for (qsizetype index = 0; index < result.files.size(); ++index) positions.insert(Key(result.files[index]), index);
    std::stable_sort(result.archives.begin(), result.archives.end(), [&](const auto& a, const auto& b) {
        return positions.value(Key(QString::fromStdWString(a.input)), result.files.size()) < positions.value(Key(QString::fromStdWString(b.input)), result.files.size());
    });
    QMap<QString, MediaInputGroup> groups;
    for (const auto& file : scan.videos + scan.images) {
        const auto directory = QFileInfo(file).absolutePath(); auto& group = groups[Key(directory)];
        group.directory = directory; group.name = QFileInfo(directory).fileName();
        if (group.name.isEmpty()) group.name = QStringLiteral("媒体文件"); group.files.append(file);
    }
    for (const auto& group : groups) result.media.append(group);
    for (const auto& file : scan.ignored) {
        if (attached.contains(Key(file))) continue;
        if (!Auxiliary(file) && QFileInfo(file).size() > 1024 * 1024) error(file, QStringLiteral("未识别此文件，可能是缺少首卷的分卷或不支持的格式"));
        else result.ignored.append(file);
    }
    return result;
}

MediaInputResult ProcessMediaInput(const MediaInputGroup& group, const QString& destination, bool testOnly, ProgressCallback progress) {
    MediaInputResult result;
    QStringList written; uint64_t total = 0, completed = 0;
    for (const auto& path : group.files) total += static_cast<uint64_t>(std::max<qint64>(0, QFileInfo(path).size()));
    auto alive = [&] { return !progress || progress(completed, total); };
    if (!alive()) { result.detail = QStringLiteral("已取消"); return result; }
    QString folder;
    if (!testOnly) {
        const auto base = destination.isEmpty() ? group.directory : destination;
        const auto name = destination.isEmpty() ? QStringLiteral("媒体文件") : group.name;
        const auto desired = QDir(base).filePath(name); folder = desired; unsigned suffix = 2;
        while (QFileInfo::exists(folder)) folder = desired + QStringLiteral(" (%1)").arg(suffix++);
        if (!QDir().mkpath(folder)) { result.detail = QStringLiteral("无法创建媒体输出目录"); return result; }
        result.output = folder;
    }
    for (const auto& path : group.files) {
        if (!alive()) { result.detail = QStringLiteral("已取消，已复制的文件保留"); return result; }
        const QFileInfo before(path); const auto size = before.size(); const auto modified = before.lastModified();
        QFile source(path), target;
        if (!source.open(QIODevice::ReadOnly)) { result.detail = QStringLiteral("无法读取媒体文件：%1").arg(before.fileName()); return result; }
        if (!testOnly) {
            target.setFileName(QDir(folder).filePath(before.fileName()));
            if (!target.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { result.detail = QStringLiteral("无法创建媒体副本，已有文件不会覆盖"); return result; }
        }
        QCryptographicHash sourceHash(QCryptographicHash::Sha256);
        while (!source.atEnd()) {
            if (!alive()) { result.detail = QStringLiteral("已取消，部分副本保留在输出目录"); return result; }
            const auto data = source.read(4 * 1024 * 1024);
            if (data.isEmpty() && source.error() != QFileDevice::NoError) { result.detail = QStringLiteral("读取媒体文件失败"); return result; }
            if (!testOnly && target.write(data) != data.size()) { result.detail = QStringLiteral("写入媒体副本失败"); return result; }
            sourceHash.addData(data); completed += static_cast<uint64_t>(data.size());
        }
        const QFileInfo after(path);
        if (source.error() != QFileDevice::NoError || after.size() != size || after.lastModified() != modified) {
            result.detail = QStringLiteral("源媒体文件在读取期间发生变化，副本未整理"); return result;
        }
        if (!testOnly) {
            if (!target.flush()) { result.detail = QStringLiteral("保存媒体副本失败"); return result; } target.close();
            QFile check(target.fileName()); QCryptographicHash copiedHash(QCryptographicHash::Sha256);
            if (!check.open(QIODevice::ReadOnly)) { result.detail = QStringLiteral("无法核验媒体副本"); return result; }
            while (!check.atEnd()) {
                if (!alive()) { result.detail = QStringLiteral("已取消核验，媒体副本已保留"); return result; }
                const auto data = check.read(4 * 1024 * 1024);
                if (data.isEmpty() && check.error() != QFileDevice::NoError) break;
                copiedHash.addData(data);
            }
            if (check.error() != QFileDevice::NoError || copiedHash.result() != sourceHash.result()) { result.detail = QStringLiteral("媒体副本校验失败，源文件保留"); return result; }
            written.append(target.fileName());
        }
    }
    if (testOnly) { result.success = true; result.detail = QStringLiteral("%1 个媒体文件读取校验通过").arg(group.files.size()); return result; }
    const auto tracked = TrackGeneratedFiles(written, folder, group.files);
    const auto organized = OrganizeExtractedMedia(folder, tracked, {}, progress);
    result.success = organized.success; result.videos = organized.videos; result.images = organized.images;
    result.detail = organized.success ? QStringLiteral("已复制并校验，整理 %1 个视频、%2 张图片").arg(result.videos.size()).arg(result.images.size()) : organized.error;
    return result;
}
