#pragma once
#include "workbench_theme.h"

namespace desktop::theme {
inline bool UsesTrayPalette(const QWidget* widget) {
    for (auto* owner = widget; owner; owner = owner->parentWidget())
        if (owner->property("trayPalette").toBool()) return true;
    return false;
}
inline QColor CanvasColor(const QWidget* widget) { return QColor(UsesTrayPalette(widget) ? "#1c1d21" : Canvas); }
inline QColor SurfaceColor(const QWidget* widget) { return QColor(UsesTrayPalette(widget) ? "#25262c" : Surface); }
inline QColor RaisedColor(const QWidget* widget) { return QColor(UsesTrayPalette(widget) ? "#2e2f36" : Raised); }
inline QColor BorderColor(const QWidget* widget) { return QColor(UsesTrayPalette(widget) ? "#323338" : Border); }
inline QColor DividerColor(const QWidget* widget) { return QColor(UsesTrayPalette(widget) ? "#323338" : Divider); }
inline QColor MutedColor(const QWidget* widget) { return QColor(UsesTrayPalette(widget) ? "#9c9ca6" : Muted); }
}
