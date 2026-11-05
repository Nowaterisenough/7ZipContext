#include "ArchivePlan.h"
#include "WindowsPath.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <set>

namespace fs = std::filesystem;
namespace {
std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

struct NamedPart {
    std::wstring prefix, digits, suffix;
    unsigned index() const { return static_cast<unsigned>(std::stoul(digits)); }
};
bool ParsePart(const std::wstring& name, NamedPart& part) {
    static const std::wregex pattern(L"(.+\\.part)([0-9]{1,6})(\\.[^.]+)?");
    std::wsmatch match;
    if (!std::regex_match(name, match, pattern)) return false;
    part = {match[1].str(), match[2].str(), match[3].str()};
    // A trailing .001 is a separate numeric volume family, even after a partN stem.
    return !(part.suffix.size() >= 4 && part.suffix.size() <= 7 &&
             std::all_of(part.suffix.begin() + 1, part.suffix.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }));
}

// Native RAR repeats its marker on every volume; a byte-split 7z/ZIP generally has only one header.
int RarVersion(const unsigned char* data, size_t count) {
    constexpr unsigned char marker[]{'R', 'a', 'r', '!', 0x1a, 0x07};
    if (count < 7 || !std::equal(std::begin(marker), std::end(marker), data)) return 0;
    if (data[6] == 0) return 4;
    return count >= 8 && data[6] == 1 && data[7] == 0 ? 5 : 0;
}

struct ZipVolumeInfo { uint32_t lastDisk = 0; uint64_t directory = 0, directorySize = 0; };
bool ZipEndPresent(const fs::path& path) {
    std::ifstream file(fs::path(NativePath(path.wstring())), std::ios::binary); file.seekg(0, std::ios::end);
    const auto end = file.tellg(); if (end < 22) return false;
    const auto length = static_cast<uint64_t>(end);
    std::string tail(static_cast<size_t>(std::min<uint64_t>(length, 256 * 1024)), '\0');
    file.seekg(static_cast<std::streamoff>(length - tail.size()));
    if (!file.read(tail.data(), tail.size())) return false;
    return tail.find(std::string("PK\x05\x06", 4)) != std::string::npos;
}
bool ZipVolumeTail(const fs::path& path, ZipVolumeInfo& info) {
    std::ifstream file(fs::path(NativePath(path.wstring())), std::ios::binary); file.seekg(0, std::ios::end);
    const auto end = file.tellg(); if (end < 22) return false;
    const uint64_t length = static_cast<uint64_t>(end);
    std::vector<unsigned char> tail(static_cast<size_t>(std::min<uint64_t>(length, 256 * 1024)));
    file.seekg(static_cast<std::streamoff>(length - tail.size()));
    if (!file.read(reinterpret_cast<char*>(tail.data()), tail.size())) return false;
    auto u16 = [](const unsigned char* p) -> uint32_t { return p[0] | (static_cast<uint32_t>(p[1]) << 8); };
    auto u32 = [&](const unsigned char* p) -> uint32_t { return u16(p) | (u16(p + 2) << 16); };
    auto u64 = [&](const unsigned char* p) -> uint64_t { return u32(p) | (static_cast<uint64_t>(u32(p + 4)) << 32); };
    for (size_t n = tail.size() - 21; n > 0; --n) {
        const auto i = n - 1; const auto* record = tail.data() + i;
        if (u32(record) != 0x06054b50 || i + 22 + u16(record + 20) > tail.size()) continue;
        uint32_t disk = u16(record + 4), directoryDisk = u16(record + 6);
        uint64_t count = u16(record + 10), size = u32(record + 12), offset = u32(record + 16);
        if (i >= 20 && u32(record - 20) == 0x07064b50) {
            const auto location = u64(record - 12);
            if (location > length - 56) continue;
            std::array<unsigned char,56> extended{};
            file.clear(); file.seekg(static_cast<std::streamoff>(location));
            if (!file.read(reinterpret_cast<char*>(extended.data()), extended.size()) || u32(extended.data()) != 0x06064b50) continue;
            disk = u32(extended.data() + 16); directoryDisk = u32(extended.data() + 20);
            count = u64(extended.data() + 32); size = u64(extended.data() + 40); offset = u64(extended.data() + 48);
        }
        if (!disk || disk > 1000 || directoryDisk != disk || !count || size < 46 || offset > length || size > length - offset) continue;
        std::array<unsigned char,4> signature{};
        file.clear(); file.seekg(static_cast<std::streamoff>(offset));
        if (!file.read(reinterpret_cast<char*>(signature.data()), 4) || u32(signature.data()) != 0x02014b50) continue;
        info = {disk, offset, size}; return true;
    }
    return false;
}

