#pragma once
#include <QList>
#include <QStringList>
#include <QHash>

struct PasswordList {
    QString id;
    QString name;
    bool enabled = true;
    QStringList entries;
    QHash<QString, QString> comments;
};

class PasswordLibrary {
public:
    explicit PasswordLibrary(QString path = {});
    bool load(QString& error);
    bool replace(const QList<PasswordList>& lists, QString& error);
    const QList<PasswordList>& lists() const { return lists_; }
    const QString& path() const { return path_; }
    bool readable() const { return readable_; }
    int enabledCount() const;
    const QStringList& enabledPasswords() const { return enabled_; }

    static bool parse(const QString& text, QStringList& entries, QString& error);
    static bool readTextFile(const QString& path, QString& text, QString& error);

private:
    QString path_;
    QList<PasswordList> lists_;
    bool readable_ = true;
    QStringList enabled_;
    int enabledLists_ = 0;
    void rebuildEnabled();
};
