#include "UpdateManager.h"
#include "AppBuild.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>
#include <QVersionNumber>
#include <Windows.h>
#include <msi.h>

namespace {
QString SettingsPath() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/7ZipContext/desktop.ini"; }
bool Version(const QString& version) {
    static const QRegularExpression pattern("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
    if (!pattern.match(version).hasMatch()) return false;
    const auto value = QVersionNumber::fromString(version);
    return value.segmentCount() == 3 && value.toString() == version &&
        value.majorVersion() <= 255 && value.minorVersion() <= 255 && value.microVersion() <= 65535;
}
bool Trusted(const QUrl& url) {
    return url.scheme() == "https" && url.userInfo().isEmpty() && url.port(443) == 443 &&
        QStringList{"github.com", "api.github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"}.contains(url.host());
}
bool Asset(const QUrl& url, const QString& repository, const QString& version, const QString& filename) {
    return Trusted(url) && url.host() == "github.com" && url.query().isEmpty() && url.fragment().isEmpty() &&
        url.path() == '/' + repository + "/releases/download/v" + version + '/' + filename;
}
bool ManagedInstallation(const QString& directory) {
    const auto application = QFileInfo(QDir(directory).filePath("7ZipContextApp.exe")).canonicalFilePath();
    for (DWORD index = 0;; ++index) {
        wchar_t product[39]{};
        const auto status = MsiEnumRelatedProductsW(L"{A1B2C3D4-0000-4B3A-9E1F-000000000001}", 0, index, product);
        if (status != ERROR_SUCCESS) break;
        std::wstring path(32768, L'\0'); DWORD size = static_cast<DWORD>(path.size());
        if (MsiGetComponentPathW(product, L"{A1B2C3D4-3333-4B3A-9E1F-000000000004}", path.data(), &size) == INSTALLSTATE_LOCAL) {
            if (QFileInfo(QString::fromWCharArray(path.data())).canonicalFilePath().compare(application, Qt::CaseInsensitive) == 0) return true;
        }
    }
    return false;
}
}

UpdateManager::UpdateManager(QObject* parent, QNetworkAccessManager* network, QString currentVersion, QString repository,
                             QString applicationDirectory, QString cacheDirectory, QString mode) : QObject(parent) {
    currentVersion_ = currentVersion.isEmpty() ? QString::fromLatin1(appbuild::Version) : currentVersion;
    repository_ = repository.isEmpty() ? QString::fromLatin1(appbuild::UpdateRepository) : repository;
    applicationDirectory_ = applicationDirectory.isEmpty() ? QCoreApplication::applicationDirPath() : applicationDirectory;
    cacheDirectory_ = cacheDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/7ZipContext/updates" : cacheDirectory;
    mode_ = mode.isEmpty() ? (ManagedInstallation(applicationDirectory_) ? "msi" : "portable") : mode;
    network_ = network ? network : new QNetworkAccessManager(this);
    QSettings settings(SettingsPath(), QSettings::IniFormat); autoCheck_ = settings.value("Updates/AutoCheck", true).toBool();
    const auto previous = settings.value("Updates/ResultFile").toString();
    if (QDir::cleanPath(previous).startsWith(QDir::cleanPath(cacheDirectory_) + '/')) {
        QFile file(previous);
        if (file.open(QIODevice::ReadOnly) && file.size() < 65536) {
            const auto result = QJsonDocument::fromJson(file.readAll()).object();
            if (result.value("success").toBool()) status_ = QStringLiteral("更新完成 · 当前版本 %1").arg(currentVersion_);
            else if (result.contains("error")) { state_ = State::Error; status_ = QStringLiteral("上次更新未完成：%1").arg(result.value("error").toString()); }
        }
    }
}
UpdateManager::~UpdateManager() { if (reply_) { reply_->disconnect(this); reply_->abort(); } }
void UpdateManager::setAutoCheck(bool enabled) { autoCheck_ = enabled; QSettings(SettingsPath(), QSettings::IniFormat).setValue("Updates/AutoCheck", enabled); emit changed(); }
void UpdateManager::setState(State state, const QString& text) { state_ = state; status_ = text; emit changed(); }
bool UpdateManager::newer(const QString& candidate, const QString& current) { return Version(candidate) && Version(current) && QVersionNumber::fromString(candidate) > QVersionNumber::fromString(current); }
bool UpdateManager::parseRelease(const QByteArray& data, const QString& repository, const QString& mode, UpdateRelease& release, QString& error) {
    release = {}; error.clear();
    const auto object = QJsonDocument::fromJson(data).object();
    const auto tag = object.value("tag_name").toString();
    if (!tag.startsWith('v') || !Version(tag.mid(1)) || object.value("draft").toBool() || object.value("prerelease").toBool()) { error = QStringLiteral("更新服务未返回有效的正式版本"); return false; }
    release.version = tag.mid(1); release.notes = object.value("body").toString().left(8000);
    release.page = QUrl("https://github.com/" + repository + "/releases/tag/" + tag);
    release.filename = "7ZipContext-" + release.version + "-windows-x64" + (mode == "msi" ? ".msi" : "-portable.zip");
    int packages = 0, checksums = 0;
    for (const auto& value : object.value("assets").toArray()) {
        const auto asset = value.toObject(); const auto name = asset.value("name").toString(); const QUrl url(asset.value("browser_download_url").toString());
        if (name == release.filename) { ++packages; if (Asset(url, repository, release.version, name)) { release.package = url; release.size = asset.value("size").toInteger(); } }
        if (name == "SHA256SUMS.txt") { ++checksums; if (Asset(url, repository, release.version, name)) release.checksums = url; }
    }
    if (packages > 1 || checksums > 1) { error = QStringLiteral("更新附件重复，已停止更新"); return false; }
    return true;
}
bool UpdateManager::parseChecksum(const QByteArray& data, const QString& filename, QByteArray& checksum) {
    checksum.clear(); int matches = 0;
    QString text = QString::fromUtf8(data); if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    static const QRegularExpression line("^([a-fA-F0-9]{64})[ \\t]+\\*?(.+)$");
    for (auto value : text.split('\n')) {
        if (value.endsWith('\r')) value.chop(1);
        const auto match = line.match(value);
        if (match.hasMatch() && match.captured(2) == filename) { ++matches; checksum = match.captured(1).toLatin1().toLower(); }
    }
    return matches == 1;
}
void UpdateManager::configureReply(QNetworkReply* reply) {
    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl& redirect) {
        if (!Trusted(redirect)) { fail(QStringLiteral("更新下载跳转到不受支持的地址，已停止")); return; }
        emit reply->redirectAllowed();
    });
}
void UpdateManager::fail(const QString& message) {
    if (reply_) { auto* reply = reply_.data(); reply_ = nullptr; reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    if (file_) { file_->cancelWriting(); file_.reset(); } hash_.reset();
    setState(State::Error, message);
}
void UpdateManager::fetch(const QUrl& url, qint64 limit, std::function<void(const QByteArray&)> finished) {
    if (!Trusted(url)) { fail(QStringLiteral("更新地址无效")); return; }
    QNetworkRequest request(url); request.setRawHeader("User-Agent", "7ZipContext/" + currentVersion_.toLatin1());
    request.setRawHeader("Accept", "application/vnd.github+json"); request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    auto* reply = network_->get(request); reply_ = reply; configureReply(reply);
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [this, reply, data, limit] {
        if (reply_ != reply) return;
        data->append(reply->readAll()); if (data->size() > limit) fail(QStringLiteral("更新服务返回的数据过大"));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, limit, finished = std::move(finished)] {
        if (reply_ != reply) return;
        data->append(reply->readAll());
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200 || data->size() > limit) { fail(QStringLiteral("无法读取更新信息（HTTP %1），请稍后重试").arg(status)); return; }
        reply_ = nullptr; reply->deleteLater(); finished(*data);
    });
}
void UpdateManager::check() {
    if (busy()) return;
    QSettings(SettingsPath(), QSettings::IniFormat).remove("Updates/ResultFile");
    release_ = {}; packagePath_.clear(); setState(State::Checking, QStringLiteral("正在检查更新…"));
    fetch(QUrl("https://api.github.com/repos/" + repository_ + "/releases/latest"), 2 * 1024 * 1024, [this](const QByteArray& data) {
        QString error;
        if (!parseRelease(data, repository_, mode_, release_, error)) { fail(error); return; }
        if (!newer(release_.version, currentVersion_)) { setState(State::Idle, QStringLiteral("当前已是最新版本 · %1").arg(currentVersion_)); return; }
        if (release_.package.isEmpty() || release_.checksums.isEmpty() || release_.size <= 0 || release_.size > 512LL * 1024 * 1024) { fail(QStringLiteral("新版本 %1 缺少可校验的更新包").arg(release_.version)); return; }
        setState(State::Available, QStringLiteral("发现新版本 %1").arg(release_.version));
    });
}
void UpdateManager::download() {
    if (busy() || !newer(release_.version, currentVersion_) || release_.package.isEmpty() || release_.checksums.isEmpty() ||
        release_.size <= 0 || release_.size > 512LL * 1024 * 1024) return;
    received_ = 0; packagePath_.clear(); setState(State::Downloading, QStringLiteral("正在获取更新校验值…"));
    fetch(release_.checksums, 1024 * 1024, [this](const QByteArray& data) {
        if (!parseChecksum(data, release_.filename, checksum_)) { fail(QStringLiteral("更新校验清单无效")); return; }
        startPackageDownload();
    });
}
void UpdateManager::startPackageDownload() {
    workDirectory_ = QDir(cacheDirectory_).filePath(release_.version + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(workDirectory_)) { fail(QStringLiteral("无法创建更新下载目录")); return; }
    file_ = std::make_unique<QSaveFile>(QDir(workDirectory_).filePath(release_.filename));
    if (!file_->open(QIODevice::WriteOnly)) { fail(QStringLiteral("无法保存更新包")); return; }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    QNetworkRequest request(release_.package); request.setTransferTimeout(60000);
    request.setRawHeader("User-Agent", "7ZipContext/" + currentVersion_.toLatin1());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    auto* reply = network_->get(request); reply_ = reply; configureReply(reply);
    const auto consume = [this, reply] {
        if (reply_ != reply || !file_) return;
        const auto bytes = reply->readAll(); received_ += bytes.size();
        if (received_ > release_.size || file_->write(bytes) != bytes.size()) { fail(QStringLiteral("更新包大小异常或写入失败")); return; }
        hash_->addData(bytes); status_ = QStringLiteral("正在下载更新 · %1%").arg(release_.size ? received_ * 100 / release_.size : 0); emit changed();
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply, consume] {
        consume();
        if (reply_ != reply || !file_) return;
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
            received_ != release_.size || hash_->result().toHex() != checksum_) { fail(QStringLiteral("更新包未完整下载或 SHA-256 校验失败，请重新下载")); return; }
        if (!file_->commit()) { fail(QStringLiteral("无法保存已校验的更新包")); return; }
        packagePath_ = file_->fileName(); file_.reset(); hash_.reset(); reply_ = nullptr; reply->deleteLater();
        setState(State::Ready, QStringLiteral("版本 %1 已下载并通过校验，可以安装").arg(release_.version));
    });
}
void UpdateManager::cancel() {
    if (state_ != State::Checking && state_ != State::Downloading) return;
    if (reply_) { auto* reply = reply_.data(); reply_ = nullptr; reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    if (file_) { file_->cancelWriting(); file_.reset(); } hash_.reset();
    setState(release_.package.isEmpty() ? State::Idle : State::Available, QStringLiteral("已取消更新下载"));
}
bool UpdateManager::install(QString& error) {
    error.clear();
    if (state_ != State::Ready || packagePath_.isEmpty()) { error = QStringLiteral("请先下载并校验更新"); return false; }
    const auto reject = [&](const QString& message) { error = message; setState(State::Ready, message); return false; };
    if (mode_ == "portable") {
        QTemporaryFile writable(QDir(applicationDirectory_).filePath(".7zipcontext-write-XXXXXX"));
        if (!writable.open()) return reject(QStringLiteral("程序目录不可写，请将便携版放在可写目录后更新"));
    }
    const auto helper = QDir(workDirectory_).filePath("Apply-Update.ps1");
    QFile source(QDir(applicationDirectory_).filePath("Apply-Update.ps1"));
    QSaveFile destination(helper);
    if (!source.open(QIODevice::ReadOnly) || source.size() > 1024 * 1024 || !destination.open(QIODevice::WriteOnly) ||
        destination.write(source.readAll()) != source.size() || !destination.commit()) {
        return reject(QStringLiteral("无法准备更新组件，请使用完整发行包"));
    }
    wchar_t system[MAX_PATH]{}; if (!GetSystemDirectoryW(system, MAX_PATH)) return reject(QStringLiteral("无法定位系统更新工具"));
    const auto powershell = QDir(QString::fromWCharArray(system)).filePath("WindowsPowerShell/v1.0/powershell.exe");
    const QStringList arguments{"-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", helper,
        "-Package", packagePath_, "-Sha256", QString::fromLatin1(checksum_), "-Version", release_.version, "-TargetDirectory", applicationDirectory_,
        "-Mode", mode_, "-ParentPid", QString::number(QCoreApplication::applicationPid())};
    QProcess process; process.setProgram(powershell); process.setArguments(arguments); process.setWorkingDirectory(workDirectory_);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
    if (!process.startDetached()) return reject(QStringLiteral("无法启动更新组件"));
    QSettings(SettingsPath(), QSettings::IniFormat).setValue("Updates/ResultFile", QDir(workDirectory_).filePath("update-result.json"));
    setState(State::Installing, QStringLiteral("正在安装更新，程序将重新启动…")); return true;
}