ArchivePlan NativeZipPlan(const fs::path& main, const ZipVolumeInfo& info, const std::vector<std::wstring>* related) {
    ArchivePlan plan; plan.input = plan.firstVolume = main.wstring(); plan.format = L"zip"; plan.split = true;
    const auto base = main.stem().wstring(); plan.volumeName = base + L".zip";
    std::vector<fs::path> available; std::error_code ec;
    if (related) for (const auto& path : *related) available.push_back(fs::path(path).lexically_normal());
    const auto listing = ReadWindowsDirectory(main.parent_path().wstring());
    for (const auto& entry : listing.entries) if (!(entry.attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) available.push_back(fs::path(entry.path).lexically_normal());
    plan.volumes.push_back(main.wstring()); plan.volumeAliases.emplace_back(plan.volumeName, main.wstring());
    for (uint32_t index = 1; index <= info.lastDisk; ++index) {
        std::wostringstream number; number << std::setw(2) << std::setfill(L'0') << index;
        const auto alias = base + L".z" + number.str();
        std::wostringstream numeric; numeric << std::setw(3) << std::setfill(L'0') << index;
        const std::vector<std::wstring> names{Lower(alias), Lower(base + std::to_wstring(index)), Lower(base + L"." + numeric.str()), Lower(base + L".zip." + numeric.str())};
        std::vector<fs::path> matches, local; std::set<std::wstring> seen;
        for (const auto& file : available) {
            if (Lower(file.wstring()) == Lower(main.wstring()) || !seen.insert(Lower(file.wstring())).second) continue;
            if (std::find(names.begin(), names.end(), Lower(file.filename().wstring())) == names.end()) continue;
            matches.push_back(file);
            if (Lower(file.parent_path().wstring()) == Lower(main.parent_path().wstring())) local.push_back(file);
        }
        const auto& choices = local.empty() ? matches : local;
        if (choices.empty()) { plan.error = L"ZIP 分卷缺少 " + alias + L"（也可提供 " + base + std::to_wstring(index) + L"）"; return plan; }
        if (choices.size() != 1) { plan.error = L"ZIP 分卷候选不唯一：" + alias; return plan; }
        if (index == 1) {
            std::ifstream first(fs::path(NativePath(choices.front().wstring())), std::ios::binary); std::array<unsigned char,4> bytes{}; first.read(reinterpret_cast<char*>(bytes.data()), 4);
            if (bytes[0] != 'P' || bytes[1] != 'K' || !((bytes[2] == 3 && bytes[3] == 4) || (bytes[2] == 7 && bytes[3] == 8))) {
                plan.error = L"ZIP 首卷文件头无效：" + choices.front().filename().wstring(); return plan;
            }
        }
        plan.volumes.push_back(choices.front().wstring()); plan.volumeAliases.emplace_back(alias, choices.front().wstring());
    }
    for (const auto& path : plan.volumes) {
        const auto size = fs::file_size(NativePath(path), ec); if (ec) { plan.error = L"无法读取 ZIP 分卷"; return plan; } plan.size += size;
    }
    return plan;
}

bool AppendedZip(std::ifstream& file, uint64_t& offset, uint64_t& size) {
    file.clear(); file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 22) return false;
    const uint64_t length = static_cast<uint64_t>(end);
    std::vector<unsigned char> tail(static_cast<size_t>(std::min<uint64_t>(length, 256 * 1024)));
    file.seekg(static_cast<std::streamoff>(length - tail.size()));
    if (!file.read(reinterpret_cast<char*>(tail.data()), tail.size())) return false;
    auto u16 = [](const unsigned char* bytes) -> uint32_t { return bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8); };
    auto u32 = [&](const unsigned char* bytes) -> uint64_t { return u16(bytes) | (static_cast<uint64_t>(u16(bytes + 2)) << 16); };
    auto u64 = [&](const unsigned char* bytes) -> uint64_t { return u32(bytes) | (u32(bytes + 4) << 32); };
    auto readAt = [&](uint64_t at, unsigned char* data, size_t count) {
        file.clear(); file.seekg(static_cast<std::streamoff>(at));
        return static_cast<bool>(file.read(reinterpret_cast<char*>(data), count));
    };
    for (size_t remaining = tail.size() - 21; remaining > 0; --remaining) {
        const size_t i = remaining - 1; const auto* record = tail.data() + i;
        if (u32(record) != 0x06054b50 || u16(record + 4) != 0 || u16(record + 6) != 0) continue;
        uint64_t entries = u16(record + 10), directorySize = u32(record + 12), directoryOffset = u32(record + 16);
        const uint64_t recordAt = length - tail.size() + i;
        if (recordAt + 22 + u16(record + 20) > length) continue;
        uint64_t directoryEnd = recordAt, locatedBase = 0;
        const bool zip64 = i >= 20 && u32(record - 20) == 0x07064b50;
        if (zip64) {
            const auto* locator = record - 20;
            if (u32(locator + 4) != 0 || u32(locator + 16) != 1) continue;
            bool found = false;
            // A ZIP64 end record (including optional extra data) precedes the locator, not the ordinary end record.
            const size_t locatorAt = i - 20;
            if (locatorAt < 56) continue;
            for (size_t remaining64 = locatorAt - 55; remaining64 > 0; --remaining64) {
                const size_t at = remaining64 - 1; const auto* extended = tail.data() + at;
                if (u32(extended) != 0x06064b50 || u64(extended + 4) != locatorAt - at - 12) continue;
                if (u32(extended + 16) != 0 || u32(extended + 20) != 0 || u64(extended + 24) != u64(extended + 32)) continue;
                const uint64_t physical = length - tail.size() + at, relative = u64(locator + 8);
                if (relative > physical) continue;
                entries = u64(extended + 32); directorySize = u64(extended + 40); directoryOffset = u64(extended + 48);
                directoryEnd = physical; locatedBase = physical - relative; found = true; break;
            }
            if (!found) continue;
        } else if (entries == 65535 || u16(record + 8) != entries || directorySize == 0xffffffff || directoryOffset == 0xffffffff) continue;
        if (!entries || entries > directorySize / 46 || directorySize > directoryEnd || directoryOffset > directoryEnd - directorySize) continue;
        const auto directoryAt = directoryEnd - directorySize, base = directoryAt - directoryOffset;
        if (zip64 && base != locatedBase) continue;
        if (directoryAt < 4) continue;
        std::array<unsigned char, 46> directory{};
        if (!readAt(directoryAt, directory.data(), directory.size()) || u32(directory.data()) != 0x02014b50) continue;
        uint64_t localOffset = u32(directory.data() + 42), localDisk = u16(directory.data() + 34);
        if (localOffset == 0xffffffff || localDisk == 0xffff) {
            const auto nameSize = u16(directory.data() + 28), extraSize = u16(directory.data() + 30);
            if (46ULL + nameSize + extraSize > directorySize) continue;
            std::vector<unsigned char> extra(extraSize);
            if (!readAt(directoryAt + 46 + nameSize, extra.data(), extra.size())) continue;
            bool resolved = false;
            for (size_t at = 0; at + 4 <= extra.size();) {
                const auto type = u16(extra.data() + at), count = u16(extra.data() + at + 2);
                at += 4; if (count > extra.size() - at) break;
                if (type == 1) {
                    size_t field = 0;
                    if (u32(directory.data() + 24) == 0xffffffff) field += 8;
                    if (u32(directory.data() + 20) == 0xffffffff) field += 8;
                    if (localOffset == 0xffffffff) {
                        if (field + 8 > count) break;
                        localOffset = u64(extra.data() + at + field); field += 8;
                    }
                    if (localDisk == 0xffff) {
                        if (field + 4 > count) break;
                        localDisk = u32(extra.data() + at + field);
                    }
                    resolved = true; break;
                }
                at += count;
            }
            if (!resolved) continue;
        }
        if (localDisk != 0 || directoryOffset < 4 || localOffset > directoryOffset - 4) continue;
        std::array<unsigned char, 4> local{};
        if (!readAt(base + localOffset, local.data(), local.size()) || u32(local.data()) != 0x04034b50) continue;
        // The central directory locates the ZIP after an arbitrarily large prefix; do not scan the video payload.
        offset = base; size = recordAt + 22 + u16(record + 20) - base;
        return true;
    }
    return false;
}

