#pragma once
#include <QWidget>
#include <QIcon>
#include <QList>
#include <QDateTime>
#include <QPointer>
#include "ArchiveController.h"

class QApplication;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class SpeedChart;
namespace desktop { class MagpieShell; class SegmentedTabBar; }

void ApplyArchiveTheme(QApplication& app);
QIcon ArchiveIcon();

class ArchiveWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ArchiveWindow(ArchiveController& controller, bool compact = false);
    void navigate(const QString& page);
    QString currentPage() const { return page_; }
    QWidget* pageWidget(int index) const;
    void toggleAtTray(const QRect& trayGeometry);
    bool compact() const { return compact_; }
signals:
    void expandRequested(const QString& page);
    void quickPanelRequested();
protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    QWidget* makeExtractPage();
    QWidget* makeCompressPage();
    QWidget* makeTasksPage();
    QWidget* makePasswordsPage();
    QWidget* makeSettingsPage(bool about);
    QWidget* makeQueue(bool sources);
    void pickFiles(bool folder = false);
    void pickDestination(bool compression);
    void refreshQueues();
    void refreshTasks();
    void syncOptions();
    void syncBusy();
    void syncQueueActions();
    void syncOutputAction();
    void syncStatus();
    void setDropActive(bool active);
    void syncProgress();
    void run(ArchiveController::Operation operation);
    void openOutput();
    void filterSettings();
    void alignPages();
    void refreshPasswords();
    void importPasswords();
    void removeSelectedPasswords();
    void syncPasswordActions();
    void syncUpdates();

    ArchiveController& controller_;
    bool compact_, dialogOpen_ = false, synchronizing_ = false;
    QString page_ = QStringLiteral("extract");
    qint64 dismissedAt_ = 0;
    desktop::MagpieShell* shell_ = nullptr;
    QLabel* status_ = nullptr;
    QListWidget *archives_ = nullptr, *sources_ = nullptr, *tasks_ = nullptr;
    QStackedWidget *archiveQueue_ = nullptr, *sourceQueue_ = nullptr, *taskQueue_ = nullptr;
    QLabel *archiveCount_ = nullptr, *sourceCount_ = nullptr, *taskCount_ = nullptr;
    QLineEdit *extractDestination_ = nullptr, *compressDestination_ = nullptr;
    QLineEdit *extractPassword_ = nullptr, *compressPassword_ = nullptr, *archiveName_ = nullptr;
    QComboBox* volume_ = nullptr;
    desktop::SegmentedTabBar *format_ = nullptr, *level_ = nullptr, *extractMode_ = nullptr;
    desktop::SegmentedTabBar *hideOnBlur_ = nullptr, *rememberGeometry_ = nullptr, *autoNested_ = nullptr;
    QList<QWidget*> editable_;
    QPushButton *pause_ = nullptr, *cancel_ = nullptr;
    QLabel *taskState_ = nullptr, *processed_ = nullptr, *speed_ = nullptr, *percent_ = nullptr, *jobs_ = nullptr;
    QProgressBar* progress_ = nullptr;
    SpeedChart* chart_ = nullptr;
    QListWidget* passwordLists_ = nullptr;
    QStackedWidget* passwordQueue_ = nullptr;
    QLabel *passwordSummary_ = nullptr, *passwordError_ = nullptr;
    QPushButton *editPassword_ = nullptr, *removePassword_ = nullptr;
    desktop::SegmentedTabBar* autoPasswords_ = nullptr;
    desktop::SegmentedTabBar* organizeMedia_ = nullptr;
    QLabel *updateStatus_ = nullptr, *updateNotes_ = nullptr;
    QPushButton *checkUpdate_ = nullptr, *downloadUpdate_ = nullptr, *installUpdate_ = nullptr, *cancelUpdate_ = nullptr;
    desktop::SegmentedTabBar* autoUpdate_ = nullptr;
};
