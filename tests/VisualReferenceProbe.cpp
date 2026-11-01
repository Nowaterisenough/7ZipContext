#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include "ui/theme/typography.h"

// Diagnostic renderings for comparison with the user's original PNGs. No desktop windows.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    desktop::type::InstallFallbacks();
    const auto args = app.arguments();
    if (args.size() < 2) return 1;
    QDir output(args[1]);
    if (!QDir().mkpath(output.absolutePath())) return 2;
    const QStringList families{"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", "SimSun", "SimHei"};
    const QStringList texts{QStringLiteral("自动更新"), QStringLiteral("自动检查 magpie 新版本"), QStringLiteral("关于")};
    QJsonArray entries;
    for (int role = 0; role < texts.size(); ++role) {
        for (const auto& family : families) {
            for (int weight : {400, 500, 550, 600, 700}) {
                for (int quarter = 52; quarter <= 80; ++quarter) {
                    const double pixels = quarter / 4.0;
                    QFont font;
                    font.setFamilies({family, "Microsoft YaHei UI", "Microsoft YaHei"});
                    font.setPointSizeF(pixels * 72 / 96);
                    font.setWeight(static_cast<QFont::Weight>(weight));
                    if (role == 0) font.setLetterSpacing(QFont::AbsoluteSpacing, -.005 * pixels);
                    if (role == 2) font.setLetterSpacing(QFont::AbsoluteSpacing, .05 * pixels);
                    QImage image(400, 64, QImage::Format_RGB32);
                    image.setDotsPerMeterX(3780); image.setDotsPerMeterY(3780);
                    image.fill(QColor(role == 2 ? "#2d2d33" : "#232327"));
                    QPainter painter(&image);
                    painter.setFont(font);
                    painter.setPen(QColor(role == 1 ? "#b9b9c1" : "#ededf1"));
                    painter.drawText(QPointF(8, 44), texts[role]);
                    painter.end();
                    const QString name = QString::number(entries.size()) + ".png";
                    if (!image.save(output.filePath(name))) return 3;
                    entries.append(QJsonObject{{"file", name}, {"family", family}, {"weight", weight}, {"pixels", pixels}, {"role", role}});
                }
            }
        }
    }
    QFile metadata(output.filePath("candidates.json"));
    if (!metadata.open(QIODevice::WriteOnly)) return 4;
    metadata.write(QJsonDocument(entries).toJson());
    return 0;
}
