#include "ui/ArchiveController.h"
#include "ui/NestedArchives.h"
#include "ui/ArchiveCleanup.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool Write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray Read(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
bool Pack(const QStringList& paths, const QString& target, const QString& password = {}, bool split = false) {
    SevenZipCore core; CompressionOptions options;
    options.format = QFileInfo(target).suffix() == "zip" ? L"zip" : L"7z";
    options.password = password.toStdWString(); options.encryptHeaders = options.format == L"7z" && !password.isEmpty();
    options.volumeSize = split ? 8192 : 0; options.level = 0;
    std::vector<std::wstring> inputs; for (const auto& path : paths) inputs.push_back(path.toStdWString());
    return core.Compress(inputs, target.toStdWString(), options);
}
void Put(QByteArray& data, qsizetype at, quint64 value, int width) {
    for (int i = 0; i < width; ++i) data[at + i] = static_cast<char>(value >> (i * 8));
}
quint64 Get(const QByteArray& data, qsizetype at, int width) {
    quint64 value = 0;
    for (int i = 0; i < width; ++i) value |= static_cast<quint64>(static_cast<unsigned char>(data[at + i])) << (i * 8);
    return value;
}
QStringList SplitNativeZip(const QString& path, const QString& parts, bool zip64) {
    auto bytes = Read(path); const auto end = bytes.size() - 22;
    if (end < 131072 || Get(bytes, end, 4) != 0x06054b50) return {};
    const auto central = Get(bytes, end + 16, 4) - 131072, size = Get(bytes, end + 12, 4);
    Put(bytes, end + 4, 2, 2); Put(bytes, end + 6, 2, 2); Put(bytes, end + 16, central, 4);
    if (zip64) {
        QByteArray extended(76, 0);
        Put(extended, 0, 0x06064b50, 4); Put(extended, 4, 44, 8);
        Put(extended, 12, 45, 2); Put(extended, 14, 45, 2);
        Put(extended, 16, 2, 4); Put(extended, 20, 2, 4);
        Put(extended, 24, 1, 8); Put(extended, 32, 1, 8);
        Put(extended, 40, size, 8); Put(extended, 48, central, 8);
        Put(extended, 56, 0x07064b50, 4); Put(extended, 60, 2, 4);
        Put(extended, 64, end - 131072, 8); Put(extended, 72, 3, 4);
        Put(bytes, end + 8, 0xffff, 2); Put(bytes, end + 10, 0xffff, 2);
        Put(bytes, end + 12, 0xffffffff, 4); Put(bytes, end + 16, 0xffffffff, 4);
        bytes.insert(end, extended);
    }
    const QStringList paths{parts + "/body/bundle1", parts + "/body/bundle2", parts + "/tail/bundle.pdf"};
    if (!Write(paths[0], bytes.first(65536)) || !Write(paths[1], bytes.mid(65536, 65536)) || !Write(paths[2], bytes.mid(131072))) return {};
    return paths;
}
QByteArray Zip64WithComment(QByteArray bytes, bool sentinels, bool localOffset64) {
    auto end = bytes.size() - 22;
    const auto central = Get(bytes, end + 16, 4), entries = Get(bytes, end + 10, 2);
    auto size = Get(bytes, end + 12, 4);
    if (localOffset64) {
        QByteArray extra(12, 0); Put(extra, 0, 1, 2); Put(extra, 2, 8, 2);
        Put(extra, 4, Get(bytes, central + 42, 4), 8);
        const auto nameLength = Get(bytes, central + 28, 2), oldExtra = Get(bytes, central + 30, 2);
        Put(bytes, central + 30, oldExtra + 12, 2); Put(bytes, central + 42, 0xffffffff, 4);
        bytes.insert(central + 46 + nameLength, extra); end += 12; size += 12; Put(bytes, end + 12, size, 4);
    }
    QByteArray extended(76, 0);
    Put(extended, 0, 0x06064b50, 4); Put(extended, 4, 44, 8);
    Put(extended, 12, 45, 2); Put(extended, 14, 45, 2);
    Put(extended, 24, entries, 8); Put(extended, 32, entries, 8);
    Put(extended, 40, size, 8); Put(extended, 48, central, 8);
    Put(extended, 56, 0x07064b50, 4); Put(extended, 64, end, 8); Put(extended, 72, 1, 4);
    if (sentinels) {
        Put(bytes, end + 8, 0xffff, 2); Put(bytes, end + 10, 0xffff, 2);
        Put(bytes, end + 12, 0xffffffff, 4); Put(bytes, end + 16, 0xffffffff, 4);
    }
    Put(bytes, end + 20, 201, 2); bytes.insert(end, extended); bytes.append(QByteArray(201, 'c'));
    return bytes;
}
}

class NestedArchiveTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("7ZipContextTests"); QCoreApplication::setApplicationName("7ZipContextTests");
    }
    void longPathsPreserveNamesThroughExtractionAndOrganization() {
        QTemporaryDir temp;
        const auto folder = temp.filePath("fixture/" + QString(70, 'f'));
        const auto filename = QString(100, 'p') + ".jpg";
        const QByteArray image = QByteArray::fromHex("ffd8ffe0") + "long-path-image";
        QVERIFY(Write(folder + '/' + filename, image)); QVERIFY(Write(folder + "/note.txt", "auxiliary"));
        const auto inner = temp.filePath(QString(120, 'i') + ".7z");
        const auto outer = temp.filePath(QString(100, 'o') + ".7z");
        QVERIFY(Pack({folder}, inner)); QVERIFY(Pack({inner}, outer));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.extractDestination = temp.filePath("out/" + QString(90, 'd') + '/' + QString(90, 'e'));
        controller.addPaths({outer}, false); QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.imageFiles.size(), 1); QVERIFY(controller.imageFiles.first().size() > 260);
        QCOMPARE(QFileInfo(controller.imageFiles.first()).fileName(), filename);
        QCOMPARE(Read(controller.imageFiles.first()), image);
        QCOMPARE(controller.cleanedIntermediateFiles, 1); QCOMPARE(controller.cleanedOtherFiles, 1);
        QVERIFY(QFileInfo::exists(inner)); QVERIFY(QFileInfo::exists(outer));
        const auto found = DiscoverInputs({controller.extractDestination});
        QVERIFY(found.archives.empty()); QCOMPARE(found.files.size(), 1); QCOMPARE(found.media.size(), 1);
        QCOMPARE(Read(found.media.first().files.first()), image);
    }
    void finds7zBehindVideoPrefix_data() {
        QTest::addColumn<int>("prefixSize"); QTest::addColumn<bool>("nested"); QTest::addColumn<bool>("encrypted");
        QTest::newRow("mp4-free-box") << 36 << false << false;
        QTest::newRow("nested-encrypted-mp4") << 36 << true << true;
        QTest::newRow("last-complete-header-in-probe") << (65536 - 32) << false << false;
    }
    void finds7zBehindVideoPrefix() {
        QFETCH(int, prefixSize); QFETCH(bool, nested); QFETCH(bool, encrypted); QTemporaryDir temp;
        const QByteArray video("actual-inner-video"); QVERIFY(Write(temp.filePath("clip.mp4"), video));
        const auto secret = encrypted ? QStringLiteral("embedded-7z-password") : QString{};
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("payload.7z"), secret));
        // MP4 ftyp followed by a free box containing the 7z start header, as in the reported files.
        QByteArray prefix = QByteArray::fromHex("0000001c6674797069736f350000020069736f3569736f366d7034310000002866726565");
        QCOMPARE(prefix.size(), 36); prefix.append(QByteArray(prefixSize - prefix.size(), 'v'));
        const auto carrier = prefix + Read(temp.filePath("payload.7z"));
        const auto disguised = temp.filePath("disguised.mp4"); QVERIFY(Write(disguised, carrier));
        const auto plan = InspectArchive(disguised.toStdWString());
        QVERIFY(plan.error.empty()); QCOMPARE(plan.format, std::wstring(L"7z"));
        QCOMPARE(plan.dataOffset, static_cast<uint64_t>(prefix.size()));
        const auto found = DiscoverInputs({disguised}); QCOMPARE(found.archives.size(), size_t(1)); QVERIFY(found.media.isEmpty());
        SevenZipCore core; QVERIFY(core.OpenArchive(plan, secret.toStdWString())); QVERIFY(core.TestArchive(secret.toStdWString())); core.CloseArchive();
        const auto input = nested ? temp.filePath("outer.7z") : disguised;
        if (nested) QVERIFY(Pack({disguised}, input));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true; controller.autoPasswords = true;
        if (encrypted) QVERIFY(controller.addPasswordList("embedded", secret));
        controller.extractDestination = temp.filePath("output"); controller.addPaths({input}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), nested ? 2 : 1);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), video);
        QCOMPARE(controller.cleanedIntermediateFiles, nested ? 1 : 0); QCOMPARE(Read(disguised), carrier);
    }
    void embedded7zRequiresValidStartHeaderAndReportsTruncation() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("clip.mp4"), "video"));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("payload.7z")));
        const auto archive = Read(temp.filePath("payload.7z"));
        const auto prefix = QByteArray::fromHex("000000186674797069736f6d") + QByteArray(25, 'v');
        auto corrupt = archive; corrupt[8] = static_cast<char>(corrupt[8] ^ 1);
        const auto ordinary = temp.filePath("ordinary.mp4"); QVERIFY(Write(ordinary, prefix + corrupt));
        QVERIFY(DetectArchiveSignature(ordinary.toStdWString()).empty());
        const auto media = DiscoverInputs({ordinary}); QVERIFY(media.archives.empty()); QCOMPARE(media.media.size(), 1);
        const auto marker = temp.filePath("marker.mp4"); QVERIFY(Write(marker, prefix + archive.first(6)));
        QVERIFY(DetectArchiveSignature(marker.toStdWString()).empty());
        // A valid start header with missing data must fail as an archive, not be copied as a video.
        const auto truncated = temp.filePath("truncated.mp4"); QVERIFY(Write(truncated, prefix + archive.first(32)));
        const auto found = DiscoverInputs({truncated}); QCOMPARE(found.archives.size(), size_t(1)); QVERIFY(found.media.isEmpty());
        SevenZipCore core; QVERIFY(!core.OpenArchive(found.archives.front())); QVERIFY(QFileInfo::exists(truncated));
    }
    void findsZip64BehindVideoPrefix_data() {
        QTest::addColumn<bool>("sentinels"); QTest::addColumn<bool>("localOffset64");
        QTest::newRow("ordinary-end-fields") << false << false;
        QTest::newRow("zip64-end-fields") << true << false;
        QTest::newRow("zip64-local-offset") << true << true;
    }
    void findsZip64BehindVideoPrefix() {
        QFETCH(bool, sentinels); QFETCH(bool, localOffset64); QTemporaryDir temp;
        const QByteArray video("actual-inner-video"); QVERIFY(Write(temp.filePath("clip.mp4"), video));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("payload.zip")));
        const auto zip = Zip64WithComment(Read(temp.filePath("payload.zip")), sentinels, localOffset64);
        QByteArray prefix(5 * 1024 * 1024, 'v'); prefix.replace(0, 12, QByteArray::fromHex("000000186674797069736f6d"));
        const auto disguised = temp.filePath("disguised.mp4"); QVERIFY(Write(disguised, prefix + zip));
        const auto plan = InspectArchive(disguised.toStdWString());
        QVERIFY(plan.error.empty()); QCOMPARE(plan.format, std::wstring(L"zip"));
        QCOMPARE(plan.dataOffset, static_cast<uint64_t>(prefix.size())); QCOMPARE(plan.dataSize, static_cast<uint64_t>(zip.size()));
        const auto found = DiscoverInputs({disguised}); QCOMPARE(found.archives.size(), size_t(1)); QVERIFY(found.media.isEmpty());
        QVERIFY(Pack({disguised}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.extractDestination = temp.filePath("output"); controller.addPaths({temp.filePath("outer.7z")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), video);
        QCOMPARE(controller.cleanedIntermediateFiles, 1); QVERIFY(QFileInfo::exists(disguised));
    }
    void nativeZipTailInsideWrapperFindsExternalBodies_data() {
        QTest::addColumn<bool>("zip64");
        QTest::newRow("classic") << false; QTest::newRow("zip64") << true;
    }
    void nativeZipTailInsideWrapperFindsExternalBodies() {
        QFETCH(bool, zip64); QTemporaryDir temp; const QByteArray video(200000, 'v');
        QVERIFY(Write(temp.filePath("clip.mp4"), video));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole.zip")));
        const auto parts = SplitNativeZip(temp.filePath("whole.zip"), temp.filePath("parts"), zip64);
        QCOMPARE(parts.size(), 3);
        QVERIFY(Pack({parts.last()}, temp.filePath("input/wrapper.7z")));
        QVERIFY(Write(temp.filePath("input/bodies/bundle1"), Read(parts[0])));
        QVERIFY(Write(temp.filePath("input/bodies/bundle2"), Read(parts[1])));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.addPaths({temp.filePath("input")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), video);
        QCOMPARE(controller.cleanedIntermediateFiles, 1); QCOMPARE(controller.associatedInputFiles.size(), 1);
        QVERIFY(!QFileInfo::exists(temp.filePath("output/wrapper/bundle.pdf")));
        QCOMPARE(Read(temp.filePath("input/bodies/bundle1")), Read(parts[0]));
        QCOMPARE(Read(temp.filePath("input/bodies/bundle2")), Read(parts[1]));
        QVERIFY(QFileInfo::exists(temp.filePath("input/wrapper.7z")));
    }
    void missingNativeZipBodiesRemainPreserved() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(200000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole.zip")));
        const auto parts = SplitNativeZip(temp.filePath("whole.zip"), temp.filePath("parts"), true); QCOMPARE(parts.size(), 3);
        QVERIFY(Pack({parts.last()}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); QVERIFY(!controller.tasks.last().success);
        QVERIFY(controller.tasks.last().detail.contains(QStringLiteral("ZIP 分卷缺少 bundle.z01")));
        QCOMPARE(controller.cleanedIntermediateFiles, 0); QVERIFY(controller.videoFiles.isEmpty());
        QCOMPARE(Read(temp.filePath("output/outer/bundle.pdf")), Read(parts.last()));
    }
    void nativeZipBodiesAndTailFromOneLayerAreProcessedOnce() {
        QTemporaryDir temp; const QByteArray video(200000, 'v'); QVERIFY(Write(temp.filePath("clip.mp4"), video));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole.zip")));
        const auto parts = SplitNativeZip(temp.filePath("whole.zip"), temp.filePath("parts"), true); QCOMPARE(parts.size(), 3);
        const auto direct = DiscoverInputs({temp.filePath("parts")}); QCOMPARE(direct.archives.size(), size_t(1));
        QVERIFY(direct.ignored.isEmpty()); QVERIFY(direct.archives.front().error.empty());
        QVERIFY(Pack({temp.filePath("parts")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), video);
        QCOMPARE(controller.cleanedIntermediateFiles, 3);
        QVERIFY(!QFileInfo::exists(temp.filePath("output/outer/parts")));
        for (const auto& path : parts) QVERIFY(QFileInfo::exists(path));
    }
    void extractedPdfIsProbedBeforeAuxiliaryClassification_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("embedded-7z") << 0; QTest::newRow("plain-pdf") << 1; QTest::newRow("unknown-binary") << 2;
    }
    void extractedPdfIsProbedBeforeAuxiliaryClassification() {
        QFETCH(int, kind); QTemporaryDir temp; const QByteArray video("sample-video");
        QVERIFY(Write(temp.filePath("clip.mp4"), video));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("payload.7z")));
        const auto pdf = kind == 0 ? QByteArray("%PDF-1.7\n") + Read(temp.filePath("payload.7z"))
            : kind == 1 ? QByteArray("%PDF-1.7\nordinary document") : QByteArray("unrecognized binary data");
        QVERIFY(Write(temp.filePath("wrapped.pdf"), pdf));
        QVERIFY(Pack({temp.filePath("wrapped.pdf"), temp.filePath("clip.mp4")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); QCOMPARE(controller.tasks.last().success, kind != 2);
        QCOMPARE(controller.videoFiles.size(), kind == 0 ? 2 : 1);
        for (const auto& path : controller.videoFiles) QCOMPARE(Read(path), video);
        QCOMPARE(QFileInfo::exists(temp.filePath("output/outer/wrapped.pdf")), kind == 2);
        QCOMPARE(controller.cleanedIntermediateFiles, kind == 0 ? 1 : 0);
        if (kind == 1) QVERIFY(controller.tasks.last().detail.contains(QStringLiteral("已尝试解压 PDF")));
        if (kind == 2) QCOMPARE(Read(temp.filePath("output/outer/wrapped.pdf")), pdf);
    }
    void accompanyingPasswordNoteUnlocksInnerArchive_data() {
        QTest::addColumn<bool>("enabled"); QTest::newRow("enabled") << true; QTest::newRow("disabled") << false;
    }
    void accompanyingPasswordNoteUnlocksInnerArchive() {
        QFETCH(bool, enabled); QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("clip.mp4"), "video"));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("inner.zip"), "temporary-note-secret"));
        const auto note = temp.filePath(QStringLiteral("解压密码.txt"));
        QVERIFY(Write(note, QStringLiteral("解压密码：temporary-note-secret\n").toUtf8()));
        QVERIFY(Pack({note, temp.filePath("inner.zip")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true; controller.autoPasswords = enabled;
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); QCOMPARE(controller.tasks.last().success, enabled);
        for (const auto& task : controller.tasks) QVERIFY(!task.detail.contains("temporary-note-secret"));
        if (enabled) {
            QVERIFY(controller.tasks.last().detail.contains(QStringLiteral("随包密码说明命中")));
            QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), QByteArray("video"));
        } else QVERIFY(QFileInfo::exists(temp.filePath("output/outer/inner.zip")));
        QVERIFY(controller.passwordLibrary().lists().isEmpty());
    }
    void recursivelyExtractsDisguisedEncryptedCrossDirectoryVolumes() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QByteArray video = QByteArray::fromHex("000000186674797069736f6d") + QByteArray(50000, 'v');
        const auto clip = temp.filePath("clip.mp4"); QVERIFY(Write(clip, video));
        QVERIFY(Pack({clip}, temp.filePath("inner.7z"), "nested-pass", true));
        const auto parts = QDir(temp.path()).entryList({"inner.7z.*"}, QDir::Files, QDir::Name); QVERIFY(parts.size() > 2);
        for (int i = 0; i < parts.size(); ++i) {
            const auto folder = temp.filePath(QStringLiteral("parts/分卷%1").arg(i + 1)); QVERIFY(QDir().mkpath(folder));
            QVERIFY(QFile::copy(temp.filePath(parts[i]), folder + '/' + parts[i]));
        }
        QVERIFY(Pack({temp.filePath("parts")}, temp.filePath("middle.zip"), "middle-pass"));
        QVERIFY(QFile::copy(temp.filePath("middle.zip"), temp.filePath("middle.bin")));
        QVERIFY(Pack({temp.filePath("middle.bin"), clip}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false;
        controller.autoNested = true; controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("nested-test", "wrong\nnested-pass\nmiddle-pass"));
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 30000);
        QCOMPARE(controller.tasks.size(), 3); QCOMPARE(controller.jobCount(), 3u); QCOMPARE(controller.extractionDepth(), 3u);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 2);
        for (const auto& path : controller.videoFiles) QCOMPARE(Read(path), video);
        QVERIFY(QFileInfo::exists(controller.extractDestination + "/outer/middle/inner/clip.mp4"));
        QVERIFY(!QFileInfo::exists(controller.extractDestination + "/outer/middle.bin"));
        for (int i = 0; i < parts.size(); ++i) {
            QVERIFY(!QFileInfo::exists(controller.extractDestination + QStringLiteral("/outer/middle/parts/分卷%1/").arg(i + 1) + parts[i]));
            QVERIFY(QFileInfo::exists(temp.filePath(parts[i])));
        }
        QVERIFY(QFileInfo::exists(temp.filePath("outer.7z")));
        QCOMPARE(controller.cleanedIntermediateFiles, static_cast<int>(parts.size()) + 1);
        QVERIFY(controller.cleanedIntermediateBytes > 50000);
    }
    void disablingStopsAtOuterAndDirectExtractionIgnoresExistingFiles() {
        QTemporaryDir temp;
        const auto clip = temp.filePath("clip.mp4"); QVERIFY(Write(clip, "sample-video"));
        QVERIFY(Pack({clip}, temp.filePath("inner.7z")));
        QVERIFY(Pack({temp.filePath("inner.7z")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoNested = false;
        controller.extractDestination = temp.filePath("disabled"); controller.addPaths({temp.filePath("outer.7z")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 1); QVERIFY(controller.tasks.last().success); QVERIFY(controller.videoFiles.isEmpty());
        QVERIFY(QFileInfo::exists(temp.filePath("disabled/outer/inner.7z")));
        QVERIFY(Write(temp.filePath("note.txt"), "not an archive")); QVERIFY(Pack({temp.filePath("note.txt")}, temp.filePath("plain.7z")));
        QVERIFY(QDir().mkpath(temp.filePath("direct"))); QVERIFY(QFile::copy(temp.filePath("inner.7z"), temp.filePath("direct/unrelated.7z")));
        controller.autoNested = true; controller.subfolder = false; controller.clear(false); controller.addPaths({temp.filePath("plain.7z")}, false);
        controller.extractDestination = temp.filePath("direct");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); QVERIFY(controller.tasks.last().success); QVERIFY(controller.videoFiles.isEmpty());
        QVERIFY(!QFileInfo::exists(temp.filePath("direct/unrelated")));
        QVERIFY(QFileInfo::exists(temp.filePath("direct/unrelated.7z")));
        QCOMPARE(controller.cleanedIntermediateFiles, 0);
    }
    void missingInnerPartReportsFailureWithoutFalseCompletion() {
        QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(40000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("inner.7z"), "nested-pass", true));
        QVERIFY(QDir().mkpath(temp.filePath("broken")));
        QVERIFY(QFile::copy(temp.filePath("inner.7z.001"), temp.filePath("broken/inner.7z.001")));
        QVERIFY(QFile::copy(temp.filePath("inner.7z.003"), temp.filePath("broken/inner.7z.003")));
        QVERIFY(Pack({temp.filePath("broken")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoNested = true;
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); QVERIFY(controller.tasks.first().success); QVERIFY(!controller.tasks.last().success);
        QVERIFY(controller.tasks.last().detail.contains(QStringLiteral("缺少"))); QVERIFY(controller.videoFiles.isEmpty());
        QVERIFY(controller.status().contains(QStringLiteral("1 失败")));
        QVERIFY(QFileInfo::exists(temp.filePath("output/outer/broken/inner.7z.001")));
        QVERIFY(QFileInfo::exists(temp.filePath("output/outer/broken/inner.7z.003")));
        QCOMPARE(controller.cleanedIntermediateFiles, 0);
    }
    void identicalPartNamesStayInIndependentBundles() {
        QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(20000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("inner.7z"), {}, true));
        QStringList files;
        for (const auto& folder : {"one", "two"}) {
            QVERIFY(QDir().mkpath(temp.filePath(folder)));
            for (const auto& part : QDir(temp.path()).entryList({"inner.7z.*"}, QDir::Files)) {
                const auto path = temp.filePath(folder) + '/' + part;
                QVERIFY(QFile::copy(temp.filePath(part), path)); files.append(path);
            }
        }
        const auto found = FindNestedArchives(files); QCOMPARE(found.archives.size(), size_t(2));
        for (const auto& plan : found.archives) {
            QVERIFY(plan.error.empty()); SevenZipCore core; QVERIFY(core.OpenArchive(plan)); QVERIFY(core.TestArchive());
            const auto folder = QFileInfo(QString::fromStdWString(plan.firstVolume)).absolutePath();
            for (const auto& volume : plan.volumes) QCOMPARE(QFileInfo(QString::fromStdWString(volume)).absolutePath(), folder);
        }
    }
    void crossDirectoryCopiesStayIndependent_data() {
        QTest::addColumn<QString>("style"); QTest::addColumn<QString>("format"); QTest::addColumn<QString>("intake");
        const QList<QPair<QString, QString>> cases{{"numeric", "7z"}, {"masked", "zip"}, {"inserted", "7z"},
            {"named", "7z"}, {"numeric", "rar"}, {"named", "rar"}};
        for (const auto& entry : cases) for (const auto* intake : {"parent", "folders", "nested"}) {
            const auto label = (entry.first + '-' + entry.second + '-' + intake).toUtf8();
            QTest::newRow(label.constData()) << entry.first << entry.second << QString(intake);
        }
    }
    void crossDirectoryCopiesStayIndependent() {
        QFETCH(QString, style); QFETCH(QString, format); QFETCH(QString, intake); QTemporaryDir temp;
        const auto selected = temp.filePath("selected"); QStringList files, folders;
        QList<QByteArray> expected; QMap<QString, QByteArray> originals;
        for (int group = 0; group < 3; ++group) {
            QByteArray firstBytes, lastBytes;
            if (format == "rar") {
                firstBytes = Read(QFINDTESTDATA("fixtures/rar/sample.part1.rar"));
                lastBytes = Read(QFINDTESTDATA("fixtures/rar/sample.part2.rar"));
                expected << QByteArray("0123456789abcdef").repeated(384);
            } else {
                const auto payload = QByteArray(20000 + group * 1000, static_cast<char>('a' + group)); expected << payload;
                const auto source = temp.filePath(QString("fixture%1/clip.mp4").arg(group)); QVERIFY(Write(source, payload));
                const auto packed = temp.filePath(QString("whole%1.").arg(group) + format); QVERIFY(Pack({source}, packed, "folder-secret"));
                const auto bytes = Read(packed); firstBytes = bytes.first(bytes.size() / 2); lastBytes = bytes.mid(bytes.size() / 2);
            }
            QString firstName = "same." + format + ".001", lastName = "same." + format + ".002";
            if (style == "masked") { firstName = "p.001.pdf"; lastName = "p.002.pdf"; }
            if (style == "inserted") { firstName = QStringLiteral("same.7z.00删除中文1"); lastName = QStringLiteral("same.7z.00删除中文2"); }
            if (style == "named") { firstName = "same.part1.bin"; lastName = "same.part2.mask"; }
            const auto base = selected + QString("/bundle%1").arg(group);
            const auto firstDir = base + (group == 1 ? QString{} : "/volume1");
            const auto lastDir = base + (group == 1 ? QString{} : "/volume2");
            const auto first = firstDir + '/' + firstName, last = lastDir + '/' + lastName;
            QVERIFY(Write(first, firstBytes)); QVERIFY(Write(last, lastBytes));
            folders << lastDir << firstDir; files << last << first;
            originals.insert(first, firstBytes); originals.insert(last, lastBytes);
        }
        const auto other = temp.filePath("fixture-other/other.mp4"); const QByteArray otherBytes("unrelated video");
        QVERIFY(Write(other, otherBytes)); expected << otherBytes;
        QVERIFY(Pack({other}, selected + "/unrelated/other.7z")); folders.prepend(selected + "/unrelated");
        std::vector<std::wstring> context;
        for (const auto& file : files) context.push_back(file.toStdWString());
        context.push_back(context.front()); // Reinspection may see a file in both current and related inputs.
        for (const auto& file : files) {
            const auto plan = InspectArchiveFiles(file.toStdWString(), context);
            QVERIFY2(plan.error.empty(), qPrintable(QString::fromStdWString(plan.error))); QCOMPARE(plan.volumes.size(), size_t(2));
            const auto parent = QFileInfo(file).absolutePath();
            const auto bundle = parent.endsWith("volume1") || parent.endsWith("volume2") ? QFileInfo(parent).absolutePath() : parent;
            for (const auto& volume : plan.volumes) QVERIFY(QString::fromStdWString(volume).replace('\\', '/').startsWith(bundle + '/'));
        }
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true; controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("folder-test", "folder-secret")); controller.extractDestination = temp.filePath("output");
        if (intake == "nested") {
            QVERIFY(Pack({selected}, temp.filePath("outer.7z"))); controller.addPaths({temp.filePath("outer.7z")}, false);
        } else controller.addPaths(intake == "folders" ? folders : QStringList{selected}, false);
        QCOMPARE(controller.archives.size(), intake == "nested" ? size_t(1) : size_t(4));
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), intake == "nested" ? 5 : 4);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.input + ": " + task.detail));
        QCOMPARE(controller.videoFiles.size(), 4);
        for (const auto& file : controller.videoFiles) QVERIFY(expected.removeOne(Read(file)));
        QVERIFY(expected.isEmpty());
        QCOMPARE(controller.cleanedIntermediateFiles, intake == "nested" ? 7 : 0);
        for (auto it = originals.cbegin(); it != originals.cend(); ++it) QCOMPARE(Read(it.key()), it.value());
    }
    void ambiguousCopiesDoNotBorrowEachOthersVolumes() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(30000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole.7z")));
        const auto bytes = Read(temp.filePath("whole.7z")); const auto cut = bytes.size() / 2;
        const auto first = bytes.first(cut), last = bytes.mid(cut);
        const auto root = temp.filePath("input");
        QVERIFY(Write(root + "/complete/same.7z.001", first)); QVERIFY(Write(root + "/complete/same.7z.002", last));
        QVERIFY(Write(root + "/incomplete/same.7z.001", first));
        auto found = DiscoverInputs({root}); QCOMPARE(found.archives.size(), size_t(2));
        int opened = 0;
        for (const auto& plan : found.archives) { SevenZipCore core; if (core.OpenArchive(plan) && core.TestArchive()) ++opened; }
        QCOMPARE(opened, 1);
        const auto ambiguous = temp.filePath("ambiguous");
        QVERIFY(Write(ambiguous + "/a/same.7z.001", first)); QVERIFY(Write(ambiguous + "/b/same.7z.001", first));
        QVERIFY(Write(ambiguous + "/c/same.7z.002", last)); QVERIFY(Write(ambiguous + "/d/same.7z.002", last));
        found = DiscoverInputs({ambiguous}); QCOMPARE(found.archives.size(), size_t(4));
        for (const auto& plan : found.archives) { SevenZipCore core; QVERIFY(!core.OpenArchive(plan) || !core.TestArchive()); }
    }
    void maskedNumericVolumes_data() {
        QTest::addColumn<QString>("intake"); QTest::addColumn<QString>("format"); QTest::addColumn<QString>("mask1"); QTest::addColumn<QString>("mask2");
        QTest::newRow("first-pdf") << QString("first") << QString("7z") << QString(".pdf") << QString();
        QTest::newRow("later-plain") << QString("last") << QString("7z") << QString(".pdf") << QString();
        QTest::newRow("both-reversed") << QString("both") << QString("7z") << QString(".pdf") << QString();
        QTest::newRow("directory-both-masked") << QString("folder") << QString("zip") << QString(".docx") << QString(".jpg");
        QTest::newRow("later-masked") << QString("last") << QString("7z") << QString() << QString(".bin");
        QTest::newRow("nested-cross-folder") << QString("nested") << QString("7z") << QString(".pdf") << QString(".mp4");
    }
    void maskedNumericVolumes() {
        QFETCH(QString, intake); QFETCH(QString, format); QFETCH(QString, mask1); QFETCH(QString, mask2);
        QTemporaryDir temp; const QByteArray video(40000, 'v'); QVERIFY(Write(temp.filePath("clip.mp4"), video));
        const auto whole = temp.filePath("whole." + format); QVERIFY(Pack({temp.filePath("clip.mp4")}, whole));
        const auto bytes = Read(whole); const auto cut = bytes.size() / 2; QVERIFY(cut > 32);
        const auto folder = temp.filePath("drop"), base = QStringLiteral("课程Set.") + format;
        const auto first = folder + '/' + base + ".001" + mask1;
        const auto last = folder + (intake == "nested" ? "/side/" : "/") + base + ".002" + mask2;
        QVERIFY(Write(first, bytes.first(cut))); QVERIFY(Write(last, bytes.mid(cut)));
        QStringList inputs;
        if (intake == "nested") {
            const auto outer = temp.filePath("outer.7z"); QVERIFY(Pack({folder}, outer)); inputs << outer;
        } else {
            const auto direct = InspectArchive(last.toStdWString()); QVERIFY(direct.error.empty()); QVERIFY(direct.concatenated);
            QCOMPARE(direct.volumes.size(), size_t(2)); QCOMPARE(QString::fromStdWString(ArchiveOutputName(direct)), QStringLiteral("课程Set"));
            if (intake == "both") inputs << last << first;
            else inputs << (intake == "folder" ? folder : intake == "last" ? last : first);
        }
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.extractDestination = temp.filePath("out"); controller.addPaths(inputs, false);
        QCOMPARE(controller.archives.size(), size_t(1)); QVERIFY(!QFileInfo::exists(controller.extractDestination));
        if (intake != "nested") QCOMPARE(controller.archives.front().volumes.size(), size_t(2));
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), intake == "nested" ? 2 : 1);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        if (intake == "nested") {
            QCOMPARE(controller.tasks.last().depth, 2U);
            QCOMPARE(controller.tasks.last().rootInput, controller.tasks.first().input);
            QCOMPARE(controller.tasks.last().inputFiles.size(), 2);
            QVERIFY(controller.tasks.last().inputFiles.contains(controller.tasks.last().input));
        }
        QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), video);
        QCOMPARE(controller.cleanedIntermediateFiles, intake == "nested" ? 2 : 0);
        QCOMPARE(Read(first), bytes.first(cut)); QCOMPARE(Read(last), bytes.mid(cut));
    }
    void maskedNumericConflictsAndOrdinaryDocuments() {
        QTemporaryDir temp; const auto first = temp.filePath("a/book.7z.001.pdf"), last = temp.filePath("a/book.7z.002.img");
        QVERIFY(Write(last, "continuation"));
        const auto missing = DiscoverInputs({last}); QCOMPARE(missing.archives.size(), size_t(1));
        QVERIFY(missing.archives.front().error.find(L"缺少首卷") != std::wstring::npos);
        QVERIFY(Write(first, QByteArray::fromHex("377abcaf271c"))); QVERIFY(Write(temp.filePath("a/book.7z.003"), "third"));
        QVERIFY(QFile::remove(last)); QVERIFY(InspectArchive(first.toStdWString()).error.find(L"缺少第 2 卷") != std::wstring::npos);
        QVERIFY(Write(last, "continuation")); const auto duplicate = temp.filePath("a/book.7z.002"); QVERIFY(QFile::copy(last, duplicate));
        const auto ambiguous = DiscoverInputs({first}); QCOMPARE(ambiguous.archives.size(), size_t(1));
        QVERIFY(ambiguous.archives.front().error.find(L"重复编号") != std::wstring::npos);
        QVERIFY(QFile::remove(duplicate));
        QVERIFY(Write(temp.filePath("b/book.7z.001"), Read(first))); QVERIFY(Write(temp.filePath("b/book.7z.002.pdf"), Read(last)));
        const auto bundles = DiscoverInputs({temp.path()}); QCOMPARE(bundles.archives.size(), size_t(2));
        for (const auto& plan : bundles.archives) {
            QVERIFY(plan.error.empty()); const QFileInfo entry(QString::fromStdWString(plan.firstVolume));
            for (const auto& volume : plan.volumes) QCOMPARE(QFileInfo(QString::fromStdWString(volume)).absolutePath(), entry.absolutePath());
        }
        const auto ordinary = temp.filePath("ordinary");
        QVERIFY(Write(ordinary + "/chapter.001.pdf", "%PDF-1.7")); QVERIFY(Write(ordinary + "/chapter.002.jpg", QByteArray::fromHex("ffd8ffe0")));
        const auto docs = DiscoverInputs({ordinary}); QVERIFY(docs.archives.empty()); QCOMPARE(docs.ignored.size(), 1); QCOMPARE(docs.media.size(), 1);
    }
    void obfuscatedNumericVolumes_data() {
        QTest::addColumn<QString>("format"); QTest::addColumn<QString>("style"); QTest::addColumn<QString>("intake");
        for (const auto* format : {"7z", "zip", "rar"}) for (const auto* style : {"inserted", "pdf"})
            for (const auto* intake : {"first", "later", "both", "folder", "nested"}) {
                const auto label = QByteArray(format) + '-' + style + '-' + intake;
                QTest::newRow(label.constData()) << QString(format) << QString(style) << QString(intake);
            }
        QTest::newRow("prefixed-encrypted-7z") << QString("7z") << QString("prefixed") << QString("nested");
    }
    void obfuscatedNumericVolumes() {
        QFETCH(QString, format); QFETCH(QString, style); QFETCH(QString, intake); QTemporaryDir temp;
        const bool rar = format == "rar";
        const auto video = rar ? QByteArray("0123456789abcdef").repeated(384) : QByteArray(40000, 'v');
        QByteArray firstBytes, lastBytes;
        if (rar) {
            firstBytes = Read(QFINDTESTDATA("fixtures/rar/sample.part1.rar"));
            lastBytes = Read(QFINDTESTDATA("fixtures/rar/sample.part2.rar"));
            QVERIFY(!firstBytes.isEmpty() && !lastBytes.isEmpty());
        } else {
            QVERIFY(Write(temp.filePath("clip.mp4"), video));
            QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole." + format), "volume-secret"));
            auto bytes = Read(temp.filePath("whole." + format));
            if (style == "prefixed") bytes.prepend("%PDF-1.7\n");
            const auto cut = bytes.size() / 2; QVERIFY(cut > 32);
            firstBytes = bytes.first(cut); lastBytes = bytes.mid(cut);
        }
        const auto folder = temp.filePath("input");
        const auto base = style == "inserted" ? QString("YWNS888.") + format : QString("p");
        const auto firstName = base + (style == "inserted" ? QStringLiteral(".00删除中文1") : QString(".001.pdf"));
        const auto lastName = base + (style == "inserted" ? QStringLiteral(".00删除中文2") : QString(".002.pdf"));
        const auto first = folder + '/' + firstName;
        const auto last = folder + (intake == "nested" ? "/side/" : "/") + lastName;
        QVERIFY(Write(first, firstBytes)); QVERIFY(Write(last, lastBytes));
        if (intake != "nested") {
            const auto plan = InspectArchive(last.toStdWString()); QVERIFY(plan.error.empty());
            QCOMPARE(plan.volumes.size(), size_t(2)); QCOMPARE(plan.concatenated, !rar);
            SevenZipCore core; QVERIFY2(core.OpenArchive(plan, L"volume-secret"), qPrintable(QString::fromStdWString(core.LastError())));
            QVERIFY(core.TestArchive(L"volume-secret"));
        }
        QStringList inputs;
        if (intake == "nested") { const auto outer = temp.filePath("outer.7z"); QVERIFY(Pack({folder}, outer)); inputs << outer; }
        else if (intake == "both") inputs << last << first;
        else inputs << (intake == "folder" ? folder : intake == "later" ? last : first);
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true; controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("test-volumes", "wrong-password\nvolume-secret"));
        controller.extractDestination = temp.filePath("output"); controller.addPaths(inputs, false);
        QCOMPARE(controller.archives.size(), size_t(1));
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), intake == "nested" ? 2 : 1);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 1); QCOMPARE(Read(controller.videoFiles.first()), video);
        QCOMPARE(controller.cleanedIntermediateFiles, intake == "nested" ? 2 : 0);
        QCOMPARE(Read(first), firstBytes); QCOMPARE(Read(last), lastBytes);
    }
    void ambiguousNumericNamesRequireLocalArchiveEvidence() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(30000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole.7z")));
        const auto bytes = Read(temp.filePath("whole.7z")), first = bytes.first(bytes.size() / 2), last = bytes.mid(bytes.size() / 2);
        const auto archive = temp.filePath("archive/p.001.pdf");
        QVERIFY(Write(archive, first)); QVERIFY(Write(temp.filePath("archive/p.002.pdf"), last));
        QVERIFY(Write(temp.filePath("documents/p.001.pdf"), "%PDF-1.7\nordinary document"));
        QVERIFY(Write(temp.filePath("documents/p.002.jpg"), QByteArray::fromHex("ffd8ffe0") + "image"));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("whole.zip")));
        QVERIFY(Write(temp.filePath("documents/book.001.docx"), Read(temp.filePath("whole.zip"))));
        const auto found = DiscoverInputs({temp.filePath("archive"), temp.filePath("documents")});
        QCOMPARE(found.archives.size(), size_t(1)); QCOMPARE(found.ignored.size(), 2); QCOMPARE(found.media.size(), 1);
        QVERIFY(QFile::remove(temp.filePath("archive/p.002.pdf")));
        QVERIFY(Write(temp.filePath("archive/p.003.pdf"), last));
        const auto missing = DiscoverInputs({archive}); QCOMPARE(missing.archives.size(), size_t(1));
        QVERIFY(missing.archives.front().error.find(L"缺少第 2 卷") != std::wstring::npos);
        QVERIFY(Write(temp.filePath("archive/p.002.pdf"), last));
        QVERIFY(Write(temp.filePath("archive/p.002.mp4"), last));
        const auto duplicate = DiscoverInputs({archive}); QCOMPARE(duplicate.archives.size(), size_t(1));
        QVERIFY(duplicate.archives.front().error.find(L"重复编号") != std::wstring::npos);
        const auto chinese = temp.filePath("chinese/YWNS888.7z.00删除中文1"); QVERIFY(Write(chinese, first));
        QVERIFY(Write(temp.filePath("chinese/YWNS888.7z.002"), last));
        QVERIFY(Write(temp.filePath("chinese/YWNS888.7z.00删除中文2"), last));
        QVERIFY(InspectArchive(chinese.toStdWString()).error.find(L"重复编号") != std::wstring::npos);
    }
    void namedPartsWithDisguisedExtensions_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("mixed");
        QTest::newRow("7z") << QString("7z") << false;
        QTest::newRow("zip") << QString("zip") << false;
        QTest::newRow("7z-mixed-suffix") << QString("7z") << true;
        QTest::newRow("zip-mixed-suffix") << QString("zip") << true;
    }
    void namedPartsWithDisguisedExtensions() {
        QFETCH(QString, format); QFETCH(bool, mixed); QTemporaryDir temp;
        const auto source = temp.filePath("clip.mp4"); const QByteArray payload(50000, 'v'); QVERIFY(Write(source, payload));
        QVERIFY(Pack({source}, temp.filePath("original." + format), "named-pass", true));
        const auto parts = QDir(temp.path()).entryList({"original." + format + ".*"}, QDir::Files, QDir::Name);
        QVERIFY(parts.size() > 2); QStringList paths;
        for (int i = 0; i < parts.size(); ++i) {
            const auto suffix = mixed && i > 0 ? QString(".mask%1").arg(i) : QString(".apk");
            const auto path = temp.filePath(QString("renamed.part%1").arg(i + 1, 3, 10, QChar('0')) + suffix);
            QVERIFY(QFile::copy(temp.filePath(parts[i]), path)); paths.append(path);
        }
        const auto plan = InspectArchive(paths.last().toStdWString());
        QVERIFY2(plan.error.empty(), qPrintable(QString::fromStdWString(plan.error)));
        QVERIFY(plan.split); QVERIFY(plan.concatenated); QCOMPARE(plan.volumes.size(), static_cast<size_t>(parts.size()));
        QCOMPARE(QString::fromStdWString(ArchiveOutputName(plan)), QString("renamed"));
        const auto nested = FindNestedArchives(paths); QCOMPARE(nested.archives.size(), size_t(1));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("named-parts", "named-pass"));
        controller.addPaths({paths.last()}, false); controller.addPaths(paths, false); QCOMPARE(controller.archives.size(), size_t(1));
        controller.extractDestination = temp.filePath("output"); QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY2(controller.tasks.last().success, qPrintable(controller.tasks.last().detail));
        QCOMPARE(Read(temp.filePath("output/renamed/clip.mp4")), payload);
        QVERIFY(QDir().mkpath(temp.filePath("incomplete")));
        const auto lone = temp.filePath("incomplete/renamed.part001.apk"); QVERIFY(QFile::copy(paths.first(), lone));
        const auto missing = FindNestedArchives({lone}); QCOMPARE(missing.archives.size(), size_t(1));
        SevenZipCore truncated;
        QVERIFY(!truncated.OpenArchive(missing.archives.front(), L"named-pass") || !truncated.TestArchive(L"named-pass"));
        QVERIFY(QFile::copy(paths.first(), temp.filePath("renamed.part01.apk")));
        QVERIFY(!InspectArchive(paths.last().toStdWString()).error.empty());
    }
    void mixedRarVolumes_data() {
        QTest::addColumn<QString>("intake");
        for (const auto* mode : {"first", "later", "both", "directory", "nested"}) QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void mixedRarVolumes() {
        QFETCH(QString, intake); QTemporaryDir temp;
        const auto folder = temp.filePath("drop");
        const auto first = folder + QStringLiteral("/样本.part01.rar");
        const auto last = folder + QStringLiteral("/样本.part2.r删a删删删r");
        const auto original1 = Read(QFINDTESTDATA("fixtures/rar/sample.part1.rar"));
        const auto original2 = Read(QFINDTESTDATA("fixtures/rar/sample.part2.rar"));
        QVERIFY(!original1.isEmpty() && !original2.isEmpty());
        QVERIFY(Write(first, original1)); QVERIFY(Write(last, original2));
        QStringList inputs;
        if (intake == "nested") {
            // The wrapper is a complete 7z with a misleading media extension.
            const auto wrapper = temp.filePath("wrapper.mp4"); QVERIFY(Pack({folder}, wrapper)); inputs << wrapper;
        } else if (intake == "both") inputs << last << first;
        else inputs << (intake == "first" ? first : intake == "later" ? last : folder);
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.extractDestination = temp.filePath("out"); controller.addPaths(inputs, false);
        QCOMPARE(controller.archives.size(), size_t(1));
        if (intake != "nested") {
            const auto& plan = controller.archives.front(); QVERIFY(plan.error.empty()); QCOMPARE(plan.volumes.size(), size_t(2));
            QCOMPARE(QFileInfo(QString::fromStdWString(plan.firstVolume)), QFileInfo(first));
            QCOMPARE(QString::fromStdWString(plan.volumeAliases.back().first), QStringLiteral("样本.part02.rar"));
        }
        QVERIFY(!QFileInfo::exists(controller.extractDestination));
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), intake == "nested" ? 2 : 1);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 1);
        QCOMPARE(Read(controller.videoFiles.first()), QByteArray("0123456789abcdef").repeated(384));
        QCOMPARE(controller.cleanedIntermediateFiles, intake == "nested" ? 2 : 0);
        QCOMPARE(Read(first), original1); QCOMPARE(Read(last), original2);
    }
    void mixedRarVolumesDoNotGuessDuplicateOrMissingParts() {
        QTemporaryDir temp;
        const auto first = temp.filePath("a/clip.part1");
        const auto last = temp.filePath("a/clip.part02.mp4");
        QVERIFY(Write(last, Read(QFINDTESTDATA("fixtures/rar/sample.part2.rar"))));
        QVERIFY(InspectArchive(last.toStdWString()).error.find(L"缺少首卷") != std::wstring::npos);
        QVERIFY(Write(first, Read(QFINDTESTDATA("fixtures/rar/sample.part1.rar"))));
        const auto good = InspectArchive(last.toStdWString()); QVERIFY(good.error.empty());
        SevenZipCore core; QVERIFY(core.OpenArchive(good)); QVERIFY(core.TestArchive()); core.CloseArchive();
        const auto other = temp.filePath("a/clip.part2.pdf"); QVERIFY(QFile::copy(last, other));
        const auto ambiguous = DiscoverInputs({first}); QCOMPARE(ambiguous.archives.size(), size_t(1));
        QVERIFY(ambiguous.archives.front().error.find(L"重复编号") != std::wstring::npos);
        QVERIFY(QFile::remove(other));
        const auto copy1 = temp.filePath("b/clip.part1.bin"), copy2 = temp.filePath("b/clip.part2.rar");
        QVERIFY(Write(copy1, Read(first))); QVERIFY(Write(copy2, Read(last)));
        const auto separate = DiscoverInputs({temp.path()}); QCOMPARE(separate.archives.size(), size_t(2));
        for (const auto& plan : separate.archives) {
            QVERIFY(plan.error.empty()); QCOMPARE(plan.volumes.size(), size_t(2));
            QCOMPARE(QFileInfo(QString::fromStdWString(plan.volumes[0])).absolutePath(), QFileInfo(QString::fromStdWString(plan.volumes[1])).absolutePath());
        }
        const auto unused = temp.filePath("a/clip.part3.jpg"); QVERIFY(QFile::copy(last, unused));
        QVERIFY(!core.OpenArchive(InspectArchive(first.toStdWString()))); QVERIFY(core.LastError().find(L"全部候选分卷") != std::wstring::npos);
    }
    void complete7zDoesNotConsumeUnrelatedTrailingPart() {
        QTemporaryDir temp; const auto video = temp.filePath("clip.mp4"); QVERIFY(Write(video, "video"));
        const auto first = temp.filePath("input/test.part1.bin"), extra = temp.filePath("input/test.part2.jpg");
        QVERIFY(Pack({video}, first)); QVERIFY(Write(extra, QByteArray::fromHex("ffd8ffe0") + "separate image"));
        const auto discovered = DiscoverInputs({first}); QCOMPARE(discovered.archives.size(), size_t(1));
        SevenZipCore core; QVERIFY(!core.OpenArchive(discovered.archives.front()));
        QVERIFY(core.LastError().find(L"全部候选分卷") != std::wstring::npos);
    }
    void distinctSameStemSetsAreNotAddedByDraggingOnePart() {
        QTemporaryDir temp; const auto folder = temp.filePath("inputs");
        for (const auto& suffix : {QString(".doc"), QString(".bin")}) {
            const auto clip = temp.filePath("clip.mp4"), pack = temp.filePath("whole" + suffix);
            QVERIFY(Write(clip, suffix.toUtf8())); QVERIFY(Pack({clip}, pack));
            const auto bytes = Read(pack); const auto cut = bytes.size() / 2;
            QVERIFY(Write(folder + "/same.part1" + suffix, bytes.first(cut)));
            QVERIFY(Write(folder + "/same.part2" + suffix, bytes.mid(cut)));
        }
        const auto dropped = DiscoverInputs({folder + "/same.part2.bin"});
        QCOMPARE(dropped.files.size(), 2); QCOMPARE(dropped.archives.size(), size_t(1));
        const auto folderScan = DiscoverInputs({folder}); QCOMPARE(folderScan.archives.size(), size_t(2));
        for (const auto& plan : folderScan.archives) {
            SevenZipCore core; QVERIFY(core.OpenArchive(plan)); QVERIFY(core.TestArchive());
        }
    }
    void optionalDisguisedRarVolumesAcrossDirectories() {
        const auto fixtures = QDir::current().absoluteFilePath("rar-fixtures");
        const auto parts = QDir(fixtures).entryList({"sample.part*.rar"}, QDir::Files, QDir::Name);
        if (parts.size() < 2) QSKIP("Optional locally generated RAR fixture is absent");
        QTemporaryDir temp; QStringList paths;
        for (int i = 0; i < parts.size(); ++i) {
            const auto directory = temp.filePath(QString("volume%1").arg(i + 1)); QVERIFY(QDir().mkpath(directory));
            const auto path = directory + QString("/renamed.part%1.apk").arg(i + 1, 3, 10, QChar('0'));
            QVERIFY(QFile::copy(QDir(fixtures).filePath(parts[i]), path)); paths.append(path);
        }
        const auto nested = FindNestedArchives(paths); QCOMPARE(nested.archives.size(), size_t(1));
        const auto& plan = nested.archives.front(); QVERIFY(plan.error.empty()); QVERIFY(!plan.concatenated);
        QCOMPARE(plan.volumeName, std::wstring(L"renamed.part001.rar"));
        SevenZipCore core;
        QVERIFY2(core.OpenArchive(plan, L"test-secret"), qPrintable(QString::fromStdWString(core.LastError())));
        QVERIFY(core.TestArchive(L"test-secret"));
        QVERIFY(core.Extract(temp.filePath("output").toStdWString(), L"test-secret"));
        QCOMPARE(Read(temp.filePath("output/payload.bin")), Read(QDir(fixtures).filePath("payload.bin")));
    }
    void preservesFinishedZipContainersAndStillUnwrapsDisguisedArchives() {
        QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(24000, 'v')));
        QVERIFY(Write(temp.filePath("AndroidManifest.xml"), "package manifest"));
        QVERIFY(Pack({temp.filePath("AndroidManifest.xml")}, temp.filePath("package.zip")));
        QVERIFY(QFile::copy(temp.filePath("package.zip"), temp.filePath("application.APK")));
        QVERIFY(QFile::copy(temp.filePath("package.zip"), temp.filePath("document.docx")));
        const auto package = Read(temp.filePath("package.zip"));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("video.zip")));
        QVERIFY(QFile::copy(temp.filePath("video.zip"), temp.filePath("payload.bin")));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("video.7z")));
        QVERIFY(QFile::copy(temp.filePath("video.7z"), temp.filePath("disguised.apk")));
        QVERIFY(Pack({temp.filePath("application.APK"), temp.filePath("document.docx"), temp.filePath("payload.bin"), temp.filePath("disguised.apk")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoNested = true;
        controller.extractDestination = temp.filePath("output"); controller.addPaths({temp.filePath("outer.7z")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 3);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 2); QCOMPARE(controller.cleanedIntermediateFiles, 2);
        QCOMPARE(Read(temp.filePath("output/outer/application.APK")), package);
        QCOMPARE(Read(temp.filePath("output/outer/document.docx")), package);
        QVERIFY(!QFileInfo::exists(temp.filePath("output/outer/payload.bin")));
        QVERIFY(!QFileInfo::exists(temp.filePath("output/outer/disguised.apk")));
    }
    void findsZipAfterLargeVideoPrefixAndTrailingPadding() {
        QTemporaryDir temp;
        const auto secret = QStringLiteral("中文测试密码");
        const QByteArray payload(32000, 'v'); QVERIFY(Write(temp.filePath("clip.mp4"), payload));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("inner.7z"), secret));
        QVERIFY(Pack({temp.filePath("inner.7z")}, temp.filePath("contents.zip")));
        const auto zip = Read(temp.filePath("contents.zip"));
        const auto prefix = QByteArray::fromHex("000000146674797069736f6d0000000169736f6d") + QByteArray(5 * 1024 * 1024, 'p');
        const auto source = temp.filePath("carrier.mp4");
        QVERIFY(Write(source, prefix + zip + QByteArray(16000, 't')));
        const auto plan = InspectArchive(source.toStdWString());
        QVERIFY(plan.error.empty()); QCOMPARE(plan.format, std::wstring(L"zip"));
        QCOMPARE(plan.dataOffset, static_cast<uint64_t>(prefix.size())); QCOMPARE(plan.dataSize, static_cast<uint64_t>(zip.size()));
        SevenZipCore core; QVERIFY2(core.OpenArchive(plan), qPrintable(QString::fromStdWString(core.LastError()))); QVERIFY(core.TestArchive()); core.CloseArchive();
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoNested = true; controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("nested-zip", secret));
        controller.extractDestination = temp.filePath("output"); controller.addPaths({source}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 2); QCOMPARE(controller.videoFiles.size(), 1);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(Read(controller.videoFiles.first()), payload); QCOMPARE(controller.cleanedIntermediateFiles, 1);
        QVERIFY(QFileInfo::exists(source));
        const auto ordinary = temp.filePath("ordinary.mp4");
        QVERIFY(Write(ordinary, prefix + QByteArray::fromHex("504b0506") + QByteArray(50, '\0')));
        QVERIFY(!InspectArchive(ordinary.toStdWString()).error.empty());
        QCOMPARE(FindNestedArchives({ordinary}).archives.size(), size_t(0));
    }
    void pauseAndCancelStopsRecursiveWork() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(40000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("inner.7z")));
        QVERIFY(Pack({temp.filePath("inner.7z")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoNested = true;
        controller.addPaths({temp.filePath("outer.7z")}, false); controller.extractDestination = temp.filePath("output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); controller.togglePause();
        QTest::qWait(50); QVERIFY(controller.busy()); controller.cancel(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(controller.videoFiles.isEmpty()); QVERIFY(controller.status().contains(QStringLiteral("取消")));
        QCOMPARE(controller.cleanedIntermediateFiles, 0); QVERIFY(QFileInfo::exists(temp.filePath("outer.7z")));
    }
    void failedDescendantPreservesAncestorsButCleansSuccessfulSibling() {
        QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("clip.mp4"), QByteArray(40000, 'v')));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("locked.7z"), "unavailable-password"));
        QVERIFY(Pack({temp.filePath("locked.7z")}, temp.filePath("branch.zip")));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("good.7z")));
        QVERIFY(Pack({temp.filePath("branch.zip"), temp.filePath("good.7z")}, temp.filePath("outer.7z"), {}, true));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = false; controller.autoNested = true; controller.autoPasswords = false;
        controller.extractDestination = temp.filePath("output"); controller.addPaths({temp.filePath("outer.7z.001")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 4);
        QVERIFY(controller.status().contains(QStringLiteral("1 失败")));
        QVERIFY(QFileInfo::exists(temp.filePath("output/outer/branch.zip")));
        QVERIFY(QFileInfo::exists(temp.filePath("output/outer/branch/locked.7z")));
        QVERIFY(!QFileInfo::exists(temp.filePath("output/outer/good.7z")));
        QCOMPARE(Read(temp.filePath("output/outer/good/clip.mp4")), QByteArray(40000, 'v'));
        QCOMPARE(controller.cleanedIntermediateFiles, 1);
        for (const auto& volume : controller.archives.front().volumes) QVERIFY(QFileInfo::exists(QString::fromStdWString(volume)));
    }
    void cleanupRequiresProvenanceAndPreservesLockedChangedOrCancelledGroups() {
        QTemporaryDir temp; const auto root = temp.filePath("produced");
        const auto first = root + "/inner.7z.001", second = root + "/inner.7z.002";
        QVERIFY(Write(first, "part-one")); QVERIFY(Write(second, "part-two"));
        ArchivePlan plan; plan.volumes = {first.toStdWString(), second.toStdWString()};
        const auto tracked = TrackIntermediateArchive(plan, {first, second}, root, {}); QVERIFY2(tracked.error.isEmpty(), qPrintable(tracked.error));
        QVERIFY(!DeleteIntermediateArchive(TrackIntermediateArchive(plan, {first}, root, {})).success);
        QVERIFY(!DeleteIntermediateArchive(TrackIntermediateArchive(plan, {first, second}, root, {first})).success);
        const auto outside = temp.filePath("original.7z"); QVERIFY(Write(outside, "original"));
        ArchivePlan external; external.volumes = {outside.toStdWString()};
        QVERIFY(!DeleteIntermediateArchive(TrackIntermediateArchive(external, {outside}, root, {})).success);
        int calls = 0;
        QVERIFY(!DeleteIntermediateArchive(tracked, [&](uint64_t, uint64_t) { return ++calls < 3; }).success);
        QCOMPARE(Read(first), QByteArray("part-one")); QCOMPARE(Read(second), QByteArray("part-two"));
        HANDLE locked = CreateFileW(second.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE);
        const auto blocked = DeleteIntermediateArchive(tracked); CloseHandle(locked);
        QVERIFY(!blocked.success); QVERIFY(QFileInfo::exists(first)); QVERIFY(QFileInfo::exists(second));
        QVERIFY(Write(second, "modified-part-two"));
        QVERIFY(!DeleteIntermediateArchive(tracked).success);
        QCOMPARE(Read(first), QByteArray("part-one")); QCOMPARE(Read(second), QByteArray("modified-part-two"));
        const auto updated = TrackIntermediateArchive(plan, {first, second}, root, {});
        const auto deleted = DeleteIntermediateArchive(updated);
        QVERIFY2(deleted.success, qPrintable(deleted.error)); QCOMPARE(deleted.files, 2);
        QCOMPARE(deleted.bytes, uint64_t(8 + 17));
        QVERIFY(!QFileInfo::exists(first)); QVERIFY(!QFileInfo::exists(second)); QCOMPARE(Read(outside), QByteArray("original"));
    }
};
QTEST_MAIN(NestedArchiveTests)
#include "NestedArchiveTests.moc"
