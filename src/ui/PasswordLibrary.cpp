#include "PasswordLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <Windows.h>
#include <wincrypt.h>
#include <algorithm>

namespace {
constexpr qint64 MaxFileBytes = 8 * 1024 * 1024;
constexpr qint64 MaxStoreBytes = 32 * 1024 * 1024;
constexpr qsizetype MaxPasswords = 100000;
const QByteArray Magic("7ZCPWD01");

bool valid(const QList<PasswordList>& lists, QString& error) {
    QSet<QString> ids;
    qsizetype count = 0;
    for (const auto& list : lists) {
        if (list.id.isEmpty() || ids.contains(list.id) || list.name.trimmed().isEmpty() || list.name.size() > 120 || list.entries.isEmpty()) {
            error = QStringLiteral("密码单名称或内容无效"); return false;
        }
        ids.insert(list.id); count += list.entries.size();
        for (const auto& entry : list.entries) {
            if (entry.isEmpty() || entry.size() > 1024 || entry.contains(QChar::Null) || entry.contains('\n') || entry.contains('\r')) {
                error = QStringLiteral("密码单包含无效的密码行"); return false;
            }
        }
        const QSet<QString> values(list.entries.cbegin(), list.entries.cend());
        for (auto note = list.comments.cbegin(); note != list.comments.cend(); ++note) {
            if (!values.contains(note.key()) || note.value().size() > 2048 || note.value().contains(QChar::Null)) {
                error = QStringLiteral("密码注释无效，或超过 2048 个字符"); return false;
            }
        }
    }
    if (count > MaxPasswords) { error = QStringLiteral("密码单合计最多保存 100000 个密码"); return false; }
    return true;
}
}

PasswordLibrary::PasswordLibrary(QString path) : path_(std::move(path)) {
    if (path_.isEmpty()) path_ = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/7ZipContext/password-lists.dat";
}

bool PasswordLibrary::load(QString& error) {
    error.clear();
    QFile file(path_);
    if (!file.exists()) { lists_.clear(); rebuildEnabled(); readable_ = true; return true; }
    readable_ = false;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxStoreBytes) {
        error = QStringLiteral("无法读取本机密码单，原文件未更改"); return false;
    }
    auto stored = file.readAll();
    if (file.error() != QFileDevice::NoError || stored.size() > MaxStoreBytes) {
        error = QStringLiteral("无法完整读取本机密码单，原文件未更改"); return false;
    }
    if (!stored.startsWith(Magic)) { error = QStringLiteral("本机密码单文件格式无效，原文件未更改"); return false; }
    auto encrypted = stored.mid(Magic.size());
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE*>(encrypted.data())}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = QStringLiteral("无法解锁本机密码单，请使用保存它的 Windows 账户"); return false;
    }
    QByteArray plaintext(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(plaintext, &parseError);
    SecureZeroMemory(plaintext.data(), static_cast<SIZE_T>(plaintext.size()));
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || document.object().value("version").toInt() != 1 ||
        !document.object().value("lists").isArray()) {
        error = QStringLiteral("本机密码单内容损坏，原文件未更改"); return false;
    }
    QList<PasswordList> loaded;
    for (const auto& value : document.object().value("lists").toArray()) {
        if (!value.isObject()) { error = QStringLiteral("本机密码单内容损坏"); return false; }
        const auto object = value.toObject();
        if (!object.value("enabled").isBool() || !object.value("entries").isArray()) {
            error = QStringLiteral("本机密码单内容损坏"); return false;
        }
        PasswordList list{object.value("id").toString(), object.value("name").toString(), object.value("enabled").toBool(), {}};
        for (const auto& entry : object.value("entries").toArray()) {
            if (!entry.isString()) { error = QStringLiteral("本机密码单内容损坏"); return false; }
            list.entries.append(entry.toString());
        }
        if (object.contains("comments") && !object.value("comments").isObject()) { error = QStringLiteral("密码注释格式无效"); return false; }
        const auto comments = object.value("comments").toObject();
        for (auto note = comments.constBegin(); note != comments.constEnd(); ++note) {
            if (!note.value().isString()) { error = QStringLiteral("密码注释格式无效"); return false; }
            list.comments.insert(note.key(), note.value().toString());
        }
        loaded.append(std::move(list));
    }
    if (!valid(loaded, error)) return false;
    lists_ = std::move(loaded); rebuildEnabled(); readable_ = true; return true;
}

