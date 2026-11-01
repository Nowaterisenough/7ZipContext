#include "ui/ArchiveWindow.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

// Use Windows' native font integration without opening or activating a desktop window.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() < 2 || QGuiApplication::platformName() != "windows") return 1;
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("7ZipContextTests"); app.setApplicationName("7ZipContextTests");
    ApplyArchiveTheme(app);
    const QString output = app.arguments()[1];
    if (!QDir().mkpath(output)) return 2;
    QTemporaryDir temporary;
    QFile source(temporary.filePath(QStringLiteral("工作资料.txt")));
    if (!source.open(QIODevice::WriteOnly)) return 3;
    source.write(QByteArray(200000, 'x')); source.close();
    ArchiveController controller(nullptr, temporary.filePath("password-lists.dat"));
    const QHash<QString, QString> notes{{"preview-example-one", QStringLiteral("示例：下载分卷的解压密码")}, {"preview-example-two", QStringLiteral("示例：备用密码")}};
    if (!controller.addPasswordList(QStringLiteral("常用密码"), "preview-example-one\npreview-example-two", &notes)) return 8;
    const QHash<QString, QString> extraNotes{{"preview-example-one", QStringLiteral("同一密码的另一份来源")}};
    if (!controller.addPasswordList(QStringLiteral("工作资料"), "preview-example-one\npreview-example-three", &extraNotes)) return 8;
    controller.rememberGeometry = false;
    controller.addPaths({source.fileName()}, true);
    controller.archiveName = QStringLiteral("工作资料备份");
    controller.compression.format = L"zip";
    if (!controller.start(ArchiveController::Operation::Compress)) return 4;
    QElapsedTimer timeout; timeout.start();
    while (controller.busy() && timeout.elapsed() < 20000) { app.processEvents(); QThread::msleep(5); }
    if (controller.busy() || controller.tasks.isEmpty() || !controller.tasks.last().success) return 5;
    controller.addPaths({temporary.filePath(QStringLiteral("工作资料备份.zip"))}, false);
    ArchiveWindow main(controller), popup(controller, true);
    QJsonArray captures;
    auto capture = [&](ArchiveWindow& window, const QString& page, QSize size, const QString& name) {
        window.resize(size); window.navigate(page); window.ensurePolished();
        QEventLoop settle; QTimer::singleShot(320, &settle, &QEventLoop::quit); settle.exec();
        const auto picture = window.grab();
        if (!picture.save(output + '/' + name + ".png")) return false;
        if (!window.compact() && page == "general" &&
            !window.grab(QRect(0, 0, window.width(), 420)).save(output + '/' + name + "-detail.png")) return false;
        captures.append(QJsonObject{{"file", name + ".png"}, {"logicalWidth", window.width()}, {"logicalHeight", window.height()},
            {"pixelWidth", picture.width()}, {"pixelHeight", picture.height()}, {"devicePixelRatio", picture.devicePixelRatio()}});
        return true;
    };
    for (const auto& page : {"extract", "compress", "tasks", "general", "passwords", "about"}) {
        const QString id = QString::fromLatin1(page);
        if (!capture(main, id, {1100, 820}, "main-" + id)) return 6;
        if (!capture(popup, id, {480, 700}, "tray-" + id)) return 6;
    }
    if (!capture(main, "general", {1412, 1002}, "reference-size-settings")) return 6;
    if (!capture(main, "compress", {820, 620}, "main-small")) return 6;
    QFile metadata(output + "/render-info.json");
    if (!metadata.open(QIODevice::WriteOnly)) return 7;
    metadata.write(QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()}, {"qt", qVersion()},
        {"resolvedFont", QFontInfo(app.font()).family()}, {"logicalDpi", app.primaryScreen()->logicalDotsPerInch()}, {"captures", captures}}).toJson());
    if (qEnvironmentVariableIsSet("ARCHIVE_CHECK_UPDATES")) {
        auto& updates = controller.updates(); updates.check(); timeout.restart();
        while (updates.busy() && timeout.elapsed() < 45000) { app.processEvents(); QThread::msleep(5); }
        const bool timedOut = updates.busy();
        if (timedOut) updates.cancel();
        QFile report(output + "/online-check.json"); if (!report.open(QIODevice::WriteOnly)) return 9;
        report.write(QJsonDocument(QJsonObject{{"currentVersion", updates.currentVersion()}, {"latestVersion", updates.release().version},
            {"status", updates.status()}, {"timedOut", timedOut}, {"state", static_cast<int>(updates.state())},
            {"packageAvailable", !updates.release().package.isEmpty()}, {"checksumsAvailable", !updates.release().checksums.isEmpty()}}).toJson());
        if (timedOut || updates.state() == UpdateManager::State::Error) return 10;
    }
    return 0;
}
