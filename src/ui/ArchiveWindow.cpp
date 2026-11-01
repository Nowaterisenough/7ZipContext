#include "ArchiveWindow.h"
#include "theme/dark_style.h"
#include "theme/magpie_shell.h"
#include "theme/segmented_tabs.h"
#include "theme/typography.h"
#include "theme/layout_metrics.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QScreen>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QWindow>
#include <dwmapi.h>
#include <cmath>

namespace theme = desktop::theme;
namespace {
QString Q(const std::wstring& text) { return QString::fromStdWString(text); }
enum ItemRole { Detail = Qt::UserRole + 1, Badge, Ink, Path, ListIds, SearchText };
constexpr int PasswordRowHeight = 44;
QColor MagpieAccent() { return QColor("#a5b4fc"); }

QString ShellStyle() {
    return QStringLiteral(R"(
        QFrame#topNavigation { border: 0; }
        QPushButton[navItem="true"], QPushButton[navItem="true"]:checked,
        QPushButton[navItem="true"]:hover, QPushButton[navItem="true"]:focus {
            background: transparent; border: 0;
        }
        QToolButton[shellTool="true"] { background: transparent; border: 0; padding: 0; }
        QToolButton[shellTool="true"]:hover, QToolButton[shellTool="true"]:checked { background: #2d2d33; }
        QToolButton[shellTool="true"]:focus { border: 0; }
        QScrollBar[scrollable="false"]::handle { background: transparent; }
    )");
}

QLabel* Label(const QString& text, bool muted = false, bool title = false) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    if (muted) label->setProperty("muted", true);
    if (title) label->setProperty("cardTitle", true);
    return label;
}
QPushButton* Button(const QString& text, const char* name, bool primary = false) {
    auto* button = new QPushButton(text);
    button->setObjectName(QString::fromLatin1(name));
    button->setCursor(Qt::ArrowCursor);
    button->setAccessibleName(text);
    if (primary) button->setProperty("primary", true);
    return button;
}
QLineEdit* Edit(const char* name, const QString& placeholder, bool secret = false) {
    auto* edit = new QLineEdit;
    edit->setObjectName(QString::fromLatin1(name));
    edit->setPlaceholderText(placeholder);
    edit->setAccessibleName(placeholder);
    edit->setMinimumWidth(130);
    edit->setClearButtonEnabled(!secret);
    if (secret) { edit->setEchoMode(QLineEdit::Password); edit->setMaxLength(1024); }
    return edit;
}
desktop::SegmentedTabBar* Segments(const char* name, const QStringList& items) {
    auto* segments = new desktop::SegmentedTabBar;
    segments->setObjectName(QString::fromLatin1(name));
    segments->setAccessibleName(QString::fromLatin1(name));
    for (const auto& item : items) segments->addTab(item);
    segments->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return segments;
}
QFrame* Card() {
    auto* card = new QFrame;
    card->setProperty("settingsCard", true);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    return card;
}
void Row(QFrame* card, const QString& title, const QString& description, QWidget* control, bool last = false) {
    auto* row = new QFrame;
    row->setProperty("settingsRow", true);
    row->setProperty("searchText", title + ' ' + description);
    if (last) row->setProperty("settingsLastRow", true);
    row->setMinimumHeight(description.isEmpty() ? 54 : 67);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(15, 13, 15, 13);
    layout->setSpacing(20);
    auto* labels = new QVBoxLayout;
    labels->setSpacing(2);
    auto* name = Label(title, false, true);
    name->setFont(desktop::type::RowTitle());
    name->setMinimumHeight(20);
    labels->addWidget(name);
    if (!description.isEmpty()) {
        auto* note = Label(description);
        note->setProperty("settingDescription", true);
        note->setMinimumHeight(18);
        note->setWordWrap(true);
        labels->addWidget(note);
    }
    layout->addLayout(labels, 1);
    layout->addWidget(control, control->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding ? 2 : 0);
    static_cast<QVBoxLayout*>(card->layout())->addWidget(row);
}
QWidget* WithBrowse(QLineEdit* edit, QPushButton* browse) {
    auto* widget = new QWidget;
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8); row->addWidget(edit, 1); row->addWidget(browse);
    return widget;
}
QWidget* Empty(const QString& title, const QString& detail) {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(18, 20, 18, 20);
    layout->setSpacing(7); layout->addStretch();
    auto* headline = Label(title, false, true); headline->setAlignment(Qt::AlignCenter);
    auto* note = Label(detail, true); note->setWordWrap(true); note->setAlignment(Qt::AlignCenter);
    layout->addWidget(headline); layout->addWidget(note); layout->addStretch();
    return widget;
}
QWidget* Page(QVBoxLayout*& layout, const char* name) {
    auto* widget = new QWidget;
    widget->setObjectName(QString::fromLatin1(name));
    layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 10);
    layout->setSpacing(14);
    return widget;
}
class ArchiveDelegate final : public QStyledItemDelegate {
public:
    explicit ArchiveDelegate(QObject* parent = nullptr, bool dense = false) : QStyledItemDelegate(parent), dense_(dense) {}
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {300, dense_ ? PasswordRowHeight : 66}; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save(); painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(option.rect, option.state & QStyle::State_Selected ? theme::RaisedColor(option.widget) : theme::SurfaceColor(option.widget));
        if (!dense_) {
            const QRectF badge(option.rect.left() + 15, option.rect.top() + 15, 36, 36);
            painter->setPen(Qt::NoPen); painter->setBrush(theme::RaisedColor(option.widget)); painter->drawRoundedRect(badge, 7, 7);
            painter->setFont(desktop::type::Font(10.5));
            painter->setPen(QColor(theme::Text)); painter->drawText(badge, Qt::AlignCenter, index.data(Badge).toString());
        }
        const int textLeft = dense_ ? 12 : 65;
        const QRect title(option.rect.left() + textLeft, option.rect.top() + (dense_ ? 3 : 9), option.rect.width() - textLeft - 17, dense_ ? 20 : 23);
        painter->setFont(desktop::type::RowTitle()); painter->setPen(QColor(theme::Text));
        painter->drawText(title, Qt::AlignLeft | Qt::AlignVCenter, painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideMiddle, title.width()));
        painter->setFont(desktop::type::Detail());
        painter->setPen(index.data(Ink).value<QColor>());
        const QRect detail = dense_ ? QRect(title.left(), option.rect.top() + 23, title.width(), 17) : title.translated(0, 24);
        painter->drawText(detail, Qt::AlignLeft | Qt::AlignVCenter,
            painter->fontMetrics().elidedText(index.data(Detail).toString(), Qt::ElideRight, title.width()));
        painter->setPen(QColor("#2c2c32")); painter->drawLine(option.rect.bottomLeft() + QPoint(15, 0), option.rect.bottomRight() - QPoint(15, 0));
        if (option.state & QStyle::State_HasFocus) {
            painter->setPen(QPen(theme::AccentColor(option.widget), 1)); painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(option.rect).adjusted(1, 1, -1, -1), 5, 5);
        }
        painter->restore();
    }
private:
    bool dense_;
};
class RoundedArchiveList final : public QListWidget {
protected:
    void resizeEvent(QResizeEvent* event) override {
        QListWidget::resizeEvent(event);
        QPainterPath outline;
        outline.addRoundedRect(QRectF(rect()), 12, 12);
        setMask(QRegion(outline.toFillPolygon().toPolygon()));
    }
};
void AddItem(QListWidget* list, const QString& name, const QString& detail, const QString& badge, QColor ink, const QString& path) {
    auto* item = new QListWidgetItem(name, list);
    item->setData(Detail, detail); item->setData(Badge, badge); item->setData(Ink, ink); item->setData(Path, path);
    item->setData(Qt::AccessibleDescriptionRole, detail);
    item->setToolTip(name + '\n' + detail + (path.isEmpty() ? QString{} : '\n' + path));
}
}