bool PasswordLibrary::replace(const QList<PasswordList>& lists, QString& error) {
    error.clear();
    if (!readable_) { error = QStringLiteral("现有密码单无法读取，请先备份并移开原文件后重启应用"); return false; }
    if (!valid(lists, error)) return false;
    QJsonArray values;
    for (const auto& list : lists) {
        QJsonArray entries;
        for (const auto& entry : list.entries) entries.append(entry);
        QJsonObject comments;
        for (auto note = list.comments.cbegin(); note != list.comments.cend(); ++note) comments.insert(note.key(), note.value());
        values.append(QJsonObject{{"id", list.id}, {"name", list.name}, {"enabled", list.enabled}, {"entries", entries}, {"comments", comments}});
    }
    auto plaintext = QJsonDocument(QJsonObject{{"version", 1}, {"lists", values}}).toJson(QJsonDocument::Compact);
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()), reinterpret_cast<BYTE*>(plaintext.data())}, output{};
    const BOOL protectedData = CryptProtectData(&input, L"7-Zip Context password lists", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(plaintext.data(), static_cast<SIZE_T>(plaintext.size()));
    if (!protectedData) { error = QStringLiteral("Windows 无法加密保存密码单"); return false; }
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    if (Magic.size() + encrypted.size() > MaxStoreBytes) {
        error = QStringLiteral("密码单总大小超过 32 MB，请减少密码单内容"); return false;
    }
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) { error = QStringLiteral("无法创建密码单保存目录"); return false; }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly) || file.write(Magic) != Magic.size() || file.write(encrypted) != encrypted.size() || !file.commit()) {
        error = QStringLiteral("密码单保存失败，原有密码单未更改"); return false;
    }
    lists_ = lists; rebuildEnabled(); return true;
}

bool PasswordLibrary::parse(const QString& text, QStringList& entries, QString& error) {
    entries.clear(); error.clear();
    auto normalized = text;
    if (normalized.startsWith(QChar::ByteOrderMark)) normalized.remove(0, 1);
    normalized.replace("\r\n", "\n").replace('\r', '\n');
    QSet<QString> seen;
    for (const auto& line : normalized.split('\n')) {
        if (line.isEmpty()) continue;
        if (line.size() > 1024 || line.contains(QChar::Null)) {
            error = QStringLiteral("密码行过长或包含空字符；单个密码最多 1024 个字符"); return false;
        }
        if (!seen.contains(line)) { seen.insert(line); entries.append(line); }
        if (entries.size() > MaxPasswords) { error = QStringLiteral("一个密码单最多导入 100000 个密码"); return false; }
    }
    if (entries.isEmpty()) { error = QStringLiteral("密码单为空，请每行填写一个密码"); return false; }
    return true;
}

bool PasswordLibrary::readTextFile(const QString& path, QString& text, QString& error) {
    QFile file(path); error.clear(); text.clear();
    if (!file.open(QIODevice::ReadOnly)) { error = QStringLiteral("无法读取密码单文件"); return false; }
    if (file.size() > MaxFileBytes) { error = QStringLiteral("密码单文件不能超过 8 MB"); return false; }
    const auto data = file.readAll();
    if (file.error() != QFileDevice::NoError || data.size() > MaxFileBytes) {
        error = QStringLiteral("无法完整读取密码单文件，或文件超过 8 MB"); return false;
    }
    const auto detected = QStringConverter::encodingForData(data);
    QStringDecoder decoder(detected.value_or(QStringConverter::Utf8), QStringConverter::Flag::Stateless);
    text = decoder(data);
    if (!decoder.hasError()) return true;
    if (detected && *detected != QStringConverter::Utf8) { error = QStringLiteral("密码单的 Unicode 编码无效"); return false; }
    // Common Chinese password lists are saved as GBK/GB18030 rather than UTF-8.
    const int count = MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, data.constData(), static_cast<int>(data.size()), nullptr, 0);
    if (count <= 0) { error = QStringLiteral("无法识别密码单编码，请保存为 UTF-8 或 UTF-16"); return false; }
    std::wstring decoded(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, data.constData(), static_cast<int>(data.size()), decoded.data(), count)) {
        error = QStringLiteral("无法读取密码单编码"); return false;
    }
    text = QString::fromStdWString(decoded); return true;
}

int PasswordLibrary::enabledCount() const {
    return enabledLists_;
}
void PasswordLibrary::rebuildEnabled() {
    enabled_.clear(); unique_.clear(); enabledLists_ = 0;
    QSet<QString> enabledSeen;
    QHash<QString, qsizetype> positions;
    for (const auto& list : lists_) {
        if (list.enabled) ++enabledLists_;
        QSet<QString> listSeen;
        for (const auto& password : list.entries) {
            if (listSeen.contains(password)) continue;
            listSeen.insert(password);
            auto position = positions.constFind(password);
            if (position == positions.cend()) {
                const auto index = unique_.size(); positions.insert(password, index);
                unique_.append({password, {}, false}); position = positions.constFind(password);
            }
            auto& entry = unique_[position.value()];
            entry.sources.append({list.id, list.name, list.comments.value(password), list.enabled});
            entry.enabled |= list.enabled;
            if (list.enabled && !enabledSeen.contains(password)) { enabledSeen.insert(password); enabled_.append(password); }
        }
    }
}
