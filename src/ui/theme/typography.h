#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QStringList>

namespace desktop::type {
// Qt's native Windows fallback order, calibrated against the 110% Magpie reference.
inline constexpr qreal Scale = 1.10;
inline QStringList Families() {
    return {QStringLiteral("Segoe UI"), QStringLiteral("Segoe UI Variable Text"),
            QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei")};
}
inline QFont Font(qreal cssPixels, QFont::Weight weight = QFont::Normal, qreal spacingEm = 0) {
    QFont font;
    font.setFamilies(Families());
    // Point sizes preserve fractional CSS pixels; setPixelSize would round them.
    font.setPointSizeF(cssPixels * Scale * 72.0 / 96.0);
    font.setWeight(weight);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacingEm * cssPixels * Scale);
    return font;
}
inline QFont Body() { return Font(13); }
// Native Qt's 500 Han fallback is regular; 600 matches the reference's visible strokes.
inline QFont RowTitle() { return Font(13, QFont::DemiBold, -.005); }
inline QFont Detail() { return Font(11.5); }
inline QFont Navigation(bool child, bool tray, bool selected) {
    if (child) return Font(11, QFont::DemiBold, .05);
    return Font(tray ? 12 : 12.5, tray && selected ? static_cast<QFont::Weight>(550) : QFont::Normal);
}
inline void InstallFallbacks() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script_Han,
        {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei")});
#endif
}
inline QString StyleSheet() {
    auto pt = [](qreal pixels) { return QString::number(pixels * Scale * 72.0 / 96.0, 'f', 4) + "pt"; };
    return QStringLiteral(R"(
        QWidget { font-family: "Segoe UI", "Segoe UI Variable Text", "Microsoft YaHei UI", "Microsoft YaHei"; font-size: $body; }
        QLabel[cardTitle="true"] { font-size: $body; font-weight: 600; }
        QLabel[muted="true"] { font-size: $small; }
        QLabel[sectionTitle="true"] { font-size: $section; }
        QLabel[settingDescription="true"] { color: #b9b9c1; font-size: $detail; font-weight: 400; }
        QLabel[brand="true"] { font-size: $body; font-weight: 600; }
        QLabel[statValue="true"] { font-size: 20px; font-weight: 600; }
        QPushButton[navItem="true"] { font-size: $navigation; font-weight: 400; min-height: 0; max-height: 16777215px; }
        QPushButton[navChild="true"] { font-size: $section; font-weight: 600; min-height: 0; max-height: 16777215px; }
        QLineEdit#searchSettings { font-size: $small; min-height: 0; padding: 0 9px; }
        QToolTip { font-size: $small; }
    )").replace("$body", pt(13)).replace("$small", pt(12)).replace("$detail", pt(11.5))
       .replace("$navigation", pt(12.5)).replace("$section", pt(11));
}
}
