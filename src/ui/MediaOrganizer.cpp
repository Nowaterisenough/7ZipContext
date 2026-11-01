#include "MediaOrganizer.h"
#include "WindowsPath.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace {
QString Key(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded(); }
const QSet<QString> Videos{"mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv", "ts", "m2ts", "mts", "mpg", "mpeg", "vob", "flv", "3gp"};
const QSet<QString> Images{"jpg", "jpeg", "png", "gif", "webp", "bmp", "tif", "tiff", "avif", "heic", "heif", "jxl", "ico", "svg", "dng", "cr2", "cr3", "nef", "arw", "psd"};
QString TargetName(const IntermediateFile& file, const MediaType& type) {
    const QFileInfo info(file.path);
    const auto extension = info.suffix().toLower();
    const auto& supported = type.kind == MediaKind::Video ? Videos : Images;
    if (supported.contains(extension)) return info.fileName();
    auto name = info.completeBaseName();
    if (name.isEmpty()) name = info.fileName();
    return name + '.' + type.extension;
}
}

MediaType DetectMediaFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {MediaKind::None, {}, false};
    const auto header = file.read(512);
    if (file.error() != QFileDevice::NoError) return {MediaKind::None, {}, false};
    const auto ext = QFileInfo(path).suffix().toLower();
    if (header.startsWith(QByteArray::fromHex("ffd8ff"))) return {MediaKind::Image, "jpg"};
    if (header.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) return {MediaKind::Image, "png"};
    if (header.startsWith("GIF87a") || header.startsWith("GIF89a")) return {MediaKind::Image, "gif"};
    if (header.startsWith("RIFF") && header.mid(8, 4) == "WEBP") return {MediaKind::Image, "webp"};
    if (header.startsWith("BM")) return {MediaKind::Image, "bmp"};
    if (header.startsWith(QByteArray::fromHex("49492a00")) || header.startsWith(QByteArray::fromHex("4d4d002a"))) return {MediaKind::Image, "tiff"};
    if (header.startsWith("8BPS")) return {MediaKind::Image, "psd"};
    if (header.size() >= 12 && header.mid(4, 4) == "ftyp") {
        const auto brand = header.mid(8, 4).toLower();
        if (brand == "avif" || brand == "avis") return {MediaKind::Image, "avif"};
        if (brand == "heic" || brand == "heix" || brand == "hevc" || brand == "hevx" || brand == "mif1" || brand == "msf1") return {MediaKind::Image, "heic"};
        if (brand == "crx ") return {MediaKind::Image, "cr3"};
        if (brand == "m4a " || brand == "m4b " || brand == "f4a " || brand == "f4b ") return {};
        return {MediaKind::Video, brand == "qt  " ? "mov" : "mp4"};
    }
    if (header.startsWith("RIFF") && header.mid(8, 4) == "AVI ") return {MediaKind::Video, "avi"};
    if (header.startsWith("FLV")) return {MediaKind::Video, "flv"};
    if (header.startsWith(QByteArray::fromHex("1a45dfa3"))) return {MediaKind::Video, ext == "webm" ? "webm" : "mkv"};
    if (header.startsWith("MZ") || header.startsWith("%PDF") || header.startsWith("PK\x03\x04") ||
        header.startsWith(QByteArray::fromHex("377abcaf271c"))) return {};
    if (Images.contains(ext)) return {MediaKind::Image, ext};
    if (Videos.contains(ext)) return {MediaKind::Video, ext};
    return {};
}

