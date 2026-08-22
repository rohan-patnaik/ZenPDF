#include "DesktopJobScheduler.h"

#include <QElapsedTimer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <limits>
#include <stdexcept>

class DesktopJobSchedulerTest final : public QObject {
    Q_OBJECT

private slots:
    void appliesAdmissionAndSubmissionOrder();
    void boundsCompletionsWaitingForEarlierJob();
    void cancelsQueuedAndRunningJobsOnce();
    void cancellationCompletionRaceDeliversOnce();
    void isolatesAndBoundsExceptions();
    void shutdownRejectsAndWaitsForCooperation();
    void destructorCancelsAndJoins();
    void rejectsInvalidConfiguration();
    void rejectsIdentifierExhaustion();
};

void DesktopJobSchedulerTest::appliesAdmissionAndSubmissionOrder() {
    DesktopJobScheduler scheduler(1, 2);
    QSignalSpy finished(&scheduler, &DesktopJobScheduler::jobFinished);
    bool wrongDeliveryThread = false;
    connect(&scheduler,
            &DesktopJobScheduler::jobFinished,
            &scheduler,
            [&scheduler, &wrongDeliveryThread] {
                wrongDeliveryThread = wrongDeliveryThread
                    || QThread::currentThread() != scheduler.thread();
            });
    QSemaphore firstStarted;
    QSemaphore releaseFirst;

    const auto missing = scheduler.submit({});
    QVERIFY(!missing.accepted);
    QCOMPARE(missing.rejection, DesktopJobScheduler::AdmissionRejection::MissingTask);
    QCOMPARE(scheduler.runningJobCount(), 0);
    QCOMPARE(scheduler.queuedJobCount(), 0);

    const auto first = scheduler.submit([&](const std::atomic_bool&) {
        firstStarted.release();
        releaseFirst.acquire();
        return QVariant{QStringLiteral("first")};
    });
    QVERIFY(first.accepted);
    QVERIFY(firstStarted.tryAcquire(1, 1'000));
    const auto second = scheduler.submit([](const std::atomic_bool&) {
        return QVariant{QStringLiteral("second")};
    });
    const auto third = scheduler.submit([](const std::atomic_bool&) {
        return QVariant{QStringLiteral("third")};
    });
    const auto rejected = scheduler.submit([](const std::atomic_bool&) { return QVariant{}; });
    QVERIFY(second.accepted);
    QVERIFY(third.accepted);
    QVERIFY(!rejected.accepted);
    QCOMPARE(rejected.rejection, DesktopJobScheduler::AdmissionRejection::CapacityReached);
    QCOMPARE(scheduler.maximumRunningJobs(), 1);
    QCOMPARE(scheduler.maximumQueuedJobs(), 2);
    QCOMPARE(scheduler.runningJobCount(), 1);
    QCOMPARE(scheduler.queuedJobCount(), 2);

    releaseFirst.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 2'000);
    for (auto index = 0; index < finished.count(); ++index) {
        QCOMPARE(finished.at(index).at(0).toULongLong(), quint64(index + 1));
        QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(finished.at(index).at(1)),
                 DesktopJobScheduler::Completion::Succeeded);
    }
    QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("first"));
    QCOMPARE(finished.at(1).at(2).toString(), QStringLiteral("second"));
    QCOMPARE(finished.at(2).at(2).toString(), QStringLiteral("third"));
    QCOMPARE(scheduler.runningJobCount(), 0);
    QCOMPARE(scheduler.queuedJobCount(), 0);
    QVERIFY(!wrongDeliveryThread);
}

