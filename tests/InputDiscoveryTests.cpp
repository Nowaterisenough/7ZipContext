#include "ui/ArchiveController.h"
#include "ui/ArchiveWindow.h"
#include "ui/InputDiscovery.h"
#include <QApplication>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool Write(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray Read(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
bool Pack(const QStringList& paths, const QString& output, bool split = false) {
    SevenZipCore core; CompressionOptions options; options.level = 0; options.volumeSize = split ? 8192 : 0;
    std::vector<std::wstring> inputs; for (const auto& path : paths) inputs.push_back(path.toStdWString());
    return core.Compress(inputs, output.toStdWString(), options);
}
}

class InputDiscoveryTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true); QCoreApplication::setOrganizationName("7ZipContextTests"); QCoreApplication::setApplicationName("7ZipContextTests");
        ApplyArchiveTheme(*qobject_cast<QApplication*>(QApplication::instance()));
    }
    void mixedFolderExpandsDeduplicatesAndOrganizesEverything() {
        QTemporaryDir temp; const auto input = temp.filePath("input");
        const auto video = temp.filePath("fixture/clip.mp4"); const QByteArray payload(40000, 'v'); QVERIFY(Write(video, payload));
        QVERIFY(Pack({video}, input + "/archives/disguised.bin"));
        QVERIFY(Pack({video}, input + "/parts/volumes.7z", true));
        QVERIFY(Write(input + "/loose/standalone.mp4", "direct-video"));
        QVERIFY(Write(input + "/loose/photo.jpg", QByteArray::fromHex("ffd8ffe0") + "photo"));
        QVERIFY(Write(input + "/readme.txt", "auxiliary notes"));
        QVERIFY(Write(input + "/help.pdf", "%PDF-1.7\nhelp document"));
        QVERIFY(Write(input + "/application.apk", QByteArray::fromHex("504b0304") + "finished package"));
        QMap<QString,QByteArray> original;
        QDirIterator files(input, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) { const auto path = files.next(); original.insert(path, Read(path)); }
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.organizeMedia = true;
        controller.extractDestination = temp.filePath("output");
        controller.addPaths({input, input + "/archives/disguised.bin", input + "/parts/volumes.7z.002", input + "/loose/standalone.mp4"}, false);
        QCOMPARE(controller.archives.size(), size_t(2)); QCOMPARE(controller.mediaInputs.size(), 1); QCOMPARE(controller.ignoredInputFiles.size(), 3);
        QCOMPARE(controller.discoveredInputFiles.size(), original.size());
        controller.addPaths({input}, false); QCOMPARE(controller.archives.size(), size_t(2)); QCOMPARE(controller.mediaInputs.size(), 1);
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 3);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(controller.videoFiles.size(), 3); QCOMPARE(controller.imageFiles.size(), 1);
        QCOMPARE(Read(temp.filePath(QStringLiteral("output/loose/视频/standalone.mp4"))), QByteArray("direct-video"));
        QCOMPARE(Read(temp.filePath(QStringLiteral("output/loose/图片/photo.jpg"))), original.value(input + "/loose/photo.jpg"));
        QCOMPARE(Read(temp.filePath(QStringLiteral("output/disguised/视频/clip.mp4"))), payload);
        for (auto it = original.cbegin(); it != original.cend(); ++it) QCOMPARE(Read(it.key()), it.value());
    }
    void selectingLaterVolumeStillDiscoversTheWholeGroup() {
        QTemporaryDir temp; const auto source = temp.filePath("fixture/clip.mp4"); QVERIFY(Write(source, QByteArray(40000, 'v')));
        QVERIFY(Pack({source}, temp.filePath("input/archive.7z"), true));
        const auto discovered = DiscoverInputs({temp.filePath("input/archive.7z.003")});
        QCOMPARE(discovered.archives.size(), size_t(1)); QVERIFY(discovered.archives.front().error.empty());
        QVERIFY(discovered.archives.front().volumes.size() > 3); QVERIFY(discovered.ignored.isEmpty());
    }
    void unreadableAndUnknownInputsAreNotSilentlyDiscarded() {
        QTemporaryDir temp;
        QVERIFY(Write(temp.filePath("folder/unknown"), QByteArray(2 * 1024 * 1024, 'x')));
        QVERIFY(Write(temp.filePath("folder/missing.part002.apk"), QByteArray(8192, 'x')));
        QVERIFY(Write(temp.filePath("folder/note.txt"), "auxiliary"));
        const auto result = DiscoverInputs({temp.filePath("folder"), temp.filePath("absent")});
        QCOMPARE(result.archives.size(), size_t(3)); QCOMPARE(result.ignored.size(), 1);
        for (const auto& archive : result.archives) QVERIFY(!archive.error.empty());
    }
    void emptyDirectoriesAreSkippedWithoutHidingMissingPaths() {
        QTemporaryDir temp; QVERIFY(QDir().mkpath(temp.filePath("input/empty/nested")));
        QVERIFY(Write(temp.filePath("input/video.mp4"), "video"));
        const auto result = DiscoverInputs({temp.filePath("input"), temp.filePath("missing")});
        QCOMPARE(result.files.size(), 1); QCOMPARE(result.media.size(), 1); QCOMPARE(result.archives.size(), size_t(1));
        QCOMPARE(QFileInfo(QString::fromStdWString(result.archives.front().input)).fileName(), QString("missing"));
        QVERIFY(!result.archives.front().error.empty());
        const auto empty = DiscoverInputs({temp.filePath("input/empty")});
        QVERIFY(empty.files.isEmpty()); QVERIFY(empty.archives.empty()); QVERIFY(empty.media.isEmpty());
    }
    void cancellingBackgroundScanPreservesExistingQueue() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("fixture/video.mp4"), "video"));
        QVERIFY(Pack({temp.filePath("fixture/video.mp4")}, temp.filePath("existing.7z")));
        for (int i = 0; i < 50; ++i) QVERIFY(Write(temp.filePath(QString("folder/%1.txt").arg(i)), "text"));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.addPaths({temp.filePath("existing.7z")}, false);
        controller.queuePaths({temp.filePath("folder")}, false, true);
        QVERIFY(controller.scanningInputs()); QVERIFY(controller.busy()); QVERIFY(!controller.start(ArchiveController::Operation::Extract));
        controller.cancel(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10000);
        QCOMPARE(controller.archives.size(), size_t(1)); QVERIFY(controller.ignoredInputFiles.isEmpty());
        QVERIFY(controller.status().contains(QStringLiteral("取消")));
    }
    void dropWaitsForStartThenOrganizesEverything_data() {
        QTest::addColumn<bool>("compact"); QTest::addColumn<bool>("folder");
        QTest::newRow("main-folder") << false << true;
        QTest::newRow("main-files") << false << false;
        QTest::newRow("tray-folder") << true << true;
        QTest::newRow("tray-files") << true << false;
    }
    void dropWaitsForStartThenOrganizesEverything() {
        QFETCH(bool, compact); QFETCH(bool, folder);
        QTemporaryDir temp; const auto input = temp.filePath("drop");
        QVERIFY(Write(temp.filePath("fixture/video.mp4"), "packed-video"));
        QVERIFY(Pack({temp.filePath("fixture/video.mp4")}, input + "/archive.7z"));
        QVERIFY(Write(input + "/photo.png", QByteArray::fromHex("89504e470d0a1a0a") + "photo"));
        QVERIFY(Write(input + "/help.pdf", "%PDF-1.7\nnotes"));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.rememberGeometry = false; controller.organizeMedia = true; controller.hideOnBlur = false;
        controller.extractDestination = temp.filePath("output"); ArchiveWindow main(controller), popup(controller, true);
        auto& window = compact ? popup : main;
        if (compact) {
            popup.toggleAtTray(QRect(100, 100, 24, 24));
            QEvent leave(QEvent::Leave), deactivate(QEvent::WindowDeactivate);
            QApplication::sendEvent(&popup, &leave); QApplication::sendEvent(&popup, &deactivate);
            QTest::qWait(20); QVERIFY(popup.isVisible());
        } else window.show();
        QMimeData mime; QList<QUrl> urls;
        if (folder) urls.append(QUrl::fromLocalFile(input));
        else for (const auto& path : QDir(input).entryInfoList(QDir::Files)) urls.append(QUrl::fromLocalFile(path.absoluteFilePath()));
        mime.setUrls(urls);
        QList<bool> busyStates;
        connect(&controller, &ArchiveController::busyChanged, &controller, [&] { busyStates.append(controller.busy()); });
        QDragEnterEvent drag(QPoint(30,30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drag); QVERIFY(drag.isAccepted());
        QDropEvent drop(QPointF(30,30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop); QVERIFY(drop.isAccepted()); QVERIFY(controller.scanningInputs());
        QCOMPARE(window.currentPage(), QStringLiteral("extract"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(main.findChild<QListWidget*>("archiveList")->count(), 2); QCOMPARE(popup.findChild<QListWidget*>("archiveList")->count(), 2);
        QVERIFY(controller.tasks.isEmpty()); QVERIFY(controller.videoFiles.isEmpty());
        QVERIFY(!QFileInfo::exists(controller.extractDestination));
        QVERIFY(QFileInfo::exists(input + "/help.pdf"));
        QVERIFY(Write(temp.filePath("second/new.mp4"), "next-batch"));
        mime.setUrls({QUrl::fromLocalFile(temp.filePath("second"))});
        QDragEnterEvent nextDrag(QPoint(30,30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &nextDrag); QVERIFY(nextDrag.isAccepted());
        QDropEvent nextDrop(QPointF(30,30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &nextDrop); QVERIFY(nextDrop.isAccepted());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(controller.tasks.isEmpty()); QVERIFY(!QFileInfo::exists(controller.extractDestination));
        QCOMPARE(window.findChild<QListWidget*>("archiveList")->count(), 3);
        busyStates.clear();
        QTest::mouseClick(window.findChild<QPushButton*>("startExtraction"), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("tasks"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QCOMPARE(controller.tasks.size(), 3); QCOMPARE(controller.videoFiles.size(), 2); QCOMPARE(controller.imageFiles.size(), 1);
        for (const auto& task : controller.tasks) QVERIFY2(task.success, qPrintable(task.detail));
        QCOMPARE(busyStates.count(false), 1); QVERIFY(!busyStates.last());
        QCOMPARE(Read(temp.filePath(QStringLiteral("output/archive/视频/video.mp4"))), QByteArray("packed-video"));
        QCOMPARE(Read(temp.filePath(QStringLiteral("output/second/视频/new.mp4"))), QByteArray("next-batch"));
        QVERIFY(!QFileInfo::exists(temp.filePath("output/archive (2)")));
    }
    void cancelledAutomaticIntakeDoesNotStartExtraction() {
        QTemporaryDir temp; QVERIFY(Write(temp.filePath("input/video.mp4"), "video"));
        ArchiveController controller(nullptr, temp.filePath("passwords.dat")); controller.extractDestination = temp.filePath("output");
        controller.extractPaths({temp.filePath("input")}); controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10000);
        QVERIFY(controller.tasks.isEmpty()); QVERIFY(!QFileInfo::exists(controller.extractDestination));
    }
};
QTEST_MAIN(InputDiscoveryTests)
#include "InputDiscoveryTests.moc"
