#include "ArchiveWindow.h"
#include "theme/dark_style.h"
#include "theme/magpie_shell.h"
#include "theme/segmented_tabs.h"
#include "theme/typography.h"
#include "theme/layout_metrics.h"
#include <QApplication>
#include <QCloseEvent>
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
#include <QStyleOptionComboBox>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>
#include <dwmapi.h>
#include <cmath>

namespace theme = desktop::theme;
namespace {
QString Q(const std::wstring& text) { return QString::fromStdWString(text); }
enum ItemRole { Detail = Qt::UserRole + 1, Badge, Ink, Path, PasswordValue, SearchText };
constexpr int PasswordRowHeight = 36;
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
class ComboBox final : public QComboBox {
protected:
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        QStyleOptionComboBox option; initStyleOption(&option);
        const auto arrow = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
        const QPointF center = QRectF(arrow).center();
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(isEnabled() ? theme::MutedColor(this) : QColor("#56565f"), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{center + QPointF(-2.5, -1), center + QPointF(0, 1.5), center + QPointF(2.5, -1)});
    }
};
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
QFrame* Row(QFrame* card, const QString& title, const QString& description, QWidget* control, bool last = false) {
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
    auto* field = qobject_cast<QLineEdit*>(control);
    if (!field) field = control->findChild<QLineEdit*>();
    name->setBuddy(field ? field : control);
    if (field) field->setAccessibleName(title);
    control->setAccessibleName(title);
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
    return row;
}
QWidget* WithBrowse(QLineEdit* edit, QPushButton* browse) {
    auto* widget = new QWidget;
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8); row->addWidget(edit, 1); row->addWidget(browse);
    return widget;
}
QWidget* Empty(const QString& title) {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(18, 20, 18, 20);
    layout->setSpacing(7); layout->addStretch();
    auto* headline = Label(title, false, true); headline->setAlignment(Qt::AlignCenter);
    layout->addWidget(headline); layout->addStretch();
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
        if (index.row() + 1 < index.model()->rowCount()) {
            painter->setPen(theme::DividerColor(option.widget));
            painter->drawLine(option.rect.bottomLeft() + QPoint(15, 0), option.rect.bottomRight() - QPoint(15, 0));
        }
        if ((option.state & QStyle::State_HasFocus) && (option.state & QStyle::State_KeyboardFocusChange)) {
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
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 44, 48, 50, 64, 128, 150, 256}) {
        QPixmap image(size, size); image.fill(Qt::transparent);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 64.0, size / 64.0);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(theme::Text)); painter.drawRoundedRect(QRectF(5, 5, 54, 54), 10, 10);
        QFont font(QStringLiteral("Microsoft YaHei UI")); font.setPixelSize(42); font.setBold(true); painter.setFont(font);
        painter.setPen(QColor(theme::Canvas)); painter.drawText(QRectF(0, 0, 64, 64), Qt::AlignCenter, QStringLiteral("7"));
        painter.end(); icon.addPixmap(image);
    }
    return icon;
}

