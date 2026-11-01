#pragma once
#include <Windows.h>
#include <algorithm>
#include <string>
#include <vector>

// Use extended paths at Win32/file-stream boundaries; keep ordinary paths in the UI and archive plans.
inline std::wstring NativePath(std::wstring path) {
    if (path.empty()) return path;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.starts_with(L"\\\\?\\")) return path;
    const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!required) return path;
    std::wstring absolute(required, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), required, absolute.data(), nullptr);
    if (!written || written >= required) return path;
    absolute.resize(written);
    return absolute.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + absolute.substr(2) : L"\\\\?\\" + absolute;
}

inline std::wstring DisplayPath(const std::wstring& path) {
    if (_wcsnicmp(path.c_str(), L"\\\\?\\UNC\\", 8) == 0) return L"\\\\" + path.substr(8);
    return path.starts_with(L"\\\\?\\") ? path.substr(4) : path;
}

struct WindowsDirectoryEntry { std::wstring path; DWORD attributes; };
struct WindowsDirectoryListing { std::vector<WindowsDirectoryEntry> entries; DWORD error = ERROR_SUCCESS; };

inline WindowsDirectoryListing ReadWindowsDirectory(const std::wstring& path) {
    WindowsDirectoryListing result;
    auto native = NativePath(path);
    if (native.empty()) { result.error = ERROR_PATH_NOT_FOUND; return result; }
    if (native.back() != L'\\') native += L'\\';
    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW((native + L'*').c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        result.error = GetLastError();
        // Virtual drives can signal an empty wildcard enumeration with FILE_NOT_FOUND.
        if (result.error == ERROR_FILE_NOT_FOUND) {
            const DWORD attributes = GetFileAttributesW(NativePath(path).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) result.error = ERROR_SUCCESS;
        }
        return result;
    }
    const auto base = DisplayPath(native);
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
            result.entries.push_back({base + data.cFileName, data.dwFileAttributes});
    } while (FindNextFileW(handle, &data));
    result.error = GetLastError(); FindClose(handle);
    if (result.error == ERROR_NO_MORE_FILES) result.error = ERROR_SUCCESS;
    return result;
}
