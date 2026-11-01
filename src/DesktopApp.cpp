#include "ui/ArchiveWindow.h"
#include "ui/ApplicationExit.h"
#include "AppBuild.h"
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("7ZipContext"));
    app.setApplicationVersion(QString::fromLatin1(appbuild::Version));
    app.setOrganizationName(QStringLiteral("7ZipContext"));
    app.setQuitOnLastWindowClosed(false);
    ApplyArchiveTheme(app);
    app.setWindowIcon(ArchiveIcon());
    const QString serverName = QStringLiteral("7ZipContext.Desktop.Qt.") + qEnvironmentVariable("USERNAME");
    const QStringList arguments = app.arguments().mid(1);
    if (arguments.contains(QStringLiteral("--import-password-list"))) {
        QString input, reportPath;
        for (int i = 0; i < arguments.size(); ++i) {
            const auto option = arguments[i];
            if ((option != "--import-password-list" && option != "--report") || ++i >= arguments.size()) return 2;
            (option == "--report" ? reportPath : input) = arguments[i];
        }
        if (!QDir::isAbsolutePath(input) || !QDir::isAbsolutePath(reportPath)) return 2;
        QFile report(reportPath); if (!report.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 2;
        ArchiveController controller; const bool imported = controller.importPasswordList(input);
        report.write(QJsonDocument(QJsonObject{{"success", imported}, {"status", controller.status()},
            {"enabledLists", controller.passwordLibrary().enabledCount()}, {"uniquePasswords", controller.passwordLibrary().enabledPasswords().size()}}).toJson());
        return imported ? 0 : 1;
    }
    if (arguments.contains(QStringLiteral("--extract-auto"))) {
        QString output, reportPath, passwordList; QStringList paths; bool keepLayout = false;
        for (int i = 0; i < arguments.size(); ++i) {
            const auto& argument = arguments[i];
            if (argument == "--extract-auto") continue;
            if (argument == "--keep-layout") { keepLayout = true; continue; }
            if (argument == "--output" || argument == "--report" || argument == "--password-list") {
                if (++i >= arguments.size()) return 2;
                if (argument == "--password-list") passwordList = arguments[i];
                else (argument == "--output" ? output : reportPath) = arguments[i];
            } else if (argument.startsWith("--")) return 2;
            else paths.append(argument);
        }
        if (paths.isEmpty() || !QDir::isAbsolutePath(output) || !QDir::isAbsolutePath(reportPath)) return 2;
        if (!passwordList.isEmpty() && !QDir::isAbsolutePath(passwordList)) return 2;
        QFile report(reportPath);
        if (!report.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 2;
        ArchiveController controller;
        controller.autoNested = true; controller.autoPasswords = true; controller.extractDestination = output;
        controller.organizeMedia = !keepLayout;
        auto record = [&] {
            QJsonArray tasks, videos, images;
            for (const auto& task : controller.tasks) tasks.append(QJsonObject{{"name", task.name}, {"detail", task.detail}, {"output", task.output}, {"success", task.success}});
            for (const auto& path : controller.videoFiles) videos.append(path);
            for (const auto& path : controller.imageFiles) images.append(path);
            const auto bytes = QJsonDocument(QJsonObject{{"busy", controller.busy()}, {"status", controller.status()},
                {"layer", static_cast<int>(controller.extractionDepth())}, {"job", static_cast<int>(controller.jobIndex())},
                {"jobs", static_cast<int>(controller.jobCount())}, {"completed", static_cast<qint64>(controller.completed())},
                {"total", static_cast<qint64>(controller.total())}, {"tasks", tasks}, {"videos", videos}, {"images", images},
                {"inputFiles", controller.discoveredInputFiles.size()}, {"archiveGroups", static_cast<int>(controller.archives.size())},
                {"inputs", QJsonArray::fromStringList(paths)}, {"intake", "automatic-extraction"},
                {"mediaGroups", controller.mediaInputs.size()}, {"ignoredInputFiles", QJsonArray::fromStringList(controller.ignoredInputFiles)},
                {"associatedInputFiles", QJsonArray::fromStringList(controller.associatedInputFiles)},
                {"organizedMedia", controller.organizeMedia}, {"cleanedOtherFiles", controller.cleanedOtherFiles},
                {"cleanedOtherBytes", static_cast<qint64>(controller.cleanedOtherBytes)},
                {"cleanedIntermediateFiles", controller.cleanedIntermediateFiles},
                {"cleanedIntermediateBytes", static_cast<qint64>(controller.cleanedIntermediateBytes)}}).toJson();
            report.seek(0); report.resize(0); report.write(bytes); report.flush();
        };
        QObject::connect(&controller, &ArchiveController::progressChanged, &app, record);
        QObject::connect(&controller, &ArchiveController::busyChanged, &app, [&] {
            record();
            if (!controller.busy()) {
                bool success = !controller.tasks.isEmpty();
                for (const auto& task : controller.tasks) success &= task.success;
                app.exit(success ? 0 : 1);
            }
        });
        if (!passwordList.isEmpty() && !controller.importPasswordList(passwordList)) { record(); return 1; }
        QTimer::singleShot(0, &app, [&] { controller.extractPaths(paths); });
        return app.exec();
    }
    if (arguments.contains(QStringLiteral("--smoke-test"))) {
        QStandardPaths::setTestModeEnabled(true);
        ArchiveController controller;
        controller.rememberGeometry = false;
        ArchiveWindow mainWindow(controller), popup(controller, true);
        for (const auto& page : {"extract", "compress", "tasks", "general", "about", "passwords"}) {
            mainWindow.navigate(QString::fromLatin1(page));
            popup.navigate(QString::fromLatin1(page));
        }
        return mainWindow.grab().isNull() || popup.grab().isNull() ? 1 : 0;
    }
    QLocalSocket existing;
    existing.connectToServer(serverName);
    if (existing.waitForConnected(300)) {
        QJsonArray array;
        for (const auto& argument : arguments) array.append(argument);
        existing.write(QJsonDocument(array).toJson(QJsonDocument::Compact) + '\n');
        existing.waitForBytesWritten(3000);
        return 0;
    }
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(runtime);
    QLockFile lock(runtime + "/desktop.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(2000)) {
        QMessageBox::information(nullptr, QStringLiteral("7-Zip Context"), QStringLiteral("程序正在启动或暂时无响应，请稍后再试。"));
        return 1;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(serverName)) return 1;

    ArchiveController controller;
    ArchiveWindow mainWindow(controller), popup(controller, true);
    QSystemTrayIcon tray(app.windowIcon());
    tray.setToolTip(QStringLiteral("7-Zip Context · 单击显示或收起"));
    QMenu menu;
    auto* show = menu.addAction(QStringLiteral("打开主窗口"));
    menu.addSeparator();
    auto* quit = menu.addAction(QStringLiteral("退出"));
    tray.setContextMenu(&menu);
    auto showMain = [&] {
        popup.hide(); mainWindow.showNormal(); mainWindow.raise(); mainWindow.activateWindow();
    };
    QObject::connect(show, &QAction::triggered, &mainWindow, showMain);
    QObject::connect(quit, &QAction::triggered, &controller, &ArchiveController::requestQuit);
    ConnectApplicationExit(controller, app);
    QObject::connect(&popup, &ArchiveWindow::expandRequested, &mainWindow, [&](const QString& page) {
        mainWindow.navigate(page); showMain();
    });
    QObject::connect(&mainWindow, &ArchiveWindow::quickPanelRequested, &popup, [&] {
        popup.navigate(mainWindow.currentPage()); popup.toggleAtTray(tray.geometry());
    });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &popup, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) popup.toggleAtTray(tray.geometry());
    });
    auto dispatch = [&](const QStringList& args) {
        bool compression = false, trayOnly = false, trayPopup = false, directlyHere = false;
        QStringList paths;
        for (const auto& argument : args) {
            if (argument == "--compress") { compression = true; controller.compression.format = L"7z"; }
            else if (argument == "--zip") { compression = true; controller.compression.format = L"zip"; }
            else if (argument == "--extract") { controller.subfolder = true; }
            else if (argument == "--here") { directlyHere = true; controller.subfolder = false; controller.extractDestination.clear(); }
            else if (argument == "--tray") trayOnly = true;
            else if (argument == "--popup") trayPopup = true;
            else paths.append(argument);
        }
        controller.notifyOptions();
        if (!paths.isEmpty()) {
            if (directlyHere) {
                const QFileInfo first(paths.first());
                controller.extractDestination = first.isDir() ? first.absoluteFilePath() : first.absolutePath();
            }
            controller.queuePaths(paths, compression);
            mainWindow.navigate(compression ? QStringLiteral("compress") : QStringLiteral("extract"));
            popup.navigate(mainWindow.currentPage());
        }
        if (trayPopup) popup.toggleAtTray(tray.geometry());
        else if (!trayOnly || !QSystemTrayIcon::isSystemTrayAvailable()) showMain();
    };
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll();
                if (buffer->size() > 1024 * 1024) { socket->disconnectFromServer(); return; }
                if (!buffer->contains('\n')) return;
                const auto document = QJsonDocument::fromJson(buffer->left(buffer->indexOf('\n')));
                if (document.isArray()) {
                    QStringList args;
                    for (const auto& value : document.array()) if (value.isString()) args.append(value.toString());
                    dispatch(args);
                }
                socket->disconnectFromServer();
            });
        }
    });
    tray.show();
    dispatch(arguments);
    QTimer::singleShot(4000, &controller.updates(), [&controller] {
        auto& updates = controller.updates();
        if (updates.autoCheck() && updates.state() == UpdateManager::State::Idle) updates.check();
    });
    const int result = app.exec();
    tray.hide();
    return result;
}