std::wstring Signature(const fs::path& path, uint64_t* dataOffset = nullptr, uint64_t* dataSize = nullptr) {
    std::ifstream file(fs::path(NativePath(path.wstring())), std::ios::binary);
    std::array<unsigned char, 65536> data{};
    file.read(reinterpret_cast<char*>(data.data()), data.size());
    const auto n = file.gcount();
    auto match = [&](size_t at, std::initializer_list<unsigned char> bytes) {
        return at + bytes.size() <= static_cast<size_t>(n) &&
            std::equal(bytes.begin(), bytes.end(), data.begin() + at);
    };
    if (match(0, {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c})) return L"7z";
    if (match(0, {0x50, 0x4b, 0x03, 0x04}) || match(0, {0x50, 0x4b, 0x05, 0x06}) ||
        match(0, {0x50, 0x4b, 0x07, 0x08})) return L"zip";
    const auto rar = RarVersion(data.data(), static_cast<size_t>(n));
    if (rar) return rar == 5 ? L"rar5" : L"rar";
    if (match(0, {0x1f, 0x8b})) return L"gz";
    if (match(0, {0x42, 0x5a, 0x68})) return L"bz2";
    if (match(0, {0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00})) return L"xz";
    if (match(0, {0x4d, 0x53, 0x43, 0x46})) return L"cab";
    if (match(257, {0x75, 0x73, 0x74, 0x61, 0x72})) return L"tar";
    if (match(32769, {0x43, 0x44, 0x30, 0x30, 0x31})) return L"iso";
    uint64_t offset = 0, size = 0;
    if (AppendedZip(file, offset, size)) {
        if (dataOffset) *dataOffset = offset;
        if (dataSize) *dataSize = size;
        return L"zip";
    }
    ZipVolumeInfo nativeZip;
    if (ZipVolumeTail(path, nativeZip)) return L"zip";
    return L"";
}
}

