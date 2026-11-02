#pragma once

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <cmath>
#include "workbench_theme.h"
#include "animated_navigation.h"
#include "layout_metrics.h"
#include "shell_icons.h"

namespace desktop {
enum class ShellIcon { Terminal, File, Library, Sliders, Nodes, Shield, Chart, Timeline, Help, Settings, Panel, Refresh, External };

inline QIcon LineIcon(ShellIcon kind)
{
    QIcon icon;
    for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected}) {
        QPixmap image(40, 40);
        image.setDevicePixelRatio(2);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(mode == QIcon::Normal ? theme::Muted : theme::Text), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        switch (kind) {
        case ShellIcon::Terminal:
            p.drawPolyline(QPolygonF{{4, 5}, {8, 9}, {4, 13}}); p.drawLine(QPointF(10, 14), QPointF(16, 14)); break;
        case ShellIcon::File:
            p.drawRoundedRect(QRectF(4, 3, 12, 14), 1, 1); p.drawLine(7, 7, 13, 7); p.drawLine(7, 10, 13, 10); p.drawLine(7, 13, 11, 13); break;
        case ShellIcon::Library:
            p.drawPolyline(QPolygonF{{5, 3}, {15, 3}, {15, 16}, {4, 16}, {4, 13}, {12, 13}}); break;
        case ShellIcon::Sliders:
            p.drawLine(4, 5, 16, 5); p.drawLine(4, 10, 16, 10); p.drawLine(4, 15, 16, 15);
            p.setBrush(QColor(theme::Canvas)); p.drawEllipse(QPointF(8, 5), 2, 2); p.drawEllipse(QPointF(13, 10), 2, 2); p.drawEllipse(QPointF(7, 15), 2, 2); break;
        case ShellIcon::Nodes:
            p.drawPolyline(QPolygonF{{4, 10}, {8, 10}, {13, 4}, {16, 4}});
            p.drawPolyline(QPolygonF{{8, 10}, {13, 16}, {16, 16}});
            p.drawPolyline(QPolygonF{{14, 2}, {16, 4}, {14, 6}});
            p.drawPolyline(QPolygonF{{14, 14}, {16, 16}, {14, 18}}); break;
        case ShellIcon::Shield:
            p.drawPolygon(QPolygonF{{10, 2}, {16, 5}, {16, 11}, {14, 15}, {10, 18}, {6, 15}, {4, 11}, {4, 5}}); break;
        case ShellIcon::Chart:
            p.drawLine(4, 15, 4, 8); p.drawLine(8, 15, 8, 4); p.drawLine(12, 15, 12, 10); p.drawLine(16, 15, 16, 6); break;
        case ShellIcon::Timeline:
            p.drawRoundedRect(QRectF(3, 3, 14, 5), 1, 1); p.drawRoundedRect(QRectF(3, 12, 14, 5), 1, 1);
            p.drawLine(6, 5, 9, 5); p.drawLine(9, 14, 14, 14); break;
        case ShellIcon::Help:
            p.drawEllipse(QRectF(3, 3, 14, 14)); p.drawArc(QRectF(7, 5, 6, 6), 0, 200 * 16); p.drawLine(10, 10, 10, 11); p.drawPoint(10, 14); break;
        case ShellIcon::Settings: {
            QPolygonF gear;
            for (int i = 0; i < 24; ++i) {
                const qreal a = i * 3.141592653589793 / 12;
                const qreal r = i % 4 == 0 || i % 4 == 3 ? 8 : 6;
                gear << QPointF(10 + r * std::cos(a), 10 + r * std::sin(a));
            }
            p.drawPolygon(gear); p.drawEllipse(QPointF(10, 10), 2.8, 2.8); break;
        }
        case ShellIcon::Panel:
            p.drawRoundedRect(QRectF(3, 4, 14, 12), 2, 2); p.drawLine(8, 4, 8, 16); break;
        case ShellIcon::External:
            p.drawPolyline(QPolygonF{{9, 4}, {4, 4}, {4, 16}, {16, 16}, {16, 11}});
            p.drawLine(9, 11, 17, 3); p.drawPolyline(QPolygonF{{12, 3}, {17, 3}, {17, 8}}); break;
        case ShellIcon::Refresh:
            p.drawArc(QRectF(4, 4, 12, 12), 35 * 16, 290 * 16);
            p.drawPolyline(QPolygonF{{16, 3}, {16, 7}, {12, 7}}); break;
        }
        p.end();
        icon.addPixmap(image, mode);
    }
    return icon;
}

class MagpieShell final : public QWidget {
public:
    QStackedWidget *pages;
    QVBoxLayout *bodyLayout;
    QLineEdit *search;
    QToolButton *refresh, *settings, *panel;
    std::function<void(const QString &)> navigate;

