#include "ui/ArchiveController.h"
#include "ui/MediaOrganizer.h"
#include <QDir>
#include <QFile>
#include <QSet>
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
bool Pack(const QStringList& paths, const QString& target, const QString& password = {}) {
    SevenZipCore core; CompressionOptions options;
    options.password = password.toStdWString(); options.encryptHeaders = !password.isEmpty(); options.level = 0;
    std::vector<std::wstring> inputs; for (const auto& path : paths) inputs.push_back(path.toStdWString());
    return core.Compress(inputs, target.toStdWString(), options);
}
}

class MediaOrganizationTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("7ZipContextTests"); QCoreApplication::setApplicationName("7ZipContextTests");
    }
    void sortsNestedMediaAndOnlyDeletesOtherProducedFiles() {
        QTemporaryDir temp;
        const QByteArray jpg = QByteArray::fromHex("ffd8ffe0") + "photo";
        const QByteArray png = QByteArray::fromHex("89504e470d0a1a0a") + "image";
        QVERIFY(Write(temp.filePath("payload/a/clip.mp4"), "video-A"));
        QVERIFY(Write(temp.filePath("payload/b/clip.mp4"), "video-B"));
        QVERIFY(Write(temp.filePath("payload/a/pic.jpg"), jpg + 'A'));
        QVERIFY(Write(temp.filePath("payload/b/pic.jpg"), jpg + 'B'));
        QVERIFY(Write(temp.filePath("payload/image_without_extension"), png));
        QVERIFY(Write(temp.filePath("payload/readme.txt"), "remove this note"));
        QVERIFY(Write(temp.filePath("payload/install.apk"), QByteArray::fromHex("504b0304") + "package"));
        QVERIFY(Write(temp.filePath("payload/start.bat"), "unused command text"));
        QVERIFY(Pack({temp.filePath("payload")}, temp.filePath("inner.7z")));
        QVERIFY(Write(temp.filePath("trailer.mp4"), "trailer"));
        QVERIFY(Pack({temp.filePath("inner.7z"), temp.filePath("trailer.mp4")}, temp.filePath("outer.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat"));
        controller.organizeMedia = true; controller.autoNested = false; controller.subfolder = false;
        controller.extractDestination = temp.filePath("output"); controller.addPaths({temp.filePath("outer.7z")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.tasks.size(), 2); QCOMPARE(controller.videoFiles.size(), 3); QCOMPARE(controller.imageFiles.size(), 3);
        QCOMPARE(controller.cleanedIntermediateFiles, 1); QCOMPARE(controller.cleanedOtherFiles, 3);
        const auto root = temp.filePath("output/outer");
        QCOMPARE(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList({QStringLiteral("图片"), QStringLiteral("视频")}));
        QVERIFY(QDir(root).entryList(QDir::Files).isEmpty());
        QSet<QByteArray> videos, images;
        for (const auto& path : controller.videoFiles) { QVERIFY(path.startsWith(root + QStringLiteral("/视频/"))); videos.insert(Read(path)); }
        for (const auto& path : controller.imageFiles) { QVERIFY(path.startsWith(root + QStringLiteral("/图片/"))); images.insert(Read(path)); }
        QCOMPARE(videos, QSet<QByteArray>({"video-A", "video-B", "trailer"}));
        QCOMPARE(images, QSet<QByteArray>({jpg + 'A', jpg + 'B', png}));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/视频/clip (2).mp4")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/图片/image_without_extension.png")));
        QVERIFY(QFileInfo::exists(temp.filePath("outer.7z")));
        for (const auto& task : controller.tasks) QCOMPARE(QDir::fromNativeSeparators(task.output), root);
    }
    void noMediaAndFailedNestedBranchKeepTheirContents() {
        QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("note.txt"), "keep without media"));
        QVERIFY(Pack({temp.filePath("note.txt")}, temp.filePath("document.7z")));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.extractDestination = temp.filePath("output"); controller.addPaths({temp.filePath("document.7z")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(controller.tasks.last().success); QCOMPARE(controller.cleanedOtherFiles, 0);
        QCOMPARE(Read(temp.filePath("output/document/note.txt")), QByteArray("keep without media"));
        QVERIFY(Write(temp.filePath("clip.mp4"), "visible video"));
        QVERIFY(Pack({temp.filePath("clip.mp4")}, temp.filePath("locked.7z"), "missing-password"));
        QVERIFY(Pack({temp.filePath("clip.mp4"), temp.filePath("locked.7z"), temp.filePath("note.txt")}, temp.filePath("outer.7z")));
        controller.clear(false); controller.autoPasswords = false; controller.addPaths({temp.filePath("outer.7z")}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(!controller.tasks.last().success); QCOMPARE(controller.cleanedOtherFiles, 0);
        QVERIFY(QFileInfo::exists(temp.filePath("output/outer/locked.7z")));
        QCOMPARE(Read(temp.filePath("output/outer/clip.mp4")), QByteArray("visible video"));
        QVERIFY(QFileInfo::exists(temp.filePath("output/outer/note.txt")));
        QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("output/outer/视频"))));
    }
    void existingFilesAreNotOverwrittenOrRemoved() {
        QTemporaryDir temp; const auto root = temp.filePath("output");
        const auto media = root + "/incoming/clip.mp4", other = root + "/incoming/readme.txt";
        QVERIFY(Write(media, "new-video")); QVERIFY(Write(other, "junk"));
        const auto original = root + QStringLiteral("/视频/clip.mp4"); QVERIFY(Write(original, "existing-video"));
        QVERIFY(Write(root + "/existing.txt", "existing-note")); QVERIFY(QDir().mkpath(root + "/existing-empty"));
        const auto tracked = TrackGeneratedFiles({media, other}, root, {original});
        const auto result = OrganizeExtractedMedia(root, tracked, {root + "/incoming"});
        QVERIFY2(result.success, qPrintable(result.error)); QCOMPARE(result.removedFiles, 1);
        QCOMPARE(Read(original), QByteArray("existing-video")); QCOMPARE(Read(root + "/existing.txt"), QByteArray("existing-note"));
        QVERIFY(QFileInfo::exists(root + "/existing-empty")); QVERIFY(!QFileInfo::exists(root + "/incoming"));
        QCOMPARE(Read(result.videos.first()), QByteArray("new-video"));
        QVERIFY(result.videos.first().endsWith(QStringLiteral("/视频/clip (2).mp4")));
    }
    void changedLockedAndCancelledOperationsKeepOtherFiles() {
        QTemporaryDir temp; const auto root = temp.filePath("output");
        const auto media = root + "/source/clip.mp4", other = root + "/source/note.txt";
        QVERIFY(Write(media, "video")); QVERIFY(Write(other, "note"));
        auto tracked = TrackGeneratedFiles({media, other}, root, {});
        QVERIFY(Write(other, "changed-note"));
        QVERIFY(!OrganizeExtractedMedia(root, tracked, {}).success);
        QCOMPARE(Read(media), QByteArray("video")); QCOMPARE(Read(other), QByteArray("changed-note"));
        tracked = TrackGeneratedFiles({media, other}, root, {});
        HANDLE locked = CreateFileW(media.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE);
        const auto blocked = OrganizeExtractedMedia(root, tracked, {}); CloseHandle(locked);
        QVERIFY(!blocked.success); QVERIFY(QFileInfo::exists(media)); QVERIFY(QFileInfo::exists(other));
        int calls = 0;
        const auto cancelled = OrganizeExtractedMedia(root, tracked, {root + "/source"}, [&](uint64_t, uint64_t) { return ++calls < 4; });
        QVERIFY(!cancelled.success); QCOMPARE(cancelled.removedFiles, 0); QVERIFY(QFileInfo::exists(other));
        QCOMPARE(cancelled.relocated.size(), 1); QCOMPARE(Read(cancelled.relocated.value(media)), QByteArray("video"));
    }
    void recognizesImageHeadersAndKeepsImageContainersOutOfVideos() {
        QTemporaryDir temp;
        const auto png = temp.filePath("photo.bin"), avif = temp.filePath("image.avif"), numbered = temp.filePath("photo.001");
        const auto pngBytes = QByteArray::fromHex("89504e470d0a1a0a") + "pixels";
        QVERIFY(Write(png, pngBytes)); QVERIFY(Write(numbered, pngBytes));
        QVERIFY(Write(avif, QByteArray::fromHex("000000186674797061766966") + "picture"));
        QCOMPARE(DetectMediaFile(png).kind, MediaKind::Image); QCOMPARE(DetectMediaFile(png).extension, QString("png"));
        QCOMPARE(DetectMediaFile(avif).kind, MediaKind::Image);
    }
};
QTEST_MAIN(MediaOrganizationTests)
#include "MediaOrganizationTests.moc"