std::vector<std::vector<std::wstring>> GroupNamedArchiveParts(const std::vector<std::wstring>& paths) {
    struct Entry { NamedPart part; std::wstring path, directory; };
    std::map<std::pair<std::wstring, int>, std::vector<Entry>> families;
    for (const auto& path : paths) {
        const auto file = fs::path(path).lexically_normal();
        NamedPart part;
        if (!ParsePart(Lower(file.filename().wstring()), part)) continue;
        std::ifstream stream(fs::path(NativePath(path)), std::ios::binary);
        std::array<unsigned char, 8> head{};
        stream.read(reinterpret_cast<char*>(head.data()), head.size());
        families[{part.prefix, RarVersion(head.data(), static_cast<size_t>(stream.gcount()))}].push_back(
            {part, path, Lower(file.parent_path().wstring())});
    }
    std::vector<std::vector<std::wstring>> groups;
    for (const auto& [family, entries] : families) {
        std::map<unsigned, std::wstring> locations;
        bool separateFolders = false;
        for (const auto& e : entries) {
            const auto [it, added] = locations.emplace(e.part.index(), e.directory);
            if (!added && it->second != e.directory) separateFolders = true;
        }
        std::map<std::wstring, std::vector<const Entry*>> folders;
        for (const auto& e : entries) folders[separateFolders ? e.directory : L""].push_back(&e);
        for (const auto& [directory, bundle] : folders) {
            // Independent same-stem sets with distinct first volumes retain their original suffixes.
            std::set<std::wstring> firstSuffixes;
            bool allowMixedSuffixes = family.second != 0;
            for (const auto* e : bundle) if (e->part.index() == 1) {
                firstSuffixes.insert(e->part.suffix);
                if (!allowMixedSuffixes) {
                    const auto signature = Signature(e->path);
                    allowMixedSuffixes = signature == L"7z" || signature == L"zip";
                }
            }
            const bool keepSuffix = !allowMixedSuffixes || firstSuffixes.size() > 1;
            std::map<std::wstring, std::vector<std::wstring>> bySuffix;
            for (const auto* e : bundle) bySuffix[keepSuffix ? e->part.suffix : L""].push_back(e->path);
            for (auto& [suffix, files] : bySuffix) groups.push_back(std::move(files));
        }
    }
    return groups;
}

