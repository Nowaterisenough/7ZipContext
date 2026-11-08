// SevenZipCore.h - 7-Zip functionality wrapper
#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

// Forward declarations for 7-Zip types
struct IInArchive;
struct IOutArchive;
struct IInStream;
struct ArchivePlan;

// Progress callback: returns false to cancel operation
using ProgressCallback = std::function<bool(uint64_t completed, uint64_t total)>;

// Archive item information
struct ArchiveItem {
    std::wstring path;
    uint64_t size;
    uint64_t packedSize;
    bool isDir;
    bool isEncrypted;
    FILETIME mtime;
};

// Archive format information
struct ArchiveFormat {
    std::wstring name;
    std::wstring extension;
    GUID classId;
    bool canUpdate;
};

struct CompressionOptions {
    std::wstring format = L"7z";
    std::wstring password;
    unsigned int level = 5;
    uint64_t volumeSize = 0;
    bool encryptHeaders = false;
};

// 7-Zip Core functionality wrapper
class SevenZipCore {
public:
    SevenZipCore();
    ~SevenZipCore();
    static SevenZipCore& Instance();

    // Get supported formats
    const std::vector<ArchiveFormat>& GetFormats() const { return m_formats; }

    // Open an archive for reading
    bool OpenArchive(const std::wstring& path, const std::wstring& password = L"", ProgressCallback progress = nullptr);
    bool OpenArchive(const ArchivePlan& plan, const std::wstring& password = L"", ProgressCallback progress = nullptr);

    // Close the current archive
    void CloseArchive();

    // Check if an archive is open
    bool IsOpen() const { return m_archive != nullptr; }

    // Get the list of items in the archive
    std::vector<ArchiveItem> GetItems();

    // Get number of items
    uint32_t GetItemCount();

    // Also true if the last OpenArchive failed while requesting an encrypted header password.
    bool NeedsPassword() const { return m_needsPassword; }
    // Missing data and unsupported codecs cannot be fixed by trying another password.
    bool CanRetryPassword() const { return m_needsPassword && !m_passwordIndependentFailure; }

    // Test if a password is correct (tries to extract first file)
    bool TestPassword(const std::wstring& password);
    bool TestArchive(const std::wstring& password = L"", ProgressCallback progress = nullptr);
    const std::wstring& LastError() const { return m_error; }

    // Extract all files to the output directory
    bool Extract(const std::wstring& outDir,
                 const std::wstring& password = L"",
                 ProgressCallback progress = nullptr);

    // Extract specific files by index
    bool ExtractFiles(const std::vector<uint32_t>& indices,
                      const std::wstring& outDir,
                      const std::wstring& password = L"",
                      ProgressCallback progress = nullptr);

    // Compress files to an archive
    bool Compress(const std::vector<std::wstring>& srcPaths,
                  const std::wstring& archivePath,
                  const std::wstring& format = L"7z",
                  ProgressCallback progress = nullptr);
    bool Compress(const std::vector<std::wstring>& srcPaths,
                  const std::wstring& archivePath,
                  const CompressionOptions& options,
                  ProgressCallback progress = nullptr);

    // Get the format GUID for a file extension
    const GUID* GetFormatForExtension(const std::wstring& ext);

    // Detect format from file content
    const GUID* DetectFormat(const std::wstring& path);

private:
    // Disable copy
    SevenZipCore(const SevenZipCore&) = delete;
    SevenZipCore& operator=(const SevenZipCore&) = delete;

    // Initialize format list
    void InitFormats();

    // Create archive handler by format GUID
    IInArchive* CreateInArchive(const GUID& formatId);
    IOutArchive* CreateOutArchive(const GUID& formatId);

    // Current archive state
    IInArchive* m_archive = nullptr;
    IInStream* m_inStream = nullptr;
    std::wstring m_currentPath;
    bool m_needsPassword = false;
    bool m_passwordIndependentFailure = false;
    std::wstring m_error;

    // Supported formats
    std::vector<ArchiveFormat> m_formats;
};
