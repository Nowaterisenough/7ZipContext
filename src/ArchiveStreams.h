// Included by SevenZipCore.cpp after the concrete file stream definitions.
#pragma once

class CVolumeInStream final : public IInStream {
    struct Part { HANDLE file; UInt64 start; UInt64 size; };
    std::vector<Part> parts;
    UInt64 position = 0, length = 0, base = 0;
    ULONG refs = 1;
public:
    ~CVolumeInStream() { for (auto& part : parts) CloseHandle(part.file); }
    bool Open(const std::vector<std::wstring>& paths, UInt64 dataOffset = 0, UInt64 dataSize = 0) {
        for (const auto& path : paths) {
            HANDLE file = CreateFileW(NativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
                static_cast<UInt64>(size.QuadPart) > static_cast<UInt64>((std::numeric_limits<Int64>::max)()) - length) { CloseHandle(file); return false; }
            parts.push_back({file, length, static_cast<UInt64>(size.QuadPart)});
            length += size.QuadPart;
        }
        if (dataOffset > length || dataSize > length - dataOffset) return false;
        base = dataOffset; length = dataSize ? dataSize : length - base;
        return true;
    }
    STDMETHOD(QueryInterface)(REFIID iid, void** out) noexcept {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IInStream && iid != IID_ISequentialInStream) return E_NOINTERFACE;
        *out = static_cast<IInStream*>(this); AddRef(); return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() noexcept { return ++refs; }
    STDMETHOD_(ULONG, Release)() noexcept { auto result = --refs; if (!result) delete this; return result; }
    STDMETHOD(Read)(void* data, UInt32 size, UInt32* processed) noexcept {
        if (processed) *processed = 0;
        if (position >= length) return S_OK;
        size = static_cast<UInt32>((std::min)(static_cast<UInt64>(size), length - position));
        UInt32 done = 0;
        for (const auto& part : parts) {
            if (!size || base + position < part.start || base + position >= part.start + part.size) continue;
            LARGE_INTEGER offset{}; offset.QuadPart = base + position - part.start;
            if (!SetFilePointerEx(part.file, offset, nullptr, FILE_BEGIN)) return HRESULT_FROM_WIN32(GetLastError());
            DWORD count = 0;
            auto request = static_cast<DWORD>((std::min)(static_cast<UInt64>(size), part.size - offset.QuadPart));
            if (!ReadFile(part.file, static_cast<char*>(data) + done, request, &count, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
            if (!count && request) return E_FAIL;
            position += count; done += count; size -= count;
        }
        if (processed) *processed = done;
        return S_OK;
    }
    STDMETHOD(Seek)(Int64 offset, UInt32 origin, UInt64* result) noexcept {
        if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        const auto start = static_cast<Int64>(origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? position : length);
        if (offset < -start || (offset > 0 && offset > (std::numeric_limits<Int64>::max)() - start)) return STG_E_INVALIDFUNCTION;
        const auto next = static_cast<UInt64>(start + offset);
        if (next > static_cast<UInt64>((std::numeric_limits<Int64>::max)()) - base) return STG_E_INVALIDFUNCTION;
        position = next;
        if (result) *result = position;
        return S_OK;
    }
};

class COpenCallback final : public IArchiveOpenCallback, public IArchiveOpenVolumeCallback,
                            public ICryptoGetTextPassword {
    ULONG refs = 1;
    std::filesystem::path path;
    std::wstring secret;
    ProgressCallback progress;
    std::vector<std::wstring> volumes;
    std::wstring volumeName;
    std::vector<std::pair<std::wstring,std::wstring>> volumeAliases;
    UInt64 total = 0;
public:
    bool passwordRequested = false;
    std::wstring missingVolumeName;
    COpenCallback(const std::wstring& file, const std::wstring& password, ProgressCallback callback,
                  const std::vector<std::wstring>& files, const std::wstring& name,
                  const std::vector<std::pair<std::wstring,std::wstring>>& aliases)
        : path(file), secret(password), progress(std::move(callback)), volumes(files), volumeName(name), volumeAliases(aliases) {}
    STDMETHOD(QueryInterface)(REFIID iid, void** out) noexcept {
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IArchiveOpenCallback) *out = static_cast<IArchiveOpenCallback*>(this);
        else if (iid == IID_IArchiveOpenVolumeCallback) *out = static_cast<IArchiveOpenVolumeCallback*>(this);
        else if (iid == IID_ICryptoGetTextPassword) *out = static_cast<ICryptoGetTextPassword*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() noexcept { return ++refs; }
    STDMETHOD_(ULONG, Release)() noexcept { auto result = --refs; if (!result) delete this; return result; }
    STDMETHOD(SetTotal)(const UInt64*, const UInt64* bytes) noexcept {
        if (bytes) total = *bytes;
        return progress && !progress(0, total) ? E_ABORT : S_OK;
    }
    STDMETHOD(SetCompleted)(const UInt64*, const UInt64* bytes) noexcept {
        return progress && !progress(bytes ? *bytes : 0, total) ? E_ABORT : S_OK;
    }
    STDMETHOD(GetProperty)(PROPID id, PROPVARIANT* value) noexcept {
        PropVariantInit(value);
        if (id == kpidName) {
            auto name = path.filename();
            if (!volumeName.empty()) name = volumeName;
            value->vt = VT_BSTR; value->bstrVal = SysAllocString(name.c_str());
        }
        if (id == kpidSize) {
            std::error_code ec;
            auto size = std::filesystem::file_size(NativePath(path.wstring()), ec);
            if (!ec) { value->vt = VT_UI8; value->uhVal.QuadPart = size; }
        }
        return S_OK;
    }
    STDMETHOD(GetStream)(const wchar_t* name, IInStream** out) noexcept {
        *out = nullptr;
        std::filesystem::path relative(name);
        if (relative.has_parent_path() || relative.is_absolute()) return S_FALSE;
        auto resolved = path.parent_path() / relative;
        if (!volumeAliases.empty()) {
            const auto alias = std::find_if(volumeAliases.begin(), volumeAliases.end(), [&](const auto& entry) { return _wcsicmp(entry.first.c_str(), name) == 0; });
            if (alias == volumeAliases.end()) { missingVolumeName = name; return S_FALSE; }
            resolved = alias->second;
        } else {
            for (const auto& volume : volumes) {
                if (_wcsicmp(std::filesystem::path(volume).filename().c_str(), name) == 0) { resolved = volume; break; }
            }
        }
        auto stream = new CFullInFileStream;
        stream->AddRef();
        if (!stream->Open(resolved.c_str())) {
            missingVolumeName = name;
            stream->Release(); return S_FALSE;
        }
        *out = stream; return S_OK;
    }
    STDMETHOD(CryptoGetTextPassword)(BSTR* password) noexcept {
        passwordRequested = true;
        if (progress && !progress(0, total)) { *password = nullptr; return E_ABORT; }
        *password = SysAllocString(secret.c_str()); return S_OK;
    }
};