static ArchivePlan InspectArchiveImpl(const std::wstring& path, const std::vector<std::wstring>* relatedFiles = nullptr) {
    ArchivePlan plan;
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec).lexically_normal();
    plan.input = absolute.wstring();
    plan.firstVolume = plan.input;
    if (ec || !fs::is_regular_file(NativePath(absolute.wstring()), ec)) {
        plan.error = L"文件不存在或无法读取";
        return plan;
    }
    ZipVolumeInfo nativeZip;
    if (ZipVolumeTail(absolute, nativeZip)) return NativeZipPlan(absolute, nativeZip, relatedFiles);
    const auto name = Lower(absolute.filename().wstring());
    std::wsmatch match;
    std::wstring prefix;
    NamedPart selectedPart;
    auto sibling = [&](const std::wstring& filename) -> fs::path {
        if (relatedFiles) {
            fs::path found;
            for (const auto& file : *relatedFiles) if (Lower(fs::path(file).filename().wstring()) == filename) {
                if (!found.empty()) { plan.error = L"分卷名称重复，无法确定属于哪组压缩包"; return {}; }
                found = file;
            }
            return found;
        }
        const auto file = absolute.parent_path() / filename;
        return fs::exists(NativePath(file.wstring()), ec) ? file : fs::path{};
    };
    unsigned int first = 1;
    enum class Kind { None, Numeric, NamedPart, RarOld, Zip } kind = Kind::None;
    if (std::regex_match(name, match, std::wregex(L"(.+)\\.([0-9]{3,6})"))) {
        prefix = match[1].str() + L".";
        kind = Kind::Numeric;
        plan.concatenated = true;
    } else if (ParsePart(name, selectedPart)) {
        prefix = selectedPart.prefix; kind = Kind::NamedPart;
    } else if (std::regex_match(name, match, std::wregex(L"(.+)\\.r([0-9]{2,3})"))) {
        prefix = match[1].str() + L".r"; kind = Kind::RarOld; first = 0;
    } else if (std::regex_match(name, match, std::wregex(L"(.+)\\.z([0-9]{2,3})"))) {
        prefix = match[1].str() + L".z"; kind = Kind::Zip;
    } else if (absolute.has_extension()) {
        const auto stem = Lower(absolute.stem().wstring());
        if (Lower(absolute.extension().wstring()) == L".rar" && !sibling(stem + L".r00").empty()) {
            prefix = stem + L".r"; kind = Kind::RarOld; first = 0;
        } else if (Lower(absolute.extension().wstring()) == L".zip" && !sibling(stem + L".z01").empty()) {
            prefix = stem + L".z"; kind = Kind::Zip;
        }
    }
    if (!plan.error.empty()) return plan;
    if (kind != Kind::None) {
        plan.split = true;
        std::map<unsigned int, fs::path> parts;
        std::vector<fs::path> files;
        if (relatedFiles) for (const auto& file : *relatedFiles) files.emplace_back(file);
        else {
            const auto listing = ReadWindowsDirectory(absolute.parent_path().wstring());
            if (listing.error != ERROR_SUCCESS) { plan.error = L"无法读取分卷所在目录"; return plan; }
            for (const auto& entry : listing.entries) if (!(entry.attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) files.emplace_back(entry.path);
        }
        if (kind == Kind::NamedPart) {
            std::vector<std::wstring> candidates;
            for (const auto& file : files) {
                NamedPart part;
                if (ParsePart(Lower(file.filename().wstring()), part) && part.prefix == prefix) candidates.push_back(file.wstring());
            }
            files.clear();
            for (const auto& group : GroupNamedArchiveParts(candidates)) {
                if (std::none_of(group.begin(), group.end(), [&](const auto& p) { return _wcsicmp(fs::path(p).lexically_normal().c_str(), absolute.c_str()) == 0; })) continue;
                for (const auto& p : group) files.emplace_back(p);
                break;
            }
        }
        for (const auto& file : files) {
            const auto other = Lower(file.filename().wstring());
            if (!other.starts_with(prefix) || other.size() <= prefix.size()) continue;
            const auto number = kind == Kind::NamedPart ? other.substr(prefix.size(), other.find(L'.', prefix.size()) - prefix.size()) : other.substr(prefix.size());
            if (number.size() > 6 || !std::all_of(number.begin(), number.end(), iswdigit)) continue;
            const auto index = static_cast<unsigned int>(std::stoul(number));
            if (parts.count(index)) { plan.error = L"存在重复编号的分卷：第 " + std::to_wstring(index) + L" 卷"; return plan; }
            parts[index] = file;
        }
        if (ec) { plan.error = L"无法读取分卷所在目录"; return plan; }
        if (parts.empty() || parts.begin()->first != first) {
            plan.error = L"缺少首卷，请将所有分卷放在同一文件夹";
            return plan;
        }
        auto expected = first;
        for (const auto& [index, file] : parts) {
            if (index != expected++) { plan.error = L"缺少第 " + std::to_wstring(expected - 1) + L" 卷"; return plan; }
            plan.volumes.push_back(file.wstring());
        }
        if (kind == Kind::RarOld || kind == Kind::Zip) {
            auto base = prefix.substr(0, prefix.size() - 2);
            const auto entryName = base + (kind == Kind::Zip ? L".zip" : L".rar");
            auto entry = sibling(entryName);
            if (!plan.error.empty()) return plan;
            if (entry.empty()) { plan.error = L"缺少主压缩包 " + entryName; return plan; }
            plan.firstVolume = entry.wstring();
            plan.volumes.insert(plan.volumes.begin(), entry.wstring());
        } else {
            plan.firstVolume = plan.volumes.front();
        }
    } else {
        plan.volumes.push_back(plan.input);
    }
    for (const auto& file : plan.volumes) {
        auto size = fs::file_size(NativePath(file), ec);
        if (ec) { plan.error = L"无法读取分卷大小"; return plan; }
        plan.size += size;
    }
    plan.format = kind == Kind::Zip ? L"zip" : Signature(plan.firstVolume, &plan.dataOffset, &plan.dataSize);
    if (plan.format.empty()) {
        // Signature-less TAR and self-extracting archives are validated by the engine.
        const auto ext = Lower(fs::path(plan.firstVolume).extension().wstring());
        if (ext == L".tar") plan.format = L"tar";
        else if (ext == L".exe") plan.format = L"sfx";
        else { plan.error = L"未识别到支持的压缩包文件头"; return plan; }
    }
    if (kind == Kind::NamedPart) {
        const bool nativeRar = plan.format == L"rar" || plan.format == L"rar5";
        plan.concatenated = !nativeRar;
        if (nativeRar) {
            NamedPart firstPart;
            ParsePart(Lower(fs::path(plan.firstVolume).filename().wstring()), firstPart);
            for (size_t i = 0; i < plan.volumes.size(); ++i) {
                std::wostringstream alias;
                alias << firstPart.prefix << std::setfill(L'0') << std::setw(static_cast<int>(firstPart.digits.size())) << i + 1 << L".rar";
                plan.volumeAliases.emplace_back(alias.str(), plan.volumes[i]);
            }
            plan.volumeName = plan.volumeAliases.front().first;
        }
    }
    if (!plan.split && plan.format == L"zip" && !ZipEndPresent(absolute)) plan.awaitingVolume = true;
    auto extension = L"." + (plan.format == L"rar5" ? L"rar" : plan.format);
    const auto ext = Lower(absolute.extension().wstring());
    if (!plan.split && plan.format != L"sfx" && ext != extension &&
        !(plan.format == L"gz" && ext == L".tgz") && !(plan.format == L"bz2" && ext == L".tbz2")) {
        auto corrected = absolute;
        corrected.replace_extension(extension);
        plan.suggestedPath = corrected.wstring();
        plan.needsRename = true;
    }
    return plan;
}