void DesktopJobSchedulerTest::boundsCompletionsWaitingForEarlierJob() {
    DesktopJobScheduler scheduler(2, 1);
    QSignalSpy finished(&scheduler, &DesktopJobScheduler::jobFinished);
    QSemaphore firstStarted;
    QSemaphore releaseFirst;

    const auto first = scheduler.submit([&](const std::atomic_bool&) {
        firstStarted.release();
        releaseFirst.acquire();
        return QVariant{quint64(1)};
    });
    QVERIFY(firstStarted.tryAcquire(1, 1'000));
    const auto second = scheduler.submit([](const std::atomic_bool&) {
        return QVariant{quint64(2)};
    });
    const auto third = scheduler.submit([](const std::atomic_bool&) {
        return QVariant{quint64(3)};
    });
    QVERIFY(first.accepted);
    QVERIFY(second.accepted);
    QVERIFY(third.accepted);
    QTRY_COMPARE_WITH_TIMEOUT(scheduler.pendingCompletionCount(), 2, 2'000);
    QCOMPARE(finished.count(), 0);

    const auto rejected = scheduler.submit([](const std::atomic_bool&) { return QVariant{}; });
    QVERIFY(!rejected.accepted);
    QCOMPARE(rejected.rejection, DesktopJobScheduler::AdmissionRejection::CapacityReached);
    releaseFirst.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 2'000);
    QCOMPARE(scheduler.pendingCompletionCount(), 0);
}

void DesktopJobSchedulerTest::cancelsQueuedAndRunningJobsOnce() {
    DesktopJobScheduler scheduler(1, 2);
    QSignalSpy finished(&scheduler, &DesktopJobScheduler::jobFinished);
    QSemaphore started;
    QSemaphore release;
    std::atomic_int queuedExecutions{0};

    const auto running = scheduler.submit([&](const std::atomic_bool& cancelled) {
        started.release();
        while (!cancelled.load()) {
            QThread::msleep(1);
        }
        release.acquire();
        return QVariant{QStringLiteral("discarded")};
    });
    QVERIFY(started.tryAcquire(1, 1'000));
    const auto queued = scheduler.submit([&](const std::atomic_bool&) {
        ++queuedExecutions;
        return QVariant{};
    });
    QVERIFY(scheduler.cancel(queued.id));
    QVERIFY(scheduler.cancel(running.id));
    QVERIFY(scheduler.cancel(running.id));
    QVERIFY(!scheduler.cancel(999'999));
    release.release();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 2'000);
    QCOMPARE(queuedExecutions.load(), 0);
    QCOMPARE(finished.at(0).at(0).toULongLong(), running.id);
    QCOMPARE(finished.at(1).at(0).toULongLong(), queued.id);
    for (const auto& arguments : finished) {
        QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(arguments.at(1)),
                 DesktopJobScheduler::Completion::Cancelled);
        QVERIFY(!arguments.at(2).isValid());
    }
}

void DesktopJobSchedulerTest::cancellationCompletionRaceDeliversOnce() {
    DesktopJobScheduler scheduler(1, 0);
    QSignalSpy finished(&scheduler, &DesktopJobScheduler::jobFinished);

    for (auto iteration = 0; iteration < 50; ++iteration) {
        const auto submission = scheduler.submit([](const std::atomic_bool&) {
            return QVariant{QStringLiteral("must be discarded")};
        });
        QVERIFY(submission.accepted);
        QVERIFY(scheduler.cancel(submission.id));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), iteration + 1, 1'000);
        const auto& arguments = finished.at(iteration);
        QCOMPARE(arguments.at(0).toULongLong(), submission.id);
        QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(arguments.at(1)),
                 DesktopJobScheduler::Completion::Cancelled);
        QVERIFY(!arguments.at(2).isValid());
    }
    QTest::qWait(20);
    QCOMPARE(finished.count(), 50);
}

void DesktopJobSchedulerTest::isolatesAndBoundsExceptions() {
    DesktopJobScheduler scheduler(2, 0);
    QSignalSpy finished(&scheduler, &DesktopJobScheduler::jobFinished);
    const QByteArray noisy(DesktopJobScheduler::maximumDiagnosticCharacters + 500, 'x');

    QVERIFY(scheduler.submit([noisy](const std::atomic_bool&) -> QVariant {
        auto message = noisy;
        message[4] = '\n';
        throw std::runtime_error(message.constData());
    }).accepted);
    QVERIFY(scheduler.submit([](const std::atomic_bool&) -> QVariant { throw 7; }).accepted);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 2'000);
    for (const auto& arguments : finished) {
        QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(arguments.at(1)),
                 DesktopJobScheduler::Completion::Failed);
        QVERIFY(!arguments.at(2).isValid());
        QVERIFY(!arguments.at(3).toString().isEmpty());
        QVERIFY(arguments.at(3).toString().size()
                <= DesktopJobScheduler::maximumDiagnosticCharacters);
        QVERIFY(!arguments.at(3).toString().contains(QChar{u'\n'}));
    }
}

void DesktopJobSchedulerTest::shutdownRejectsAndWaitsForCooperation() {
    DesktopJobScheduler scheduler(2, 1);
    QSignalSpy finished(&scheduler, &DesktopJobScheduler::jobFinished);
    QSemaphore started;
    QSemaphore releaseStubborn;
    std::atomic_int queuedExecutions{0};

    QVERIFY(scheduler.submit([&](const std::atomic_bool& cancelled) {
        started.release();
        while (!cancelled.load()) {
            QThread::msleep(1);
        }
        return QVariant{};
    }).accepted);
    QVERIFY(scheduler.submit([&](const std::atomic_bool&) {
        started.release();
        releaseStubborn.acquire();
        return QVariant{};
    }).accepted);
    QVERIFY(started.tryAcquire(2, 1'000));
    QVERIFY(scheduler.submit([&](const std::atomic_bool&) {
        ++queuedExecutions;
        return QVariant{};
    }).accepted);

    QVERIFY(!scheduler.shutdown(10));
    QVERIFY(!scheduler.isAcceptingJobs());
    const auto rejected = scheduler.submit([](const std::atomic_bool&) { return QVariant{}; });
    QVERIFY(!rejected.accepted);
    QCOMPARE(rejected.rejection, DesktopJobScheduler::AdmissionRejection::ShuttingDown);
    releaseStubborn.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 2'000);
    QVERIFY(scheduler.shutdown(100));
    QCOMPARE(scheduler.runningJobCount(), 0);
    QCOMPARE(queuedExecutions.load(), 0);
    QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(finished.at(2).at(1)),
             DesktopJobScheduler::Completion::Cancelled);
}

void DesktopJobSchedulerTest::destructorCancelsAndJoins() {
    std::atomic_bool observedCancellation{false};
    QSemaphore started;
    QElapsedTimer elapsed;
    elapsed.start();
    {
        DesktopJobScheduler scheduler(1, 0);
        QVERIFY(scheduler.submit([&](const std::atomic_bool& cancelled) {
            started.release();
            while (!cancelled.load()) {
                QThread::msleep(1);
            }
            observedCancellation.store(true);
            return QVariant{};
        }).accepted);
        QVERIFY(started.tryAcquire(1, 1'000));
    }
    QVERIFY(observedCancellation.load());
    QVERIFY(elapsed.elapsed() < 2'000);
}

void DesktopJobSchedulerTest::rejectsInvalidConfiguration() {
    DesktopJobScheduler maximum(
        DesktopJobScheduler::hardMaximumRunningJobs,
        DesktopJobScheduler::hardMaximumQueuedJobs);
    QCOMPARE(maximum.maximumRunningJobs(), DesktopJobScheduler::hardMaximumRunningJobs);
    QCOMPARE(maximum.maximumQueuedJobs(), DesktopJobScheduler::hardMaximumQueuedJobs);
    QVERIFY_EXCEPTION_THROWN(DesktopJobScheduler(0, 1), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(DesktopJobScheduler(1, -1), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        DesktopJobScheduler(DesktopJobScheduler::hardMaximumRunningJobs + 1, 1),
        std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        DesktopJobScheduler(1, DesktopJobScheduler::hardMaximumQueuedJobs + 1),
        std::invalid_argument);
}

void DesktopJobSchedulerTest::rejectsIdentifierExhaustion() {
    DesktopJobScheduler scheduler;
    scheduler.nextJobId_ = std::numeric_limits<quint64>::max();
    const auto submission = scheduler.submit([](const std::atomic_bool&) { return QVariant{}; });
    QVERIFY(!submission.accepted);
    QCOMPARE(submission.rejection,
             DesktopJobScheduler::AdmissionRejection::IdentifierExhausted);
    QCOMPARE(scheduler.runningJobCount(), 0);
    QCOMPARE(scheduler.queuedJobCount(), 0);
}

QTEST_MAIN(DesktopJobSchedulerTest)
#include "DesktopJobSchedulerTest.moc"
