#pragma once
#include <QObject>
#include <QUrl>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QCryptographicHash>
#include <functional>
#include <memory>

struct UpdateRelease {
    QString version, filename, notes;
    QUrl page, package, checksums;
    qint64 size = 0;
};

class UpdateManager final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, Available, Downloading, Ready, Installing, Error };
    explicit UpdateManager(QObject* parent = nullptr, QNetworkAccessManager* network = nullptr,
                           QString currentVersion = {}, QString repository = {}, QString applicationDirectory = {},
                           QString cacheDirectory = {}, QString mode = {});
    ~UpdateManager() override;
    State state() const { return state_; }
    QString status() const { return status_; }
    QString currentVersion() const { return currentVersion_; }
    const UpdateRelease& release() const { return release_; }
    QString packagePath() const { return packagePath_; }
    bool autoCheck() const { return autoCheck_; }
    bool busy() const { return state_ == State::Checking || state_ == State::Downloading || state_ == State::Installing; }
    qint64 received() const { return received_; }
    void setAutoCheck(bool enabled);
    void check();
    void download();
    void cancel();
    bool install(QString& error);
    static bool parseRelease(const QByteArray& data, const QString& repository, const QString& mode, UpdateRelease& release, QString& error, const QString& toolchain = {});
    static bool parseChecksum(const QByteArray& data, const QString& filename, QByteArray& checksum);
    static bool newer(const QString& candidate, const QString& current);
signals:
    void changed();
private:
    void setState(State state, const QString& status);
    void fail(const QString& message);
    void fetch(const QUrl& url, qint64 limit, std::function<void(const QByteArray&)> finished);
    void startPackageDownload();
    void configureReply(class QNetworkReply* reply);
    void pollInstallation();
    void stopInstallationMonitor();
    QString currentVersion_, repository_, applicationDirectory_, cacheDirectory_, mode_;
    QString status_ = QStringLiteral("尚未检查更新"), packagePath_, workDirectory_;
    bool autoCheck_ = true;
    State state_ = State::Idle;
    UpdateRelease release_;
    QNetworkAccessManager* network_ = nullptr;
    QPointer<class QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> file_;
    std::unique_ptr<QCryptographicHash> hash_;
    QByteArray checksum_;
    qint64 received_ = 0;
    QTimer installMonitor_;
    QString installResultPath_;
    qint64 helperPid_ = 0;
    void* helperProcessHandle_ = nullptr;
};