    explicit MagpieShell(bool compact = false, QWidget *parent = nullptr) : QWidget(parent), compact_(compact) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *navigation = new QFrame(this);
        navigation->setObjectName(QStringLiteral("topNavigation"));
        navigation->setFixedHeight(metrics::NavigationHeight);
        auto *bar = new QHBoxLayout(navigation);
        bar->setContentsMargins(11, 8, 11, 8);
        bar->setSpacing(0);
        auto tool = [this, compact](HeaderIcon icon, const char *name, const QString &tooltip) {
            auto *button = new QToolButton(this);
            button->setObjectName(QString::fromLatin1(name));
            button->setProperty("shellTool", true);
            button->setIcon(HeaderLineIcon(icon, compact));
            button->setIconSize(icon == HeaderIcon::Panel ? QSize(17, 17) : QSize(15, 15));
            button->setToolTip(tooltip); button->setAccessibleName(tooltip);
            button->setFixedSize(29, 29); button->setCursor(Qt::ArrowCursor);
            return button;
        };
        panel = tool(HeaderIcon::Panel, "openQuickPanel", QStringLiteral("打开快捷面板"));
        refresh = tool(HeaderIcon::Refresh, "refreshArchives", QStringLiteral("重新识别压缩包"));
        settings = tool(HeaderIcon::Settings, "nav_settings", QStringLiteral("设置"));
        settings->setCheckable(true);
        connect(settings, &QToolButton::clicked, this, [this] {
            Select(QStringLiteral("general"));
            if (navigate) navigate(QStringLiteral("general"));
        });
        if (!compact) {
            auto *left = new QWidget; left->setFixedWidth(60);
            auto *row = new QHBoxLayout(left); row->setContentsMargins(5, 0, 0, 0); row->addWidget(panel); row->addStretch();
            bar->addWidget(left); bar->addStretch();
        } else panel->hide();
        primary_ = new NavigationStrip(true, navigation);
        primary_->setObjectName(QStringLiteral("centeredNavigation"));
        auto *items = new QHBoxLayout(primary_);
        items->setContentsMargins(0, 0, 0, 0); items->setSpacing(4);
        AddItem(items, "extract", QStringLiteral("解压"));
        AddItem(items, "compress", QStringLiteral("压缩"));
        AddItem(items, "tasks", QStringLiteral("任务"));
        bar->addWidget(primary_, compact ? 1 : 0);
        if (!compact) {
            bar->addStretch();
            auto *right = new QWidget; right->setFixedWidth(60);
            auto *row = new QHBoxLayout(right); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(2);
            row->addWidget(refresh); row->addWidget(settings); bar->addWidget(right);
        }
        layout->addWidget(navigation);

        auto *body = new QWidget; body->setObjectName(QStringLiteral("workspaceBody"));
        bodyLayout = new QVBoxLayout(body); bodyLayout->setSpacing(0);
        header_ = new QWidget; header_->setFixedHeight(40);
        auto *header = headerLayout_ = new QHBoxLayout(header_); header->setContentsMargins(0, 0, 0, 0); header->setSpacing(12);
        auto *children = new NavigationStrip(false); children->SetPlain(true);
        children->setObjectName(QStringLiteral("settingsChildren"));
        auto *childRow = new QHBoxLayout(children); childRow->setContentsMargins(0, 0, 0, 0); childRow->setSpacing(4);
        AddItem(childRow, "general", QStringLiteral("常规"), true);
        AddItem(childRow, "passwords", QStringLiteral("密码单"), true);
        AddItem(childRow, "about", QStringLiteral("关于"), true);
        header->addWidget(children); header->addStretch();
        search = new QLineEdit; search->setObjectName(QStringLiteral("searchSettings"));
        search->setPlaceholderText(QStringLiteral("查找设置")); search->setAccessibleName(search->placeholderText());
        search->setClearButtonEnabled(true); search->setFixedSize(compact ? 180 : 198, 27); header->addWidget(search);
        bodyLayout->addWidget(header_);
        pages = new QStackedWidget; pages->setObjectName(QStringLiteral("workspacePages"));
        bodyLayout->addWidget(pages, 1); layout->addWidget(body, 1);
        Select(QStringLiteral("extract"));
    }

    void Select(const QString &id) {
        const auto key = id == "settings" ? QStringLiteral("general") : id;
        const bool inSettings = key == "general" || key == "about" || key == "passwords";
        for (auto *button : buttons_) button->setChecked(button->property("pageId").toString() == key);
        if (inSettings) primary_->ClearSelection();
        settings->setChecked(inSettings);
        header_->setVisible(inSettings);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        AlignHeader();
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event); AlignHeader(); }
private:
    void AlignHeader() {
        const int margin = metrics::ContentInset(width(), compact_);
        headerLayout_->setContentsMargins(std::max(0, margin - 6), 0, margin + 2, 0);
    }
    QPushButton *AddItem(QBoxLayout *layout, const char *id, const QString &text, bool child = false) {
        auto *button = new NavigationButton(text, child);
        button->setObjectName(QStringLiteral("nav_%1").arg(QString::fromLatin1(id)));
        button->setProperty("navItem", true); button->setProperty("navChild", child);
        button->setProperty("pageId", QString::fromLatin1(id)); button->setAccessibleName(text);
        button->setCheckable(true); button->setFixedHeight(child ? 28 : compact_ ? 33 : 30);
        button->setSizePolicy(compact_ && !child ? QSizePolicy::Expanding : QSizePolicy::Minimum, QSizePolicy::Fixed);
        layout->addWidget(button, compact_ && !child ? 1 : 0);
        static_cast<NavigationStrip *>(layout->parentWidget())->AddButton(button);
        buttons_.append(button);
        connect(button, &QPushButton::clicked, this, [this, key = QString::fromLatin1(id)] {
            Select(key); if (navigate) navigate(key);
        });
        return button;
    }
    bool compact_;
    QWidget *header_;
    QHBoxLayout *headerLayout_;
    NavigationStrip *primary_;
    QList<QPushButton *> buttons_;
};
} // namespace desktop