ArchivePlan InspectArchive(const std::wstring& path) {
    try { return InspectArchiveImpl(path); }
    catch (const std::exception&) {
        ArchivePlan plan;
        plan.input = plan.firstVolume = path;
        plan.error = L"无法访问文件或分卷所在目录";
        return plan;
    }
}

ArchivePlan InspectArchiveFiles(const std::wstring& path, const std::vector<std::wstring>& relatedFiles) {
    try { return InspectArchiveImpl(path, &relatedFiles); }
    catch (const std::exception&) {
        ArchivePlan plan; plan.input = plan.firstVolume = path;
        plan.error = L"无法访问内层压缩包或分卷"; return plan;
    }
}

std::wstring DetectArchiveSignature(const std::wstring& path) { return Signature(fs::path(path)); }

bool CorrectArchiveName(ArchivePlan& plan, std::wstring& error) {
    if (!plan.needsRename || !plan.error.empty()) return false;
    if (!MoveFileExW(NativePath(plan.input).c_str(), NativePath(plan.suggestedPath).c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = L"修正失败：目标文件已存在，或文件正在使用（" + std::to_wstring(GetLastError()) + L"）";
        return false;
    }
    plan = InspectArchive(plan.suggestedPath);
    return true;
}

std::wstring ArchiveOutputName(const ArchivePlan& plan) {
    auto name = fs::path(plan.firstVolume).filename().wstring();
    name = std::regex_replace(name, std::wregex(L"\\.[0-9]{3,6}$"), L"");
    if (plan.split) name = std::regex_replace(name, std::wregex(L"\\.part[0-9]+(?:\\.[^.]+)?$", std::regex::icase),
                                            L"." + (plan.format == L"rar5" ? L"rar" : plan.format));
    auto stem = fs::path(name).stem().wstring();
    return stem.empty() ? L"解压文件" : stem;
}

std::wstring FormatBytes(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
    std::wostringstream text;
    text << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << L" " << units[unit];
    return text.str();
}

bool ParseVolumeSize(const std::wstring& text, uint64_t& bytes) {
    bytes = 0;
    if (text.empty() || text == L"不分卷" || text == L"0") return true;
    std::wsmatch match;
    if (!std::regex_match(text, match, std::wregex(L"\\s*([0-9]+)\\s*([KMG]?)B?\\s*", std::regex::icase))) return false;
    try {
        const auto value = std::stoull(match[1]);
        auto unit = Lower(match[2]);
        uint64_t multiplier = unit == L"g" ? 1073741824ULL : unit == L"k" ? 1024ULL : 1048576ULL;
        if (value == 0 || value > (1ULL << 50) / multiplier) return false;
        bytes = value * multiplier;
        return true;
    } catch (...) { return false; }
}