class SpeedChart final : public QWidget {
public:
    explicit SpeedChart(ArchiveController& controller, QWidget* parent = nullptr) : QWidget(parent), controller_(controller) {
        setMinimumHeight(130); setObjectName(QStringLiteral("speedChart"));
        connect(&controller_, &ArchiveController::progressChanged, this, qOverload<>(&SpeedChart::update));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(46, 10, -10, -22);
        double max = 1;
        for (double value : controller_.speedHistory()) max = std::max(max, value);
        p.setFont(desktop::type::Font(10.5));
        for (int line = 0; line < 4; ++line) {
            double y = area.bottom() - area.height() * line / 3;
            p.setPen(QPen(theme::BorderColor(this), 1, Qt::DotLine)); p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            p.setPen(QColor(theme::Muted));
            p.drawText(QRectF(0, y - 10, 40, 20), Qt::AlignRight | Qt::AlignVCenter,
                line == 0 ? QStringLiteral("0") : controller_.speedHistory().isEmpty() ? QString{} : QString::number(max * line / (3 * 1048576), 'f', 1));
        }
        p.setPen(Qt::NoPen); p.setBrush(theme::AccentColor(this));
        int index = 0;
        for (double value : controller_.speedHistory()) {
            double h = (area.height() - 3) * value / max;
            if (h > 0) p.drawRoundedRect(QRectF(area.left() + index * area.width() / 80, area.bottom() - h, std::max(2.0, area.width() / 80 - 2), h), 1, 1);
            ++index;
        }
        p.setPen(QColor(theme::Muted));
        p.drawText(QRectF(area.left(), area.bottom() + 4, area.width(), 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("处理速度 · MB/s"));
        if (controller_.speedHistory().isEmpty()) p.drawText(area, Qt::AlignCenter, QStringLiteral("任务开始后显示实时速度"));
    }
private:
    ArchiveController& controller_;
};

void ApplyArchiveTheme(QApplication& app) {
    app.setStyle(new DarkStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    auto palette = app.palette();
    palette.setColor(QPalette::Accent, MagpieAccent()); palette.setColor(QPalette::Link, MagpieAccent());
    app.setPalette(palette);
    app.setStyleSheet(app.styleSheet() + theme::WorkbenchStyleSheet(MagpieAccent()) + ShellStyle() + desktop::type::StyleSheet());
}
QIcon ArchiveIcon() {
    QPixmap image(64, 64); image.fill(Qt::transparent);
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(theme::Text)); painter.drawRoundedRect(QRectF(5, 5, 54, 54), 10, 10);
    QFont font(QStringLiteral("Microsoft YaHei UI")); font.setPixelSize(42); font.setBold(true); painter.setFont(font);
    painter.setPen(QColor(theme::Canvas)); painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("7"));
    painter.end(); return QIcon(image);
}

ArchiveWindow::ArchiveWindow(ArchiveController& controller, bool compact) : controller_(controller), compact_(compact) {
    setObjectName(QStringLiteral("workbenchContent"));
    setProperty("workbenchAccent", MagpieAccent());
    setProperty("trayPalette", compact);
    if (compact) {
        auto sheet = theme::WorkbenchStyleSheet(MagpieAccent()) + ShellStyle() + desktop::type::StyleSheet();
        sheet.replace(theme::Canvas, "#1c1d21").replace(theme::Surface, "#25262c")
             .replace(theme::Raised, "#2e2f36").replace(theme::Border, "#323338").replace(theme::Muted, "#9c9ca6");
        setStyleSheet(sheet);
    }
    setWindowTitle(compact ? QStringLiteral("7-Zip Context · 托盘") : QStringLiteral("7-Zip Context"));
    setWindowIcon(ArchiveIcon()); setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    if (compact_) setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    QHBoxLayout* popupHeader = nullptr;
    if (compact_) {
        auto* header = popupHeader = new QHBoxLayout; header->setContentsMargins(10, 3, 10, 0); header->setSpacing(2);
        auto* brandGroup = new QWidget;
        auto* brandLayout = new QHBoxLayout(brandGroup); brandLayout->setContentsMargins(0, 0, 0, 0); brandLayout->setSpacing(9);
        auto* icon = new QLabel; icon->setPixmap(windowIcon().pixmap(20, 20)); brandLayout->addWidget(icon);
        auto* brand = Label(QStringLiteral("7-Zip Context"), false, true); brand->setProperty("brand", true);
        brandLayout->addWidget(brand); header->addWidget(brandGroup); header->addStretch();
        auto* expand = new QToolButton; expand->setObjectName(QStringLiteral("expandWindow"));
        expand->setProperty("shellTool", true);
        expand->setIcon(desktop::HeaderLineIcon(desktop::HeaderIcon::External, true)); expand->setIconSize(QSize(15, 15));
        expand->setToolTip(QStringLiteral("打开主窗口"));
        expand->setAccessibleName(expand->toolTip()); expand->setFixedSize(30, 28); header->addWidget(expand);
        connect(expand, &QToolButton::clicked, this, [this] { emit expandRequested(page_); });
        layout->addLayout(header);
    }
    shell_ = new desktop::MagpieShell(compact_, this); layout->addWidget(shell_, 1);
    if (popupHeader) {
        popupHeader->insertWidget(popupHeader->count() - 1, shell_->refresh);
        popupHeader->addWidget(shell_->settings);
        auto* close = new QToolButton; close->setObjectName(QStringLiteral("closePopup"));
        close->setProperty("shellTool", true);
        close->setIcon(desktop::HeaderLineIcon(desktop::HeaderIcon::Close, true)); close->setIconSize(QSize(15, 15));
        close->setToolTip(QStringLiteral("关闭弹窗")); close->setAccessibleName(close->toolTip()); close->setFixedSize(30, 28);
        popupHeader->addWidget(close);
        connect(close, &QToolButton::clicked, this, &QWidget::hide);
    }
    shell_->navigate = [this](const QString& page) { navigate(page); };
    for (auto* page : {makeExtractPage(), makeCompressPage(), makeTasksPage(), makeSettingsPage(false), makeSettingsPage(true), makePasswordsPage()}) {
        auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        auto* scrollbar = scroll->verticalScrollBar();
        scrollbar->setProperty("scrollable", false);
        connect(scrollbar, &QScrollBar::rangeChanged, scrollbar, [scrollbar](int minimum, int maximum) {
            const bool scrollable = maximum > minimum;
            if (scrollbar->property("scrollable").toBool() == scrollable) return;
            scrollbar->setProperty("scrollable", scrollable);
            scrollbar->style()->unpolish(scrollbar); scrollbar->style()->polish(scrollbar); scrollbar->update();
        });
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(page); shell_->pages->addWidget(scroll);
    }
    status_ = Label(controller_.status(), true); status_->setObjectName(QStringLiteral("operationStatus"));
    status_->setWordWrap(true); status_->setContentsMargins(20, 6, 20, 10); layout->addWidget(status_);
    connect(shell_->refresh, &QToolButton::clicked, &controller_, &ArchiveController::rescan);
    connect(shell_->panel, &QToolButton::clicked, this, &ArchiveWindow::quickPanelRequested);
    connect(shell_->search, &QLineEdit::textChanged, this, &ArchiveWindow::filterSettings);
    connect(&controller_, &ArchiveController::queueChanged, this, &ArchiveWindow::refreshQueues);
    connect(&controller_, &ArchiveController::tasksChanged, this, &ArchiveWindow::refreshTasks);
    connect(&controller_, &ArchiveController::optionsChanged, this, &ArchiveWindow::syncOptions);
    connect(&controller_, &ArchiveController::busyChanged, this, &ArchiveWindow::syncBusy);
    connect(&controller_, &ArchiveController::progressChanged, this, &ArchiveWindow::syncProgress);
    connect(&controller_, &ArchiveController::passwordsChanged, this, &ArchiveWindow::refreshPasswords);
    connect(&controller_.updates(), &UpdateManager::changed, this, &ArchiveWindow::syncUpdates);
    connect(&controller_, &ArchiveController::statusChanged, status_, &QLabel::setText);
    auto* open = new QShortcut(QKeySequence::Open, this);
    connect(open, &QShortcut::activated, this, [this] { if (page_ == "extract" || page_ == "compress") pickFiles(); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::hide);
    refreshQueues(); refreshTasks(); refreshPasswords(); syncOptions(); syncBusy(); syncProgress(); syncUpdates(); navigate(QStringLiteral("extract"));
    setFocus(Qt::OtherFocusReason);
    if (compact_) { setMinimumWidth(440); resize(480, 700); }
    else {
        setMinimumSize(820, 620); resize(1100, 820);
        if (controller_.rememberGeometry) restoreGeometry(controller_.windowGeometry());
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { controller_.saveWindowGeometry(saveGeometry()); });
    }
    alignPages();
}

