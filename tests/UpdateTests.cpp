#include "ui/UpdateManager.h"
#include "AppBuild.h"
#include <QTest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <cstring>

namespace {
const QString Repository = "example/archive";
const QString Filename = "7ZipContext-1.2.0-windows-x64-" + QString::fromLatin1(appbuild::Toolchain) + "-portable.zip";
QByteArray Release(qint64 size = 7) {
    const QString base = "https://github.com/" + Repository + "/releases/download/v1.2.0/";
    return QJsonDocument(QJsonObject{{"tag_name", "v1.2.0"}, {"body", "Update notes"},
        {"assets", QJsonArray{QJsonObject{{"name", Filename}, {"size", size}, {"browser_download_url", base + Filename}},
            QJsonObject{{"name", "SHA256SUMS.txt"}, {"browser_download_url", base + "SHA256SUMS.txt"}}}}}).toJson();
}
struct Response { QByteArray body; int status = 200; bool finalOnly = false; QUrl redirect; int delay = 0; };
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly | Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        QTimer::singleShot(response.delay, this, [this, response] {
            if (isFinished()) return;
            if (!response.redirect.isEmpty()) emit redirected(response.redirect);
            if (isFinished()) return;
            data_ = response.body;
            if (!response.finalOnly) emit readyRead();
            if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return data_.size() + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* destination, qint64 size) override {
        const auto count = qMin<qint64>(size, data_.size());
        if (!count) return isFinished() ? -1 : 0;
        std::memcpy(destination, data_.constData(), static_cast<size_t>(count)); data_.remove(0, count); return count;
    }
private:
    QByteArray data_;
};
class Network : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QUrl> requests;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        requests.append(request.url());
        return new Reply(request, responses.isEmpty() ? Response{{}, 500} : responses.takeFirst(), this);
    }
};
}
class UpdateTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void validatesVersionNumbers() {
        QVERIFY(UpdateManager::newer("1.10.0", "1.9.99"));
        QVERIFY(!UpdateManager::newer("1.2.0", "1.2.0"));
        for (const auto& bad : {"01.2.0", "1.2", "1.2.0.0", "1.2.0-beta", "256.0.0", "1.0.65536", "1.2.99999999999999999999999"})
            QVERIFY2(!UpdateManager::newer(bad, "1.0.0"), bad);
    }
    void validatesReleaseAssets() {
        UpdateRelease release; QString error;
        QVERIFY(UpdateManager::parseRelease(Release(), Repository, "portable", release, error));
        QCOMPARE(release.filename, Filename); QVERIFY(!release.package.isEmpty());
        auto object = QJsonDocument::fromJson(Release()).object();
        auto assets = object["assets"].toArray(); auto package = assets[0].toObject();
        for (const auto& unsafe : {"http://github.com/example/archive/releases/download/v1.2.0/", "https://github.com/other/archive/releases/download/v1.2.0/", "https://evil.example/"}) {
            package["browser_download_url"] = QString(unsafe) + Filename; assets[0] = package; object["assets"] = assets;
            QVERIFY(UpdateManager::parseRelease(QJsonDocument(object).toJson(), Repository, "portable", release, error));
            QVERIFY(release.package.isEmpty());
        }
        assets.append(assets[0]); object["assets"] = assets;
        QVERIFY(!UpdateManager::parseRelease(QJsonDocument(object).toJson(), Repository, "portable", release, error));
        object = QJsonDocument::fromJson(Release()).object(); object["prerelease"] = true;
        QVERIFY(!UpdateManager::parseRelease(QJsonDocument(object).toJson(), Repository, "portable", release, error));
    }
    void validatesChecksumEntries() {
        QByteArray checksum; const QByteArray hash(64, 'a');
        QVERIFY(UpdateManager::parseChecksum(hash + "  " + Filename.toUtf8() + "\r\n", Filename, checksum));
        QCOMPARE(checksum, hash);
        const auto line = hash + " *" + Filename.toUtf8() + "\n";
        QVERIFY(!UpdateManager::parseChecksum(line + line, Filename, checksum));
        QVERIFY(!UpdateManager::parseChecksum(hash + "  other.zip", Filename, checksum));
    }
    void downloadsAndVerifiesFinalBytes_data() { QTest::addColumn<bool>("finalOnly"); QTest::newRow("ready-read") << false; QTest::newRow("finished-buffer") << true; }
    void selectsOnlyMatchingToolchain_data() {
        QTest::addColumn<QString>("toolchain"); QTest::addColumn<QString>("mode");
        for (const auto& toolchain : {"msvc", "ucrt"}) for (const auto& mode : {"msi", "portable"})
            QTest::newRow(qPrintable(QString(toolchain) + '-' + mode)) << QString(toolchain) << QString(mode);
    }
    void selectsOnlyMatchingToolchain() {
        QFETCH(QString, toolchain); QFETCH(QString, mode);
        const QString base = "https://github.com/" + Repository + "/releases/download/v1.2.0/";
        auto object = QJsonDocument::fromJson(Release()).object();
        QJsonArray assets;
        for (const auto& variant : {QString{}, QString("-msvc"), QString("-ucrt")}) {
            for (const auto& suffix : {".msi", "-portable.zip"}) {
                const auto name = "7ZipContext-1.2.0-windows-x64" + variant + suffix;
                assets.append(QJsonObject{{"name", name}, {"size", 7}, {"browser_download_url", base + name}});
            }
        }
        assets.append(QJsonObject{{"name", "SHA256SUMS.txt"}, {"browser_download_url", base + "SHA256SUMS.txt"}});
        object["assets"] = assets;
        UpdateRelease release; QString error;
        QVERIFY(UpdateManager::parseRelease(QJsonDocument(object).toJson(), Repository, mode, release, error, toolchain));
        const auto expected = "7ZipContext-1.2.0-windows-x64-" + toolchain + (mode == "msi" ? ".msi" : "-portable.zip");
        QCOMPARE(release.filename, expected); QCOMPARE(release.package, QUrl(base + expected));
        QJsonArray missing;
        for (const auto& asset : assets) if (asset.toObject()["name"].toString() != expected) missing.append(asset);
        object["assets"] = missing;
        QVERIFY(UpdateManager::parseRelease(QJsonDocument(object).toJson(), Repository, mode, release, error, toolchain));
        QVERIFY(release.package.isEmpty()); // Never switch compilers or select a legacy alias automatically.
        QVERIFY(!UpdateManager::parseRelease(QJsonDocument(object).toJson(), Repository, mode, release, error, "other"));
    }
    void downloadsAndVerifiesFinalBytes() {
        QFETCH(bool, finalOnly); QTemporaryDir temp; Network network;
        const QByteArray payload("package"); const auto hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
        network.responses = {{Release()}, {hash + "  " + Filename.toUtf8()}, {payload, 200, finalOnly}};
        UpdateManager manager(nullptr, &network, "1.1.0", Repository, temp.path(), temp.path(), "portable");
        manager.check(); QTRY_COMPARE(manager.state(), UpdateManager::State::Available);
        manager.download(); QTRY_COMPARE(manager.state(), UpdateManager::State::Ready);
        QFile file(manager.packagePath()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), payload);
        QCOMPARE(network.requests.size(), 3); QCOMPARE(manager.received(), payload.size());
        QString error; QVERIFY(!manager.install(error)); QVERIFY(!error.isEmpty()); // Incomplete installations cannot launch the helper.
    }
    void rejectsInvalidDownloads_data() {
        QTest::addColumn<QByteArray>("payload"); QTest::addColumn<int>("status"); QTest::addColumn<QUrl>("redirect");
        QTest::newRow("checksum") << QByteArray("damaged") << 200 << QUrl();
        QTest::newRow("oversize") << QByteArray("packageextra") << 200 << QUrl();
        QTest::newRow("short") << QByteArray("pack") << 200 << QUrl();
        QTest::newRow("http-error") << QByteArray("package") << 503 << QUrl();
        QTest::newRow("untrusted-redirect") << QByteArray("package") << 200 << QUrl("https://evil.example/update.zip");
        QTest::newRow("downgrade-redirect") << QByteArray("package") << 200 << QUrl("http://github.com/update.zip");
    }
    void rejectsInvalidDownloads() {
        QFETCH(QByteArray, payload); QFETCH(int, status); QFETCH(QUrl, redirect);
        QTemporaryDir temp; Network network;
        const auto hash = QCryptographicHash::hash("package", QCryptographicHash::Sha256).toHex();
        network.responses = {{Release()}, {hash + "  " + Filename.toUtf8()}, {payload, status, false, redirect}};
        UpdateManager manager(nullptr, &network, "1.1.0", Repository, temp.path(), temp.path(), "portable");
        manager.check(); QTRY_COMPARE(manager.state(), UpdateManager::State::Available);
        manager.download(); QTRY_COMPARE(manager.state(), UpdateManager::State::Error); QVERIFY(manager.packagePath().isEmpty());
        QDir directory(temp.path());
        for (const auto& child : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            QVERIFY(QDir(directory.filePath(child)).entryList(QDir::Files).isEmpty());
    }
    void cancelsAndCanRetry() {
        QTemporaryDir temp; Network network;
        network.responses = {{Release(), 200, false, {}, 100}, {Release()}};
        UpdateManager manager(nullptr, &network, "1.1.0", Repository, temp.path(), temp.path(), "portable");
        manager.check(); manager.cancel(); QCOMPARE(manager.state(), UpdateManager::State::Idle);
        manager.check(); QTRY_COMPARE(manager.state(), UpdateManager::State::Available);
        const auto hash = QCryptographicHash::hash("package", QCryptographicHash::Sha256).toHex();
        network.responses = {{hash + "  " + Filename.toUtf8()}, {"package", 200, false, {}, 200}};
        manager.download(); QTRY_COMPARE(network.requests.size(), 4); manager.cancel();
        QCOMPARE(manager.state(), UpdateManager::State::Available); QVERIFY(manager.packagePath().isEmpty());
        QTest::qWait(220); QCOMPARE(manager.state(), UpdateManager::State::Available);
    }
    void failedInstallRestoresRetryControls_data() {
        QTest::addColumn<QByteArray>("result");
        QTest::newRow("reported-failure") << QByteArray(R"({"success":false,"error":"fixture: parent did not exit"})");
        QTest::newRow("malformed-result") << QByteArray("incomplete json");
        QTest::newRow("silent-exit") << QByteArray();
    }
    void failedInstallRestoresRetryControls() {
        QFETCH(QByteArray, result);
        QTemporaryDir temp; Network network;
        const auto hash = QCryptographicHash::hash("package", QCryptographicHash::Sha256).toHex();
        network.responses = {{Release()}, {hash + "  " + Filename.toUtf8()}, {"package"}};
        QFile helper(temp.filePath("Apply-Update.ps1")); QVERIFY(helper.open(QIODevice::WriteOnly));
        QByteArray script("param($Package,$Sha256,$Version,$TargetDirectory,$Mode,$ParentPid)\n");
        if (!result.isEmpty()) script += "[IO.File]::WriteAllText([IO.Path]::Combine([IO.Path]::GetDirectoryName($Package),'update-result.json'),'" + result + "')\n";
        script += "exit 1\n"; QCOMPARE(helper.write(script), script.size()); helper.close();
        UpdateManager manager(nullptr, &network, "1.1.0", Repository, temp.path(), temp.path(), "portable");
        manager.check(); QTRY_COMPARE(manager.state(), UpdateManager::State::Available);
        manager.download(); QTRY_COMPARE(manager.state(), UpdateManager::State::Ready);
        QString error; QVERIFY2(manager.install(error), qPrintable(error)); QCOMPARE(manager.state(), UpdateManager::State::Installing);
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), UpdateManager::State::Ready, 20000);
        QVERIFY(!manager.busy()); QVERIFY(QFileInfo::exists(manager.packagePath()));
        if (result.contains("parent did not exit")) QVERIFY(manager.status().contains("parent did not exit"));
        QVERIFY2(manager.install(error), qPrintable(error)); QCOMPARE(manager.state(), UpdateManager::State::Installing);
        QTRY_COMPARE_WITH_TIMEOUT(manager.state(), UpdateManager::State::Ready, 20000);
        const QDir work(QFileInfo(manager.packagePath()).absolutePath());
        QCOMPARE(work.entryList({"update-result.previous-*.json"}, QDir::Files).size(), result.isEmpty() ? 0 : 1);
    }
    void noDownloadForCurrentVersionOrIncompleteRelease() {
        QTemporaryDir temp; Network network; network.responses = {{Release()}};
        UpdateManager manager(nullptr, &network, "1.2.0", Repository, temp.path(), temp.path(), "portable");
        manager.check(); QTRY_COMPARE(manager.state(), UpdateManager::State::Idle); manager.download(); QCOMPARE(network.requests.size(), 1);
        Network invalid; invalid.responses = {{Release(0)}};
        UpdateManager incomplete(nullptr, &invalid, "1.1.0", Repository, temp.path(), temp.path(), "portable");
        incomplete.check(); QTRY_COMPARE(incomplete.state(), UpdateManager::State::Error); incomplete.download(); QCOMPARE(invalid.requests.size(), 1);
    }
};
QTEST_MAIN(UpdateTests)
#include "UpdateTests.moc"
