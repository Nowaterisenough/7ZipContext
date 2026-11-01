#pragma once
#include "ArchivePlan.h"
#include "SevenZipCore.h"
#include <QList>
#include <QStringList>

struct MediaInputGroup { QString directory, name; QStringList files; };
struct DiscoveredInputs {
    std::vector<ArchivePlan> archives;
    QList<MediaInputGroup> media;
    QStringList ignored, files;
    bool cancelled = false;
};
DiscoveredInputs DiscoverInputs(const QStringList& paths, ProgressCallback progress = {});

struct MediaInputResult {
    bool success = false;
    QString detail, output;
    QStringList videos, images;
};
MediaInputResult ProcessMediaInput(const MediaInputGroup& group, const QString& destination,
                                  bool testOnly, ProgressCallback progress = {});