QWidget* ArchiveWindow::makeQueue(bool sources) {
    auto* card = new QFrame; card->setProperty("statModule", true);
    auto* layout = new QVBoxLayout(card); layout->setContentsMargins(1, 1, 1, 1);
    auto* pages = new QStackedWidget;
    pages->addWidget(Empty(QStringLiteral("将文件或文件夹拖到这里"), sources
        ? QStringLiteral("7z / ZIP · 密码加密 · 自定义分卷") : QStringLiteral("自动识别压缩包、分卷、视频和图片")));
    auto* list = new RoundedArchiveList;
    list->setFrameShape(QFrame::NoFrame);
    list->setObjectName(sources ? QStringLiteral("sourceList") : QStringLiteral("archiveList"));
    list->setItemDelegate(new ArchiveDelegate(list)); list->setUniformItemSizes(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    pages->addWidget(list); layout->addWidget(pages);
    editable_.append(list);
    auto* remove = new QShortcut(QKeySequence(Qt::Key_Delete), list); remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, [this, list, sources] { controller_.remove(list->currentRow(), sources); });
    if (sources) { sources_ = list; sourceQueue_ = pages; } else { archives_ = list; archiveQueue_ = pages; }
    return card;
}

QWidget* ArchiveWindow::makeExtractPage() {
    QVBoxLayout* layout; auto* page = Page(layout, "extractPage");
    auto* toolbar = new QHBoxLayout; toolbar->setSpacing(8);
    auto* add = Button(QStringLiteral("＋ 添加文件"), "addArchive");
    auto* folder = Button(QStringLiteral("添加文件夹"), "addExtractionFolder");
    auto* correct = correctName_ = Button(QStringLiteral("修正名称"), "correctName");
    auto* remove = Button(QStringLiteral("移除"), "removeArchive");
    auto* clear = Button(QStringLiteral("清空"), "clearArchives");
    toolbar->addWidget(add); toolbar->addWidget(folder); toolbar->addWidget(correct); toolbar->addStretch(); toolbar->addWidget(remove); toolbar->addWidget(clear);
    layout->addLayout(toolbar);
    archiveCount_ = Label({}, true); layout->addWidget(archiveCount_); layout->addWidget(makeQueue(false));
    auto* card = Card();
    extractDestination_ = Edit("extractDestination", QStringLiteral("留空：下载文件夹/整理结果"));
    auto* browse = Button(QStringLiteral("选择…"), "browseExtraction");
    Row(card, QStringLiteral("保存位置"), {}, WithBrowse(extractDestination_, browse));
    extractMode_ = Segments("extractMode", {QStringLiteral("独立目录"), QStringLiteral("直接解压")});
    Row(card, QStringLiteral("解压方式"), QStringLiteral("已有文件不会覆盖"), extractMode_);
    extractPassword_ = Edit("extractPassword", QStringLiteral("手动密码，可留空"), true);
    auto* passwords = Button(QStringLiteral("密码单…"), "openPasswordLists");
    Row(card, QStringLiteral("解压密码"), QStringLiteral("优先尝试手动密码，再匹配启用的密码单"), WithBrowse(extractPassword_, passwords));
    autoNested_ = new desktop::ToggleSwitch; autoNested_->setObjectName(QStringLiteral("autoNested"));
    autoNested_->setAccessibleName(QStringLiteral("自动解压内层"));
    Row(card, QStringLiteral("自动解压内层"), QStringLiteral("使用密码单逐层解压，成功后清理中间包"), autoNested_);
    autoNested_->setToolTip(QStringLiteral("整条嵌套分支成功后删除中间压缩包及分卷；最初添加的压缩包保留。失败或取消时保留未完成分支。"));
    organizeMedia_ = new desktop::ToggleSwitch; organizeMedia_->setObjectName(QStringLiteral("organizeMedia"));
    organizeMedia_->setAccessibleName(QStringLiteral("仅保留视频和图片"));
    organizeMedia_->setToolTip(QStringLiteral("每组压缩包解压成功后，整理到独立子文件夹的“视频”和“图片”目录，再删除其他解压文件。无媒体、失败或取消时保留内容；原始压缩包和已有文件保留。"));
    Row(card, QStringLiteral("仅保留视频和图片"), QStringLiteral("分类整理到独立目录，完成后删除其他解压文件"), organizeMedia_, true);
    layout->addWidget(card);
    auto* actions = new QHBoxLayout;
    auto* test = Button(QStringLiteral("检测完整性"), "testArchives");
    auto* start = Button(QStringLiteral("开始解压"), "startExtraction", true);
    actions->addWidget(test); actions->addStretch(); actions->addWidget(start); layout->addLayout(actions); layout->addStretch();
    editable_ << add << folder << correct << remove << clear << card << test << start;
    connect(add, &QPushButton::clicked, this, [this] { pickFiles(); });
    connect(folder, &QPushButton::clicked, this, [this] { pickFiles(true); });
    connect(archives_, &QListWidget::currentRowChanged, this, [this](int row) {
        correctName_->setEnabled(!controller_.busy() && row >= 0 && static_cast<size_t>(row) < controller_.archives.size());
    });
    connect(correct, &QPushButton::clicked, this, [this] { controller_.start(ArchiveController::Operation::Correct, archives_->currentRow()); });
    connect(remove, &QPushButton::clicked, this, [this] { controller_.remove(archives_->currentRow(), false); });
    connect(clear, &QPushButton::clicked, this, [this] { controller_.clear(false); });
    connect(browse, &QPushButton::clicked, this, [this] { pickDestination(false); });
    connect(passwords, &QPushButton::clicked, this, [this] { navigate(QStringLiteral("passwords")); });
    connect(test, &QPushButton::clicked, this, [this] { run(ArchiveController::Operation::Test); });
    connect(start, &QPushButton::clicked, this, [this] { run(ArchiveController::Operation::Extract); });
    connect(extractDestination_, &QLineEdit::textEdited, this, [this](const QString& text) { controller_.extractDestination = text; controller_.notifyOptions(); });
    connect(extractPassword_, &QLineEdit::textEdited, this, [this](const QString& text) { controller_.extractPassword = text; controller_.notifyOptions(); });
    connect(extractMode_, &QTabBar::currentChanged, this, [this](int index) { if (synchronizing_) return; controller_.subfolder = index == 0; controller_.notifyOptions(); });
    connect(autoNested_, &QAbstractButton::toggled, this, [this](bool enabled) {
        controller_.autoNested = enabled; controller_.savePreferences(); controller_.notifyOptions();
    });
    connect(organizeMedia_, &QAbstractButton::toggled, this, [this](bool enabled) {
        controller_.organizeMedia = enabled; controller_.savePreferences(); controller_.notifyOptions();
    });
    return page;
}

QWidget* ArchiveWindow::makeCompressPage() {
    QVBoxLayout* layout; auto* page = Page(layout, "compressPage");
    auto* toolbar = new QHBoxLayout; toolbar->setSpacing(8);
    auto* add = Button(QStringLiteral("＋ 添加文件"), "addSource");
    auto* folder = Button(QStringLiteral("添加文件夹"), "addFolder");
    auto* remove = Button(QStringLiteral("移除"), "removeSource");
    auto* clear = Button(QStringLiteral("清空"), "clearSources");
    toolbar->addWidget(add); toolbar->addWidget(folder); toolbar->addStretch(); toolbar->addWidget(remove); toolbar->addWidget(clear); layout->addLayout(toolbar);
    sourceCount_ = Label({}, true); layout->addWidget(sourceCount_); layout->addWidget(makeQueue(true));
    auto* card = Card();
    archiveName_ = Edit("archiveName", QStringLiteral("压缩包名称"));
    Row(card, QStringLiteral("压缩包名称"), {}, archiveName_);
    compressDestination_ = Edit("compressDestination", QStringLiteral("留空：源文件所在目录"));
    auto* browse = Button(QStringLiteral("选择…"), "browseCompression");
    Row(card, QStringLiteral("保存位置"), {}, WithBrowse(compressDestination_, browse));
    format_ = Segments("archiveFormat", {QStringLiteral("7z"), QStringLiteral("ZIP")});
    Row(card, QStringLiteral("压缩格式"), {}, format_);
    level_ = Segments("compressionLevel", {QStringLiteral("快速"), QStringLiteral("标准"), QStringLiteral("极限")});
    Row(card, QStringLiteral("压缩级别"), {}, level_);
    volume_ = new QComboBox; volume_->setObjectName(QStringLiteral("volumeSize")); volume_->setAccessibleName(QStringLiteral("分卷大小"));
    volume_->setEditable(true); volume_->addItems({QStringLiteral("不分卷"), "100M", "1G", "4G"}); volume_->setMinimumWidth(150);
    Row(card, QStringLiteral("分卷大小"), QStringLiteral("支持 K / M / G，纯数字按 MB"), volume_);
    compressPassword_ = Edit("compressPassword", QStringLiteral("留空不加密"), true);
    Row(card, QStringLiteral("加密密码"), {}, compressPassword_, true); layout->addWidget(card);
    auto* actions = new QHBoxLayout;
    auto* note = Label(QStringLiteral("分卷文件将按 .001、.002 顺序保存"), true); note->setWordWrap(true);
    auto* start = Button(QStringLiteral("开始压缩"), "startCompression", true);
    actions->addWidget(note, 1); actions->addWidget(start); layout->addLayout(actions); layout->addStretch();
    editable_ << add << folder << remove << clear << card << start;
    connect(add, &QPushButton::clicked, this, [this] { pickFiles(); });
    connect(folder, &QPushButton::clicked, this, [this] { pickFiles(true); });
    connect(remove, &QPushButton::clicked, this, [this] { controller_.remove(sources_->currentRow(), true); });
    connect(clear, &QPushButton::clicked, this, [this] { controller_.clear(true); });
    connect(browse, &QPushButton::clicked, this, [this] { pickDestination(true); });
    connect(start, &QPushButton::clicked, this, [this] { run(ArchiveController::Operation::Compress); });
    connect(archiveName_, &QLineEdit::textEdited, this, [this](const QString& text) { controller_.archiveName = text; controller_.notifyOptions(); });
    connect(compressDestination_, &QLineEdit::textEdited, this, [this](const QString& text) { controller_.compressDestination = text; controller_.notifyOptions(); });
    connect(compressPassword_, &QLineEdit::textEdited, this, [this](const QString& text) { controller_.compressPassword = text; controller_.notifyOptions(); });
    connect(volume_, &QComboBox::currentTextChanged, this, [this](const QString& text) { controller_.volumeSize = text; controller_.notifyOptions(); });
    connect(format_, &QTabBar::currentChanged, this, [this](int index) { if (synchronizing_) return; controller_.compression.format = index ? L"zip" : L"7z"; controller_.notifyOptions(); });
    connect(level_, &QTabBar::currentChanged, this, [this](int index) { if (synchronizing_) return; controller_.compression.level = index == 0 ? 1 : index == 1 ? 5 : 9; controller_.notifyOptions(); });
    return page;
}

