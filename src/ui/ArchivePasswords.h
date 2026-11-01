#pragma once
#include "PasswordLibrary.h"
#include "SevenZipCore.h"
#include "ArchivePlan.h"

struct PasswordCandidate {
    QString value;
    QString source;
};
QList<PasswordList> ReadPasswordNotes(const QStringList& files);
struct ArchivePasswordResult {
    enum class State { Ready, NoMatch, Invalid, Cancelled } state = State::Invalid;
    QString password, source, error;
    bool verified = false;
    int attempts = 0;
};
QList<PasswordCandidate> BuildPasswordCandidates(const QString& manual, const QList<PasswordList>& lists,
                                               bool enabled, const QString& cached = {}, const QString& recent = {});
ArchivePasswordResult ResolveArchivePassword(SevenZipCore& core, const std::wstring& path,
    const QList<PasswordCandidate>& candidates, ProgressCallback progress,
    std::function<void(int, int)> attempting = {});
ArchivePasswordResult ResolveArchivePassword(SevenZipCore& core, const ArchivePlan& plan,
    const QList<PasswordCandidate>& candidates, ProgressCallback progress,
    std::function<void(int, int)> attempting = {});