MediaOrganizationResult OrganizeExtractedMedia(const QString& outputRoot, const IntermediateArchive& generated,
                                              const QStringList& generatedDirectories, ProgressCallback progress) {
    MediaOrganizationResult result;
    if (!generated.error.isEmpty()) { result.error = generated.error; return result; }
    const auto anchoredRoot = QFileInfo(outputRoot).canonicalFilePath();
    if (anchoredRoot.isEmpty()) { result.error = QStringLiteral("输出目录不可用，已停止整理"); return result; }
    struct Entry { IntermediateFile file; MediaType type; };
    QList<Entry> media; IntermediateArchive other;
    for (const auto& file : generated.files) {
        if (progress && !progress(0, 0)) { result.error = QStringLiteral("已取消整理，其他文件已保留"); return result; }
        if (!VerifyGeneratedFile(file, result.error)) return result;
        const auto type = DetectMediaFile(file.path);
        if (!type.readable) { result.error = QStringLiteral("无法识别解压文件，其他文件已保留：%1").arg(QFileInfo(file.path).fileName()); return result; }
        if (type.kind == MediaKind::None) other.files.append(file);
        else media.append({file, type});
    }
    if (media.isEmpty()) { result.success = true; result.noMedia = true; return result; }
    const auto videos = QDir(outputRoot).filePath(QStringLiteral("视频")), images = QDir(outputRoot).filePath(QStringLiteral("图片"));
    for (const auto& folder : {videos, images}) {
        if (!QDir().mkpath(folder) || (GetFileAttributesW(NativePath(folder.toStdWString()).c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            result.error = QStringLiteral("无法创建安全的媒体分类目录，其他文件已保留"); return result;
        }
    }
    QSet<QString> reserved;
    QList<IntermediateFile> movedFiles;
    std::sort(media.begin(), media.end(), [](const auto& a, const auto& b) { return a.file.path < b.file.path; });
    for (const auto& entry : media) {
        auto target = QDir(entry.type.kind == MediaKind::Video ? videos : images).filePath(TargetName(entry.file, entry.type));
        const QFileInfo desired(target); unsigned suffix = 2;
        while (reserved.contains(Key(target)) || (QFileInfo::exists(target) && Key(target) != Key(entry.file.path))) {
            target = desired.dir().filePath(desired.completeBaseName() + QStringLiteral(" (%1).").arg(suffix++) + desired.suffix());
        }
        if (progress && !progress(0, 0)) { result.error = QStringLiteral("已取消整理，其他文件已保留"); return result; }
        if (Key(target) != Key(entry.file.path)) {
            IntermediateFile moved;
            if (!MoveGeneratedFile(entry.file, target, outputRoot, moved, result.error)) return result;
            result.relocated.insert(entry.file.path, moved.path); target = moved.path;
            movedFiles.append(moved);
        } else movedFiles.append(entry.file);
        reserved.insert(Key(target));
        (entry.type.kind == MediaKind::Video ? result.videos : result.images).append(target);
    }
    if (progress && !progress(0, 0)) { result.error = QStringLiteral("已取消清理，其他文件已保留"); return result; }
    for (const auto& file : movedFiles) if (!VerifyGeneratedFile(file, result.error)) return result;
    if (!other.files.isEmpty()) {
        const auto cleanup = DeleteIntermediateArchive(other, progress);
        if (!cleanup.success) {
            result.error = cleanup.error; result.error.replace(QStringLiteral("中间包"), QStringLiteral("其他解压文件")); return result;
        }
        result.removedFiles = cleanup.files; result.removedBytes = cleanup.bytes;
    }
    auto directories = generatedDirectories; directories.removeDuplicates();
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    for (const auto& directory : directories) {
        if (progress && !progress(0, 0)) { result.error = QStringLiteral("已取消清理空目录"); return result; }
        const auto key = Key(directory);
        if (key == Key(videos) || key == Key(images) || !key.startsWith(Key(outputRoot) + '/')) continue;
        const auto attributes = GetFileAttributesW(NativePath(directory.toStdWString()).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        const auto resolved = QFileInfo(directory).canonicalFilePath();
        if (Key(resolved).startsWith(Key(anchoredRoot) + '/')) RemoveDirectoryW(NativePath(directory.toStdWString()).c_str());
    }
    result.success = true; return result;
}