QWidget* ArchiveWindow::makeTasksPage() {
    QVBoxLayout* layout; auto* page = Page(layout, "tasksPage");
    auto* toolbar = new QHBoxLayout; taskCount_ = Label({}, true); toolbar->addWidget(taskCount_); toolbar->addStretch();
    auto* open = Button(QStringLiteral("打开输出文件夹"), "openOutput"); toolbar->addWidget(open); layout->addLayout(toolbar);
    connect(open, &QPushButton::clicked, this, &ArchiveWindow::openOutput);
    auto* card = new QFrame; card->setProperty("statModule", true);
    auto* rows = new QVBoxLayout(card); rows->setContentsMargins(1, 1, 1, 1);
    taskQueue_ = new QStackedWidget;
    taskQueue_->addWidget(Empty(QStringLiteral("还没有任务记录"), QStringLiteral("检测、压缩和解压的结果会显示在这里")));
    tasks_ = new RoundedArchiveList; tasks_->setObjectName(QStringLiteral("taskList")); tasks_->setItemDelegate(new ArchiveDelegate(tasks_));
    tasks_->setFrameShape(QFrame::NoFrame);
    tasks_->setUniformItemSizes(true); tasks_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    taskQueue_->addWidget(tasks_); rows->addWidget(taskQueue_); layout->addWidget(card);
    connect(tasks_, &QListWidget::itemDoubleClicked, this, [this] { openOutput(); });
    auto* metrics = Card(); auto* grid = new QHBoxLayout; grid->setContentsMargins(14, 12, 14, 12); grid->setSpacing(16);
    auto metric = [&](const QString& title, QLabel*& value) {
        auto* column = new QVBoxLayout; column->setSpacing(5); column->addWidget(Label(title, true));
        value = Label(QStringLiteral("—"), false, true); value->setProperty("statValue", true); column->addWidget(value); grid->addLayout(column, 1);
    };
    metric(QStringLiteral("任务"), jobs_); metric(QStringLiteral("已处理"), processed_); metric(QStringLiteral("速度"), speed_); metric(QStringLiteral("进度"), percent_);
    static_cast<QVBoxLayout*>(metrics->layout())->addLayout(grid); layout->addWidget(metrics);
    auto* graphCard = Card(); auto* body = static_cast<QVBoxLayout*>(graphCard->layout()); body->setContentsMargins(14, 10, 14, 14); body->setSpacing(10);
    auto* header = new QHBoxLayout; taskState_ = Label(QStringLiteral("等待任务"), false, true); header->addWidget(taskState_); header->addStretch();
    pause_ = Button(QStringLiteral("暂停"), "pauseTask"); cancel_ = Button(QStringLiteral("取消"), "cancelTask"); header->addWidget(pause_); header->addWidget(cancel_); body->addLayout(header);
    chart_ = new SpeedChart(controller_); body->addWidget(chart_);
    progress_ = new QProgressBar; progress_->setObjectName(QStringLiteral("taskProgress")); progress_->setRange(0, 1000); progress_->setValue(0);
    progress_->setTextVisible(false); progress_->setFixedHeight(6); body->addWidget(progress_); layout->addWidget(graphCard); layout->addStretch();
    connect(pause_, &QPushButton::clicked, &controller_, &ArchiveController::togglePause);
    connect(cancel_, &QPushButton::clicked, &controller_, &ArchiveController::cancel);
    return page;
}

QWidget* ArchiveWindow::makePasswordsPage() {
    QVBoxLayout* layout; auto* page = Page(layout, "passwordsPage");
    auto* toolbar = new QHBoxLayout;
    auto* import = Button(QStringLiteral("＋ 导入文件"), "importPasswords");
    auto* add = Button(QStringLiteral("手动添加"), "addPasswords");
    toolbar->addWidget(import); toolbar->addWidget(add); toolbar->addStretch(); layout->addLayout(toolbar);
    passwordSummary_ = Label({}, true); passwordSummary_->setObjectName(QStringLiteral("passwordSummary"));
    layout->addWidget(passwordSummary_);
    passwordError_ = Label({}); passwordError_->setWordWrap(true); passwordError_->setStyleSheet(QStringLiteral("color: #f08078;"));
    passwordError_->hide(); layout->addWidget(passwordError_);

    auto* listCard = new QFrame; listCard->setProperty("statModule", true);
    auto* listLayout = new QVBoxLayout(listCard); listLayout->setContentsMargins(1, 1, 1, 1);
    passwordQueue_ = new QStackedWidget; passwordQueue_->setMinimumHeight(compact_ ? 132 : 164);
    passwordQueue_->addWidget(Empty(QStringLiteral("还没有密码单"), QStringLiteral("导入文本文件或手动添加，每行一个密码")));
    passwordLists_ = new RoundedArchiveList; passwordLists_->setObjectName(QStringLiteral("passwordLists"));
    passwordLists_->setFrameShape(QFrame::NoFrame); passwordLists_->setItemDelegate(new ArchiveDelegate(passwordLists_, true));
    passwordLists_->setUniformItemSizes(true); passwordLists_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    passwordQueue_->addWidget(passwordLists_); listLayout->addWidget(passwordQueue_); layout->addWidget(listCard);
    passwordSourceRow_ = new QWidget;
    auto* sourceLayout = new QHBoxLayout(passwordSourceRow_); sourceLayout->setContentsMargins(0, 0, 0, 0); sourceLayout->setSpacing(10);
    sourceLayout->addWidget(Label(QStringLiteral("来源密码单"), true));
    passwordSource_ = new QComboBox; passwordSource_->setObjectName("passwordListSource"); passwordSource_->setAccessibleName(QStringLiteral("选择要管理的来源密码单"));
    sourceLayout->addWidget(passwordSource_, 1); layout->addWidget(passwordSourceRow_); passwordSourceRow_->hide();
    auto* actions = new QHBoxLayout;
    editPasswordList_ = Button(QStringLiteral("编辑密码单…"), "editPasswords");
    togglePasswordList_ = Button(QStringLiteral("停用密码单"), "togglePasswords");
    removePasswordList_ = Button(QStringLiteral("移除密码单"), "removePasswords");
    actions->addWidget(editPasswordList_); actions->addWidget(togglePasswordList_); actions->addStretch(); actions->addWidget(removePasswordList_);
    layout->addLayout(actions);

    auto* options = Card();
    autoPasswords_ = new desktop::ToggleSwitch; autoPasswords_->setObjectName(QStringLiteral("autoPasswords"));
    autoPasswords_->setAccessibleName(QStringLiteral("自动使用密码单"));
    Row(options, QStringLiteral("自动使用密码单"), QStringLiteral("解压和检测时尝试已启用的密码单，手动密码优先"), autoPasswords_, true);
    layout->addWidget(options);
    auto* note = Label(QStringLiteral("支持 UTF-8、UTF-16 和 GB18030 文本。自动去重，保留密码中的空格。\n密码单由当前 Windows 账户加密保存，原始导入文件不会被修改。"), true);
    note->setWordWrap(true); layout->addWidget(note); layout->addStretch();
    editable_ << import << add << options;
    connect(import, &QPushButton::clicked, this, &ArchiveWindow::importPasswords);
    connect(add, &QPushButton::clicked, this, [this] { editPasswords(false); });
    connect(editPasswordList_, &QPushButton::clicked, this, [this] { editPasswords(true); });
    connect(passwordLists_, &QListWidget::itemDoubleClicked, this, [this] { editPasswords(true); });
    connect(passwordLists_, &QListWidget::currentRowChanged, this, &ArchiveWindow::syncPasswordSources);
    connect(passwordSource_, &QComboBox::currentIndexChanged, this, &ArchiveWindow::syncPasswordActions);
    connect(togglePasswordList_, &QPushButton::clicked, this, [this] {
        const auto id = selectedPasswordList();
        for (const auto& list : controller_.passwordLibrary().lists()) if (list.id == id) {
            controller_.setPasswordListEnabled(id, !list.enabled); break;
        }
    });
    connect(removePasswordList_, &QPushButton::clicked, this, [this] { controller_.removePasswordList(selectedPasswordList()); });
    connect(autoPasswords_, &QAbstractButton::toggled, this, [this](bool enabled) {
        controller_.autoPasswords = enabled; controller_.savePreferences(); controller_.notifyOptions();
    });
    return page;
}

