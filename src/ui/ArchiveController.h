#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QHash>
#include <atomic>
#include <chrono>
#include <thread>
#include "ArchivePlan.h"
#include "SevenZipCore.h"
#include "PasswordLibrary.h"
#include "InputDiscovery.h"

struct ArchiveTask {
    QString name;
    QString detail;
    QString output;
    bool success = false;
};

class ArchiveController final : public QObject {
    Q_OBJECT
public:
    enum class Operation { Extract, Compress, Test, Correct };
    explicit ArchiveController(QObject *parent = nullptr, const QString& passwordStoragePath = {});
    ~ArchiveController() override;

    std::vector<ArchivePlan> archives;
    QList<MediaInputGroup> mediaInputs;
    QStringList ignoredInputFiles, discoveredInputFiles;
    QStringList associatedInputFiles;
    QStringList sources;
    QList<ArchiveTask> tasks;
    QStringList videoFiles, imageFiles;
    int cleanedIntermediateFiles = 0;
    uint64_t cleanedIntermediateBytes = 0;
    int cleanedOtherFiles = 0;
    uint64_t cleanedOtherBytes = 0;
    QString extractDestination, compressDestination, extractPassword, compressPassword;
    QString archiveName = QStringLiteral("Archive"), volumeSize;
    CompressionOptions compression;
    bool subfolder = true, hideOnBlur = false, rememberGeometry = true;
    bool autoPasswords = true;
    bool autoNested = true;
    bool organizeMedia = true;
    unsigned extractionDepth() const { return extractionDepth_; }
    const PasswordLibrary& passwordLibrary() const { return passwords_; }
    const QString& passwordLibraryError() const { return passwordError_; }
    bool addPasswordList(const QString& name, const QString& text, const QHash<QString, QString>* comments = nullptr);
    bool importPasswordList(const QString& path);
    bool editPasswordList(const QString& id, const QString& name, const QString& text, const QHash<QString, QString>* comments = nullptr);
    bool setPasswordListEnabled(const QString& id, bool enabled);
    bool removePasswordList(const QString& id);
    int passwordAttempt() const { return passwordAttempt_; }
    int passwordTotal() const { return passwordTotal_; }
    bool matchingPassword() const { return phase_ == Phase::Passwords; }
    bool cleaningIntermediates() const { return phase_ == Phase::Cleaning; }
    bool organizingMedia() const { return phase_ == Phase::Organizing; }

    bool busy() const { return busy_ || scanning_; }
    bool scanningInputs() const { return scanning_; }
    bool paused() const { return paused_; }
    bool cancelling() const { return cancelled_; }
    QString status() const { return status_; }
    uint64_t completed() const { return completed_; }
    uint64_t total() const { return total_; }
    unsigned jobIndex() const { return jobIndex_; }
    unsigned jobCount() const { return jobCount_; }
    double speed() const { return speed_; }
    const QList<double>& speedHistory() const { return speeds_; }

    void addPaths(const QStringList& paths, bool compress);
    void extractPaths(const QStringList& paths);
    void queuePaths(const QStringList& paths, bool compress, bool replace = false, bool startAfterScan = false);
    void remove(int index, bool compress);
    void clear(bool compress);
    void rescan();
    bool start(Operation operation, int selected = -1);
    void togglePause();
    void cancel();
    void requestQuit();
    void setStatus(const QString& text);
    void notifyOptions();
    void savePreferences();
    QByteArray windowGeometry() const;
    void saveWindowGeometry(const QByteArray& geometry);

signals:
    void queueChanged();
    void optionsChanged();
    void tasksChanged();
    void progressChanged();
    void busyChanged();
    void statusChanged(const QString& text);
    void quitReady();
    void passwordsChanged();

private:
    struct Result {
        QList<ArchiveTask> tasks;
        std::vector<std::pair<size_t, ArchivePlan>> corrections;
        QHash<QString, QString> matches;
        QStringList videos, images;
        QStringList associatedInputs;
        bool extraction = false;
        int cleanedFiles = 0;
        uint64_t cleanedBytes = 0;
        int removedOtherFiles = 0;
        uint64_t removedOtherBytes = 0;
    };
    void finish(Result result);
    void applyDiscoveredInputs(DiscoveredInputs inputs);
    bool savePasswordLists(const QList<PasswordList>& lists);
    enum class Phase { Idle, InputScanning, Opening, Passwords, Extracting, Scanning, Cleaning, Organizing, CopyingMedia, Testing, Correcting, Compressing };
    std::atomic<Phase> phase_{Phase::Idle};
    std::atomic<int> passwordAttempt_{0}, passwordTotal_{0};
    PasswordLibrary passwords_;
    QString passwordError_;
    QHash<QString, QString> passwordMatches_;
    bool busy_ = false, quitAfterJob_ = false;
    bool scanning_ = false;
    std::atomic<bool> scanCancelled_{false};
    std::atomic<bool> paused_{false}, cancelled_{false};
    std::atomic<uint64_t> completed_{0}, total_{0};
    std::atomic<unsigned> jobIndex_{0}, jobCount_{0};
    std::atomic<unsigned> extractionDepth_{0};
    std::thread worker_;
    std::thread scanWorker_;
    QTimer timer_;
    uint64_t lastBytes_ = 0;
    double speed_ = 0;
    QList<double> speeds_;
    std::chrono::steady_clock::time_point sampledAt_;
    QString status_ = QStringLiteral("添加压缩包，自动识别真实格式与分卷。");
};
