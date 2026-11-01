#include "ui/ArchiveWindow.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QSaveFile>

// Export the application's existing painted mark; no desktop window is created.
int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    if (application.arguments().size() != 2) return 1;
    QDir output(application.arguments()[1]); if (!QDir().mkpath(output.absolutePath())) return 2;
    const auto icon = ArchiveIcon();
    const QList<int> sizes{16, 20, 24, 32, 40, 48, 64, 128, 256};
    QList<QByteArray> frames;
    for (const auto size : sizes) {
        const auto image = icon.pixmap(QSize(size, size), 1.0).toImage();
        if (image.size() != QSize(size, size)) return 3;
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) return 4;
        frames.append(bytes);
    }
    QByteArray data; QDataStream stream(&data, QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(frames.size());
    quint32 offset = 6 + 16 * frames.size();
    for (qsizetype index = 0; index < sizes.size(); ++index) {
        const quint8 dimension = sizes[index] == 256 ? 0 : sizes[index];
        stream << dimension << dimension << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(frames[index].size()) << offset;
        offset += frames[index].size();
    }
    for (const auto& frame : frames) data.append(frame);
    QSaveFile file(output.filePath("icon.ico"));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return 5;
    const QList<QPair<QString, int>> logos{{"icon.png", 256}, {"StoreLogo.png", 50},
        {"Square44x44Logo.png", 44}, {"Square150x150Logo.png", 150}};
    for (const auto& logo : logos) {
        if (!icon.pixmap(QSize(logo.second, logo.second), 1.0).save(output.filePath(logo.first), "PNG")) return 6;
    }
    return 0;
}