QString ArchiveWindow::selectedPasswordList() const {
    return passwordSource_ ? passwordSource_->currentData().toString() : QString{};
}
void ArchiveWindow::syncPasswordSources() {
    const auto previous = selectedPasswordList();
    const auto ids = passwordLists_->currentItem() ? passwordLists_->currentItem()->data(ListIds).toStringList() : QStringList{};
    const QSet<QString> sourceIds(ids.cbegin(), ids.cend());
    {
        const QSignalBlocker blocker(passwordSource_); passwordSource_->clear();
        int firstEnabled = -1;
        for (const auto& list : controller_.passwordLibrary().lists()) if (sourceIds.contains(list.id)) {
            passwordSource_->addItem(list.name + (list.enabled ? QStringLiteral(" · 已启用") : QStringLiteral(" · 已停用")), list.id);
            if (list.enabled && firstEnabled < 0) firstEnabled = passwordSource_->count() - 1;
        }
        const int retained = passwordSource_->findData(previous);
        if (retained >= 0) passwordSource_->setCurrentIndex(retained);
        else if (firstEnabled >= 0) passwordSource_->setCurrentIndex(firstEnabled);
    }
    passwordSourceRow_->setVisible(ids.size() > 1);
    syncPasswordActions();
}
void ArchiveWindow::syncPasswordActions() {
    const auto id = selectedPasswordList();
    const auto& lists = controller_.passwordLibrary().lists();
    auto selected = std::find_if(lists.begin(), lists.end(), [&](const auto& list) { return list.id == id; });
    const bool enabled = !controller_.busy() && selected != lists.end();
    editPasswordList_->setEnabled(enabled); togglePasswordList_->setEnabled(enabled); removePasswordList_->setEnabled(enabled);
    togglePasswordList_->setText(selected != lists.end() && !selected->enabled ? QStringLiteral("启用密码单") : QStringLiteral("停用密码单"));
    togglePasswordList_->setToolTip(QStringLiteral("仅修改所选来源密码单；其他已启用来源仍可提供相同密码"));
}
void ArchiveWindow::refreshPasswords() {
    const auto id = selectedPasswordList();
    const auto selectedValue = passwordLists_->currentItem() ? passwordLists_->currentItem()->text() : QString{};
    passwordLists_->clear();
    int selectedByValue = -1, selectedByList = -1;
    const auto& library = controller_.passwordLibrary();
    for (const auto& entry : library.uniquePasswords()) {
        QStringList ids, names, notes, descriptions;
        QSet<QString> seenNames, seenNotes;
        for (const auto& source : entry.sources) {
            ids.append(source.listId);
            if (!seenNames.contains(source.name)) { seenNames.insert(source.name); names.append(source.name); }
            if (!source.comment.isEmpty() && !seenNotes.contains(source.comment)) { seenNotes.insert(source.comment); notes.append(source.comment); }
            descriptions.append(QStringLiteral("来源：%1 · %2%3").arg(source.name,
                source.enabled ? QStringLiteral("已启用") : QStringLiteral("已停用"), source.comment.isEmpty() ? QString{} : '\n' + source.comment));
        }
        const auto description = (notes.isEmpty() ? QString{} : notes.join(QStringLiteral("；")) + QStringLiteral(" · ")) +
            QStringLiteral("来源：%1 · %2").arg(names.join(QStringLiteral("、")), entry.enabled ? QStringLiteral("已启用") : QStringLiteral("已停用"));
        AddItem(passwordLists_, entry.value, description, {},
                entry.enabled ? QColor(theme::Success) : theme::MutedColor(this), {});
        const int row = passwordLists_->count() - 1; auto* item = passwordLists_->item(row);
        const auto complete = entry.value + '\n' + descriptions.join('\n');
        item->setData(ListIds, ids); item->setData(SearchText, complete);
        item->setToolTip(QStringLiteral("<pre>%1</pre>").arg(complete.toHtmlEscaped()));
        if (ids.contains(id) && selectedByList < 0) selectedByList = row;
        if (entry.value == selectedValue) selectedByValue = row;
    }
    int selected = selectedByValue >= 0 ? selectedByValue : selectedByList;
    if (selected < 0 && passwordLists_->count()) selected = 0;
    passwordLists_->setCurrentRow(selected);
    const int previousSource = passwordSource_->findData(id);
    if (previousSource >= 0) passwordSource_->setCurrentIndex(previousSource);
    passwordQueue_->setCurrentIndex(passwordLists_->count() ? 1 : 0);
    const int height = passwordLists_->count() ? std::min(passwordLists_->count(), compact_ ? 3 : 5) * PasswordRowHeight : compact_ ? 132 : 164;
    passwordQueue_->setFixedHeight(height);
    passwordQueue_->parentWidget()->setFixedHeight(height + 2);
    passwordSummary_->setText(QStringLiteral("共 %1 个密码 · %2 份密码单 · 已启用 %3 个").arg(library.uniquePasswords().size()).arg(library.lists().size()).arg(library.enabledPasswords().size()));
    passwordError_->setText(controller_.passwordLibraryError()); passwordError_->setVisible(!controller_.passwordLibraryError().isEmpty());
    syncPasswordActions(); if (page_ == "passwords") filterSettings();
}
void ArchiveWindow::importPasswords() {
    if (controller_.busy()) return;
    QScopedValueRollback<bool> modal(dialogOpen_, true);
    const auto files = QFileDialog::getOpenFileNames(this, QStringLiteral("导入密码单"), {}, QStringLiteral("密码单文本 (*.txt *.lst *.list *.dic);;所有文件 (*)"));
    if (files.isEmpty()) return;
    int imported = 0; QString lastError;
    for (const auto& file : files) {
        if (controller_.importPasswordList(file)) ++imported;
        else lastError = controller_.status();
    }
    controller_.setStatus(lastError.isEmpty() ? QStringLiteral("已导入 %1 份密码单").arg(imported)
        : QStringLiteral("已导入 %1 份，%2 份失败：%3").arg(imported).arg(files.size() - imported).arg(lastError));
    if (compact_) { raise(); activateWindow(); }
}
void ArchiveWindow::editPasswords(bool existing) {
    if (controller_.busy()) return;
    const auto id = existing ? selectedPasswordList() : QString{};
    QString name; QStringList values; QHash<QString, QString> notes;
    if (existing) {
        const auto& lists = controller_.passwordLibrary().lists();
        auto list = std::find_if(lists.begin(), lists.end(), [&](const auto& item) { return item.id == id; });
        if (list == lists.end()) return;
        name = list->name; values = list->entries; notes = list->comments;
    }
    QScopedValueRollback<bool> modal(dialogOpen_, true);
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("passwordListEditor"));
    dialog.setWindowTitle(existing ? QStringLiteral("编辑密码单") : QStringLiteral("添加密码单"));
    dialog.setWindowModality(Qt::ApplicationModal); dialog.resize(600, 460);
    auto* layout = new QVBoxLayout(&dialog); layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(12);
    layout->addWidget(Label(QStringLiteral("名称"), false, true));
    auto* title = Edit("passwordListName", QStringLiteral("例如：常用密码")); title->setMaxLength(120); title->setText(name); layout->addWidget(title);
    auto* hint = Label(QStringLiteral("密码直接显示；可填写用途、来源等注释。保留密码中的空格，空行会忽略。"), true);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* entries = new QTableWidget(std::max(1, static_cast<int>(values.size())), 2); entries->setObjectName(QStringLiteral("passwordListEntries"));
    entries->setAccessibleName(QStringLiteral("密码和注释")); entries->setHorizontalHeaderLabels({QStringLiteral("密码"), QStringLiteral("注释")});
    entries->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); entries->verticalHeader()->setDefaultSectionSize(34);
    entries->setSelectionBehavior(QAbstractItemView::SelectRows); entries->setShowGrid(false);
    for (int row = 0; row < entries->rowCount(); ++row) {
        entries->setItem(row, 0, new QTableWidgetItem(values.value(row)));
        entries->setItem(row, 1, new QTableWidgetItem(notes.value(values.value(row))));
    }
    layout->addWidget(entries, 1);
    auto* rowActions = new QHBoxLayout;
    auto* addRow = Button(QStringLiteral("添加一行"), "addPasswordRow"); auto* removeRow = Button(QStringLiteral("删除所选行"), "removePasswordRow");
    rowActions->addWidget(addRow); rowActions->addWidget(removeRow); rowActions->addStretch(); layout->addLayout(rowActions);
    connect(addRow, &QPushButton::clicked, &dialog, [entries] {
        const auto row = entries->rowCount(); entries->insertRow(row);
        entries->setItem(row, 0, new QTableWidgetItem); entries->setItem(row, 1, new QTableWidgetItem); entries->setCurrentCell(row, 0); entries->editItem(entries->item(row, 0));
    });
    connect(removeRow, &QPushButton::clicked, &dialog, [entries] {
        const auto selected = entries->selectionModel()->selectedRows();
        QList<int> rows; for (const auto& row : selected) rows.append(row.row()); std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (const auto row : rows) entries->removeRow(row);
    });
    auto* error = Label({}); error->setWordWrap(true); error->setStyleSheet(QStringLiteral("color: #f08078;")); error->hide(); layout->addWidget(error);
    auto* buttons = new QHBoxLayout; buttons->addStretch();
    auto* cancel = Button(QStringLiteral("取消"), "cancelPasswordEditor");
    auto* save = Button(QStringLiteral("保存"), "savePasswordEditor", true); save->setDefault(true);
    buttons->addWidget(cancel); buttons->addWidget(save); layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, [&] {
        QStringList passwords; QHash<QString, QString> comments;
        for (int row = 0; row < entries->rowCount(); ++row) {
            const auto value = entries->item(row, 0) ? entries->item(row, 0)->text() : QString{};
            const auto note = entries->item(row, 1) ? entries->item(row, 1)->text() : QString{};
            if (value.isEmpty() && note.isEmpty()) continue;
            if (value.isEmpty() || value.contains('\n') || value.contains('\r') || (comments.contains(value) && comments.value(value) != note)) {
                error->setText(QStringLiteral("请检查空密码、换行或注释不同的重复密码")); error->show(); return;
            }
            passwords.append(value); comments.insert(value, note);
        }
        const bool ok = existing ? controller_.editPasswordList(id, title->text(), passwords.join('\n'), &comments)
                                 : controller_.addPasswordList(title->text(), passwords.join('\n'), &comments);
        if (ok) dialog.accept(); else { error->setText(controller_.status()); error->show(); }
    });
    if (QGuiApplication::platformName() == "windows") {
        BOOL dark = TRUE; auto handle = reinterpret_cast<HWND>(dialog.winId()); DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
    }
    dialog.exec();
    if (compact_) { raise(); activateWindow(); }
}

