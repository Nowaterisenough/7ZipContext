#include "ui/ArchiveWindow.h"
#include "ui/theme/animated_navigation.h"
#include "ui/theme/segmented_tabs.h"
#include "ui/theme/typography.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
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
        QCOMPARE(titles, 4); QCOMPARE(descriptions, 0);
        for (auto* label : window.pageWidget(0)->findChildren<QLabel*>()) {
            if (!label->property("settingDescription").toBool()) continue;
            ++descriptions;
            QVERIFY(qAbs(label->font().pointSizeF() - 9.4875) < .05);
            QCOMPARE(label->font().weight(), QFont::Normal);
            QCOMPARE(label->palette().color(QPalette::WindowText), QColor("#b9b9c1"));
        }
        QVERIFY(descriptions > 0);
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
    void queueActionsFollowAvailableInputsAndSelection() {
        QTemporaryDir temporary;
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.rememberGeometry = false;
        ArchiveWindow window(controller); window.show();
        for (const auto* name : {"removeArchive", "clearArchives", "correctName", "testArchives", "startExtraction", "removeSource", "clearSources", "startCompression", "openOutput"})
            QVERIFY2(!window.findChild<QPushButton*>(name)->isEnabled(), name);
        QVERIFY(!window.findChild<QLabel*>("operationStatus")->isVisible());
        QVERIFY(window.findChild<QPushButton*>("chooseArchives")->isEnabled());

        ArchivePlan archive; archive.input = L"example.bin"; archive.format = L"zip"; archive.needsRename = true;
        controller.archives.push_back(archive); controller.sources.append(temporary.filePath("example.txt"));
        emit controller.queueChanged();
        for (const auto* name : {"removeArchive", "clearArchives", "correctName", "testArchives", "startExtraction", "removeSource", "clearSources", "startCompression"})
            QVERIFY2(window.findChild<QPushButton*>(name)->isEnabled(), name);
        window.findChild<QListWidget*>("archiveList")->setCurrentRow(-1);
        QVERIFY(!window.findChild<QPushButton*>("removeArchive")->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("correctName")->isEnabled());
        QVERIFY(window.findChild<QPushButton*>("startExtraction")->isEnabled());
        controller.archives[0].needsRename = false; emit controller.queueChanged();
        QVERIFY(!window.findChild<QPushButton*>("correctName")->isEnabled());
        controller.clear(false); controller.clear(true);
        QVERIFY(!window.findChild<QPushButton*>("startExtraction")->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("startCompression")->isEnabled());
        controller.mediaInputs.append({temporary.path(), QStringLiteral("媒体"), {temporary.filePath("example.jpg")}});
        emit controller.queueChanged();
        QVERIFY(window.findChild<QPushButton*>("testArchives")->isEnabled());
        QVERIFY(window.findChild<QPushButton*>("startExtraction")->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("correctName")->isEnabled());

        controller.tasks.append({QStringLiteral("已完成"), {}, temporary.path(), true}); emit controller.tasksChanged();
        QVERIFY(window.findChild<QPushButton*>("openOutput")->isEnabled());
        controller.tasks.append({QStringLiteral("检测结果"), {}, {}, true}); emit controller.tasksChanged();
        window.findChild<QListWidget*>("taskList")->setCurrentRow(1);
        QVERIFY(!window.findChild<QPushButton*>("openOutput")->isEnabled());
    }
    void compactChoicesFitTheirLabelsAndViewport() {
        QTemporaryDir temporary;
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.rememberGeometry = false;
        controller.hideOnBlur = false;
        ArchiveWindow popup(controller, true); popup.resize(440, 700); popup.show();
        for (const auto& page : {"extract", "compress", "general", "passwords", "about"}) {
            popup.navigate(page); QCoreApplication::processEvents();
            for (auto* tabs : popup.findChildren<QTabBar*>()) {
                if (!tabs->isVisible()) continue;
                QCOMPARE(tabs->width(), tabs->tabRect(tabs->count() - 1).right() + 1);
            }
            for (auto* scroll : popup.findChildren<QScrollArea*>()) {
                if (!scroll->isVisible()) continue;
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                for (auto* control : scroll->widget()->findChildren<QWidget*>()) {
                    if (!control->isVisible() || !(qobject_cast<QAbstractButton*>(control) || qobject_cast<QLineEdit*>(control) || qobject_cast<QTabBar*>(control))) continue;
                    const auto origin = control->mapTo(scroll->viewport(), QPoint());
                    QVERIFY2(origin.x() >= 0 && origin.x() + control->width() <= scroll->viewport()->width(), qPrintable(control->objectName()));
                }
            }
        }
        popup.navigate("general");
        auto* choice = popup.findChild<QTabBar*>("hideOnBlur");
        QSignalSpy changes(choice, &QTabBar::currentChanged);
        QTest::mouseClick(choice, Qt::LeftButton, Qt::NoModifier, choice->tabRect(choice->currentIndex()).center());
        QCOMPARE(changes.count(), 0);
        choice->setFocus(Qt::TabFocusReason); QTest::keyClick(choice, Qt::Key_Right);
        QCOMPARE(choice->currentIndex(), 1); QVERIFY(controller.hideOnBlur);
        choice->setCurrentIndex(0);
    }
    void localFileDragHighlightsOnlyTheTargetQueue() {
        QTemporaryDir temporary;
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.rememberGeometry = false;
        ArchiveWindow window(controller); window.show();
        QMimeData local; local.setUrls({QUrl::fromLocalFile(temporary.filePath("example.zip"))});
        for (const auto& page : {"extract", "compress"}) {
            window.navigate(page);
            QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &local, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &enter); QVERIFY(enter.isAccepted());
            auto* target = window.findChild<QFrame*>(QString(page) == "compress" ? "sourceDropZone" : "archiveDropZone");
            QVERIFY(target->property("dropActive").toBool());
            QDragLeaveEvent leave; QApplication::sendEvent(&window, &leave);
            QVERIFY(!target->property("dropActive").toBool());
        }
        QMimeData remote; remote.setUrls({QUrl("https://example.com/archive.zip")});
        QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter); QVERIFY(!enter.isAccepted());
    }
    void searchHidesEmptyCardsAndRestoresThem() {
        QTemporaryDir temporary;
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.rememberGeometry = false;
        ArchiveWindow window(controller); window.show(); window.navigate("general");
        auto* search = window.findChild<QLineEdit*>("searchSettings");
        search->setText("no-such-setting");
        for (auto* card : window.pageWidget(3)->findChildren<QFrame*>())
            if (card->property("settingsCard").toBool()) QVERIFY(!card->isVisible());
        QVERIFY(window.pageWidget(3)->findChild<QLabel*>("settingsNoResults")->isVisible());
        search->clear();
        for (auto* card : window.pageWidget(3)->findChildren<QFrame*>())
            if (card->property("settingsCard").toBool()) QVERIFY(card->isVisible());
        window.navigate("passwords"); search->setText("no-such-password");
        QVERIFY(window.findChild<QWidget*>("passwordsNoResults")->isVisible());
        QVERIFY(!window.findChild<QPushButton*>("removePasswords")->isEnabled());
        search->clear(); QVERIFY(!window.findChild<QWidget*>("passwordsNoResults")->isVisible());
    }
    void trayAndMainShareSettingsWithoutShowingHiddenWindows() {
        ArchiveController controller; controller.rememberGeometry = false; controller.organizeMedia = true;
        ArchiveWindow main(controller), popup(controller, true);
        main.show(); main.navigate("extract");
        auto* media = main.findChild<QTabBar*>("organizeMedia"); QVERIFY(media);
        QVERIFY(!main.findChild<QTabBar*>("autoNested")->isEnabled());
        QTest::mouseClick(media, Qt::LeftButton, Qt::NoModifier, media->tabRect(1 - media->currentIndex()).center()); QVERIFY(!controller.organizeMedia);
        QVERIFY(!popup.findChild<QTabBar*>("organizeMedia")->currentIndex());
        auto* recursive = main.findChild<QTabBar*>("autoNested"); QVERIFY(recursive);
        const bool original = recursive->currentIndex() == 1;
        QTest::mouseClick(recursive, Qt::LeftButton, Qt::NoModifier, recursive->tabRect(1 - recursive->currentIndex()).center());
        QCOMPARE(controller.autoNested, !original);
        QCOMPARE(popup.findChild<QTabBar*>("autoNested")->currentIndex(), original ? 0 : 1);
        QTest::mouseClick(recursive, Qt::LeftButton, Qt::NoModifier, recursive->tabRect(1 - recursive->currentIndex()).center());
        QTest::mouseClick(media, Qt::LeftButton, Qt::NoModifier, media->tabRect(1 - media->currentIndex()).center()); QVERIFY(controller.organizeMedia);
        QVERIFY(popup.findChild<QTabBar*>("organizeMedia")->currentIndex());
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
        main.navigate("general"); popup.navigate("general");
        controller.rememberGeometry = true; controller.notifyOptions(); QTest::qWait(320);
        for (auto* window : {&main, &popup}) {
            auto* choice = static_cast<desktop::SegmentedTabBar*>(window->findChild<QTabBar*>("rememberGeometry"));
            QCOMPARE(choice->currentIndex(), 1);
            QVERIFY(choice->IndicatorRect().center().x() > choice->tabRect(0).right());
        }
        controller.rememberGeometry = false; controller.notifyOptions();
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
        QVERIFY(!popup.findChild<QTabBar*>("hideOnBlur")->currentIndex());
        const QRect tray(100, 100, 24, 24);
        popup.toggleAtTray(tray); QTest::qWait(20); QVERIFY(popup.isVisible());
        QEvent leave(QEvent::Leave), deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&popup, &leave); QApplication::sendEvent(&popup, &deactivate);
        QTest::qWait(20); QVERIFY(popup.isVisible());
        popup.toggleAtTray(tray); QVERIFY(!popup.isVisible());
        popup.toggleAtTray(tray); QTest::qWait(20); QVERIFY(popup.isVisible());
        QTest::keyClick(&popup, Qt::Key_Escape); QTRY_VERIFY(!popup.isVisible());
        QSignalSpy quit(&controller, &ArchiveController::quitReady);
        popup.toggleAtTray(tray); QTest::qWait(20); QVERIFY(popup.isVisible());
        auto* close = popup.findChild<QAbstractButton*>("closePopup"); QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton); QTRY_VERIFY(!popup.isVisible()); QCOMPARE(quit.count(), 0);
        popup.toggleAtTray(tray); QTest::qWait(20); QVERIFY(popup.isVisible()); popup.hide();
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
        ArchiveWindow main(controller), popup(controller, true); main.show(); QTest::qWait(30);
        QTest::mouseClick(main.findChild<QPushButton*>("openPasswordLists"), Qt::LeftButton);
        QCOMPARE(main.currentPage(), QStringLiteral("passwords"));
        auto* list = main.findChild<QListWidget*>("passwordLists");
        auto* popupList = popup.findChild<QListWidget*>("passwordLists");
        QVERIFY(list && popupList); QCOMPARE(list->count(), 0);
        QVERIFY(controller.importPasswords(imported.fileName()));
        QCOMPARE(list->count(), 2); QCOMPARE(popupList->count(), 2);
        QVERIFY(!main.findChild<QComboBox*>("passwordListSource"));
        QVERIFY(!main.findChild<QLineEdit*>("passwordListName"));
        QVERIFY(!main.findChild<QLabel*>("passwordSummary")->text().contains(QStringLiteral("密码单")));
        QVERIFY(!list->item(0)->toolTip().contains(QStringLiteral("来源")));
        QVERIFY(list->item(0)->flags() & Qt::ItemIsEditable);
        const auto find = [&](const QString& value) { const auto matches = list->findItems(value, Qt::MatchExactly); return matches.isEmpty() ? nullptr : matches.first(); };
        find("first-private-password")->setCheckState(Qt::Unchecked);
        QTRY_VERIFY(!controller.passwordLibrary().enabledPasswords().contains("first-private-password"));
        QVERIFY(controller.passwordLibrary().enabledPasswords().contains(" second-private-password "));
        find("first-private-password")->setCheckState(Qt::Checked);
        QTRY_COMPARE(controller.passwordLibrary().enabledPasswords().size(), 2);
        auto* automatic = main.findChild<QTabBar*>("autoPasswords");
        automatic->setCurrentIndex(0); QVERIFY(!controller.autoPasswords);
        QCOMPARE(popup.findChild<QTabBar*>("autoPasswords")->currentIndex(), 0);
        automatic->setCurrentIndex(1); QVERIFY(controller.autoPasswords);

        list->setCurrentItem(find("first-private-password"));
        QTest::mouseClick(main.findChild<QPushButton*>("editPasswords"), Qt::LeftButton);
        QTRY_VERIFY(list->findChild<QLineEdit*>());
        auto* editor = list->findChild<QLineEdit*>(); editor->selectAll();
        QTest::keyClicks(editor, " changed-private-password "); QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(controller.passwordLibrary().enabledPasswords().contains(" changed-private-password "));
        QVERIFY(!controller.passwordLibrary().enabledPasswords().contains("first-private-password"));
        QCOMPARE(popupList->findItems(" changed-private-password ", Qt::MatchExactly).size(), 1);
        QVERIFY(!QApplication::activeModalWidget());
        auto* input = main.findChild<QLineEdit*>("passwordInput"); input->setText("backup-private-password");
        QTest::keyClick(input, Qt::Key_Return); QCOMPARE(list->count(), 3); QCOMPARE(popupList->count(), 3); QVERIFY(input->text().isEmpty());
        find("backup-private-password")->setText("");
        QTRY_VERIFY(find("backup-private-password")); QCOMPARE(list->count(), 3);
        auto* search = main.findChild<QLineEdit*>("searchSettings"); search->setText("backup");
        QVERIFY(find(" changed-private-password ")->isHidden()); QVERIFY(!find("backup-private-password")->isHidden());
        QTest::mouseClick(main.findChild<QPushButton*>("removePasswords"), Qt::LeftButton);
        QCOMPARE(list->count(), 2); QCOMPARE(popupList->count(), 2); QVERIFY(!find("backup-private-password"));
        search->clear(); list->setFocus(); list->selectAll(); QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(list->count(), 0); QCOMPARE(popupList->count(), 0);
        QVERIFY(QFileInfo::exists(imported.fileName())); QVERIFY(!popup.isVisible());
        QString error; PasswordLibrary restored(temporary.filePath("library.dat")); QVERIFY(restored.load(error));
        QVERIFY(restored.uniquePasswords().isEmpty());
    }
    void flatPasswordActionsCoverAllLegacyCopies() {
        QTemporaryDir temp; ArchiveController controller(nullptr, temp.filePath("library.dat")); controller.rememberGeometry = false;
        const QHash<QString, QString> firstNotes{{"shared-code", "first-source-note"}}, secondNotes{{"shared-code", "second-source-note"}};
        QVERIFY(controller.addPasswordList("source A", "shared-code\nonly-a", &firstNotes));
        QVERIFY(controller.addPasswordList("source B", "shared-code\nonly-b", &secondNotes));
        QVERIFY(controller.setPasswordListEnabled(controller.passwordLibrary().lists().last().id, false));
        ArchiveWindow main(controller), popup(controller, true); main.show(); main.navigate("passwords"); QTest::qWait(30);
        auto* list = main.findChild<QListWidget*>("passwordLists"); auto* trayList = popup.findChild<QListWidget*>("passwordLists");
        QCOMPARE(list->count(), 3); QCOMPARE(trayList->count(), 3);
        QCOMPARE(list->findItems("shared-code", Qt::MatchExactly).size(), 1);
        auto* item = list->findItems("shared-code", Qt::MatchExactly).first();
        QVERIFY(!item->toolTip().contains("source"));
        item->setText("renamed-code");
        QTRY_COMPARE(list->findItems("renamed-code", Qt::MatchExactly).size(), 1);
        QTRY_VERIFY(!controller.passwordLibrary().enabledPasswords().contains("shared-code"));
        for (const auto& source : controller.passwordLibrary().lists()) QVERIFY(!source.entries.contains("shared-code"));
        QCOMPARE(trayList->findItems("renamed-code", Qt::MatchExactly).size(), 1);
        list->findItems("renamed-code", Qt::MatchExactly).first()->setCheckState(Qt::Unchecked);
        QTRY_VERIFY(!controller.passwordLibrary().enabledPasswords().contains("renamed-code"));
        QCOMPARE(controller.passwordLibrary().enabledPasswords(), QStringList({"only-a"}));
        list->setCurrentItem(list->findItems("renamed-code", Qt::MatchExactly).first());
        QTest::mouseClick(main.findChild<QPushButton*>("removePasswords"), Qt::LeftButton);
        QCOMPARE(list->count(), 2); QCOMPARE(trayList->count(), 2);
        for (const auto& source : controller.passwordLibrary().lists()) QVERIFY(!source.entries.contains("renamed-code"));
        QCOMPARE(controller.passwordLibrary().enabledPasswords(), QStringList({"only-a"})); QVERIFY(!popup.isVisible());
    }
    void homeQuickAddWorksWithoutNamingOrLeavingExtraction() {
        QTemporaryDir temp; ArchiveController controller(nullptr, temp.filePath("library.dat")); controller.rememberGeometry = false;
        ArchiveWindow main(controller), popup(controller, true);
        for (auto* window : {&main, &popup}) {
            window->show(); window->navigate("extract"); QTest::qWait(30);
            auto* input = window->findChild<QLineEdit*>("quickPassword"); QVERIFY(input && input->isVisible());
            input->setText("  quick-secret  "); QTest::keyClick(input, Qt::Key_Return);
            QCOMPARE(window->currentPage(), QStringLiteral("extract")); QVERIFY(input->text().isEmpty());
            QCOMPARE(controller.passwordLibrary().enabledPasswords(), QStringList({"  quick-secret  "}));
            QVERIFY(!controller.busy()); QVERIFY(!QApplication::activeModalWidget());
            QCOMPARE(main.findChild<QListWidget*>("passwordLists")->count(), 1);
            QCOMPARE(popup.findChild<QListWidget*>("passwordLists")->count(), 1);
        }
        QString error; PasswordLibrary saved(temp.filePath("library.dat")); QVERIFY(saved.load(error));
        QCOMPARE(saved.enabledPasswords(), QStringList({"  quick-secret  "}));
    }
    void updateControlsSynchronizeAndAreSearchable() {
        QTemporaryDir temporary;
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.rememberGeometry = false;
        ArchiveWindow main(controller), popup(controller, true);
        main.show(); main.navigate("about");
        auto* automatic = main.findChild<QTabBar*>("autoCheckUpdates"); QVERIFY(automatic);
        const auto previous = automatic->currentIndex() == 1;
        QTest::mouseClick(automatic, Qt::LeftButton, Qt::NoModifier, automatic->tabRect(1 - automatic->currentIndex()).center());
        QCOMPARE(controller.updates().autoCheck(), !previous);
        QCOMPARE(popup.findChild<QTabBar*>("autoCheckUpdates")->currentIndex(), previous ? 0 : 1);
        QTest::mouseClick(automatic, Qt::LeftButton, Qt::NoModifier, automatic->tabRect(1 - automatic->currentIndex()).center());
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
