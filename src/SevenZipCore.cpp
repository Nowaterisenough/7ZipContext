// SevenZipCore.cpp - 7-Zip functionality wrapper implementation
#include "SevenZipCore.h"
#include "ArchivePlan.h"
#include "WindowsPath.h"

#include <Windows.h>
#include <PropIdl.h>
#include <shlwapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <array>
#include <limits>

// 7-Zip headers (GUIDs are instantiated in GuidInit.cpp)
#include "Common/Common.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/PropID.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"

// Archive format GUIDs
// {23170F69-40C1-278A-1000-000110070000} - 7z
static const GUID CLSID_CFormat7z =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110010000} - Zip
static const GUID CLSID_CFormatZip =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110030000} - Rar
static const GUID CLSID_CFormatRar =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x03, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110EE0000} - Tar
static const GUID CLSID_CFormatTar =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0xEE, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110EF0000} - GZip
static const GUID CLSID_CFormatGZip =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0xEF, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110020000} - BZip2
static const GUID CLSID_CFormatBZip2 =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x02, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110E70000} - Iso
static const GUID CLSID_CFormatIso =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0xE7, 0x00, 0x00 } };

// {23170F69-40C1-278A-1000-000110080000} - Cab
static const GUID CLSID_CFormatCab =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x08, 0x00, 0x00 } };
static const GUID CLSID_CFormatRar5 =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0xcc, 0x00, 0x00 } };
static const GUID CLSID_CFormatXz =
    { 0x23170F69, 0x40C1, 0x278A, { 0x10, 0x00, 0x00, 0x01, 0x10, 0x0c, 0x00, 0x00 } };

// External function from 7-Zip to create archive objects
STDAPI CreateObject(const GUID *clsid, const GUID *iid, void **outObject);

//////////////////////////////////////////////////////////////////////////////
// Helper: Create directory recursively
//////////////////////////////////////////////////////////////////////////////

static void CreateDirectoryRecursive(const std::wstring& input) {
    const auto path = NativePath(input);
    size_t pos = 0;
    while ((pos = path.find(L'\\', pos + 1)) != std::wstring::npos) {
        CreateDirectoryW(path.substr(0, pos).c_str(), NULL);
    }
    CreateDirectoryW(path.c_str(), NULL);
}

//////////////////////////////////////////////////////////////////////////////
// Simple Output File Stream (minimal implementation for extraction)
//////////////////////////////////////////////////////////////////////////////

