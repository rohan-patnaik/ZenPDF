#pragma once

#include <QHash>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QThreadPool>
#include <QVariant>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

class DesktopJobSchedulerTest;

class DesktopJobScheduler final : public QObject {
    Q_OBJECT

public:
    static constexpr int defaultMaximumRunningJobs = 2;
    static constexpr int defaultMaximumQueuedJobs = 32;
    static constexpr int hardMaximumRunningJobs = 64;
    static constexpr int hardMaximumQueuedJobs = 4'096;
    static constexpr int maximumDiagnosticCharacters = 1'024;

    enum class AdmissionRejection {
        None,
        MissingTask,
        CapacityReached,
        IdentifierExhausted,
        ShuttingDown,
    };
    Q_ENUM(AdmissionRejection)

    enum class Completion { Succeeded, Cancelled, Failed };
    Q_ENUM(Completion)

    struct Submission {
        bool accepted = false;
        quint64 id = 0;
        AdmissionRejection rejection = AdmissionRejection::None;
        QString message;
    };

    using Task = std::function<QVariant(const std::atomic_bool& cancelled)>;

    explicit DesktopJobScheduler(
        int maximumRunningJobs = defaultMaximumRunningJobs,
        int maximumQueuedJobs = defaultMaximumQueuedJobs,
        QObject* parent = nullptr);
    ~DesktopJobScheduler() override;

    [[nodiscard]] Submission submit(Task task);
    [[nodiscard]] bool cancel(quint64 id);
    [[nodiscard]] bool shutdown(int timeoutMilliseconds);
    [[nodiscard]] bool isAcceptingJobs() const;
    [[nodiscard]] int runningJobCount() const;
    [[nodiscard]] int queuedJobCount() const;
    [[nodiscard]] int pendingCompletionCount() const;
    [[nodiscard]] int maximumRunningJobs() const;
    [[nodiscard]] int maximumQueuedJobs() const;

signals:
    void countsChanged(int runningJobs, int queuedJobs);
    void jobFinished(
        quint64 id,
        DesktopJobScheduler::Completion completion,
        const QVariant& value,
        const QString& message);

private:
    friend class DesktopJobSchedulerTest;

    struct PendingJob {
        quint64 id;
        Task task;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct CompletedJob {
        quint64 id;
        Completion completion;
        QVariant value;
        QString message;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    void startQueuedJobs();
    void drainWorkerCompletions();
    void deliverReadyCompletions();
    void recordCancelled(PendingJob job);
    [[nodiscard]] static QString boundedDiagnostic(const char* message);

    int maximumRunningJobs_;
    int maximumQueuedJobs_;
    bool acceptingJobs_ = true;
    quint64 nextJobId_ = 1;
    quint64 nextDeliveryId_ = 1;
    QThreadPool pool_;
    QQueue<PendingJob> queuedJobs_;
    QHash<quint64, std::shared_ptr<std::atomic_bool>> runningJobs_;
    QMap<quint64, CompletedJob> completedJobs_;
    QMutex workerCompletionMutex_;
    QVector<CompletedJob> workerCompletions_;
};
