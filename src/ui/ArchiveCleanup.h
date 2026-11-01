#pragma once
#include "ArchivePlan.h"
#include "SevenZipCore.h"
#include <QStringList>

struct IntermediateFile {
    QString path, resolvedPath;
    uint64_t id = 0, size = 0, modified = 0;
    uint32_t volume = 0;
};
struct IntermediateArchive {
    QList<IntermediateFile> files;
    QString error;
};
struct ArchiveCleanupResult {
    bool success = false;
    int files = 0;
    uint64_t bytes = 0;
    QString error;
};

IntermediateArchive TrackIntermediateArchive(const ArchivePlan& plan, const QStringList& produced,
                                            const QString& outputRoot, const QStringList& originalInputs);
ArchiveCleanupResult DeleteIntermediateArchive(const IntermediateArchive& archive, ProgressCallback progress = {});
IntermediateArchive TrackGeneratedFiles(const QStringList& files, const QString& outputRoot, const QStringList& originalInputs);
bool VerifyGeneratedFile(const IntermediateFile& file, QString& error);
bool MoveGeneratedFile(const IntermediateFile& file, const QString& destination, const QString& outputRoot,
                       IntermediateFile& moved, QString& error);