class CSimpleOutFileStream :
    public ISequentialOutStream,
    public CMyUnknownImp
{
public:
    CSimpleOutFileStream() : m_hFile(INVALID_HANDLE_VALUE), m_refCount(0) {}
    virtual ~CSimpleOutFileStream() { Close(); }

    bool Create(const wchar_t* path, bool createAlways = true) {
        // Create parent directory if needed
        std::wstring dir = path;
        size_t pos = dir.rfind(L'\\');
        if (pos != std::wstring::npos) {
            dir = dir.substr(0, pos);
            CreateDirectoryRecursive(dir);
        }

        m_hFile = CreateFileW(NativePath(path).c_str(), GENERIC_WRITE, 0, NULL,
                              createAlways ? CREATE_ALWAYS : CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (m_hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
        }
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept {
        if (iid == IID_IUnknown || iid == IID_ISequentialOutStream) {
            *outObject = this;
            AddRef();
            return S_OK;
        }
        *outObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() noexcept { return ++m_refCount; }
    STDMETHOD_(ULONG, Release)() noexcept {
        ULONG res = --m_refCount;
        if (res == 0) delete this;
        return res;
    }

    STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize) noexcept {
        DWORD written = 0;
        if (!WriteFile(m_hFile, data, size, &written, NULL)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (processedSize) *processedSize = written;
        return S_OK;
    }

private:
    HANDLE m_hFile;
    ULONG m_refCount;
};

//////////////////////////////////////////////////////////////////////////////
// Extract Callback
//////////////////////////////////////////////////////////////////////////////

class CExtractCallback :
    public IArchiveExtractCallback,
    public ICryptoGetTextPassword,
    public CMyUnknownImp
{
public:
    CExtractCallback(IInArchive* archive, const std::wstring& outDir,
                     const std::wstring& password, ProgressCallback progress)
        : m_archive(archive)
        , m_outDir(outDir)
        , m_password(password)
        , m_progress(progress)
        , m_total(0)
        , m_completed(0)
        , m_passwordWasRequested(false)
        , m_refCount(0)
    {}

    bool WasPasswordRequested() const { return m_passwordWasRequested; }
    bool Succeeded() const { return m_success; }
    const std::wstring& OutputError() const { return m_outputError; }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept {
        if (iid == IID_IUnknown) {
            *outObject = static_cast<IArchiveExtractCallback*>(this);
        } else if (iid == IID_IArchiveExtractCallback) {
            *outObject = static_cast<IArchiveExtractCallback*>(this);
        } else if (iid == IID_ICryptoGetTextPassword) {
            *outObject = static_cast<ICryptoGetTextPassword*>(this);
        } else {
            *outObject = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHOD_(ULONG, AddRef)() noexcept { return ++m_refCount; }
    STDMETHOD_(ULONG, Release)() noexcept {
        ULONG res = --m_refCount;
        if (res == 0) delete this;
        return res;
    }

    // IProgress
    STDMETHOD(SetTotal)(UInt64 total) noexcept {
        m_total = total;
        return S_OK;
    }

    STDMETHOD(SetCompleted)(const UInt64 *completeValue) noexcept {
        if (completeValue) {
            m_completed = *completeValue;
            if (m_progress && !m_progress(m_completed, m_total)) {
                return E_ABORT;
            }
        }
        return S_OK;
    }

    // IArchiveExtractCallback
    STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode) noexcept {
        *outStream = NULL;
        if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) {
            return S_OK;
        }

        // Get item path
        PROPVARIANT prop;
        PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidPath, &prop);
        std::wstring itemPath = (prop.vt == VT_BSTR) ? prop.bstrVal : L"";
        PropVariantClear(&prop);
        if (itemPath.empty()) itemPath = L"content";
        std::replace(itemPath.begin(), itemPath.end(), L'/', L'\\');
        while (itemPath.size() > 1 && itemPath.back() == L'\\') itemPath.pop_back();
        std::filesystem::path relative(itemPath);
        if (relative.is_absolute() || relative.has_root_name() || itemPath.find(L':') != std::wstring::npos)
            return E_ACCESSDENIED;
        for (const auto& part : relative) {
            const auto text = part.wstring();
            if (text == L".") continue;
            if (text == L".." || text.empty() || text.back() == L'.' || text.back() == L' ')
                return E_ACCESSDENIED;
            auto base = text.substr(0, text.find(L'.'));
            std::transform(base.begin(), base.end(), base.begin(), towupper);
            if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
                (base.size() == 4 && (base.substr(0, 3) == L"COM" || base.substr(0, 3) == L"LPT") &&
                    (base[3] >= L'1' && base[3] <= L'9')) || text.find_first_of(L"<>\"|?*") != std::wstring::npos)
                return E_ACCESSDENIED;
        }
        itemPath = relative.lexically_normal().wstring();

        // Check if directory
        PropVariantInit(&prop);
        m_archive->GetProperty(index, kpidIsDir, &prop);
        bool isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        std::wstring fullPath = m_outDir;
        if (!fullPath.empty() && fullPath.back() != L'\\') {
            fullPath += L'\\';
        }
        fullPath += itemPath;
        // Reject existing junctions/symlinks before opening any archive-controlled path.
        auto current = std::filesystem::path(fullPath);
        while (!current.empty()) {
            const auto attrs = GetFileAttributesW(NativePath(current.wstring()).c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return E_ACCESSDENIED;
            const auto parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }

        if (isDir) {
            CreateDirectoryRecursive(fullPath);
            const auto attributes = GetFileAttributesW(NativePath(fullPath).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return S_OK;
            m_outputError = L"无法创建输出目录（Windows 错误 " + std::to_wstring(GetLastError()) + L"）";
            return E_FAIL;
        }

        // Create output stream
        CSimpleOutFileStream* stream = new CSimpleOutFileStream();
        stream->AddRef();
        if (!stream->Create(fullPath.c_str(), false)) {
            const DWORD error = GetLastError();
            m_outputError = L"无法创建输出文件（Windows 错误 " + std::to_wstring(error) + L"；已有文件不会覆盖）";
            stream->Release();
            return HRESULT_FROM_WIN32(error);
        }
        *outStream = stream;
        return S_OK;
    }

    STDMETHOD(PrepareOperation)(Int32 askExtractMode) noexcept {
        return S_OK;
    }

    STDMETHOD(SetOperationResult)(Int32 opRes) noexcept {
        if (opRes != NArchive::NExtract::NOperationResult::kOK) m_success = false;
        return S_OK;
    }

    // ICryptoGetTextPassword
    STDMETHOD(CryptoGetTextPassword)(BSTR *password) noexcept {
        m_passwordWasRequested = true;
        *password = SysAllocString(m_password.c_str());
        return S_OK;
    }

private:
    IInArchive* m_archive;
    std::wstring m_outDir;
    std::wstring m_password;
    ProgressCallback m_progress;
    UInt64 m_total;
    UInt64 m_completed;
    bool m_passwordWasRequested;
    bool m_success = true;
    std::wstring m_outputError;
    ULONG m_refCount;
};

//////////////////////////////////////////////////////////////////////////////
// Simple Input File Stream (minimal implementation for compression)
//////////////////////////////////////////////////////////////////////////////

class CSimpleInFileStream :
    public ISequentialInStream,
    public CMyUnknownImp
{
public:
    CSimpleInFileStream() : m_hFile(INVALID_HANDLE_VALUE), m_refCount(0) {}
    virtual ~CSimpleInFileStream() { Close(); }

    bool Open(const wchar_t* path) {
        m_hFile = CreateFileW(NativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (m_hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
        }
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept {
        if (iid == IID_IUnknown || iid == IID_ISequentialInStream) {
            *outObject = this;
            AddRef();
            return S_OK;
        }
        *outObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() noexcept { return ++m_refCount; }
    STDMETHOD_(ULONG, Release)() noexcept {
        ULONG res = --m_refCount;
        if (res == 0) delete this;
        return res;
    }

    STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize) noexcept {
        DWORD read = 0;
        if (!ReadFile(m_hFile, data, size, &read, NULL)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (processedSize) *processedSize = read;
        return S_OK;
    }

private:
    HANDLE m_hFile;
    ULONG m_refCount;
};

//////////////////////////////////////////////////////////////////////////////
// Full Input File Stream (for archive opening)
//////////////////////////////////////////////////////////////////////////////

class CFullInFileStream :
    public IInStream,
    public IStreamGetSize,
    public CMyUnknownImp
{
public:
    CFullInFileStream() : m_hFile(INVALID_HANDLE_VALUE), m_refCount(0) {}
    virtual ~CFullInFileStream() { Close(); }

    bool Open(const wchar_t* path, UInt64 base = 0, UInt64 length = 0) {
        m_hFile = CreateFileW(NativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER actual{};
        if (!GetFileSizeEx(m_hFile, &actual) || actual.QuadPart < 0 || base > static_cast<UInt64>(actual.QuadPart) ||
            length > static_cast<UInt64>(actual.QuadPart) - base) { Close(); return false; }
        m_base = base; m_length = length ? length : static_cast<UInt64>(actual.QuadPart) - base; m_position = 0;
        return true;
    }

    void Close() {
        if (m_hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
        }
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept {
        if (iid == IID_IUnknown) {
            *outObject = static_cast<IInStream*>(this);
            AddRef();
            return S_OK;
        }
        if (iid == IID_IInStream || iid == IID_ISequentialInStream) {
            *outObject = static_cast<IInStream*>(this);
            AddRef();
            return S_OK;
        }
        if (iid == IID_IStreamGetSize) {
            *outObject = static_cast<IStreamGetSize*>(this);
            AddRef();
            return S_OK;
        }
        *outObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() noexcept { return ++m_refCount; }
    STDMETHOD_(ULONG, Release)() noexcept {
        ULONG res = --m_refCount;
        if (res == 0) delete this;
        return res;
    }

    STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize) noexcept {
        if (processedSize) *processedSize = 0;
        if (m_position >= m_length) return S_OK;
        LARGE_INTEGER position{}; position.QuadPart = static_cast<Int64>(m_base + m_position);
        if (!SetFilePointerEx(m_hFile, position, nullptr, FILE_BEGIN)) return HRESULT_FROM_WIN32(GetLastError());
        size = static_cast<UInt32>(std::min<UInt64>(size, m_length - m_position));
        DWORD read = 0;
        if (!ReadFile(m_hFile, data, size, &read, NULL)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (processedSize) *processedSize = read;
        m_position += read;
        return S_OK;
    }

    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition) noexcept {
        if (seekOrigin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        const auto origin = static_cast<Int64>(seekOrigin == STREAM_SEEK_SET ? 0 : seekOrigin == STREAM_SEEK_CUR ? m_position : m_length);
        if (offset < -origin || (offset > 0 && offset > (std::numeric_limits<Int64>::max)() - origin)) return STG_E_INVALIDFUNCTION;
        const auto next = static_cast<UInt64>(origin + offset);
        if (next > static_cast<UInt64>((std::numeric_limits<Int64>::max)()) - m_base) return STG_E_INVALIDFUNCTION;
        m_position = next;
        if (newPosition) *newPosition = m_position;
        return S_OK;
    }

    STDMETHOD(GetSize)(UInt64 *size) noexcept {
        *size = m_length;
        return S_OK;
    }

private:
    HANDLE m_hFile;
    ULONG m_refCount;
    UInt64 m_base = 0, m_length = 0, m_position = 0;
};

//////////////////////////////////////////////////////////////////////////////
// Full Output File Stream (for archive creation)
//////////////////////////////////////////////////////////////////////////////

class CFullOutFileStream :
    public IOutStream,
    public CMyUnknownImp
{
public:
    CFullOutFileStream() : m_hFile(INVALID_HANDLE_VALUE), m_refCount(0) {}
    virtual ~CFullOutFileStream() { Close(); }

    bool Create(const wchar_t* path, bool createAlways = true) {
        // Create parent directory if needed
        std::wstring dir = path;
        size_t pos = dir.rfind(L'\\');
        if (pos != std::wstring::npos) {
            dir = dir.substr(0, pos);
            CreateDirectoryRecursive(dir);
        }

        m_hFile = CreateFileW(NativePath(path).c_str(), GENERIC_WRITE, 0, NULL,
                              createAlways ? CREATE_ALWAYS : CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
        return m_hFile != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (m_hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
        }
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept {
        if (iid == IID_IUnknown) {
            *outObject = static_cast<IOutStream*>(this);
            AddRef();
            return S_OK;
        }
        if (iid == IID_IOutStream || iid == IID_ISequentialOutStream) {
            *outObject = static_cast<IOutStream*>(this);
            AddRef();
            return S_OK;
        }
        *outObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() noexcept { return ++m_refCount; }
    STDMETHOD_(ULONG, Release)() noexcept {
        ULONG res = --m_refCount;
        if (res == 0) delete this;
        return res;
    }

    STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize) noexcept {
        DWORD written = 0;
        if (!WriteFile(m_hFile, data, size, &written, NULL)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (processedSize) *processedSize = written;
        return S_OK;
    }

    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition) noexcept {
        LARGE_INTEGER li;
        li.QuadPart = offset;
        LARGE_INTEGER newPos;
        if (!SetFilePointerEx(m_hFile, li, &newPos, seekOrigin)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (newPosition) *newPosition = newPos.QuadPart;
        return S_OK;
    }

    STDMETHOD(SetSize)(UInt64 newSize) noexcept {
        LARGE_INTEGER li;
        li.QuadPart = newSize;
        if (!SetFilePointerEx(m_hFile, li, NULL, FILE_BEGIN)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (!SetEndOfFile(m_hFile)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

private:
    HANDLE m_hFile;
    ULONG m_refCount;
};

//////////////////////////////////////////////////////////////////////////////
// Update Callback for Compression
//////////////////////////////////////////////////////////////////////////////

class CUpdateCallback :
    public IArchiveUpdateCallback,
    public ICryptoGetTextPassword2,
    public CMyUnknownImp
{
public:
    struct FileItem {
        std::wstring fullPath;
        std::wstring relativePath;
        bool isDir;
        UInt64 size;
        FILETIME mtime;
        DWORD attrib;
    };

    CUpdateCallback(const std::vector<std::wstring>& srcPaths, ProgressCallback progress, const std::wstring& password = L"")
        : m_progress(progress)
        , m_total(0)
        , m_completed(0)
        , m_refCount(0)
    {
        m_password = password;
        // Enumerate all files
        for (const auto& srcPath : srcPaths) {
            std::wstring name = GetFileName(srcPath);
            DWORD attrs = GetFileAttributesW(srcPath.c_str());
            bool isDir = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));

            if (isDir) {
                // Add directory and its contents
                FileItem item;
                item.fullPath = srcPath;
                item.relativePath = name;
                item.isDir = true;
                item.size = 0;
                item.attrib = attrs;
                GetFileTimeHelper(srcPath, item.mtime);
                m_files.push_back(item);
                EnumerateDirectory(srcPath, name);
            } else {
                // Single file
                FileItem item;
                item.fullPath = srcPath;
                item.relativePath = name;
                item.isDir = false;
                item.attrib = attrs;
                GetFileSizeHelper(srcPath, item.size);
                GetFileTimeHelper(srcPath, item.mtime);
                m_files.push_back(item);
                m_total += item.size;
            }
        }
    }

    UInt32 GetItemCount() const { return (UInt32)m_files.size(); }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept {
        if (iid == IID_IUnknown) {
            *outObject = static_cast<IArchiveUpdateCallback*>(this);
        } else if (iid == IID_IArchiveUpdateCallback) {
            *outObject = static_cast<IArchiveUpdateCallback*>(this);
        } else if (iid == IID_ICryptoGetTextPassword2) {
            *outObject = static_cast<ICryptoGetTextPassword2*>(this);
        } else {
            *outObject = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHOD_(ULONG, AddRef)() noexcept { return ++m_refCount; }
    STDMETHOD_(ULONG, Release)() noexcept {
        ULONG res = --m_refCount;
        if (res == 0) delete this;
        return res;
    }

    // IProgress
    STDMETHOD(SetTotal)(UInt64 total) noexcept {
        m_total = total;
        return S_OK;
    }

    STDMETHOD(SetCompleted)(const UInt64 *completeValue) noexcept {
        if (completeValue) {
            m_completed = *completeValue;
            if (m_progress && !m_progress(m_completed, m_total)) {
                return E_ABORT;
            }
        }
        return S_OK;
    }

    // IArchiveUpdateCallback
    STDMETHOD(GetUpdateItemInfo)(UInt32 index, Int32 *newData, Int32 *newProps, UInt32 *indexInArchive) noexcept {
        if (newData) *newData = 1;      // New data
        if (newProps) *newProps = 1;    // New properties
        if (indexInArchive) *indexInArchive = (UInt32)-1;  // Not in archive
        return S_OK;
    }

    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value) noexcept {
        PropVariantInit(value);
        if (index >= m_files.size()) return E_INVALIDARG;

        const FileItem& item = m_files[index];

        switch (propID) {
            case kpidPath:
                value->vt = VT_BSTR;
                value->bstrVal = SysAllocString(item.relativePath.c_str());
                break;
            case kpidIsDir:
                value->vt = VT_BOOL;
                value->boolVal = item.isDir ? VARIANT_TRUE : VARIANT_FALSE;
                break;
            case kpidSize:
                if (!item.isDir) {
                    value->vt = VT_UI8;
                    value->uhVal.QuadPart = item.size;
                }
                break;
            case kpidAttrib:
                value->vt = VT_UI4;
                value->ulVal = item.attrib;
                break;
            case kpidMTime:
                value->vt = VT_FILETIME;
                value->filetime = item.mtime;
                break;
        }
        return S_OK;
    }

    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream) noexcept {
        *inStream = NULL;
        if (index >= m_files.size()) return E_INVALIDARG;

        const FileItem& item = m_files[index];
        if (item.isDir) return S_OK;

        CSimpleInFileStream* stream = new CSimpleInFileStream();
        stream->AddRef();
        if (!stream->Open(item.fullPath.c_str())) {
            stream->Release();
            return HRESULT_FROM_WIN32(GetLastError());
        }
        *inStream = stream;
        return S_OK;
    }

    STDMETHOD(SetOperationResult)(Int32 operationResult) noexcept {
        return S_OK;
    }

    // ICryptoGetTextPassword2
    STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR *password) noexcept {
        *passwordIsDefined = !m_password.empty();
        *password = SysAllocString(m_password.c_str());
        return S_OK;
    }

private:
    std::wstring m_password;
    std::vector<FileItem> m_files;
    ProgressCallback m_progress;
    UInt64 m_total;
    UInt64 m_completed;
    ULONG m_refCount;

    std::wstring GetFileName(const std::wstring& path) {
        size_t pos = path.find_last_of(L"\\/");
        return (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
    }

    void GetFileSizeHelper(const std::wstring& path, UInt64& size) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            size = ((UInt64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        } else {
            size = 0;
        }
    }

    void GetFileTimeHelper(const std::wstring& path, FILETIME& mtime) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            mtime = fad.ftLastWriteTime;
        } else {
            GetSystemTimeAsFileTime(&mtime);
        }
    }

    void EnumerateDirectory(const std::wstring& basePath, const std::wstring& relBasePath) {
        std::wstring searchPath = basePath + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }

            std::wstring fullPath = basePath + L"\\" + fd.cFileName;
            std::wstring relPath = relBasePath + L"\\" + fd.cFileName;

            FileItem item;
            item.fullPath = fullPath;
            item.relativePath = relPath;
            item.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            item.attrib = fd.dwFileAttributes;
            item.mtime = fd.ftLastWriteTime;

            if (item.isDir) {
                item.size = 0;
                m_files.push_back(item);
                EnumerateDirectory(fullPath, relPath);
            } else {
                item.size = ((UInt64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                m_files.push_back(item);
                m_total += item.size;
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }
};

//////////////////////////////////////////////////////////////////////////////
// SevenZipCore Implementation
//////////////////////////////////////////////////////////////////////////////

#include "ArchiveStreams.h"

SevenZipCore& SevenZipCore::Instance() {
    static thread_local SevenZipCore instance;
    return instance;
}

SevenZipCore::SevenZipCore() {
    InitFormats();
}

SevenZipCore::~SevenZipCore() {
    CloseArchive();
}

void SevenZipCore::InitFormats() {
    m_formats = {
        { L"7z",    L".7z",   CLSID_CFormat7z,    true  },
        { L"Zip",   L".zip",  CLSID_CFormatZip,   true  },
        { L"Rar",   L".rar",  CLSID_CFormatRar,   false },
        { L"rar5", L".rar", CLSID_CFormatRar5, false },
        { L"xz", L".xz", CLSID_CFormatXz, false },
        { L"Tar",   L".tar",  CLSID_CFormatTar,   true  },
        { L"GZip",  L".gz",   CLSID_CFormatGZip,  true  },
        { L"BZip2", L".bz2",  CLSID_CFormatBZip2, true  },
        { L"Iso",   L".iso",  CLSID_CFormatIso,   false },
        { L"Cab",   L".cab",  CLSID_CFormatCab,   false },
    };
}

IInArchive* SevenZipCore::CreateInArchive(const GUID& formatId) {
    IInArchive* archive = nullptr;
    HRESULT hr = CreateObject(&formatId, &IID_IInArchive, (void**)&archive);
    if (FAILED(hr)) {
        return nullptr;
    }
    return archive;
}

IOutArchive* SevenZipCore::CreateOutArchive(const GUID& formatId) {
    IOutArchive* archive = nullptr;
    HRESULT hr = CreateObject(&formatId, &IID_IOutArchive, (void**)&archive);
    if (FAILED(hr)) {
        return nullptr;
    }
    return archive;
}

const GUID* SevenZipCore::GetFormatForExtension(const std::wstring& ext) {
    for (const auto& fmt : m_formats) {
        if (_wcsicmp(ext.c_str(), fmt.extension.c_str()) == 0) {
            return &fmt.classId;
        }
    }
    // Handle compound extensions
    if (_wcsicmp(ext.c_str(), L".tgz") == 0 ||
        _wcsicmp(ext.c_str(), L".tar.gz") == 0) {
        return &CLSID_CFormatGZip;
    }
    if (_wcsicmp(ext.c_str(), L".tbz2") == 0 ||
        _wcsicmp(ext.c_str(), L".tar.bz2") == 0) {
        return &CLSID_CFormatBZip2;
    }
    return nullptr;
}

const GUID* SevenZipCore::DetectFormat(const std::wstring& path) {
    const auto plan = InspectArchive(path);
    for (const auto& format : m_formats) {
        if (_wcsicmp(plan.format.c_str(), format.name.c_str()) == 0 ||
            L"." + plan.format == format.extension) {
            if (plan.format == L"rar5") return &CLSID_CFormatRar5;
            return &format.classId;
        }
    }
    return nullptr;
}

bool SevenZipCore::OpenArchive(const std::wstring& path, const std::wstring& password, ProgressCallback progress) {
    return OpenArchive(InspectArchive(path), password, progress);
}

bool SevenZipCore::OpenArchive(const ArchivePlan& plan, const std::wstring& password, ProgressCallback progress) {
    CloseArchive();
    m_error.clear();
    if (progress && !progress(0, 0)) { m_error = L"已取消"; return false; }
    if (!plan.error.empty()) { m_error = plan.error; return false; }
    if (plan.concatenated) {
        auto stream = new CVolumeInStream;
        if (!stream->Open(plan.volumes)) { stream->Release(); m_error = L"无法读取分卷"; return false; }
        m_inStream = stream;
    } else {
        auto stream = new CFullInFileStream;
        stream->AddRef();
        if (!stream->Open(plan.firstVolume.c_str(), plan.dataOffset, plan.dataSize)) { stream->Release(); m_error = L"无法读取压缩包"; return false; }
        m_inStream = stream;
    }
    const GUID* detected = nullptr;
    for (const auto& format : m_formats) {
        if (_wcsicmp(plan.format.c_str(), format.name.c_str()) == 0 || L"." + plan.format == format.extension) {
            detected = plan.format == L"rar5" ? &CLSID_CFormatRar5 : &format.classId; break;
        }
    }
    std::vector<GUID> candidates;
    if (detected) candidates.push_back(*detected);
    for (const auto& format : m_formats) {
        if (!detected || format.classId != *detected) candidates.push_back(format.classId);
    }
    bool requestedPassword = false;
    for (const auto& id : candidates) {
        auto archive = CreateInArchive(id);
        if (!archive) continue;
        m_inStream->Seek(0, STREAM_SEEK_SET, nullptr);
        auto callback = new COpenCallback(plan.firstVolume, password, progress, plan.volumes, plan.volumeExtension, plan.volumeName, plan.volumeAliases);
        UInt64 maxCheck = 1 << 22;
        HRESULT result = archive->Open(m_inStream, &maxCheck, callback);
        requestedPassword = requestedPassword || callback->passwordRequested;
        callback->Release();
        // S_FALSE means the handler did not recognize this archive.
        if (result == S_OK) { m_archive = archive; break; }
        archive->Close();
        archive->Release();
        if (result == E_ABORT) { CloseArchive(); m_error = L"已取消"; return false; }
    }
    if (!m_archive) {
        CloseArchive();
        m_needsPassword = requestedPassword;
        m_error = requestedPassword ? L"无法打开加密文件头，请检查密码" : L"无法打开：文件损坏、分卷不完整或格式不支持";
        return false;
    }
    PROPVARIANT diagnostics{};
    m_archive->GetArchiveProperty(kpidError, &diagnostics);
    bool incomplete = diagnostics.vt == VT_BSTR && SysStringLen(diagnostics.bstrVal) != 0;
    PropVariantClear(&diagnostics);
    m_archive->GetArchiveProperty(kpidErrorFlags, &diagnostics);
    incomplete = incomplete || (diagnostics.vt == VT_UI4 && diagnostics.ulVal != 0);
    PropVariantClear(&diagnostics);
    if (incomplete) {
        CloseArchive(); m_error = L"压缩包不完整：存在缺卷或文件头损坏"; return false;
    }
    m_currentPath = plan.firstVolume;
    m_needsPassword = requestedPassword;
    UInt32 count = 0;
    m_archive->GetNumberOfItems(&count);
    for (UInt32 i = 0; i < count; ++i) {
        PROPVARIANT prop;
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidEncrypted, &prop);
        if (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE) m_needsPassword = true;
        PropVariantClear(&prop);
    }
    return true;
}

void SevenZipCore::CloseArchive() {
    if (m_archive) {
        m_archive->Close();
        m_archive->Release();
        m_archive = nullptr;
    }
    if (m_inStream) {
        m_inStream->Release();
        m_inStream = nullptr;
    }
    m_currentPath.clear();
    m_needsPassword = false;
}

uint32_t SevenZipCore::GetItemCount() {
    if (!m_archive) return 0;
    UInt32 count = 0;
    m_archive->GetNumberOfItems(&count);
    return count;
}

std::vector<ArchiveItem> SevenZipCore::GetItems() {
    std::vector<ArchiveItem> items;
    if (!m_archive) return items;

    UInt32 numItems = 0;
    m_archive->GetNumberOfItems(&numItems);

    for (UInt32 i = 0; i < numItems; i++) {
        ArchiveItem item;
        PROPVARIANT prop;

        // Path
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidPath, &prop);
        item.path = (prop.vt == VT_BSTR) ? prop.bstrVal : L"";
        PropVariantClear(&prop);

        // Size
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidSize, &prop);
        item.size = (prop.vt == VT_UI8) ? prop.uhVal.QuadPart : 0;
        PropVariantClear(&prop);

        // Packed size
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidPackSize, &prop);
        item.packedSize = (prop.vt == VT_UI8) ? prop.uhVal.QuadPart : 0;
        PropVariantClear(&prop);

        // Is directory
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidIsDir, &prop);
        item.isDir = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // Is encrypted
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidEncrypted, &prop);
        item.isEncrypted = (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        PropVariantClear(&prop);

        // Modification time
        PropVariantInit(&prop);
        m_archive->GetProperty(i, kpidMTime, &prop);
        if (prop.vt == VT_FILETIME) {
            item.mtime = prop.filetime;
        } else {
            memset(&item.mtime, 0, sizeof(item.mtime));
        }
        PropVariantClear(&prop);

        items.push_back(item);
    }

    return items;
}

bool SevenZipCore::TestPassword(const std::wstring& password) {
    return TestArchive(password);
}

bool SevenZipCore::TestArchive(const std::wstring& password, ProgressCallback progress) {
    if (!m_archive) return false;
    auto callback = new CExtractCallback(m_archive, L"", password, progress);
    callback->AddRef();
    HRESULT hr = m_archive->Extract(nullptr, static_cast<UInt32>(-1), 1, callback);
    const bool success = hr == S_OK && callback->Succeeded();
    callback->Release();
    if (!success) m_error = hr == E_ABORT ? L"已取消" : L"校验失败：密码错误、数据损坏或缺少分卷";
    return success;
}

bool SevenZipCore::Extract(const std::wstring& outDir,
                           const std::wstring& password,
                           ProgressCallback progress) {
    if (!m_archive) return false;

    CExtractCallback* callback = new CExtractCallback(m_archive, outDir, password, progress);
    callback->AddRef();
    HRESULT hr = m_archive->Extract(nullptr, (UInt32)-1, 0, callback);
    const bool success = hr == S_OK && callback->Succeeded();
    const auto outputError = callback->OutputError();
    callback->Release();
    if (!success) m_error = hr == E_ABORT ? L"已取消" : !outputError.empty() ? outputError : L"解压失败：密码、数据校验或输出路径错误（已有文件不会覆盖）";
    return success;
}

bool SevenZipCore::ExtractFiles(const std::vector<uint32_t>& indices,
                                const std::wstring& outDir,
                                const std::wstring& password,
                                ProgressCallback progress) {
    if (!m_archive || indices.empty()) return false;

    CExtractCallback* callback = new CExtractCallback(m_archive, outDir, password, progress);
    callback->AddRef();
    HRESULT hr = m_archive->Extract(indices.data(), (UInt32)indices.size(), 0, callback);
    const bool success = hr == S_OK && callback->Succeeded();
    const auto outputError = callback->OutputError();
    callback->Release();
    if (!success) m_error = hr == E_ABORT ? L"已取消" : !outputError.empty() ? outputError : L"解压失败：密码、数据校验或输出路径错误（已有文件不会覆盖）";
    return success;
}

bool SevenZipCore::Compress(const std::vector<std::wstring>& sources,
                            const std::wstring& destination, const std::wstring& format,
                            ProgressCallback progress) {
    CompressionOptions options;
    options.format = format;
    return Compress(sources, destination, options, progress);
}

bool SevenZipCore::Compress(const std::vector<std::wstring>& sources,
                            const std::wstring& destination, const CompressionOptions& options,
                            ProgressCallback progress) {
    namespace fs = std::filesystem;
    m_error.clear();
    if (sources.empty() || (options.format != L"7z" && options.format != L"zip") || options.level > 9) {
        m_error = L"请选择文件和有效的压缩格式"; return false;
    }
    std::error_code ec;
    std::set<std::wstring> names;
    const auto output = fs::absolute(destination, ec).lexically_normal();
    auto fold = [](std::wstring value) { std::transform(value.begin(), value.end(), value.begin(), towlower); return value; };
    for (const auto& source : sources) {
        const auto src = fs::absolute(source, ec).lexically_normal();
        if (ec || !fs::exists(src, ec) || !names.insert(fold(src.filename().wstring())).second) {
            m_error = L"源文件不存在，或多个输入使用相同的顶层名称"; return false;
        }
        const auto attrs = GetFileAttributesW(src.c_str());
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) { m_error = L"请直接添加链接指向的原始文件夹"; return false; }
        if (fs::is_directory(src, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::none, ec)) {
                if (GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) {
                    m_error = L"源文件夹包含链接，请移除链接后重试"; return false;
                }
            }
            if (ec) { m_error = L"无法读取源文件夹"; return false; }
            auto prefix = fold(src.wstring()) + L"\\";
            if (fold(output.wstring()).rfind(prefix, 0) == 0) {
                m_error = L"压缩包不能保存到正在压缩的文件夹内"; return false;
            }
        }
    }
    if (fs::exists(output, ec) || (options.volumeSize && fs::exists(output.wstring() + L".001", ec))) {
        m_error = L"输出文件已存在，请更换名称"; return false;
    }
    fs::create_directories(output.parent_path(), ec);
    if (ec) { m_error = L"无法创建输出文件夹"; return false; }
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(output.parent_path().c_str(), L"7zc", 0, temporary)) {
        m_error = L"无法创建临时文件"; return false;
    }
    IOutArchive* archive = CreateOutArchive(options.format == L"zip" ? CLSID_CFormatZip : CLSID_CFormat7z);
    if (!archive) { DeleteFileW(temporary); m_error = L"无法创建压缩引擎"; return false; }
    ISetProperties* properties = nullptr;
    HRESULT hr = archive->QueryInterface(IID_ISetProperties, reinterpret_cast<void**>(&properties));
    if (hr == S_OK) {
        const bool zip = options.format == L"zip";
        const wchar_t* keys[] = {L"x", zip ? L"em" : L"he"};
        PROPVARIANT values[2]{};
        values[0].vt = VT_UI4; values[0].ulVal = options.level;
        if (zip) { values[1].vt = VT_BSTR; values[1].bstrVal = SysAllocString(L"AES256"); }
        else { values[1].vt = VT_BOOL; values[1].boolVal = options.encryptHeaders ? VARIANT_TRUE : VARIANT_FALSE; }
        hr = properties->SetProperties(keys, values, !options.password.empty() && (zip || options.encryptHeaders) ? 2 : 1);
        PropVariantClear(&values[1]);
        properties->Release();
    }
    auto stream = new CFullOutFileStream;
    stream->AddRef();
    if (hr != S_OK || !stream->Create(temporary)) {
        stream->Release(); archive->Release(); DeleteFileW(temporary);
        m_error = L"无法应用压缩设置或打开输出文件"; return false;
    }
    auto callback = new CUpdateCallback(sources, progress, options.password);
    callback->AddRef();
    hr = archive->UpdateItems(stream, callback->GetItemCount(), callback);
    callback->Release(); stream->Release(); archive->Release();
    if (hr != S_OK) {
        DeleteFileW(temporary);
        m_error = hr == E_ABORT ? L"已取消" : L"压缩失败，请检查源文件及可用空间"; return false;
    }
    if (!options.volumeSize) {
        bool ok = MoveFileExW(temporary, output.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!ok) { DeleteFileW(temporary); m_error = L"无法保存：输出文件已存在或被占用"; }
        return ok;
    }
    std::vector<std::wstring> created;
    bool success = true;
    {
        std::ifstream input(fs::path(temporary), std::ios::binary);
        auto total = fs::file_size(temporary, ec);
        uint64_t completed = 0;
        std::vector<char> buffer(1024 * 1024);
        for (size_t part = 1; completed < total && success; ++part) {
            wchar_t suffix[32]; swprintf_s(suffix, L".%03zu", part);
            const auto name = output.wstring() + suffix;
            HANDLE file = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { success = false; break; }
            created.push_back(name);
            uint64_t written = 0;
            while (written < options.volumeSize && completed < total) {
                if (progress && !progress(completed, total)) { success = false; break; }
                auto size = static_cast<DWORD>((std::min)({options.volumeSize - written, total - completed, static_cast<uint64_t>(buffer.size())}));
                input.read(buffer.data(), size);
                DWORD count = 0;
                if (input.gcount() != size || !WriteFile(file, buffer.data(), size, &count, nullptr) || count != size) { success = false; break; }
                written += count; completed += count;
            }
            if (!FlushFileBuffers(file)) success = false;
            CloseHandle(file);
        }
    }
    DeleteFileW(temporary);
    if (!success) {
        for (const auto& name : created) DeleteFileW(name.c_str());
        m_error = L"分卷未完成：操作已取消、空间不足或分卷名称冲突";
    }
    return success;
}
