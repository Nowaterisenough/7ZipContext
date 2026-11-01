#include "ArchiveCleanup.h"
#include "WindowsPath.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <memory>
#include <cstddef>
#include <cstring>

namespace {
struct CloseHandleDeleter {
    void operator()(HANDLE handle) const { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
using FileHandle = std::unique_ptr<void, CloseHandleDeleter>;
QString Key(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded(); }
uint64_t Pair(DWORD high, DWORD low) { return (static_cast<uint64_t>(high) << 32) | low; }
QString FinalPath(HANDLE handle) {
    const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!size) return {};
    std::wstring path(size, L'\0');
    const DWORD count = GetFinalPathNameByHandleW(handle, path.data(), size, FILE_NAME_NORMALIZED);
    return count && count < size ? Key(QString::fromWCharArray(path.data(), static_cast<int>(count))) : QString{};
}
bool Inspect(HANDLE handle, IntermediateFile& file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    file.resolvedPath = FinalPath(handle);
    file.id = Pair(info.nFileIndexHigh, info.nFileIndexLow); file.volume = info.dwVolumeSerialNumber;
    file.size = Pair(info.nFileSizeHigh, info.nFileSizeLow);
    file.modified = Pair(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime);
    return !file.resolvedPath.isEmpty();
}
bool SameFile(const IntermediateFile& expected, const IntermediateFile& current) {
    return current.id == expected.id && current.volume == expected.volume && current.modified == expected.modified &&
           current.size == expected.size && current.resolvedPath == expected.resolvedPath;
}
}

IntermediateArchive TrackGeneratedFiles(const QStringList& files, const QString& outputRoot, const QStringList& originalInputs) {
    if (files.isEmpty()) return {};
    ArchivePlan plan;
    for (const auto& path : files) plan.volumes.push_back(path.toStdWString());
    auto tracked = TrackIntermediateArchive(plan, files, outputRoot, originalInputs);
    tracked.error.replace(QStringLiteral("中间包"), QStringLiteral("解压文件"));
    return tracked;
}

bool VerifyGeneratedFile(const IntermediateFile& file, QString& error) {
    FileHandle handle(CreateFileW(NativePath(file.path.toStdWString()).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    IntermediateFile current;
    if (!Inspect(handle.get(), current) || !SameFile(file, current)) {
        error = QStringLiteral("解压文件被更改或无法读取，已停止整理：%1").arg(QFileInfo(file.path).fileName()); return false;
    }
    return true;
}

bool MoveGeneratedFile(const IntermediateFile& file, const QString& destination, const QString& outputRoot,
                       IntermediateFile& moved, QString& error) {
    FileHandle root(CreateFileW(NativePath(outputRoot.toStdWString()).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    const auto parentPath = QFileInfo(destination).absolutePath();
    FileHandle parent(CreateFileW(NativePath(parentPath.toStdWString()).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION rootInfo{}, parentInfo{};
    const auto rootPath = FinalPath(root.get()), resolvedParent = FinalPath(parent.get());
    if (rootPath.isEmpty() || !file.resolvedPath.startsWith(rootPath + '/') ||
        !resolvedParent.startsWith(rootPath + '/') ||
        !GetFileInformationByHandle(root.get(), &rootInfo) || !GetFileInformationByHandle(parent.get(), &parentInfo) ||
        ((rootInfo.dwFileAttributes | parentInfo.dwFileAttributes) & FILE_ATTRIBUTE_REPARSE_POINT)) {
        error = QStringLiteral("分类目录无效或不在本次输出范围内，已停止整理"); return false;
    }
    FileHandle handle(CreateFileW(NativePath(file.path.toStdWString()).c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    IntermediateFile current;
    if (!Inspect(handle.get(), current) || !SameFile(file, current)) {
        error = QStringLiteral("媒体文件被占用或更改，其他文件已保留：%1").arg(QFileInfo(file.path).fileName()); return false;
    }
    const auto name = NativePath(QFileInfo(destination).absoluteFilePath().toStdWString());
    std::vector<unsigned char> buffer(offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t), 0);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::memcpy(rename->FileName, name.data(), rename->FileNameLength);
    if (!SetFileInformationByHandle(handle.get(), FileRenameInfo, rename, static_cast<DWORD>(buffer.size()))) {
        error = QStringLiteral("无法移动媒体文件，已有文件不会覆盖：%1").arg(QFileInfo(destination).fileName()); return false;
    }
    moved = file; moved.path = QFileInfo(destination).absoluteFilePath(); moved.resolvedPath = FinalPath(handle.get());
    return true;
}

IntermediateArchive TrackIntermediateArchive(const ArchivePlan& plan, const QStringList& produced,
                                            const QString& outputRoot, const QStringList& originalInputs) {
    IntermediateArchive result;
    QSet<QString> allowed, protectedPaths, seen;
    for (const auto& path : produced) allowed.insert(Key(path));
    for (const auto& path : originalInputs) protectedPaths.insert(Key(path));
    FileHandle root(CreateFileW(NativePath(outputRoot.toStdWString()).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    const auto rootPath = FinalPath(root.get());
    if (rootPath.isEmpty()) { result.error = QStringLiteral("无法确认中间包所在输出目录"); return result; }
    for (const auto& path : plan.volumes) {
        const auto text = QString::fromStdWString(path); const auto key = Key(text);
        if (seen.contains(key)) continue;
        if (!allowed.contains(key) || protectedPaths.contains(key)) {
            result.error = QStringLiteral("中间包包含非本次生成或最初添加的文件，已保留"); return result;
        }
        FileHandle handle(CreateFileW(NativePath(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        IntermediateFile file; file.path = text;
        if (!Inspect(handle.get(), file) || !file.resolvedPath.startsWith(rootPath + '/')) {
            result.error = QStringLiteral("无法确认中间包文件或输出目录范围，已保留"); return result;
        }
        seen.insert(key); result.files.append(std::move(file));
    }
    if (result.files.isEmpty()) result.error = QStringLiteral("没有可确认的中间包文件");
    return result;
}

ArchiveCleanupResult DeleteIntermediateArchive(const IntermediateArchive& archive, ProgressCallback progress) {
    ArchiveCleanupResult result;
    if (!archive.error.isEmpty() || archive.files.isEmpty()) {
        result.error = archive.error.isEmpty() ? QStringLiteral("没有可清理的中间包") : archive.error; return result;
    }
    std::vector<FileHandle> handles;
    // Hold every part against writes/replacement before committing deletion of the group.
    for (const auto& expected : archive.files) {
        if (progress && !progress(0, 0)) { result.error = QStringLiteral("已取消清理，中间包已保留"); return result; }
        FileHandle handle(CreateFileW(NativePath(expected.path.toStdWString()).c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        IntermediateFile current;
        if (!Inspect(handle.get(), current)) {
            result.error = QStringLiteral("中间包被占用或无法删除，整组已保留：%1").arg(QFileInfo(expected.path).fileName()); return result;
        }
        if (!SameFile(expected, current)) {
            result.error = QStringLiteral("中间包已被更改，整组已保留：%1").arg(QFileInfo(expected.path).fileName()); return result;
        }
        handles.push_back(std::move(handle));
    }
    if (progress && !progress(0, 0)) { result.error = QStringLiteral("已取消清理，中间包已保留"); return result; }
    size_t marked = 0;
    for (; marked < handles.size(); ++marked) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(handles[marked].get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
            bool restored = true;
            for (size_t index = 0; index < marked; ++index) {
                disposition.DeleteFile = FALSE;
                if (!SetFileInformationByHandle(handles[index].get(), FileDispositionInfo, &disposition, sizeof(disposition))) restored = false;
            }
            result.error = restored ? QStringLiteral("中间包删除失败，整组已保留") : QStringLiteral("中间包仅部分清理，请检查输出目录");
            return result;
        }
    }
    handles.clear();
    result.success = true; result.files = static_cast<int>(archive.files.size());
    for (const auto& file : archive.files) result.bytes += file.size;
    return result;
}
