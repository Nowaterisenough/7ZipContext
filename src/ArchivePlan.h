#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <utility>

struct ArchivePlan {
    std::wstring input;
    std::wstring firstVolume;
    std::wstring format;
    std::wstring suggestedPath;
    std::wstring error;
    std::wstring volumeName;
    std::vector<std::pair<std::wstring, std::wstring>> volumeAliases;
    std::vector<std::wstring> volumes;
    uint64_t size = 0;
    uint64_t dataOffset = 0, dataSize = 0;
    bool split = false;
    bool concatenated = false;
    bool needsRename = false;
    bool awaitingVolume = false, probePdf = false;
};

struct NumericArchivePart {
    std::wstring base;
    unsigned index;
    bool masked;
    // Names such as p.001.pdf are candidates only when the bundle has an archive signature.
    bool requiresSignature;
};
std::optional<NumericArchivePart> ParseNumericArchivePart(const std::wstring& filename);

ArchivePlan InspectArchive(const std::wstring& path);
ArchivePlan InspectArchiveFiles(const std::wstring& path, const std::vector<std::wstring>& relatedFiles);
// Partition partN candidates before choosing an entry volume, including across nested directories.
std::vector<std::vector<std::wstring>> GroupNamedArchiveParts(const std::vector<std::wstring>& paths);
std::vector<std::vector<std::wstring>> GroupNumericArchiveParts(const std::vector<std::wstring>& paths);
std::wstring DetectArchiveSignature(const std::wstring& path);
bool CorrectArchiveName(ArchivePlan& plan, std::wstring& error);
std::wstring ArchiveOutputName(const ArchivePlan& plan);
std::wstring FormatBytes(uint64_t bytes);
bool ParseVolumeSize(const std::wstring& text, uint64_t& bytes);