ArchiveWindow::ArchiveWindow(ArchiveController& controller, bool compact) : controller_(controller), compact_(compact) {
    setObjectName(QStringLiteral("workbenchContent"));
    setProperty("workbenchAccent", MagpieAccent());
    setProperty("trayPalette", compact);
    if (compact) {
        auto sheet = theme::WorkbenchStyleSheet(MagpieAccent()) + ShellStyle() + desktop::type::StyleSheet();
        sheet.replace(theme::Canvas, "#1c1d21").replace(theme::Surface, "#25262c")
             .replace(theme::Raised, "#2e2f36").replace(theme::Border, "#323338")
             .replace(theme::Divider, "#323338").replace(theme::Muted, "#9c9ca6");
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
    connect(&controller_, &ArchiveController::statusChanged, this, &ArchiveWindow::syncStatus);
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
    card->setObjectName(sources ? QStringLiteral("sourceDropZone") : QStringLiteral("archiveDropZone"));
    auto* layout = new QVBoxLayout(card); layout->setContentsMargins(1, 1, 1, 1);
    auto* pages = new QStackedWidget;
    auto* empty = Button(QStringLiteral("添加文件或拖入此处"), sources ? "chooseSources" : "chooseArchives");
    empty->setProperty("dropTarget", true);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    empty->setIcon(desktop::LineIcon(desktop::ShellIcon::File));
    pages->addWidget(empty);
    editable_.append(empty);
    connect(empty, &QPushButton::clicked, this, [this] { pickFiles(); });
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
    auto* quickPassword = new QWidget;
    auto* quickLayout = new QHBoxLayout(quickPassword); quickLayout->setContentsMargins(0, 0, 0, 0); quickLayout->setSpacing(8);
    auto* savedPassword = Edit("quickPassword", QStringLiteral("输入常用密码，回车添加")); savedPassword->setMaxLength(1024);
    auto* addPassword = Button(QStringLiteral("添加密码"), "quickAddPassword");
    auto* passwords = Button(QStringLiteral("密码列表"), "openPasswordLists");
    quickLayout->addWidget(savedPassword, 1); quickLayout->addWidget(addPassword); quickLayout->addWidget(passwords);
    layout->addWidget(quickPassword);
    const auto savePassword = [this, savedPassword] {
        if (controller_.addPasswords(savedPassword->text())) { savedPassword->clear(); savedPassword->setFocus(); }
    };
    connect(addPassword, &QPushButton::clicked, this, savePassword);
    connect(savedPassword, &QLineEdit::returnPressed, this, savePassword);
    auto* toolbar = new QHBoxLayout; toolbar->setSpacing(8);
    auto* add = Button(QStringLiteral("添加文件…"), "addArchive");
    auto* folder = Button(QStringLiteral("添加文件夹…"), "addExtractionFolder");
    auto* correct = Button(QStringLiteral("修正名称"), "correctName");
    auto* remove = Button(QStringLiteral("移除"), "removeArchive");
    auto* clear = Button(QStringLiteral("清空"), "clearArchives");
    toolbar->addWidget(add); toolbar->addWidget(folder); toolbar->addStretch(); toolbar->addWidget(remove); toolbar->addWidget(clear);
    layout->addLayout(toolbar);
    archiveCount_ = Label({}, true); layout->addWidget(archiveCount_); layout->addWidget(makeQueue(false));
    auto* card = Card();
    extractDestination_ = Edit("extractDestination", QDir::toNativeSeparators(QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(QStringLiteral("整理结果"))));
    auto* browse = Button(QStringLiteral("选择…"), "browseExtraction");
    Row(card, QStringLiteral("保存位置"), {}, WithBrowse(extractDestination_, browse));
    extractMode_ = Segments("extractMode", {QStringLiteral("独立目录"), QStringLiteral("直接解压")});
    Row(card, QStringLiteral("解压方式"), {}, extractMode_);
    extractMode_->setToolTip(QStringLiteral("已有文件不会覆盖"));
    extractPassword_ = Edit("extractPassword", QStringLiteral("输入密码（可选）"), true);
    Row(card, QStringLiteral("本次密码"), {}, extractPassword_);
    autoNested_ = Segments("autoNested", {QStringLiteral("关闭"), QStringLiteral("开启")});
    autoNested_->setAccessibleName(QStringLiteral("自动解压内层"));
    Row(card, QStringLiteral("自动解压内层"), {}, autoNested_);
    organizeMedia_ = Segments("organizeMedia", {QStringLiteral("关闭"), QStringLiteral("开启")});
    organizeMedia_->setAccessibleName(QStringLiteral("仅保留视频和图片"));
    organizeMedia_->setToolTip(QStringLiteral("每组压缩包解压成功后，整理到独立子文件夹的“视频”和“图片”目录，再删除其他解压文件。无媒体、失败或取消时保留内容；原始压缩包和已有文件保留。"));
    Row(card, QStringLiteral("仅保留视频和图片"), QStringLiteral("整理至“视频 / 图片”，删除其他解压文件"), organizeMedia_, true);
    layout->addWidget(card);
    auto* actions = new QHBoxLayout;
    auto* test = Button(QStringLiteral("检测完整性"), "testArchives");
    auto* start = Button(QStringLiteral("开始解压"), "startExtraction", true);
    actions->addWidget(test); actions->addWidget(correct); actions->addStretch();
    actions->addWidget(start);
    layout->addLayout(actions); layout->addStretch();
    editable_ << add << folder << correct << remove << clear << card << test << start << quickPassword;
    connect(add, &QPushButton::clicked, this, [this] { pickFiles(); });
    connect(folder, &QPushButton::clicked, this, [this] { pickFiles(true); });
    connect(archives_, &QListWidget::currentRowChanged, this, &ArchiveWindow::syncQueueActions);
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
    connect(autoNested_, &QTabBar::currentChanged, this, [this](int index) {
        if (synchronizing_) return;
        controller_.autoNested = index == 1; controller_.savePreferences(); controller_.notifyOptions();
    });
    connect(organizeMedia_, &QTabBar::currentChanged, this, [this](int index) {
        if (synchronizing_) return;
        controller_.organizeMedia = index == 1; controller_.savePreferences(); controller_.notifyOptions();
    });
    return page;
}

QWidget* ArchiveWindow::makeCompressPage() {
    QVBoxLayout* layout; auto* page = Page(layout, "compressPage");
    auto* toolbar = new QHBoxLayout; toolbar->setSpacing(8);
    auto* add = Button(QStringLiteral("添加文件…"), "addSource");
    auto* folder = Button(QStringLiteral("添加文件夹…"), "addFolder");
    auto* remove = Button(QStringLiteral("移除"), "removeSource");
    auto* clear = Button(QStringLiteral("清空"), "clearSources");
    toolbar->addWidget(add); toolbar->addWidget(folder); toolbar->addStretch(); toolbar->addWidget(remove); toolbar->addWidget(clear); layout->addLayout(toolbar);
    sourceCount_ = Label({}, true); layout->addWidget(sourceCount_); layout->addWidget(makeQueue(true));
    auto* card = Card();
    archiveName_ = Edit("archiveName", QStringLiteral("压缩包名称"));
    Row(card, QStringLiteral("压缩包名称"), {}, archiveName_);
    compressDestination_ = Edit("compressDestination", QStringLiteral("源文件所在目录"));
    auto* browse = Button(QStringLiteral("选择…"), "browseCompression");
    Row(card, QStringLiteral("保存位置"), {}, WithBrowse(compressDestination_, browse));
    format_ = Segments("archiveFormat", {QStringLiteral("7z"), QStringLiteral("ZIP")});
    Row(card, QStringLiteral("压缩格式"), {}, format_);
    level_ = Segments("compressionLevel", {QStringLiteral("快速"), QStringLiteral("标准"), QStringLiteral("极限")});
    Row(card, QStringLiteral("压缩级别"), {}, level_);
    volume_ = new ComboBox; volume_->setObjectName(QStringLiteral("volumeSize")); volume_->setAccessibleName(QStringLiteral("分卷大小"));
    volume_->setEditable(true); volume_->addItems({QStringLiteral("不分卷"), "100M", "1G", "4G"}); volume_->setMinimumWidth(150);
    Row(card, QStringLiteral("分卷大小"), {}, volume_);
    volume_->setToolTip(QStringLiteral("例如 100M、1G；纯数字按 MB"));
    compressPassword_ = Edit("compressPassword", QStringLiteral("不加密"), true);
    Row(card, QStringLiteral("加密密码"), {}, compressPassword_, true); layout->addWidget(card);
    auto* actions = new QHBoxLayout;
    auto* start = Button(QStringLiteral("开始压缩"), "startCompression", true);
    actions->addStretch(); actions->addWidget(start); layout->addLayout(actions); layout->addStretch();
    editable_ << add << folder << remove << clear << card << start;
    connect(add, &QPushButton::clicked, this, [this] { pickFiles(); });
    connect(folder, &QPushButton::clicked, this, [this] { pickFiles(true); });
    connect(remove, &QPushButton::clicked, this, [this] { controller_.remove(sources_->currentRow(), true); });
    connect(sources_, &QListWidget::currentRowChanged, this, &ArchiveWindow::syncQueueActions);
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
    taskQueue_->addWidget(Empty(QStringLiteral("暂无任务")));
    tasks_ = new RoundedArchiveList; tasks_->setObjectName(QStringLiteral("taskList")); tasks_->setItemDelegate(new ArchiveDelegate(tasks_));
    tasks_->setFrameShape(QFrame::NoFrame);
    tasks_->setUniformItemSizes(true); tasks_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    taskQueue_->addWidget(tasks_); rows->addWidget(taskQueue_); layout->addWidget(card);
    connect(tasks_, &QListWidget::itemDoubleClicked, this, [this] { openOutput(); });
    connect(tasks_, &QListWidget::currentRowChanged, this, &ArchiveWindow::syncOutputAction);
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
    auto* toolbar = new QHBoxLayout; toolbar->setSpacing(8);
    auto* input = Edit("passwordInput", QStringLiteral("输入密码，回车添加")); input->setMaxLength(1024);
    auto* add = Button(QStringLiteral("添加"), "addPasswords", true);
    auto* import = Button(QStringLiteral("导入文件…"), "importPasswords");
    toolbar->addWidget(input, 1); toolbar->addWidget(add); toolbar->addWidget(import); layout->addLayout(toolbar);
    passwordSummary_ = Label({}, true); passwordSummary_->setObjectName(QStringLiteral("passwordSummary"));
    layout->addWidget(passwordSummary_);
    passwordError_ = Label({}); passwordError_->setWordWrap(true); passwordError_->setStyleSheet(QStringLiteral("color: #f08078;"));
    passwordError_->hide(); layout->addWidget(passwordError_);

    auto* listCard = new QFrame; listCard->setProperty("statModule", true);
    auto* listLayout = new QVBoxLayout(listCard); listLayout->setContentsMargins(1, 1, 1, 1);
    passwordQueue_ = new QStackedWidget; passwordQueue_->setMinimumHeight(compact_ ? 132 : 164);
    passwordQueue_->addWidget(Empty(QStringLiteral("暂无保存的密码")));
    passwordLists_ = new RoundedArchiveList; passwordLists_->setObjectName(QStringLiteral("passwordLists"));
    passwordLists_->setAccessibleName(QStringLiteral("密码列表，双击编辑，勾选启用"));
    passwordLists_->setFrameShape(QFrame::NoFrame); passwordLists_->setUniformItemSizes(true);
    passwordLists_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); passwordLists_->setTextElideMode(Qt::ElideMiddle);
    passwordLists_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    passwordLists_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    passwordQueue_->addWidget(passwordLists_); listLayout->addWidget(passwordQueue_); layout->addWidget(listCard);
    auto* noMatches = Empty(QStringLiteral("没有匹配的密码"));
    noMatches->setObjectName(QStringLiteral("passwordsNoResults")); passwordQueue_->addWidget(noMatches);
    auto* actions = new QHBoxLayout;
    editPassword_ = Button(QStringLiteral("编辑"), "editPasswords");
    removePassword_ = Button(QStringLiteral("删除"), "removePasswords");
    actions->addWidget(editPassword_); actions->addWidget(removePassword_); actions->addStretch();
    actions->addWidget(Label(QStringLiteral("双击编辑 · 勾选启用"), true)); layout->addLayout(actions);

    auto* options = Card();
    autoPasswords_ = Segments("autoPasswords", {QStringLiteral("关闭"), QStringLiteral("开启")});
    autoPasswords_->setAccessibleName(QStringLiteral("自动尝试密码"));
    Row(options, QStringLiteral("自动尝试密码"), {}, autoPasswords_, true);
    autoPasswords_->setToolTip(QStringLiteral("解压和检测时尝试列表中勾选的密码，本次密码优先"));
    layout->addWidget(options); layout->addStretch();
    import->setToolTip(QStringLiteral("每行一个密码，支持 UTF-8、UTF-16 和 GB18030"));
    editable_ << input << import << add << options << passwordLists_;
    const auto save = [this, input] {
        if (controller_.addPasswords(input->text())) { input->clear(); input->setFocus(); }
    };
    connect(add, &QPushButton::clicked, this, save);
    connect(input, &QLineEdit::returnPressed, this, save);
    connect(import, &QPushButton::clicked, this, &ArchiveWindow::importPasswords);
    connect(editPassword_, &QPushButton::clicked, this, [this] {
        if (!controller_.busy() && passwordLists_->currentItem()) passwordLists_->editItem(passwordLists_->currentItem());
    });
    connect(removePassword_, &QPushButton::clicked, this, &ArchiveWindow::removeSelectedPasswords);
    auto* remove = new QShortcut(QKeySequence(Qt::Key_Delete), passwordLists_); remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, &ArchiveWindow::removeSelectedPasswords);
    connect(passwordLists_, &QListWidget::itemSelectionChanged, this, &ArchiveWindow::syncPasswordActions);
    connect(passwordLists_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const auto original = item->data(PasswordValue).toString(), replacement = item->text();
        const bool enabled = item->checkState() == Qt::Checked;
        // Close the inline editor before a controller notification rebuilds both windows' lists.
        QTimer::singleShot(0, this, [this, original, replacement, enabled] {
            const bool saved = original == replacement ? controller_.setPasswordEnabled(original, enabled)
                : controller_.editPassword(original, replacement);
            if (!saved) refreshPasswords();
        });
    });
    connect(autoPasswords_, &QTabBar::currentChanged, this, [this](int index) {
        if (synchronizing_) return;
        controller_.autoPasswords = index == 1; controller_.savePreferences(); controller_.notifyOptions();
    });
    return page;
}

