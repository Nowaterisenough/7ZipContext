#include "SevenZipCore.h"
#include "ArchivePlan.h"
#include "WindowsPath.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
static void Check(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
static std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary); file.write(bytes.data(), bytes.size());
}
static std::string StoredZip(const std::string& name, const std::string& data, bool badCrc = false) {
    uint32_t crc = 0xffffffff;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    crc = ~crc; if (badCrc) ++crc;
    std::string zip;
    auto u16 = [&](uint16_t n) { zip.push_back(static_cast<char>(n)); zip.push_back(static_cast<char>(n >> 8)); };
    auto u32 = [&](uint32_t n) { u16(static_cast<uint16_t>(n)); u16(static_cast<uint16_t>(n >> 16)); };
    u32(0x04034b50); u16(20); u16(0); u16(0); u16(0); u16(0); u32(crc); u32(data.size()); u32(data.size());
    u16(name.size()); u16(0); zip += name; zip += data;
    auto central = zip.size();
    u32(0x02014b50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0); u32(crc); u32(data.size()); u32(data.size());
    u16(name.size()); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0); zip += name;
    auto size = zip.size() - central;
    u32(0x06054b50); u16(0); u16(0); u16(1); u16(1); u32(size); u32(central); u16(0);
    return zip;
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        auto root = fs::current_path() / (L"archive-test-" + std::to_wstring(GetTickCount64()));
        fs::create_directories(root / L"input");
        std::mt19937 random(42); std::string payload(350000, 0);
        for (auto& byte : payload) byte = static_cast<char>(random());
        auto source = root / L"input" / L"sample.bin"; Write(source, payload);
        SevenZipCore core;
        for (const auto& format : {L"7z", L"zip"}) {
            CompressionOptions options; options.format = format; options.password = L"test-secret";
            auto archive = root / (std::wstring(L"roundtrip.") + format);
            Check(core.Compress({source.wstring()}, archive.wstring(), options), "encrypted archive creation");
            auto original = Read(archive);
            Check(!core.Compress({source.wstring()}, archive.wstring(), options) && Read(archive) == original, "existing archive is preserved");
            Check(core.OpenArchive(archive.wstring()), "open archive");
            Check(!core.TestArchive(L"wrong"), "wrong password rejected");
            Check(core.TestArchive(options.password), "correct password and CRC accepted");
            auto out = root / (std::wstring(L"extracted-") + format);
            Check(core.Extract(out.wstring(), options.password), "extract encrypted archive");
            Check(Read(out / source.filename()) == payload, "roundtrip bytes match");
            Check(!core.Extract(out.wstring(), options.password), "existing output is not overwritten");
            core.CloseArchive();
            auto disguised = root / (std::wstring(L"disguised-") + format + L".jpg"); fs::copy_file(archive, disguised);
            auto plan = InspectArchive(disguised.wstring());
            Check(plan.needsRename && plan.format == format, "content signature overrides wrong extension");
            Check(core.OpenArchive(disguised.wstring()), "open disguised archive"); core.CloseArchive();
            std::wstring error;
            Write(plan.suggestedPath, "keep-existing");
            Check(!CorrectArchiveName(plan, error) && Read(plan.suggestedPath) == "keep-existing", "rename collision preserves destination");
            fs::remove(plan.suggestedPath);
            Check(CorrectArchiveName(plan, error), "correct extension without overwriting");
            options.password.clear(); options.volumeSize = 65536;
            auto split = root / (std::wstring(L"split.") + format);
            Check(core.Compress({source.wstring()}, split.wstring(), options), "split archive creation");
            auto volume2 = split.wstring() + L".002";
            auto splitPlan = InspectArchive(volume2);
            Check(splitPlan.split && splitPlan.volumes.size() > 2 && splitPlan.error.empty(), "selecting later volume resolves first volume");
            Check(core.OpenArchive(volume2), "open concatenated volumes");
            Check(core.TestArchive(), "split archive CRC verification");
            auto splitOut = root / (std::wstring(L"split-output-") + format);
            Check(core.Extract(splitOut.wstring()), "extract concatenated volumes");
            Check(Read(splitOut / source.filename()) == payload, "split roundtrip bytes match"); core.CloseArchive();
            // A renamed suffix is now a valid disguise; make the numbered volume genuinely absent.
            fs::rename(volume2, root / (std::wstring(L"absent-middle-") + format));
            Check(!InspectArchive(split.wstring() + L".001").error.empty(), "missing middle volume detected");
            Check(!core.OpenArchive(split.wstring() + L".001"), "missing volume prevents extraction");
            auto folder = root / (std::wstring(L"nested-") + format);
            fs::create_directories(folder / L"资料" / L"empty");
            Write(folder / L"资料" / L"file.txt", "nested payload");
            options.volumeSize = 0;
            auto nestedArchive = root / (std::wstring(L"folders.") + format);
            Check(core.Compress({folder.wstring()}, nestedArchive.wstring(), options), "compress nested and empty folders");
            Check(core.OpenArchive(nestedArchive.wstring()), "open folder archive");
            auto nestedOut = root / (std::wstring(L"nested-out-") + format);
            Check(core.Extract(nestedOut.wstring()), "extract nested and empty folders");
            Check(Read(nestedOut / folder.filename() / L"资料" / L"file.txt") == "nested payload" &&
                fs::is_directory(nestedOut / folder.filename() / L"资料" / L"empty"), "Unicode names and empty folders roundtrip");
            core.CloseArchive();
        }
        auto malicious = root / L"traversal.zip"; Write(malicious, StoredZip("../escape.txt", "do not write"));
        Check(core.OpenArchive(malicious.wstring()), "open traversal fixture");
        Check(!core.Extract((root / L"safe").wstring()) && !fs::exists(root / L"escape.txt"), "path traversal rejected");
        core.CloseArchive();
        auto relative = root / L"relative.zip"; Write(relative, StoredZip("./folder/file.txt", "relative path"));
        Check(core.OpenArchive(relative.wstring()) && core.Extract((root / L"relative-out").wstring()), "safe dot-relative archive paths accepted");
        Check(Read(root / L"relative-out" / L"folder" / L"file.txt") == "relative path", "relative path stays inside destination");
        core.CloseArchive();
        auto device = root / L"device.zip"; Write(device, StoredZip("NUL.txt", "do not write"));
        Check(core.OpenArchive(device.wstring()) && !core.Extract((root / L"device-out").wstring()), "Windows device names rejected");
        core.CloseArchive();
        auto corrupt = root / L"corrupt.zip"; Write(corrupt, StoredZip("data.txt", "broken crc", true));
        Check(core.OpenArchive(corrupt.wstring()), "open bad CRC fixture");
        Check(!core.TestArchive(), "per-file CRC failure propagates");
        Check(!core.Extract((root / L"corrupt-out").wstring()), "extraction does not report CRC failure as success");
        core.CloseArchive();
        auto invalid = root / L"invalid.zip"; Write(invalid, "plain text");
        Check(!core.OpenArchive(invalid.wstring()), "unrecognized file rejected");
        auto zipBytes = StoredZip("segment.bin", payload);
        const size_t cut = 200000, endRecord = zipBytes.size() - 22;
        auto put16 = [&](size_t pos, uint16_t value) { zipBytes[pos] = static_cast<char>(value); zipBytes[pos + 1] = static_cast<char>(value >> 8); };
        put16(endRecord + 4, 1); put16(endRecord + 6, 1);
        uint32_t directoryOffset = static_cast<uint32_t>(30 + std::string("segment.bin").size() + payload.size() - cut);
        put16(endRecord + 16, static_cast<uint16_t>(directoryOffset)); put16(endRecord + 18, static_cast<uint16_t>(directoryOffset >> 16));
        Write(root / L"native.z01", zipBytes.substr(0, cut)); Write(root / L"native.zip", zipBytes.substr(cut));
        auto native = InspectArchive((root / L"native.z01").wstring());
        Check(native.split && native.volumes.size() == 2 && native.error.empty(), "native ZIP volumes grouped");
        Check(core.OpenArchive((root / L"native.z01").wstring()), "open native split ZIP");
        Check(core.TestArchive() && core.Extract((root / L"native-out").wstring()), "native split ZIP verifies and extracts");
        Check(Read(root / L"native-out" / L"segment.bin") == payload, "native split ZIP roundtrip bytes match");
        core.CloseArchive();
        const auto longRoot = root / std::wstring(150, L'a') / std::wstring(150, L'b');
        fs::create_directories(NativePath(longRoot.wstring()));
        fs::copy_file(root / L"native.z01", NativePath((longRoot / L"native.z01").wstring()));
        fs::copy_file(root / L"native.zip", NativePath((longRoot / L"native.zip").wstring()));
        const auto longPlan = InspectArchive((longRoot / L"native.zip").wstring());
        Check(longPlan.error.empty() && longPlan.volumes.size() == 2, "native volumes discovered beyond MAX_PATH");
        Check(core.OpenArchive(longPlan) && core.TestArchive(), "long-path native volumes open and pass CRC");
        Check(core.Extract((longRoot / L"output").wstring()), "long output directories and files extract");
        const auto longOutput = NativePath((longRoot / L"output" / L"segment.bin").wstring());
        Check(Read(longOutput) == payload, "long-path extracted bytes match");
        Check(!core.Extract((longRoot / L"output").wstring()) && Read(longOutput) == payload,
              "long-path collision preserves existing output");
        core.CloseArchive();
        CompressionOptions options;
        Check(!core.Compress({source.wstring()}, (root / L"cancelled.7z").wstring(), options,
            [](uint64_t, uint64_t) { return false; }), "compression cancellation propagates");
        Check(!fs::exists(root / L"cancelled.7z"), "cancelled compression leaves no final archive");
        Check(!core.Compress({(root / L"input").wstring()}, (root / L"input" / L"nested.7z").wstring(), options), "output inside input is rejected");
        uint64_t bytes = 0;
        Check(ParseVolumeSize(L"100M", bytes) && bytes == 104857600, "volume size parser");
        Check(!ParseVolumeSize(L"-1G", bytes) && !ParseVolumeSize(L"9999999999999999999999G", bytes), "invalid volume size rejected");
        if (argc == 2) {
            auto fixture = fs::absolute(argv[1]);
            auto plan = InspectArchive(fixture.wstring());
            Check(plan.split && plan.error.empty(), "RAR volumes grouped from a later volume");
            Check(!core.OpenArchive(fixture.wstring(), L"wrong"), "encrypted RAR header rejects wrong password");
            Check(core.OpenArchive(fixture.wstring(), L"test-secret"), "encrypted RAR header opens with password");
            Check(core.TestArchive(L"test-secret"), "multi-volume RAR CRC passes");
            Check(core.Extract((root / L"rar-out").wstring(), L"test-secret"), "multi-volume RAR extracts");
            Check(Read(root / L"rar-out" / L"payload.bin") == Read(fixture.parent_path() / L"payload.bin"), "RAR roundtrip bytes match");
            core.CloseArchive();
            auto last = fs::path(plan.volumes.back()); auto missing = last.wstring() + L".missing";
            fs::rename(last, missing);
            bool rejected = !core.OpenArchive(plan.firstVolume, L"test-secret"); core.CloseArchive();
            fs::rename(missing, last);
            Check(rejected, "missing final RAR volume rejected");
        }
        std::cout << "All archive integration tests passed. Fixtures: " << root.string() << '\n';
        CoUninitialize(); return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; CoUninitialize(); return 1;
    }
}
