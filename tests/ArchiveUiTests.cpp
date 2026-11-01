#include "ui/ArchiveWindow.h"
#include "ui/theme/animated_navigation.h"
#include "ui/theme/segmented_tabs.h"
#include "ui/theme/typography.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QLineEdit>
#include <QLabel>
#include <QFontInfo>
#include <QTextLayout>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSettings>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class ArchiveUiTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setOrganizationName(QStringLiteral("7ZipContextTests"));
        QApplication::setApplicationName(QStringLiteral("7ZipContextTests"));
        ApplyArchiveTheme(*qobject_cast<QApplication*>(QApplication::instance()));
        QCOMPARE(QApplication::font().families().first(), QStringLiteral("Segoe UI"));
        QVERIFY(qAbs(QApplication::font().pointSizeF() - 10.725) < .02);
    }
    void renderedTextUsesMagpieFontRoles() {
        ArchiveController controller; controller.rememberGeometry = false;
        ArchiveWindow window(controller); window.navigate("general"); window.show(); QTest::qWait(30);
        int descriptions = 0, titles = 0;
        for (auto* label : window.pageWidget(3)->findChildren<QLabel*>()) {
            if (label->property("settingDescription").toBool()) {
                ++descriptions;
                QVERIFY(qAbs(label->font().pointSizeF() - 9.4875) < .05);
                QCOMPARE(label->font().weight(), QFont::Normal);
                QCOMPARE(label->palette().color(QPalette::WindowText), QColor("#b9b9c1"));
            }
            if (label->property("cardTitle").toBool()) {
                ++titles;
                QVERIFY(qAbs(label->font().pointSizeF() - 10.725) < .05);
                QCOMPARE(label->font().weight(), QFont::DemiBold);
                QVERIFY(QFontInfo(label->font()).family().startsWith("Segoe UI"));
            }
        }
        QCOMPARE(titles, 4); QCOMPARE(descriptions, 4);
        QTextLayout text(QStringLiteral("7-Zip 设置"), desktop::type::Body());
        text.beginLayout(); text.createLine(); text.endLayout();
        bool latin = false, chinese = false;
        for (const auto& run : text.glyphRuns()) {
            latin |= run.rawFont().familyName().startsWith("Segoe UI");
            chinese |= run.rawFont().familyName().startsWith("Microsoft YaHei");
        }
        QVERIFY(latin);
        if (QFontDatabase::families().contains("Microsoft YaHei UI")) QVERIFY(chinese);
    }
    void navigationKeepsPagesAndEditsAlive() {
        ArchiveController controller;
        controller.rememberGeometry = false;
        ArchiveWindow window(controller);
        window.show(); QTest::qWait(30);
        auto* input = window.findChild<QLineEdit*>("archiveName"); QVERIFY(input);
        const auto count = window.findChildren<QWidget*>().size();
        QList<QWidget*> pages;
        for (int index = 0; index < 6; ++index) pages.append(window.pageWidget(index));
        auto* compress = window.findChild<QPushButton*>("nav_compress"); QVERIFY(compress);
        QTest::mouseClick(compress, Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("compress"));
        input->setFocus(); input->selectAll(); QTest::keyClicks(input, "MyArchive");
        for (int repetition = 0; repetition < 12; ++repetition) {
            for (const auto& page : {"extract", "tasks", "settings", "passwords", "about", "compress"}) {
                auto* button = window.findChild<QAbstractButton*>(QStringLiteral("nav_") + page); QVERIFY(button);
                QTest::mouseClick(button, Qt::LeftButton); QCoreApplication::processEvents();
                for (int index = 0; index < 6; ++index) QCOMPARE(window.pageWidget(index), pages[index]);
                QCOMPARE(window.findChild<QLineEdit*>("archiveName"), input);
            }
        }
        QCOMPARE(input->text(), QStringLiteral("MyArchive"));
        QCOMPARE(window.findChildren<QWidget*>().size(), count);
        QTest::qWait(320);
        auto* strip = static_cast<desktop::NavigationStrip*>(window.findChild<QFrame*>("centeredNavigation"));
        QVERIFY(strip); QVERIFY(!strip->SelectionAnimating());
        QVERIFY(!strip->IndicatorRect().isEmpty());
    }
    void magpieHeaderUsesTextTabsAndOneSelection() {
        ArchiveController controller; controller.rememberGeometry = false;
        ArchiveWindow window(controller); window.resize(1100, 820); window.show(); QTest::qWait(30);
        auto* strip = static_cast<desktop::NavigationStrip*>(window.findChild<QFrame*>("centeredNavigation")); QVERIFY(strip);
        for (auto* button : strip->findChildren<QPushButton*>()) QVERIFY(button->icon().isNull());
        QCOMPARE(strip->findChildren<QPushButton*>().size(), 3);
        auto* settings = window.findChild<QAbstractButton*>("nav_settings"); QVERIFY(settings);
        QVERIFY(!strip->isAncestorOf(settings));
        QVERIFY(settings->mapTo(&window, QPoint()).x() > window.width() - 90);
        QTest::mouseClick(settings, Qt::LeftButton); QTest::qWait(320);
        QCOMPARE(window.currentPage(), QStringLiteral("general"));
        QVERIFY(strip->IndicatorRect().isEmpty());
        auto image = window.grab().toImage();
        for (auto* button : strip->findChildren<QPushButton*>()) {
            QVERIFY(!button->isChecked());
            const QPoint edge = button->mapTo(&window, QPoint(1, 4));
            QCOMPARE(image.pixelColor(QPoint(qRound(edge.x() * image.devicePixelRatio()), qRound(edge.y() * image.devicePixelRatio()))), QColor("#1a1a1e"));
        }
        QVERIFY(!window.findChild<QLabel*>("breadcrumb"));
        QVERIFY(!window.findChild<QLabel*>("engineStatus"));
        auto* search = window.findChild<QLineEdit*>("searchSettings"); QVERIFY(search); QVERIFY(search->isVisible());
        search->setText(QStringLiteral("不存在的设置")); QCoreApplication::processEvents();
        QVERIFY(window.pageWidget(3)->findChild<QLabel*>("settingsNoResults")->isVisible());
        search->clear();
        for (auto* card : window.pageWidget(3)->findChildren<QFrame*>()) {
            if (card->property("settingsCard").toBool()) QCOMPARE(card->mapTo(&window, QPoint()).x(), 18);
        }
        window.resize(1412, 1002); QCoreApplication::processEvents();
        for (auto* card : window.pageWidget(3)->findChildren<QFrame*>()) {
            if (!card->property("settingsCard").toBool()) continue;
            QCOMPARE(card->mapTo(&window, QPoint()).x(), 46);
            QCOMPARE(card->width(), 1320);
        }
        auto* scroll = qobject_cast<QScrollArea*>(window.pageWidget(3)); QVERIFY(scroll);
        QVERIFY(scroll->verticalScrollBar()->mapTo(&window, QPoint()).x() > window.width() - 10);
        QVERIFY(!scroll->verticalScrollBar()->property("scrollable").toBool());
        const auto noScroll = window.grab().toImage();
        QCOMPARE(noScroll.pixelColor(qRound((window.width() - 3) * noScroll.devicePixelRatio()),
                                     qRound(300 * noScroll.devicePixelRatio())), QColor("#1a1a1e"));
    }
    void trayAndMainShareSettingsWithoutShowingHiddenWindows() {
        ArchiveController controller; controller.rememberGeometry = false; controller.organizeMedia = true;
        ArchiveWindow main(controller), popup(controller, true);
        main.show(); main.navigate("extract");
        auto* media = main.findChild<QAbstractButton*>("organizeMedia"); QVERIFY(media);
        QVERIFY(!main.findChild<QAbstractButton*>("autoNested")->isEnabled());
        QTest::mouseClick(media, Qt::LeftButton); QVERIFY(!controller.organizeMedia);
        QVERIFY(!popup.findChild<QAbstractButton*>("organizeMedia")->isChecked());
        auto* recursive = main.findChild<QAbstractButton*>("autoNested"); QVERIFY(recursive);
        const bool original = recursive->isChecked();
        QTest::mouseClick(recursive, Qt::LeftButton);
        QCOMPARE(controller.autoNested, !original);
        QCOMPARE(popup.findChild<QAbstractButton*>("autoNested")->isChecked(), !original);
        QTest::mouseClick(recursive, Qt::LeftButton);
        QTest::mouseClick(media, Qt::LeftButton); QVERIFY(controller.organizeMedia);
        QVERIFY(popup.findChild<QAbstractButton*>("organizeMedia")->isChecked());
        QVERIFY(!popup.findChild<QTabBar*>("extractMode")->isEnabled());
        main.navigate("compress");
        auto* password = main.findChild<QLineEdit*>("compressPassword");
        password->setFocus(); QTest::keyClicks(password, "session-secret");
        QCOMPARE(popup.findChild<QLineEdit*>("compressPassword")->text(), QStringLiteral("session-secret"));
        auto* format = main.findChild<QTabBar*>("archiveFormat");
        format->setCurrentIndex(1);
        QCOMPARE(popup.findChild<QTabBar*>("archiveFormat")->currentIndex(), 1);
        QCOMPARE(controller.compression.format, std::wstring(L"zip"));
        QVERIFY(!popup.isVisible());
        popup.navigate("compress"); popup.show(); QTest::qWait(320);
        auto* popupFormat = static_cast<desktop::SegmentedTabBar*>(popup.findChild<QTabBar*>("archiveFormat"));
        QVERIFY(popupFormat->IndicatorRect().center().x() > popupFormat->tabRect(0).right());
        auto* popupNavigation = static_cast<desktop::NavigationStrip*>(popup.findChild<QFrame*>("centeredNavigation"));
        const auto popupImage = popup.grab().toImage();
        QCOMPARE(popupImage.pixelColor(qRound(5 * popupImage.devicePixelRatio()), qRound(300 * popupImage.devicePixelRatio())), QColor("#1c1d21"));
        const auto thumbPixel = popupNavigation->mapTo(&popup, popupNavigation->IndicatorRect().topLeft().toPoint() + QPoint(8, 8));
        QCOMPARE(popupImage.pixelColor(QPoint(qRound(thumbPixel.x() * popupImage.devicePixelRatio()), qRound(thumbPixel.y() * popupImage.devicePixelRatio()))), QColor("#25262c"));
        auto* volume = popup.findChild<QComboBox*>("volumeSize"); QVERIFY(volume);
        volume->showPopup();
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&popup, &deactivate); QTest::qWait(20);
        QVERIFY(popup.isVisible());
        volume->hidePopup();
        popup.hide(); controller.notifyOptions(); QVERIFY(!popup.isVisible());
    }
    void trayStaysOpenAfterLeavingAndLosingFocusWithLegacyPreferences() {
        QSettings preferences(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/7ZipContext/desktop.ini", QSettings::IniFormat);
        const auto oldLegacy = preferences.value("Window/HideOnBlur"), oldCurrent = preferences.value("Window/TrayAutoHide");
        const auto restore = qScopeGuard([&] {
            for (const auto& entry : {qMakePair(QString("Window/HideOnBlur"), oldLegacy), qMakePair(QString("Window/TrayAutoHide"), oldCurrent)}) {
                if (entry.second.isValid()) preferences.setValue(entry.first, entry.second); else preferences.remove(entry.first);
            }
            preferences.sync();
        });
        preferences.setValue("Window/HideOnBlur", true); preferences.remove("Window/TrayAutoHide"); preferences.sync();
        ArchiveController controller;
        QVERIFY(!controller.hideOnBlur);
        ArchiveWindow popup(controller, true);
        QVERIFY(popup.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(!popup.findChild<QAbstractButton*>("hideOnBlur")->isChecked());
        const QRect tray(100, 100, 24, 24);
        popup.toggleAtTray(tray); QTest::qWait(20); QVERIFY(popup.isVisible());
        QEvent leave(QEvent::Leave), deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&popup, &leave); QApplication::sendEvent(&popup, &deactivate);
        QTest::qWait(20); QVERIFY(popup.isVisible());
        popup.toggleAtTray(tray); QVERIFY(!popup.isVisible());
        popup.toggleAtTray(tray); QTest::qWait(20); QVERIFY(popup.isVisible());
        QTest::keyClick(&popup, Qt::Key_Escape); QTRY_VERIFY(!popup.isVisible());
        controller.hideOnBlur = true; controller.savePreferences();
        QVERIFY(ArchiveController().hideOnBlur);
        QVERIFY(!preferences.contains("Window/HideOnBlur"));
        controller.hideOnBlur = false; controller.savePreferences();
        QVERIFY(!ArchiveController().hideOnBlur);
    }
    void pausedJobCanCancelAndReleaseControls() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        QFile file(temp.filePath("payload.bin")); QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray data(2 * 1024 * 1024, '\0');
        for (int i = 0; i < data.size(); ++i) data[i] = static_cast<char>((i * 7919) % 251);
        file.write(data); file.close();
        ArchiveController controller; controller.rememberGeometry = false;
        ArchiveWindow popup(controller, true);
        controller.addPaths({file.fileName()}, true);
        controller.archiveName = "cancelled"; controller.compressDestination = temp.path();
        QVERIFY(controller.start(ArchiveController::Operation::Compress));
        controller.togglePause(); QVERIFY(controller.paused());
        QVERIFY(!popup.findChild<QPushButton*>("startCompression")->isEnabled());
        QTest::qWait(30); controller.cancel(); QVERIFY(!controller.paused());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(!QFileInfo::exists(temp.filePath("cancelled.7z")));
        QVERIFY(popup.findChild<QPushButton*>("startCompression")->isEnabled());
        QVERIFY(!popup.isVisible());
    }
    void passwordManagerImportsEditsAndSynchronizes() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        QFile imported(temporary.filePath("imported-list.txt")); QVERIFY(imported.open(QIODevice::WriteOnly));
        imported.write("first-private-password\n second-private-password \nfirst-private-password\n"); imported.close();
        ArchiveController controller(nullptr, temporary.filePath("library.dat"));
        controller.rememberGeometry = false; controller.autoPasswords = true;
        ArchiveWindow main(controller), popup(controller, true);
        main.show(); QTest::qWait(30);
        QTest::mouseClick(main.findChild<QPushButton*>("openPasswordLists"), Qt::LeftButton);
        QCOMPARE(main.currentPage(), QStringLiteral("passwords"));
        auto* lists = main.findChild<QListWidget*>("passwordLists");
        auto* popupLists = popup.findChild<QListWidget*>("passwordLists");
        QVERIFY(lists && popupLists); QCOMPARE(lists->count(), 0);
        QVERIFY(controller.importPasswordList(imported.fileName()));
        QCOMPARE(lists->count(), 2); QCOMPARE(popupLists->count(), 2);
        QCOMPARE(controller.passwordLibrary().enabledPasswords().size(), 2);
        QCOMPARE(main.findChild<QLabel*>("passwordSummary")->text(), popup.findChild<QLabel*>("passwordSummary")->text());
        QCOMPARE(lists->item(0)->text(), QStringLiteral("first-private-password"));
        QVERIFY(lists->item(0)->toolTip().contains(QStringLiteral("来源：imported-list")));
        QTest::mouseClick(main.findChild<QPushButton*>("togglePasswords"), Qt::LeftButton);
        QCOMPARE(controller.passwordLibrary().enabledCount(), 0);
        QCOMPARE(popup.findChild<QPushButton*>("togglePasswords")->text(), QStringLiteral("启用密码单"));
        QTest::mouseClick(main.findChild<QPushButton*>("togglePasswords"), Qt::LeftButton);
        auto* automatic = main.findChild<QAbstractButton*>("autoPasswords"); QVERIFY(automatic);
        QTest::mouseClick(automatic, Qt::LeftButton); QVERIFY(!controller.autoPasswords);
        QVERIFY(!popup.findChild<QAbstractButton*>("autoPasswords")->isChecked());
        QTest::mouseClick(automatic, Qt::LeftButton); QVERIFY(controller.autoPasswords);

        QTimer timeout;
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        bool edited = false;
        QTimer::singleShot(0, &main, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto* entries = dialog->findChild<QTableWidget*>("passwordListEntries"); QVERIFY(entries);
            QCOMPARE(entries->item(0, 0)->text(), QStringLiteral("first-private-password"));
            QCOMPARE(entries->item(1, 0)->text(), QStringLiteral(" second-private-password "));
            entries->item(0, 0)->setText(" changed-private-password "); entries->item(0, 1)->setText(QStringLiteral("下载分卷专用"));
            entries->item(1, 0)->setText("new-private-password");
            dialog->findChild<QLineEdit*>("passwordListName")->setText(QStringLiteral("已编辑的密码单"));
            QVERIFY(QDir().mkpath("ui-snapshots")); QVERIFY(dialog->grab().save("ui-snapshots/editor-table.png"));
            QTest::mouseClick(dialog->findChild<QPushButton*>("savePasswordEditor"), Qt::LeftButton); edited = true;
        });
        QTest::mouseClick(main.findChild<QPushButton*>("editPasswords"), Qt::LeftButton); QVERIFY(edited);
        QCOMPARE(controller.passwordLibrary().lists()[0].entries, QStringList({" changed-private-password ", "new-private-password"}));
        QCOMPARE(lists->item(0)->text(), QStringLiteral(" changed-private-password "));
        QVERIFY(lists->item(0)->toolTip().contains(QStringLiteral("下载分卷专用")));
        QCOMPARE(controller.passwordLibrary().lists()[0].comments.value(" changed-private-password "), QStringLiteral("下载分卷专用"));
        QCOMPARE(popupLists->item(0)->text(), lists->item(0)->text());

        bool added = false;
        QTimer::singleShot(0, &main, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto* save = dialog->findChild<QPushButton*>("savePasswordEditor"); QVERIFY(save);
            QTest::mouseClick(save, Qt::LeftButton); QVERIFY(dialog->isVisible());
            dialog->findChild<QLineEdit*>("passwordListName")->setText(QStringLiteral("备用"));
            dialog->findChild<QTableWidget*>("passwordListEntries")->item(0, 0)->setText("backup-private-password");
            QTest::mouseClick(save, Qt::LeftButton); added = true;
        });
        QTest::mouseClick(main.findChild<QPushButton*>("addPasswords"), Qt::LeftButton); QVERIFY(added); timeout.stop();
        QCOMPARE(lists->count(), 3); QCOMPARE(popupLists->count(), 3);
        auto* search = main.findChild<QLineEdit*>("searchSettings"); search->setText(QStringLiteral("备用"));
        QVERIFY(lists->item(0)->isHidden()); QVERIFY(!lists->item(2)->isHidden());
        search->setText(QStringLiteral("下载分卷")); QVERIFY(!lists->item(0)->isHidden()); QVERIFY(lists->item(2)->isHidden()); search->clear();
        lists->setCurrentRow(0);
        QTest::mouseClick(main.findChild<QPushButton*>("removePasswords"), Qt::LeftButton);
        QCOMPARE(lists->count(), 1); QCOMPARE(popupLists->count(), 1);
        QVERIFY(QFileInfo::exists(imported.fileName())); QVERIFY(!popup.isVisible());
        QString error; PasswordLibrary restored(temporary.filePath("library.dat")); QVERIFY(restored.load(error));
        QCOMPARE(restored.lists().size(), 1); QCOMPARE(restored.lists()[0].name, QStringLiteral("备用"));
    }
    void updateControlsSynchronizeAndAreSearchable() {
        QTemporaryDir temporary;
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.rememberGeometry = false;
        ArchiveWindow main(controller), popup(controller, true);
        main.show(); main.navigate("about");
        auto* automatic = main.findChild<QAbstractButton*>("autoCheckUpdates"); QVERIFY(automatic);
        const auto previous = automatic->isChecked();
        QTest::mouseClick(automatic, Qt::LeftButton);
        QCOMPARE(controller.updates().autoCheck(), !previous);
        QCOMPARE(popup.findChild<QAbstractButton*>("autoCheckUpdates")->isChecked(), !previous);
        QTest::mouseClick(automatic, Qt::LeftButton);
        QVERIFY(main.findChild<QPushButton*>("checkUpdate")->isEnabled());
        QVERIFY(!main.findChild<QPushButton*>("installUpdate")->isVisible());
        auto* search = main.findChild<QLineEdit*>("searchSettings"); search->setText(QStringLiteral("在线更新"));
        QVERIFY(main.findChild<QPushButton*>("checkUpdate")->isVisible());
        QVERIFY(!main.pageWidget(4)->findChild<QLabel*>("settingsNoResults")->isVisible());
        search->setText("no-such-setting"); QVERIFY(!main.findChild<QPushButton*>("checkUpdate")->isVisible());
        QVERIFY(main.pageWidget(4)->findChild<QLabel*>("settingsNoResults")->isVisible());
        QVERIFY(!popup.isVisible());
    }
    void realOperationsDragDropAndSnapshots() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("工作资料.txt"));
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray payload(200000, 'x'); QCOMPARE(file.write(payload), payload.size()); file.close();
        ArchiveController controller; controller.rememberGeometry = false;
        ArchiveWindow main(controller), popup(controller, true);
        main.show(); QTest::qWait(30); main.navigate("compress");
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(source)});
        QDragEnterEvent drag(QPoint(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&main, &drag); QVERIFY(drag.isAccepted());
        QDropEvent drop(QPointF(30, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&main, &drop);
        QCOMPARE(controller.sources.size(), 1);
        QCOMPARE(popup.findChild<QListWidget*>("sourceList")->count(), 1);
        controller.archiveName = QStringLiteral("工作资料备份");
        controller.compression.format = L"zip";
        controller.compressDestination = temp.path();
        controller.notifyOptions();
        auto* start = main.findChild<QPushButton*>("startCompression"); QVERIFY(start);
        QSignalSpy busy(&controller, &ArchiveController::busyChanged);
        QTest::mouseClick(start, Qt::LeftButton);
        QVERIFY(controller.busy()); QCOMPARE(main.currentPage(), QStringLiteral("tasks"));
        QVERIFY(!popup.findChild<QPushButton*>("startCompression")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY2(controller.tasks.last().success, qPrintable(controller.tasks.last().detail));
        QCOMPARE(busy.size(), 2);
        const QString archive = temp.filePath(QStringLiteral("工作资料备份.zip")); QVERIFY(QFileInfo::exists(archive));
        const QString disguised = temp.filePath(QStringLiteral("下载的压缩包.bin")); QVERIFY(QFile::copy(archive, disguised));
        controller.addPaths({archive, disguised}, false);
        QCOMPARE(controller.archives.size(), size_t(2)); QVERIFY(controller.archives[1].needsRename);
        QVERIFY(controller.start(ArchiveController::Operation::Correct, 1));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(!controller.archives[1].needsRename);
        QVERIFY(QFileInfo::exists(temp.filePath(QStringLiteral("下载的压缩包.zip"))));
        QVERIFY(controller.start(ArchiveController::Operation::Test));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000); QVERIFY(controller.tasks.last().success);
        controller.extractDestination = temp.filePath("extracted"); controller.notifyOptions();
        QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000); QVERIFY(controller.tasks.last().success);
        QFile extracted(temp.filePath(QStringLiteral("extracted/工作资料备份/工作资料.txt")));
        QVERIFY(extracted.open(QIODevice::ReadOnly)); QCOMPARE(extracted.readAll(), payload);

        const QString output = qEnvironmentVariable("ARCHIVE_UI_SNAPSHOT_DIR", QDir::currentPath() + "/ui-snapshots"); QVERIFY(QDir().mkpath(output));
        auto snapshot = [&](ArchiveWindow& window, const QString& page, const QString& name) {
            window.navigate(page); window.show(); QTest::qWait(320);
            QVERIFY(window.grab().save(output + '/' + name + ".png"));
        };
        main.resize(1100, 820);
        snapshot(main, "extract", "main-extract");
        snapshot(main, "compress", "main-compress");
        snapshot(main, "general", "main-settings");
        snapshot(main, "tasks", "main-tasks");
        controller.hideOnBlur = false; popup.resize(480, 700);
        snapshot(popup, "extract", "tray-extract");
        snapshot(popup, "compress", "tray-compress");
        snapshot(popup, "tasks", "tray-tasks");
        main.resize(820, 620); snapshot(main, "compress", "main-small");
        for (const auto* name : {"archiveName", "compressDestination", "compressPassword", "extractDestination", "extractPassword"}) {
            auto* edit = main.findChild<QLineEdit*>(name); QVERIFY(edit);
            QVERIFY2(edit->width() >= 130, qPrintable(QStringLiteral("%1 width: %2").arg(name).arg(edit->width())));
        }
    }
};
QTEST_MAIN(ArchiveUiTests)
#include "ArchiveUiTests.moc"