void ArchiveWindow::syncPasswordActions() {
    const auto selected = passwordLists_->selectedItems();
    const bool idle = !controller_.busy();
    editPassword_->setEnabled(idle && selected.size() == 1 && !selected.first()->isHidden());
    removePassword_->setEnabled(idle && std::any_of(selected.begin(), selected.end(), [](const auto* item) { return !item->isHidden(); }));
}

void ArchiveWindow::refreshPasswords() {
    const auto selectedValue = passwordLists_->currentItem() ? passwordLists_->currentItem()->text() : QString{};
    const int previousRow = passwordLists_->currentRow();
    const QSignalBlocker blocker(passwordLists_); passwordLists_->clear();
    int selected = -1;
    const auto& library = controller_.passwordLibrary();
    for (const auto& entry : library.uniquePasswords()) {
        auto* item = new QListWidgetItem(entry.value, passwordLists_);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint(QSize(200, PasswordRowHeight));
        item->setData(PasswordValue, entry.value); item->setData(SearchText, entry.value);
        item->setToolTip(QStringLiteral("<pre>%1</pre>").arg(entry.value.toHtmlEscaped()));
        item->setData(Qt::AccessibleDescriptionRole, entry.enabled ? QStringLiteral("已启用") : QStringLiteral("已停用"));
        if (entry.value == selectedValue) selected = passwordLists_->count() - 1;
    }
    if (selected < 0 && passwordLists_->count()) selected = std::clamp(previousRow, 0, passwordLists_->count() - 1);
    passwordLists_->setCurrentRow(selected);
    passwordQueue_->setCurrentIndex(passwordLists_->count() ? 1 : 0);
    const int height = passwordLists_->count() ? std::max(compact_ ? 132 : 164, std::min(passwordLists_->count(), compact_ ? 6 : 10) * PasswordRowHeight) : compact_ ? 132 : 164;
    passwordQueue_->setFixedHeight(height); passwordQueue_->parentWidget()->setFixedHeight(height + 2);
    passwordSummary_->setText(QStringLiteral("共 %1 个密码 · 已启用 %2 个").arg(library.uniquePasswords().size()).arg(library.enabledPasswords().size()));
    passwordError_->setText(controller_.passwordLibraryError()); passwordError_->setVisible(!controller_.passwordLibraryError().isEmpty());
    if (page_ == "passwords") filterSettings();
    syncPasswordActions();
}