QWidget* ArchiveWindow::makeSettingsPage(bool about) {
    QVBoxLayout* layout; auto* page = Page(layout, about ? "aboutPage" : "settingsPage");
    auto* card = Card();
    if (about) {
        Row(card, QStringLiteral("7-Zip Context"), QStringLiteral("压缩、解压与托盘工作台"), Label('v' + controller_.updates().currentVersion(), true));
        Row(card, QStringLiteral("压缩引擎"), QStringLiteral("本地处理文件，无需上传"), Label(QStringLiteral("7-Zip"), true));
        Row(card, QStringLiteral("支持压缩"), QStringLiteral("密码加密和自定义分卷"), Label(QStringLiteral("7z / ZIP"), true));
        Row(card, QStringLiteral("智能识别"), QStringLiteral("读取文件头识别格式，定位首卷并纠正扩展名"), Label(QStringLiteral("已启用"), true));
        Row(card, QStringLiteral("完整性检测"), QStringLiteral("报告密码、损坏与缺卷问题；无法重建丢失的数据"), Label(QStringLiteral("CRC 校验"), true), true);
    } else {
        Row(card, QStringLiteral("外观"), QStringLiteral("炭灰色背景、列表卡片与分段控件"), Label(QStringLiteral("深色"), true));
        hideOnBlur_ = new desktop::ToggleSwitch; hideOnBlur_->setObjectName(QStringLiteral("hideOnBlur"));
        hideOnBlur_->setAccessibleName(QStringLiteral("托盘弹窗自动收起"));
        Row(card, QStringLiteral("托盘弹窗自动收起"), QStringLiteral("默认保持显示；开启后点击外部自动收起"), hideOnBlur_);
        rememberGeometry_ = new desktop::ToggleSwitch; rememberGeometry_->setObjectName(QStringLiteral("rememberGeometry"));
        rememberGeometry_->setAccessibleName(QStringLiteral("记住窗口尺寸"));
        Row(card, QStringLiteral("记住窗口尺寸"), QStringLiteral("下次启动恢复主窗口的大小和位置"), rememberGeometry_);
        auto* passwords = Button(QStringLiteral("管理…"), "managePasswordLists");
        Row(card, QStringLiteral("密码单"), QStringLiteral("密码单按 Windows 账户加密保存；手动密码不保存"), passwords, true);
        connect(passwords, &QPushButton::clicked, this, [this] { navigate(QStringLiteral("passwords")); });
        connect(hideOnBlur_, &QAbstractButton::toggled, this, [this](bool enabled) { controller_.hideOnBlur = enabled; controller_.savePreferences(); controller_.notifyOptions(); });
        connect(rememberGeometry_, &QAbstractButton::toggled, this, [this](bool enabled) { controller_.rememberGeometry = enabled; controller_.savePreferences(); controller_.notifyOptions(); });
    }
    layout->addWidget(card);
    if (about) {
        auto* updates = Card(); auto* body = static_cast<QVBoxLayout*>(updates->layout());
        updates->setProperty("searchableCard", true); updates->setProperty("searchText", QStringLiteral("在线更新 检查更新 下载更新 安装并重启 启动时自动检查更新"));
        body->setContentsMargins(16, 14, 16, 14); body->setSpacing(12);
        body->addWidget(Label(QStringLiteral("在线更新"), false, true));
        updateStatus_ = Label({}, true); updateStatus_->setObjectName("updateStatus"); updateStatus_->setWordWrap(true); body->addWidget(updateStatus_);
        auto* buttons = new QHBoxLayout;
        checkUpdate_ = Button(QStringLiteral("检查更新"), "checkUpdate"); downloadUpdate_ = Button(QStringLiteral("下载更新"), "downloadUpdate");
        installUpdate_ = Button(QStringLiteral("安装并重启"), "installUpdate", true); cancelUpdate_ = Button(QStringLiteral("取消"), "cancelUpdate");
        buttons->addWidget(checkUpdate_); buttons->addWidget(downloadUpdate_); buttons->addWidget(installUpdate_); buttons->addWidget(cancelUpdate_); buttons->addStretch(); body->addLayout(buttons);
        updateNotes_ = Label({}, true); updateNotes_->setObjectName("updateNotes"); updateNotes_->setTextFormat(Qt::PlainText); updateNotes_->setWordWrap(true); body->addWidget(updateNotes_);
        auto* automatic = new QHBoxLayout; automatic->addWidget(Label(QStringLiteral("启动时自动检查更新"))); automatic->addStretch();
        autoUpdate_ = new desktop::ToggleSwitch; autoUpdate_->setObjectName("autoCheckUpdates"); autoUpdate_->setAccessibleName(QStringLiteral("启动时自动检查更新")); automatic->addWidget(autoUpdate_); body->addLayout(automatic);
        layout->addWidget(updates);
        connect(checkUpdate_, &QPushButton::clicked, &controller_.updates(), &UpdateManager::check);
        connect(downloadUpdate_, &QPushButton::clicked, &controller_.updates(), &UpdateManager::download);
        connect(cancelUpdate_, &QPushButton::clicked, &controller_.updates(), &UpdateManager::cancel);
        connect(installUpdate_, &QPushButton::clicked, &controller_, &ArchiveController::installUpdate);
        connect(autoUpdate_, &QAbstractButton::toggled, &controller_.updates(), &UpdateManager::setAutoCheck);
    }
    if (!about) {
        auto* actions = new QHBoxLayout; auto* note = Label(QStringLiteral("关闭主窗口后保留托盘"), true); note->setWordWrap(true);
        actions->addWidget(note, 1); auto* quit = Button(QStringLiteral("退出应用"), "quitApplication"); actions->addWidget(quit); layout->addLayout(actions);
        connect(quit, &QPushButton::clicked, &controller_, &ArchiveController::requestQuit);
    }
    auto* empty = Label(QStringLiteral("没有匹配的设置"), true);
    empty->setObjectName(QStringLiteral("settingsNoResults")); empty->hide(); layout->addWidget(empty);
    layout->addStretch(); return page;
}

