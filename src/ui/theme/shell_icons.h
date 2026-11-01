#pragma once
#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include "workbench_theme.h"

namespace desktop {
enum class HeaderIcon { Panel, Refresh, External, Settings, Close };
inline QIcon HeaderLineIcon(HeaderIcon kind, bool tray = false) {
    const char* drawing = nullptr;
    const char* viewport = "0 0 16 16";
    switch (kind) {
    case HeaderIcon::Panel:
        drawing = R"(<g fill="none" stroke="currentColor" stroke-width="1.25" stroke-linecap="round"><rect x="1.75" y="2.75" width="12.5" height="10.5" rx="2.25"/><path d="M6.75 2.75v10.5"/></g>)"; break;
    case HeaderIcon::Refresh:
        drawing = R"(<path d="M13.5 8a5.5 5.5 0 1 1-1.6-3.9M13.5 2v3h-3" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>)"; break;
    case HeaderIcon::External:
        drawing = R"(<path d="M6 3H3.5A1.5 1.5 0 0 0 2 4.5v8A1.5 1.5 0 0 0 3.5 14h8a1.5 1.5 0 0 0 1.5-1.5V10M9 2h5v5M14 2 8 8" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>)"; break;
    case HeaderIcon::Close:
        drawing = R"(<path d="M3.5 3.5l9 9M12.5 3.5l-9 9" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>)"; break;
    case HeaderIcon::Settings:
        viewport = "0 0 24 24";
        drawing = R"(<g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3"/></g>)"; break;
    }
    QIcon icon;
    const int size = kind == HeaderIcon::Panel ? 17 : 15;
    for (auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled}) {
        const auto color = mode == QIcon::Disabled ? "#56565f" : mode == QIcon::Normal ? tray ? "#9c9ca6" : "#8b8b94" : theme::Text;
        auto svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='%1'>%2</svg>")
            .arg(QString::fromLatin1(viewport), QString::fromLatin1(drawing)).replace("currentColor", color).toUtf8();
        QSvgRenderer renderer(svg);
        for (qreal scale : {1.0, 1.25, 1.5, 2.0}) {
            QPixmap pixmap(qRound(size * scale), qRound(size * scale));
            pixmap.setDevicePixelRatio(scale); pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap); renderer.render(&painter, QRectF(0, 0, size, size)); painter.end();
            icon.addPixmap(pixmap, mode);
            if (mode == QIcon::Active) icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
        }
    }
    return icon;
}
}
