#pragma once
#include "ArchivePlan.h"
#include "SevenZipCore.h"
#include <QStringList>

struct NestedArchiveScan {
    std::vector<ArchivePlan> archives;
    QStringList videos, images;
    QStringList ignored;
    bool cancelled = false;
};

// Inspect only files written by the current extraction, never unrelated destination files.
NestedArchiveScan FindNestedArchives(const QStringList& files, ProgressCallback progress = {}, const QStringList& relatedFiles = {}, bool probePdfs = false);
QString ArchiveVolumeGroup(const QString& filename);
