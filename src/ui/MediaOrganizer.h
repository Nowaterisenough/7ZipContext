#pragma once
#include "ArchiveCleanup.h"
#include <QHash>

enum class MediaKind { None, Video, Image };
struct MediaType { MediaKind kind = MediaKind::None; QString extension; bool readable = true; };
MediaType DetectMediaFile(const QString& path);

struct MediaOrganizationResult {
    bool success = false, noMedia = false;
    QString error;
    QStringList videos, images;
    QHash<QString, QString> relocated;
    int removedFiles = 0;
    uint64_t removedBytes = 0;
};
MediaOrganizationResult OrganizeExtractedMedia(const QString& outputRoot, const IntermediateArchive& generated,
                                              const QStringList& generatedDirectories, ProgressCallback progress = {});