void ArchiveWindow::removeSelectedPasswords() {
    if (controller_.busy()) return;
    QStringList values;
    for (const auto* item : passwordLists_->selectedItems()) if (!item->isHidden()) values.append(item->data(PasswordValue).toString());
    controller_.removePasswords(values);
}

void ArchiveWindow::importPasswords() {
    if (controller_.busy()) return;
    QScopedValueRollback<bool> modal(dialogOpen_, true);
    const auto files = QFileDialog::getOpenFileNames(this, QStringLiteral("导入密码"), {}, QStringLiteral("密码文本 (*.txt *.lst *.list *.dic);;所有文件 (*)"));
    if (files.isEmpty()) return;
    int imported = 0; QString lastError;
    for (const auto& file : files) {
        if (controller_.importPasswords(file)) ++imported;
        else lastError = controller_.status();
    }
    controller_.setStatus(lastError.isEmpty() ? QStringLiteral("已导入 %1 个密码文件，重复密码已合并").arg(imported)
        : QStringLiteral("已导入 %1 个文件，%2 个失败：%3").arg(imported).arg(files.size() - imported).arg(lastError));
    if (compact_) { raise(); activateWindow(); }
}

QWidget* ArchiveWindow::makeSettingsPage(bool about) {
    QVBoxLayout* layout; auto* page = Page(layout, about ? "aboutPage" : "settingsPage");
    auto* card = Card();
    if (about) {
        Row(card, QStringLiteral("7-Zip Context"), {}, Label('v' + controller_.updates().currentVersion(), true));
        Row(card, QStringLiteral("压缩引擎"), {}, Label(QStringLiteral("7-Zip"), true));
        Row(card, QStringLiteral("压缩格式"), {}, Label(QStringLiteral("7z / ZIP"), true), true);
    } else {
        Row(card, QStringLiteral("外观"), {}, Label(QStringLiteral("深色"), true));
        hideOnBlur_ = Segments("hideOnBlur", {QStringLiteral("关闭"), QStringLiteral("开启")});
        hideOnBlur_->setAccessibleName(QStringLiteral("托盘弹窗自动收起"));
        Row(card, QStringLiteral("点击外部收起托盘弹窗"), {}, hideOnBlur_);
        rememberGeometry_ = Segments("rememberGeometry", {QStringLiteral("关闭"), QStringLiteral("开启")});
        rememberGeometry_->setAccessibleName(QStringLiteral("记住窗口尺寸"));
        Row(card, QStringLiteral("记住窗口大小和位置"), {}, rememberGeometry_);
        auto* passwords = Button(QStringLiteral("管理…"), "managePasswordLists");
        Row(card, QStringLiteral("密码列表"), {}, passwords, true);
        passwords->setToolTip(QStringLiteral("保存的密码按 Windows 账户加密；本次密码不保存"));
        connect(passwords, &QPushButton::clicked, this, [this] { navigate(QStringLiteral("passwords")); });
        connect(hideOnBlur_, &QTabBar::currentChanged, this, [this](int index) { if (synchronizing_) return; controller_.hideOnBlur = index == 1; controller_.savePreferences(); controller_.notifyOptions(); });
        connect(rememberGeometry_, &QTabBar::currentChanged, this, [this](int index) { if (synchronizing_) return; controller_.rememberGeometry = index == 1; controller_.savePreferences(); controller_.notifyOptions(); });
    }
    layout->addWidget(card);
    if (about) {
        auto* updates = Card();
        auto* actions = new QWidget;
        auto* buttons = new QHBoxLayout(actions); buttons->setContentsMargins(0, 0, 0, 0); buttons->setSpacing(8);
        checkUpdate_ = Button(QStringLiteral("检查更新"), "checkUpdate"); downloadUpdate_ = Button(QStringLiteral("下载更新"), "downloadUpdate");
        installUpdate_ = Button(QStringLiteral("安装并重启"), "installUpdate", true); cancelUpdate_ = Button(QStringLiteral("取消"), "cancelUpdate");
        buttons->addWidget(checkUpdate_); buttons->addWidget(downloadUpdate_); buttons->addWidget(installUpdate_); buttons->addWidget(cancelUpdate_);
        auto* updateRow = Row(updates, QStringLiteral("在线更新"), controller_.updates().status(), actions);
        updateRow->setProperty("searchText", QStringLiteral("在线更新 检查更新 下载更新 安装并重启"));
        for (auto* label : updateRow->findChildren<QLabel*>()) if (label->property("settingDescription").toBool()) updateStatus_ = label;
        updateStatus_->setObjectName("updateStatus");
        autoUpdate_ = Segments("autoCheckUpdates", {QStringLiteral("关闭"), QStringLiteral("开启")});
        Row(updates, QStringLiteral("启动时自动检查更新"), {}, autoUpdate_, true);
        layout->addWidget(updates);
        updateNotes_ = Label({}, true); updateNotes_->setObjectName("updateNotes"); updateNotes_->setWordWrap(true); layout->addWidget(updateNotes_);
        connect(checkUpdate_, &QPushButton::clicked, &controller_.updates(), &UpdateManager::check);
        connect(downloadUpdate_, &QPushButton::clicked, &controller_.updates(), &UpdateManager::download);
        connect(cancelUpdate_, &QPushButton::clicked, &controller_.updates(), &UpdateManager::cancel);
        connect(installUpdate_, &QPushButton::clicked, &controller_, &ArchiveController::installUpdate);
        connect(autoUpdate_, &QTabBar::currentChanged, this, [this](int index) { if (!synchronizing_) controller_.updates().setAutoCheck(index == 1); });
    }
    if (!about) {
        auto* actions = new QHBoxLayout; actions->addStretch(); auto* quit = Button(QStringLiteral("退出应用"), "quitApplication"); actions->addWidget(quit); layout->addLayout(actions);
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
    shell_->search->setPlaceholderText(page == "passwords" ? QStringLiteral("查找密码") : QStringLiteral("查找设置"));
    shell_->search->setAccessibleName(shell_->search->placeholderText());
    syncStatus();
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
    archives_->setCurrentRow(std::min(std::max(0, archiveRow), archives_->count() - 1));
    sources_->setCurrentRow(std::min(std::max(0, sourceRow), sources_->count() - 1));
    archiveQueue_->setCurrentIndex(archives_->count() ? 1 : 0); sourceQueue_->setCurrentIndex(sources_->count() ? 1 : 0);
    auto queueHeight = [this](int count) { return count ? std::min(count, compact_ ? 3 : 4) * 66 : compact_ ? 132 : 164; };
    archiveQueue_->setFixedHeight(queueHeight(archives_->count())); sourceQueue_->setFixedHeight(queueHeight(sources_->count()));
    archiveCount_->setText(QStringLiteral("压缩包 %1 组 · 媒体 %2 组 · 辅助文件 %3 个")
        .arg(controller_.archives.size()).arg(controller_.mediaInputs.size()).arg(controller_.ignoredInputFiles.size()));
    sourceCount_->setText(QStringLiteral("文件 · %1 个输入项目").arg(sources_->count()));
    archiveCount_->setVisible(archives_->count() || !controller_.ignoredInputFiles.isEmpty());
    sourceCount_->setVisible(sources_->count() > 0);
    syncQueueActions();
}
void ArchiveWindow::refreshTasks() {
    const int row = tasks_->currentRow(); tasks_->clear();
    for (const auto& task : controller_.tasks) AddItem(tasks_, task.name, task.detail, task.success ? QStringLiteral("✓") : QStringLiteral("!"), QColor(task.success ? theme::Success : theme::Error), task.output);
    tasks_->setCurrentRow(row < 0 ? tasks_->count() - 1 : std::min(row, tasks_->count() - 1)); taskQueue_->setCurrentIndex(tasks_->count() ? 1 : 0);
    taskQueue_->setFixedHeight(tasks_->count() ? std::min(tasks_->count(), compact_ ? 3 : 4) * 66 : compact_ ? 126 : 154);
    taskCount_->setText(QStringLiteral("本次会话 · %1 条记录").arg(tasks_->count()));
    syncOutputAction();
}
void ArchiveWindow::syncOptions() {
    QScopedValueRollback<bool> syncing(synchronizing_, true);
    auto choice = [](QTabBar* tabs, bool checked) { tabs->setCurrentIndex(checked ? 1 : 0); };
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
    choice(hideOnBlur_, controller_.hideOnBlur);
    choice(rememberGeometry_, controller_.rememberGeometry);
    choice(autoPasswords_, controller_.autoPasswords);
    choice(autoNested_, controller_.autoNested || controller_.organizeMedia);
    autoNested_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    choice(organizeMedia_, controller_.organizeMedia);
    extractMode_->setToolTip(controller_.organizeMedia ? QStringLiteral("媒体整理使用独立目录") : QStringLiteral("已有文件不会覆盖"));
    autoNested_->setToolTip(controller_.organizeMedia ? QStringLiteral("媒体整理会解压所有内层压缩包")
        : QStringLiteral("逐层解压，成功后清理中间包；原始压缩包保留"));
    extractPassword_->setPlaceholderText(controller_.autoPasswords ? QStringLiteral("留空自动尝试已保存密码") : QStringLiteral("输入密码（可选）"));
    extractPassword_->setToolTip(controller_.autoPasswords ? QStringLiteral("手动输入的密码优先") : QString{});
    syncQueueActions();
}
void ArchiveWindow::syncBusy() {
    for (auto* control : editable_) control->setEnabled(!controller_.busy());
    extractMode_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    autoNested_->setEnabled(!controller_.busy() && !controller_.organizeMedia);
    syncQueueActions(); syncProgress();
    syncPasswordActions();
    syncUpdates();
    syncStatus();
    if (controller_.busy()) setDropActive(false);
}
void ArchiveWindow::syncQueueActions() {
    if (!archives_ || !sources_) return;
    const bool idle = !controller_.busy();
    auto enable = [this, idle](const char* name, bool available) {
        if (auto* button = findChild<QPushButton*>(QString::fromLatin1(name))) button->setEnabled(idle && available);
    };
    const int selected = archives_->currentRow();
    enable("removeArchive", selected >= 0);
    enable("removeSource", sources_->currentRow() >= 0);
    enable("clearArchives", archives_->count() || !controller_.ignoredInputFiles.isEmpty());
    enable("clearSources", sources_->count() > 0);
    enable("startExtraction", archives_->count() > 0);
    enable("testArchives", archives_->count() > 0);
    enable("startCompression", sources_->count() > 0);
    enable("correctName", selected >= 0 && static_cast<size_t>(selected) < controller_.archives.size()
        && controller_.archives[selected].needsRename);
    shell_->refresh->setEnabled(idle && archives_->count() > 0);
}
void ArchiveWindow::syncOutputAction() {
    const int selected = tasks_->currentRow();
    findChild<QPushButton*>("openOutput")->setEnabled(selected >= 0 && selected < controller_.tasks.size()
        && QFileInfo(controller_.tasks[selected].output).isDir());
}
void ArchiveWindow::syncStatus() {
    status_->setText(controller_.status());
    status_->setVisible(!controller_.status().isEmpty() && (controller_.busy() || (page_ != "general" && page_ != "about")));
}
void ArchiveWindow::syncUpdates() {
    if (!updateStatus_) return;
    auto& update = controller_.updates();
    updateStatus_->setText(update.status()); updateNotes_->setText(update.release().notes); updateNotes_->setVisible(!update.release().notes.isEmpty());
    checkUpdate_->setEnabled(!update.busy());
    const bool retry = update.state() == UpdateManager::State::DownloadFailed;
    checkUpdate_->setVisible(update.state() == UpdateManager::State::Idle || update.state() == UpdateManager::State::Error || retry);
    downloadUpdate_->setText(retry ? QStringLiteral("重新下载") : QStringLiteral("下载更新"));
    downloadUpdate_->setVisible(update.state() == UpdateManager::State::Available || retry);
    installUpdate_->setVisible(update.state() == UpdateManager::State::Ready);
    installUpdate_->setEnabled(!controller_.busy());
    installUpdate_->setToolTip(controller_.busy() ? QStringLiteral("当前任务结束后可以安装更新") : QStringLiteral("安装时关闭程序，完成后重新启动"));
    cancelUpdate_->setVisible(update.state() == UpdateManager::State::Checking || update.state() == UpdateManager::State::Downloading);
    const QScopedValueRollback<bool> syncing(synchronizing_, true); autoUpdate_->setCurrentIndex(update.autoCheck() ? 1 : 0);
    if (page_ == "about") filterSettings();
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
        passwordQueue_->setCurrentIndex(firstVisible >= 0 ? 1 : query.isEmpty() ? 0 : 2);
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
    for (auto* card : page->findChildren<QFrame*>()) {
        if (!card->property("settingsCard").toBool()) continue;
        QList<QFrame*> visibleRows;
        for (auto* row : card->findChildren<QFrame*>(QString{}, Qt::FindDirectChildrenOnly))
            if (row->property("settingsRow").toBool() && !row->isHidden()) visibleRows.append(row);
        card->setVisible(!visibleRows.isEmpty());
        for (auto* row : visibleRows) {
            const bool last = row == visibleRows.last();
            if (row->property("settingsLastRow").toBool() == last) continue;
            row->setProperty("settingsLastRow", last); row->style()->unpolish(row); row->style()->polish(row);
        }
    }
    if (page_ == "about") updateNotes_->setVisible(query.isEmpty() && !controller_.updates().release().notes.isEmpty());
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
        for (const auto& url : event->mimeData()->urls()) if (url.isLocalFile()) {
            setDropActive(true); event->acceptProposedAction(); return;
        }
    }
    event->ignore();
}
void ArchiveWindow::dragLeaveEvent(QDragLeaveEvent* event) {
    setDropActive(false); event->accept();
}
void ArchiveWindow::setDropActive(bool active) {
    for (const auto* name : {"archiveDropZone", "sourceDropZone"}) {
        auto* card = findChild<QFrame*>(QString::fromLatin1(name));
        const bool highlighted = active && ((page_ == "compress") == (card->objectName() == "sourceDropZone"));
        if (card->property("dropActive").toBool() == highlighted) continue;
        card->setProperty("dropActive", highlighted);
        card->style()->unpolish(card); card->style()->polish(card); card->update();
    }
}
void ArchiveWindow::dropEvent(QDropEvent* event) {
    setDropActive(false);
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
