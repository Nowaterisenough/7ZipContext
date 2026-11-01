#include "ui/ArchiveWindow.h"
#include "ui/ApplicationExit.h"
#include <QApplication>
#include <QCloseEvent>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <Windows.h>

// Offscreen has no system tray. Model the native main window's close-to-tray behavior.
class TrayCloseFilter final : public QObject {
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::Close) return false;
        static_cast<QCloseEvent*>(event)->ignore();
        static_cast<QWidget*>(object)->hide();
        return true;
    }
};

class DesktopLifecycleTests final : public QObject {
    Q_OBJECT
private slots:
    void explicitExit_data() {
        QTest::addColumn<QString>("visibleWindows");
        for (const auto* mode : {"hidden", "main", "tray", "both"}) QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void explicitExit() {
        QFETCH(QString, visibleWindows);
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
        child.start(QCoreApplication::applicationFilePath(), {"--exit-child", visibleWindows, "-platform", "offscreen"});
        QVERIFY(child.waitForStarted());
        if (!child.waitForFinished(10000)) { child.kill(); child.waitForFinished(); QFAIL("Desktop child did not exit"); }
        QVERIFY2(child.exitStatus() == QProcess::NormalExit, child.readAll().constData());
        QCOMPARE(child.exitCode(), 0);
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    application.setApplicationName("DesktopLifecycleTests");
    application.setQuitOnLastWindowClosed(false);
    if (application.arguments().contains("--exit-child")) {
        const auto index = application.arguments().indexOf("--exit-child");
        const auto mode = application.arguments().value(index + 1);
        QTemporaryDir temp;
        ArchiveController controller(nullptr, temp.filePath("library.dat"));
        controller.rememberGeometry = false; controller.hideOnBlur = false;
        ArchiveWindow mainWindow(controller), popup(controller, true);
        TrayCloseFilter closeFilter; mainWindow.installEventFilter(&closeFilter);
        ConnectApplicationExit(controller, application);
        if (mode == "main" || mode == "both") mainWindow.show();
        if (mode == "tray" || mode == "both") { popup.navigate("about"); popup.show(); }
        QTimer::singleShot(100, &controller, &ArchiveController::requestQuit);
        QTimer::singleShot(3000, &application, [] { QCoreApplication::exit(73); });
        return application.exec();
    }
    DesktopLifecycleTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "DesktopLifecycleTests.moc"
