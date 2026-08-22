#include "DesktopJobScheduler.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

DesktopJobScheduler::DesktopJobScheduler(
    int maximumRunningJobs,
    int maximumQueuedJobs,
    QObject* parent)
    : QObject(parent),
      maximumRunningJobs_(maximumRunningJobs),
      maximumQueuedJobs_(maximumQueuedJobs) {
    if (maximumRunningJobs_ <= 0 || maximumRunningJobs_ > hardMaximumRunningJobs
        || maximumQueuedJobs_ < 0 || maximumQueuedJobs_ > hardMaximumQueuedJobs) {
        throw std::invalid_argument("Desktop job scheduler limits are outside the supported range");
    }
    pool_.setMaxThreadCount(maximumRunningJobs_);
}

DesktopJobScheduler::~DesktopJobScheduler() {
    disconnect();
    (void)shutdown(-1);
}

DesktopJobScheduler::Submission DesktopJobScheduler::submit(Task task) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!task) {
        return {false,
                0,
                AdmissionRejection::MissingTask,
                tr("The background job has no work to run.")};
    }
    if (!acceptingJobs_) {
        return {false,
                0,
                AdmissionRejection::ShuttingDown,
                tr("Background work is shutting down; try again after restart.")};
    }
    if (nextJobId_ == std::numeric_limits<quint64>::max()) {
        return {false,
                0,
                AdmissionRejection::IdentifierExhausted,
                tr("The background job identifier range is exhausted; restart the application.")};
    }
    const auto maximumOutstandingJobs = maximumRunningJobs_ + maximumQueuedJobs_;
    if (runningJobCount() + queuedJobCount() + pendingCompletionCount()
        >= maximumOutstandingJobs) {
        return {false,
                0,
                AdmissionRejection::CapacityReached,
                tr("Background work is at capacity (%1 running and %2 additional queued or awaiting delivery).")
                    .arg(maximumRunningJobs_)
                    .arg(maximumQueuedJobs_)};
    }

    const auto id = nextJobId_++;
    queuedJobs_.enqueue({id, std::move(task), std::make_shared<std::atomic_bool>(false)});
    startQueuedJobs();
    emit countsChanged(runningJobCount(), queuedJobCount());
    return {true, id, AdmissionRejection::None, {}};
}

bool DesktopJobScheduler::cancel(quint64 id) {
    Q_ASSERT(QThread::currentThread() == thread());
    for (auto index = 0; index < queuedJobs_.size(); ++index) {
        if (queuedJobs_.at(index).id == id) {
            auto job = queuedJobs_.takeAt(index);
            job.cancelled->store(true);
            recordCancelled(std::move(job));
            emit countsChanged(runningJobCount(), queuedJobCount());
            deliverReadyCompletions();
            return true;
        }
    }
    const auto running = runningJobs_.constFind(id);
    if (running == runningJobs_.cend()) {
        return false;
    }
    (*running)->store(true);
    return true;
}

bool DesktopJobScheduler::shutdown(int timeoutMilliseconds) {
    Q_ASSERT(QThread::currentThread() == thread());
    acceptingJobs_ = false;
    while (!queuedJobs_.isEmpty()) {
        auto job = queuedJobs_.dequeue();
        job.cancelled->store(true);
        recordCancelled(std::move(job));
    }
    for (const auto& cancelled : std::as_const(runningJobs_)) {
        cancelled->store(true);
    }
    emit countsChanged(runningJobCount(), 0);
    deliverReadyCompletions();

    const auto completed = pool_.waitForDone(timeoutMilliseconds);
    drainWorkerCompletions();
    return completed && runningJobs_.isEmpty();
}

bool DesktopJobScheduler::isAcceptingJobs() const {
    return acceptingJobs_;
}

int DesktopJobScheduler::runningJobCount() const {
    return static_cast<int>(runningJobs_.size());
}

int DesktopJobScheduler::queuedJobCount() const {
    return static_cast<int>(queuedJobs_.size());
}

int DesktopJobScheduler::pendingCompletionCount() const {
    return static_cast<int>(completedJobs_.size());
}

int DesktopJobScheduler::maximumRunningJobs() const {
    return maximumRunningJobs_;
}

int DesktopJobScheduler::maximumQueuedJobs() const {
    return maximumQueuedJobs_;
}

void DesktopJobScheduler::startQueuedJobs() {
    while (acceptingJobs_ && runningJobs_.size() < maximumRunningJobs_
           && !queuedJobs_.isEmpty()) {
        auto job = queuedJobs_.dequeue();
        runningJobs_.insert(job.id, job.cancelled);
        pool_.start(QRunnable::create([this, job = std::move(job)]() mutable {
            CompletedJob completed{job.id, Completion::Succeeded, {}, {}, job.cancelled};
            try {
                completed.value = job.task(*job.cancelled);
            } catch (const std::exception& error) {
                completed.completion = Completion::Failed;
                completed.message = boundedDiagnostic(error.what());
            } catch (...) {
                completed.completion = Completion::Failed;
                completed.message = tr("The background job failed with an unknown error.");
            }
            if (job.cancelled->load()) {
                completed.completion = Completion::Cancelled;
                completed.value.clear();
                completed.message = tr("The background job was cancelled.");
            }
            {
                QMutexLocker locker(&workerCompletionMutex_);
                workerCompletions_.append(std::move(completed));
            }
            QMetaObject::invokeMethod(this, &DesktopJobScheduler::drainWorkerCompletions);
        }));
    }
}

void DesktopJobScheduler::drainWorkerCompletions() {
    Q_ASSERT(QThread::currentThread() == thread());
    QVector<CompletedJob> completions;
    {
        QMutexLocker locker(&workerCompletionMutex_);
        completions.swap(workerCompletions_);
    }
    if (completions.isEmpty()) {
        return;
    }

    for (auto& completed : completions) {
        if (completed.cancelled->load()) {
            completed.completion = Completion::Cancelled;
            completed.value.clear();
            completed.message = tr("The background job was cancelled.");
        }
        runningJobs_.remove(completed.id);
        completedJobs_.insert(completed.id, std::move(completed));
    }
    if (acceptingJobs_) {
        startQueuedJobs();
    }
    emit countsChanged(runningJobCount(), queuedJobCount());
    deliverReadyCompletions();
}

void DesktopJobScheduler::deliverReadyCompletions() {
    while (completedJobs_.contains(nextDeliveryId_)) {
        const auto completed = completedJobs_.take(nextDeliveryId_++);
        emit jobFinished(
            completed.id,
            completed.completion,
            completed.value,
            completed.message);
    }
}

void DesktopJobScheduler::recordCancelled(PendingJob job) {
    completedJobs_.insert(
        job.id,
        {job.id,
         Completion::Cancelled,
         {},
         tr("The background job was cancelled before it started."),
         std::move(job.cancelled)});
}

QString DesktopJobScheduler::boundedDiagnostic(const char* message) {
    auto diagnostic = QString::fromUtf8(message != nullptr ? message : "");
    for (auto& character : diagnostic) {
        if (character.category() == QChar::Other_Control && character != QChar{u'\t'}) {
            character = QChar{u' '};
        }
    }
    diagnostic = diagnostic.left(maximumDiagnosticCharacters).trimmed();
    return diagnostic.isEmpty() ? tr("The background job failed.") : diagnostic;
}