void ArchiveWindow::navigate(const QString& requested) {
    const QString page = requested == "settings" ? QStringLiteral("general") : requested;
    const QStringList pages{"extract", "compress", "tasks", "general", "about", "passwords"};
    const int index = pages.indexOf(page); if (index < 0) return;
    if ((page_ == "passwords") != (page == "passwords")) shell_->search->clear();
    page_ = page; shell_->Select(page); shell_->pages->setCurrentIndex(index);
    shell_->search->setPlaceholderText(page == "passwords" ? QStringLiteral("查找密码和注释") : QStringLiteral("查找设置"));
    status_->setVisible(controller_.busy() || (page != "general" && page != "about"));
    filterSettings();
}
QWidget* ArchiveWindow::pageWidget(int index) const { return shell_->pages->widget(index); }
void ArchiveWindow::pickFiles(bool folder) {
    if (controller_.busy()) return;
    QScopedValueRollback<bool> modal(dialogOpen_, true);
    QStringList paths;
    if (folder) {
        auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("添加文件夹")); if (!path.isEmpty()) paths.append(path);
    } else paths = QFileDialog::getOpenFileNames(this, page_ == "compress" ? QStringLiteral("添加文件") : QStringLiteral("添加文件 · 自动识别并整理"), {}, QStringLiteral("所有文件 (*)"));
    if (paths.isEmpty()) return;
    controller_.queuePaths(paths, page_ == "compress");
    if (compact_) { raise(); activateWindow(); }
}
void ArchiveWindow::pickDestination(bool compression) {
    QScopedValueRollback<bool> modal(dialogOpen_, true);
    auto& destination = compression ? controller_.compressDestination : controller_.extractDestination;
    auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择保存位置"), destination);
    if (!path.isEmpty()) { destination = QDir::toNativeSeparators(path); controller_.notifyOptions(); }
    if (compact_) { raise(); activateWindow(); }
}
void ArchiveWindow::refreshQueues() {
    const int archiveRow = archives_->currentRow(), sourceRow = sources_->currentRow();
    archives_->clear(); sources_->clear();
    for (const auto& plan : controller_.archives) {
        QString format = plan.format == L"rar5" ? QStringLiteral("rar") : Q(plan.format);
        QString detail; QColor ink = theme::MutedColor(this);
        if (controller_.associatedInputFiles.contains(Q(plan.input), Qt::CaseInsensitive)) { detail = QStringLiteral("已关联为内层 ZIP 分卷，随主包统一处理"); format = QStringLiteral("卷"); }
        else if (!plan.error.empty()) { detail = Q(plan.error); ink = QColor(theme::Error); }
        else {
            detail = Q(FormatBytes(plan.size)) + " · " + (plan.split ? QStringLiteral("%1 卷 · 已归组").arg(plan.volumes.size()) : QStringLiteral("已识别 %1").arg(format));
            if (plan.needsRename) { detail += QStringLiteral(" · 建议修正为 .%1").arg(format); ink = QColor(theme::Amber); }
            else detail += QStringLiteral(" · 待校验");
        }
        AddItem(archives_, QFileInfo(Q(plan.input)).fileName(), detail, format.isEmpty() ? QStringLiteral("?") : format, ink, Q(plan.input));
    }
    for (const auto& group : controller_.mediaInputs) {
        AddItem(archives_, group.name, QStringLiteral("%1 个媒体文件 · 复制、校验并分类").arg(group.files.size()), QStringLiteral("媒体"), theme::MutedColor(this), group.directory);
    }
    for (const auto& path : controller_.sources) {
        QFileInfo file(path);
        AddItem(sources_, file.fileName(), QDir::toNativeSeparators(file.absolutePath()), file.isDir() ? QStringLiteral("目录") : QStringLiteral("文件"), theme::MutedColor(this), path);
    }
    archives_->setCurrentRow(std::min(archiveRow, archives_->count() - 1)); sources_->setCurrentRow(std::min(sourceRow, sources_->count() - 1));
    archiveQueue_->setCurrentIndex(archives_->count() ? 1 : 0); sourceQueue_->setCurrentIndex(sources_->count() ? 1 : 0);
    auto queueHeight = [this](int count) { return count ? std::min(count, compact_ ? 3 : 4) * 66 : compact_ ? 132 : 164; };
    archiveQueue_->setFixedHeight(queueHeight(archives_->count())); sourceQueue_->setFixedHeight(queueHeight(sources_->count()));
    archiveCount_->setText(QStringLiteral("压缩包 %1 组 · 媒体 %2 组 · 辅助文件 %3 个")
        .arg(controller_.archives.size()).arg(controller_.mediaInputs.size()).arg(controller_.ignoredInputFiles.size()));
    sourceCount_->setText(QStringLiteral("文件 · %1 个输入项目").arg(sources_->count()));
}
void ArchiveWindow::refreshTasks() {
    const int row = tasks_->currentRow(); tasks_->clear();
    for (const auto& task : controller_.tasks) AddItem(tasks_, task.name, task.detail, task.success ? QStringLiteral("✓") : QStringLiteral("!"), QColor(task.success ? theme::Success : theme::Error), task.output);
    tasks_->setCurrentRow(std::min(row, tasks_->count() - 1)); taskQueue_->setCurrentIndex(tasks_->count() ? 1 : 0);
    taskQueue_->setFixedHeight(tasks_->count() ? std::min(tasks_->count(), compact_ ? 3 : 4) * 66 : compact_ ? 126 : 154);
    taskCount_->setText(QStringLiteral("本次会话 · %1 条记录").arg(tasks_->count()));
}
void ArchiveWindow::syncOptions() {
    QScopedValueRollback<bool> syncing(synchronizing_, true);
    auto text = [](QLineEdit* edit, const QString& value) { if (edit->text() != value) { QSignalBlocker block(edit); edit->setText(value); } };
    text(extractDestination_, controller_.extractDestination); text(compressDestination_, controller_.compressDestination);
    text(extractPassword_, controller_.extractPassword); text(compressPassword_, controller_.compressPassword); text(archiveName_, controller_.archiveName);
    const QString volume = controller_.volumeSize.isEmpty() ? QStringLiteral("不分卷") : controller_.volumeSize;
    if (volume_->currentText() != volume) { QSignalBlocker block(volume_); volume_->setCurrentText(volume); }
    auto segment = [](desktop::SegmentedTabBar* tabs, int index) { if (tabs->currentIndex() != index) tabs->setCurrentIndex(index); };
    segment(format_, controller_.compression.format == L"zip" ? 1 : 0);
    segment(level_, controller_.compression.level == 1 ? 0 : controller_.compression.level == 9 ? 2 : 1);
    segment(extractMode_, controller_.subfolder || controller_.organizeMedia ? 0 : 1);
    extractMode_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    hideOnBlur_->SyncChecked(controller_.hideOnBlur);
    rememberGeometry_->SyncChecked(controller_.rememberGeometry);
    autoPasswords_->SyncChecked(controller_.autoPasswords);
    autoNested_->SyncChecked(controller_.autoNested || controller_.organizeMedia);
    autoNested_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    correctName_->setEnabled(!controller_.busy() && archives_->currentRow() >= 0 && static_cast<size_t>(archives_->currentRow()) < controller_.archives.size());
    organizeMedia_->SyncChecked(controller_.organizeMedia);
}
void ArchiveWindow::syncBusy() {
    for (auto* control : editable_) control->setEnabled(!controller_.busy());
    extractMode_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    autoNested_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    correctName_->setEnabled(!controller_.busy() && archives_->currentRow() >= 0 && static_cast<size_t>(archives_->currentRow()) < controller_.archives.size());
    shell_->refresh->setEnabled(!controller_.busy()); syncProgress();
    syncPasswordActions();
    syncUpdates();
    status_->setVisible(controller_.busy() || (page_ != "general" && page_ != "about"));
}
void ArchiveWindow::syncUpdates() {
    if (!updateStatus_) return;
    auto& update = controller_.updates();
    updateStatus_->setText(update.status()); updateNotes_->setText(update.release().notes); updateNotes_->setVisible(!update.release().notes.isEmpty());
    checkUpdate_->setEnabled(!update.busy());
    downloadUpdate_->setVisible(update.state() == UpdateManager::State::Available);
    installUpdate_->setVisible(update.state() == UpdateManager::State::Ready);
    installUpdate_->setEnabled(!controller_.busy());
    installUpdate_->setToolTip(controller_.busy() ? QStringLiteral("当前任务结束后可以安装更新") : QStringLiteral("安装时关闭程序，完成后重新启动"));
    cancelUpdate_->setVisible(update.state() == UpdateManager::State::Checking || update.state() == UpdateManager::State::Downloading);
    autoUpdate_->SyncChecked(update.autoCheck());
}
void ArchiveWindow::syncProgress() {
    const auto total = controller_.total(), completed = controller_.completed();
    const double fraction = total ? std::min(1.0, static_cast<double>(completed) / total) : 0;
    jobs_->setText(QStringLiteral("%1 / %2").arg(controller_.jobIndex()).arg(controller_.jobCount()));
    processed_->setText(Q(FormatBytes(completed))); speed_->setText(Q(FormatBytes(static_cast<uint64_t>(controller_.speed()))) + "/s");
    percent_->setText(QString::number(fraction * 100, 'f', 0) + '%');
    progress_->setValue(static_cast<int>(fraction * 1000));
    taskState_->setText(controller_.busy() ? controller_.paused() ? QStringLiteral("任务已暂停") : controller_.cancelling() ? QStringLiteral("正在取消…")
        : controller_.scanningInputs() ? QStringLiteral("正在扫描文件和目录")
        : controller_.matchingPassword() ? QStringLiteral("匹配密码 · %1 / %2").arg(controller_.passwordAttempt()).arg(controller_.passwordTotal())
        : controller_.cleaningIntermediates() ? QStringLiteral("正在清理中间包")
        : controller_.organizingMedia() ? QStringLiteral("正在整理视频和图片") : QStringLiteral("正在处理")
        : controller_.tasks.isEmpty() ? QStringLiteral("等待任务") : QStringLiteral("任务结束"));
    if (controller_.busy() && controller_.extractionDepth() && !controller_.cleaningIntermediates() && !controller_.organizingMedia()) taskState_->setText(taskState_->text() + QStringLiteral(" · 第 %1 层").arg(controller_.extractionDepth()));
    pause_->setText(controller_.paused() ? QStringLiteral("继续") : QStringLiteral("暂停"));
    pause_->setEnabled(controller_.busy() && !controller_.scanningInputs() && !controller_.cancelling()); cancel_->setEnabled(controller_.busy() && !controller_.cancelling());
}
void ArchiveWindow::filterSettings() {
    if (page_ == "passwords") {
        const auto query = shell_->search->text().trimmed();
        int firstVisible = -1;
        for (int i = 0; i < passwordLists_->count(); ++i) {
            const bool matches = passwordLists_->item(i)->data(SearchText).toString().contains(query, Qt::CaseInsensitive);
            passwordLists_->item(i)->setHidden(!matches);
            if (matches && firstVisible < 0) firstVisible = i;
        }
        if (!passwordLists_->currentItem() || passwordLists_->currentItem()->isHidden()) passwordLists_->setCurrentRow(firstVisible);
        return;
    }
    if (page_ != "general" && page_ != "about") return;
    const auto query = shell_->search->text().trimmed();
    auto* page = shell_->pages->currentWidget();
    int matches = 0;
    for (auto* row : page->findChildren<QFrame*>()) {
        if (!row->property("settingsRow").toBool() && !row->property("searchableCard").toBool()) continue;
        const bool visible = query.isEmpty() || row->property("searchText").toString().contains(query, Qt::CaseInsensitive);
        row->setVisible(visible); if (visible) ++matches;
    }
    if (auto* empty = page->findChild<QLabel*>("settingsNoResults")) empty->setVisible(matches == 0);
}
void ArchiveWindow::run(ArchiveController::Operation operation) {
    if (controller_.start(operation)) navigate(QStringLiteral("tasks"));
}
void ArchiveWindow::openOutput() {
    const int index = tasks_->currentRow();
    if (index < 0 || index >= controller_.tasks.size() || !QFileInfo(controller_.tasks[index].output).isDir()) {
        controller_.setStatus(QStringLiteral("请选择有输出目录的任务")); return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(controller_.tasks[index].output));
}
void ArchiveWindow::closeEvent(QCloseEvent* event) {
    if (!compact_) controller_.saveWindowGeometry(saveGeometry());
    if (compact_ || QSystemTrayIcon::isSystemTrayAvailable()) { event->ignore(); hide(); }
    else { event->ignore(); controller_.requestQuit(); }
}
void ArchiveWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!controller_.busy() && event->mimeData()->hasUrls()) {
        for (const auto& url : event->mimeData()->urls()) if (url.isLocalFile()) { event->acceptProposedAction(); break; }
    }
}
void ArchiveWindow::dropEvent(QDropEvent* event) {
    if (controller_.busy()) { event->ignore(); return; }
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) if (url.isLocalFile()) paths.append(url.toLocalFile());
    if (paths.isEmpty()) { event->ignore(); return; }
    if (page_ != "compress") navigate(QStringLiteral("extract"));
    controller_.queuePaths(paths, page_ == "compress");
    event->acceptProposedAction();
}
bool ArchiveWindow::event(QEvent* event) {
    if (compact_ && event->type() == QEvent::WindowDeactivate && controller_.hideOnBlur && !dialogOpen_) {
        QTimer::singleShot(0, this, [this] {
            auto* childPopup = QApplication::activePopupWidget();
            if (childPopup && isAncestorOf(childPopup)) return;
            if (controller_.hideOnBlur && !dialogOpen_ && !isActiveWindow() && !QApplication::activeModalWidget()) { dismissedAt_ = QDateTime::currentMSecsSinceEpoch(); hide(); }
        });
    }
    return QWidget::event(event);
}
void ArchiveWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    alignPages();
    if (QGuiApplication::platformName() != "windows") return;
    BOOL dark = TRUE; DWORD corners = 2; COLORREF color = RGB(26, 26, 30);
    auto handle = reinterpret_cast<HWND>(winId());
    DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark)); DwmSetWindowAttribute(handle, 33, &corners, sizeof(corners));
    DwmSetWindowAttribute(handle, 35, &color, sizeof(color));
}
void ArchiveWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    alignPages();
}
void ArchiveWindow::alignPages() {
    if (!shell_) return;
    const int inset = desktop::metrics::ContentInset(width(), compact_);
    for (int index = 0; index < shell_->pages->count(); ++index) {
        auto* scroll = qobject_cast<QScrollArea*>(shell_->pages->widget(index));
        if (!scroll) continue;
        const int gutter = scroll->verticalScrollBar()->sizeHint().width();
        scroll->widget()->layout()->setContentsMargins(inset, 2, std::max(0, inset - gutter), compact_ ? 8 : 12);
    }
    if (status_) status_->setContentsMargins(inset, 6, inset, 10);
}
void ArchiveWindow::toggleAtTray(const QRect& trayGeometry) {
    if (isVisible()) { hide(); return; }
    if (QDateTime::currentMSecsSinceEpoch() - dismissedAt_ < 250) return;
    const QPoint anchor = trayGeometry.isValid() ? trayGeometry.center() : QCursor::pos();
    auto* screen = QGuiApplication::screenAt(anchor); if (!screen) screen = QGuiApplication::primaryScreen();
    const auto area = screen->availableGeometry().adjusted(8, 8, -8, -8);
    resize(std::min(480, area.width()), std::min(700, area.height()));
    move(std::clamp(anchor.x() - width() / 2, area.left(), std::max(area.left(), area.right() - width() + 1)),
        std::clamp(anchor.y() - height() - 8, area.top(), std::max(area.top(), area.bottom() - height() + 1)));
    show(); raise(); activateWindow();
}
