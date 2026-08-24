#include "MainWindow.h"

#include "DocumentSession.h"
#include "DocumentWidget.h"
#include "LocalState.h"
#include "Preferences.h"
#include "QpdfOperations.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPdfWriter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSemaphore>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUndoCommand>
#include <QtTest>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesKeyboardAccessibleDeleteAction();
    void routesUndoForFirstDocumentImmediately();
    void routesUndoAndDirtyStateToActiveDocument();
    void routesObsoleteCommandAccountingThroughActions();
    void dirtyTabRequiresExplicitDiscard();
    void dirtyWindowRequiresExplicitDiscard();
    void persistsGeometryAndRejectsInvalidWindowBlobs();
    void preferenceSaveFailureRequiresExplicitDiscard();
    void preferenceSaveFailureExposesSemanticActions();
    void presentationModeExitsWithEscape();
    void organizerRunsThroughSchedulerAndOpensCleanTab();
    void organizerCancellationUsesSchedulerToken();
    void organizerRejectsReentrantRun();
    void organizerAdmissionFailureIsActionable();
    void organizerSchedulerJoinsOnWindowDestruction();
};

namespace {
void createPdf(const QString& path, const QString& text) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.drawText(QPointF(72, 72), text);
    painter.end();
}

class AddCommand final : public QUndoCommand {
public:
    AddCommand(int* value, int amount, QString label)
        : QUndoCommand(std::move(label)), value_(value), amount_(amount) {}

    void redo() override { *value_ += amount_; }
    void undo() override { *value_ -= amount_; }

private:
    int* value_;
    int amount_;
};

class ObsoleteCommand final : public QUndoCommand {
public:
    enum class Timing { Undo, SecondRedo };

    ObsoleteCommand(int* value, int* destructions, Timing timing)
        : value_(value), destructions_(destructions), timing_(timing) {}
    ~ObsoleteCommand() override { ++*destructions_; }

    void redo() override {
        ++*value_;
        ++redoCount_;
        if (timing_ == Timing::SecondRedo && redoCount_ == 2) {
            setObsolete(true);
        }
    }
    void undo() override {
        --*value_;
        if (timing_ == Timing::Undo) {
            setObsolete(true);
        }
    }

private:
    int* value_;
    int* destructions_;
    Timing timing_;
    int redoCount_ = 0;
};

void answerMessageBox(QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, [answer] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (auto* button = box->button(answer)) {
                button->click();
            }
        }
    });
}
}

void MainWindowTest::exposesKeyboardAccessibleDeleteAction() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);

    auto* action = window.findChild<QAction*>(QStringLiteral("deletePagesAction"));
    QVERIFY(action != nullptr);
    QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    QVERIFY(action->text().contains(QStringLiteral("new file")));
    QVERIFY(action->statusTip().contains(QStringLiteral("unchanged")));
}

void MainWindowTest::routesUndoForFirstDocumentImmediately() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    createPdf(path, QStringLiteral("Document"));
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    window.openFiles({path});
    auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
    QVERIFY(undoAction != nullptr);
    int value = 0;

    QVERIFY(window.currentDocument()->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("First change")), 1));
    QCOMPARE(value, 1);
    QVERIFY(undoAction->isEnabled());
    QVERIFY(undoAction->text().contains(QStringLiteral("First change")));
    undoAction->trigger();
    QCOMPARE(value, 0);
    QVERIFY(!window.currentDocument()->session().isDirty());
}

void MainWindowTest::routesUndoAndDirtyStateToActiveDocument() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto firstPath = directory.filePath(QStringLiteral("first.pdf"));
    const auto secondPath = directory.filePath(QStringLiteral("second.pdf"));
    createPdf(firstPath, QStringLiteral("First"));
    createPdf(secondPath, QStringLiteral("Second"));
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    window.openFiles({firstPath, secondPath});

    auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* redoAction = window.findChild<QAction*>(QStringLiteral("redoAction"));
    QVERIFY(undoAction != nullptr);
    QVERIFY(redoAction != nullptr);
    QCOMPARE(undoAction->shortcut(), QKeySequence::Undo);
    QCOMPARE(redoAction->shortcut(), QKeySequence::Redo);
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());

    auto* first = qobject_cast<DocumentWidget*>(window.tabs_->widget(0));
    QVERIFY(first != nullptr);
    int value = 0;
    QVERIFY(first->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("First change")), 1));
    QCOMPARE(value, 1);
    QVERIFY(window.tabs_->tabText(0).endsWith('*'));
    QVERIFY(!window.tabs_->tabText(1).endsWith('*'));
    QVERIFY(!undoAction->isEnabled());

    window.tabs_->setCurrentIndex(0);
    QVERIFY(undoAction->isEnabled());
    QVERIFY(undoAction->text().contains(QStringLiteral("First change")));
    undoAction->trigger();
    QCOMPARE(value, 0);
    QVERIFY(!window.tabs_->tabText(0).endsWith('*'));
    QVERIFY(redoAction->isEnabled());

    window.tabs_->setCurrentIndex(1);
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());
}

void MainWindowTest::routesObsoleteCommandAccountingThroughActions() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    createPdf(path, QStringLiteral("Document"));
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    window.openFiles({path});
    auto& session = window.currentDocument()->session();
    auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* redoAction = window.findChild<QAction*>(QStringLiteral("redoAction"));
    QVERIFY(undoAction != nullptr);
    QVERIFY(redoAction != nullptr);
    QSignalSpy retainedChanges(&session, &DocumentSession::retainedBytesChanged);
    QSignalSpy discarded(&session, &DocumentSession::obsoleteCommandDiscarded);
    int value = 0;
    int destructions = 0;

    QVERIFY(session.push(
        std::make_unique<ObsoleteCommand>(&value, &destructions, ObsoleteCommand::Timing::Undo),
        4));
    QCOMPARE(session.retainedBytes(), quint64(4));
    retainedChanges.clear();
    undoAction->trigger();
    QCOMPARE(value, 0);
    QCOMPARE(destructions, 1);
    QCOMPARE(session.undoCommandCount(), 0);
    QCOMPARE(session.retainedBytes(), quint64(0));
    QCOMPARE(retainedChanges.count(), 1);
    QCOMPARE(discarded.count(), 1);
    QVERIFY(!session.isDirty());

    QVERIFY(session.push(std::make_unique<ObsoleteCommand>(
                             &value, &destructions, ObsoleteCommand::Timing::SecondRedo),
                         6));
    retainedChanges.clear();
    undoAction->trigger();
    QCOMPARE(session.retainedBytes(), quint64(6));
    QCOMPARE(retainedChanges.count(), 0);
    QVERIFY(redoAction->isEnabled());
    redoAction->trigger();
    QCOMPARE(value, 1);
    QCOMPARE(destructions, 2);
    QCOMPARE(session.undoCommandCount(), 0);
    QCOMPARE(session.retainedBytes(), quint64(0));
    QCOMPARE(retainedChanges.count(), 1);
    QCOMPARE(discarded.count(), 2);
    QVERIFY(session.isDirty());
}

void MainWindowTest::dirtyTabRequiresExplicitDiscard() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    createPdf(path, QStringLiteral("Document"));
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    window.openFiles({path});
    auto* document = window.currentDocument();
    QVERIFY(document != nullptr);
    int value = 0;
    QVERIFY(document->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("Unsaved change")), 1));

    answerMessageBox(QMessageBox::Cancel);
    window.closeTab(0);
    QCOMPARE(window.tabs_->count(), 1);
    QCOMPARE(window.currentDocument(), document);
    QCOMPARE(value, 1);

    answerMessageBox(QMessageBox::Discard);
    window.closeTab(0);
    QCOMPARE(window.tabs_->count(), 1);
    QVERIFY(window.currentDocument() == nullptr);
}

void MainWindowTest::dirtyWindowRequiresExplicitDiscard() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    createPdf(path, QStringLiteral("Document"));
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    const auto preferencesPath = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(preferencesPath);
    MainWindow window(state, preferences);
    window.openFiles({path});
    int value = 0;
    QVERIFY(window.currentDocument()->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("Unsaved change")), 1));

    QCloseEvent cancelEvent;
    answerMessageBox(QMessageBox::Cancel);
    window.closeEvent(&cancelEvent);
    QVERIFY(!cancelEvent.isAccepted());
    QVERIFY(!QFileInfo::exists(preferencesPath));

    QCloseEvent discardEvent;
    answerMessageBox(QMessageBox::Discard);
    window.closeEvent(&discardEvent);
    QVERIFY(discardEvent.isAccepted());
    QVERIFY(QFileInfo::exists(preferencesPath));
}

void MainWindowTest::persistsGeometryAndRejectsInvalidWindowBlobs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    const auto preferencesPath = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(preferencesPath);
    QByteArray expectedGeometry;
    QByteArray expectedState;

    {
        MainWindow window(state, preferences);
        window.setWindowState(Qt::WindowMaximized);
        expectedGeometry = window.saveGeometry();
        expectedState = window.saveState();
        QCloseEvent closeEvent;
        window.closeEvent(&closeEvent);
        QVERIFY(closeEvent.isAccepted());
    }
    WindowPreferences persisted;
    QVERIFY(preferences.loadWindowPreferences(&persisted));
    QCOMPARE(persisted.geometry, expectedGeometry);
    QCOMPARE(persisted.state, expectedState);
    {
        MainWindow restored(state, preferences);
        QCOMPARE(restored.statusBar()->currentMessage(), QStringLiteral("Local-only workspace ready"));
        QVERIFY(restored.isMaximized());
        QCOMPARE(restored.saveState(), expectedState);
    }

    Preferences defaultPreferences(directory.filePath(QStringLiteral("default-preferences.ini")));
    MainWindow defaultWindow(state, defaultPreferences);
    const auto defaultGeometry = defaultWindow.saveGeometry();
    const auto defaultState = defaultWindow.saveState();
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("invalid geometry"), QByteArrayLiteral("invalid state")}));
    MainWindow fallback(state, preferences);
    QVERIFY(fallback.statusBar()->currentMessage().contains(
        QStringLiteral("invalid"), Qt::CaseInsensitive));
    QCOMPARE(fallback.saveGeometry(), defaultGeometry);
    QCOMPARE(fallback.saveState(), defaultState);
}

void MainWindowTest::preferenceSaveFailureRequiresExplicitDiscard() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    const auto preferencesPath = directory.filePath(QStringLiteral("preferences.ini"));
    QVERIFY(QDir().mkdir(preferencesPath));
    Preferences preferences(preferencesPath);
    MainWindow window(state, preferences);

    QCloseEvent cancelEvent;
    answerMessageBox(QMessageBox::Cancel);
    window.closeEvent(&cancelEvent);
    QVERIFY(!cancelEvent.isAccepted());
    QVERIFY(QFileInfo(preferencesPath).isDir());

    QCloseEvent discardEvent;
    answerMessageBox(QMessageBox::Discard);
    window.closeEvent(&discardEvent);
    QVERIFY(discardEvent.isAccepted());
    QVERIFY(QFileInfo(preferencesPath).isDir());
}

void MainWindowTest::preferenceSaveFailureExposesSemanticActions() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    const auto preferencesPath = directory.filePath(QStringLiteral("preferences.ini"));
    QVERIFY(QDir().mkdir(preferencesPath));
    Preferences preferences(preferencesPath);
    MainWindow window(state, preferences);
    bool inspected = false;

    QTimer::singleShot(0, [&inspected] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(box != nullptr);
        auto* discard = box->button(QMessageBox::Discard);
        auto* cancel = box->button(QMessageBox::Cancel);
        QVERIFY(discard != nullptr);
        QVERIFY(cancel != nullptr);
        QCOMPARE(discard->accessibleName(), QStringLiteral("Discard"));
        QCOMPARE(cancel->accessibleName(), QStringLiteral("Cancel"));
        inspected = true;
        cancel->click();
    });

    QCloseEvent event;
    window.closeEvent(&event);
    QVERIFY(inspected);
    QVERIFY(!event.isAccepted());
}

void MainWindowTest::presentationModeExitsWithEscape() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    window.show();
    auto* presentationAction = window.findChild<QAction*>(QStringLiteral("presentationModeAction"));
    QVERIFY(presentationAction != nullptr);
    QCOMPARE(presentationAction->shortcutContext(), Qt::ApplicationShortcut);

    window.togglePresentationMode();
    QVERIFY(window.presentationMode_);
    QVERIFY(window.exitPresentationShortcut_->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(window.exitPresentationShortcut_, "activated"));
    QVERIFY(!window.presentationMode_);
    QVERIFY(!window.exitPresentationShortcut_->isEnabled());
    QVERIFY(window.menuBar()->isVisible());
}

void MainWindowTest::organizerRunsThroughSchedulerAndOpensCleanTab() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    createPdf(source, QStringLiteral("Source"));
    createPdf(output, QStringLiteral("Output"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const auto sourceBytes = sourceFile.readAll();
    sourceFile.close();

    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    window.openFiles({source});
    QCOMPARE(window.tabs_->count(), 1);
    auto* sourceDocument = window.currentDocument();
    QVERIFY(sourceDocument != nullptr);
    QVERIFY(!sourceDocument->isWindowModified());
    QVERIFY(!window.tabs_->tabText(0).contains('*'));

    QSignalSpy finished(&window.jobScheduler_, &DesktopJobScheduler::jobFinished);
    std::atomic_bool receivedCancellationToken{false};
    const auto result = window.runOrganizerTask(
        QStringLiteral("Testing organizer scheduler"),
        [&](const std::atomic_bool* cancelled) {
            receivedCancellationToken.store(cancelled != nullptr);
            return QpdfResult{true, QStringLiteral("Saved output")};
        });
    QVERIFY(result.succeeded);
    QVERIFY(receivedCancellationToken.load());
    QCOMPARE(finished.count(), 1);
    QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(finished.at(0).at(1)),
             DesktopJobScheduler::Completion::Succeeded);
    QVERIFY(window.organizeMenu_->isEnabled());
    QCOMPARE(window.jobScheduler_.runningJobCount(), 0);
    QCOMPARE(window.jobScheduler_.queuedJobCount(), 0);

    window.finishOrganizerTask(output, result, QStringLiteral("Operation failed"));

    QCOMPARE(window.tabs_->count(), 2);
    QCOMPARE(window.currentDocument()->filePath(), output);
    QVERIFY(!window.currentDocument()->isWindowModified());
    QVERIFY(!sourceDocument->isWindowModified());
    QVERIFY(!window.tabs_->tabText(0).contains('*'));
    QVERIFY(!window.tabs_->tabText(1).contains('*'));
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), sourceBytes);
}

void MainWindowTest::organizerCancellationUsesSchedulerToken() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    QSignalSpy finished(&window.jobScheduler_, &DesktopJobScheduler::jobFinished);
    std::atomic_bool observedCancellation{false};
    bool clickedCancel = false;

    QTimer::singleShot(0, [&] {
        auto* progress = window.findChild<QProgressDialog*>();
        QVERIFY(progress != nullptr);
        for (auto* button : progress->findChildren<QPushButton*>()) {
            if (button->text().contains(QStringLiteral("Cancel"))) {
                clickedCancel = true;
                button->click();
                return;
            }
        }
    });
    const auto result = window.runOrganizerTask(
        QStringLiteral("Cancelable organizer job"),
        [&](const std::atomic_bool* cancelled) {
            while (!cancelled->load()) {
                QThread::msleep(1);
            }
            observedCancellation.store(true);
            return QpdfResult{true, QStringLiteral("stale successful payload")};
        });

    QVERIFY(clickedCancel);
    QVERIFY(observedCancellation.load());
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QVERIFY(!result.message.contains(QStringLiteral("stale")));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(qvariant_cast<DesktopJobScheduler::Completion>(finished.at(0).at(1)),
             DesktopJobScheduler::Completion::Cancelled);
    QVERIFY(!finished.at(0).at(2).isValid());
    QVERIFY(window.organizeMenu_->isEnabled());
}

void MainWindowTest::organizerRejectsReentrantRun() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    QSemaphore releaseOuter;
    std::optional<QpdfResult> nestedResult;
    std::atomic_bool nestedRan{false};
    bool menuDisabled = false;

    QTimer::singleShot(0, [&] {
        menuDisabled = !window.organizeMenu_->isEnabled();
        for (const auto* action : window.organizeMenu_->actions()) {
            menuDisabled = menuDisabled && !action->isEnabled();
        }
        nestedResult = window.runOrganizerTask(
            QStringLiteral("Nested organizer job"),
            [&](const std::atomic_bool*) {
                nestedRan.store(true);
                return QpdfResult{true, QStringLiteral("must not run")};
            });
        releaseOuter.release();
    });
    const auto outerResult = window.runOrganizerTask(
        QStringLiteral("Outer organizer job"),
        [&](const std::atomic_bool*) {
            releaseOuter.acquire();
            return QpdfResult{true, QStringLiteral("outer complete")};
        });

    QVERIFY(outerResult.succeeded);
    QVERIFY(menuDisabled);
    QVERIFY(nestedResult.has_value());
    QVERIFY(!nestedResult->succeeded);
    QVERIFY(nestedResult->message.contains(QStringLiteral("already running")));
    QVERIFY(!nestedRan.load());
    QVERIFY(window.organizeMenu_->isEnabled());
}

void MainWindowTest::organizerAdmissionFailureIsActionable() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    MainWindow window(state, preferences);
    std::vector<quint64> admittedIds;
    std::atomic_bool rejectedTaskRan{false};
    const auto maximumOutstanding = window.jobScheduler_.maximumRunningJobs()
        + window.jobScheduler_.maximumQueuedJobs();

    for (auto index = 0; index < maximumOutstanding; ++index) {
        const auto submission = window.jobScheduler_.submit(
            [](const std::atomic_bool& cancelled) {
                while (!cancelled.load()) {
                    QThread::msleep(1);
                }
                return QVariant{};
            });
        QVERIFY(submission.accepted);
        admittedIds.push_back(submission.id);
    }
    const auto result = window.runOrganizerTask(
        QStringLiteral("Rejected organizer job"),
        [&](const std::atomic_bool*) {
            rejectedTaskRan.store(true);
            return QpdfResult{true, QStringLiteral("must not run")};
        });
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("capacity"), Qt::CaseInsensitive));
    QVERIFY(!rejectedTaskRan.load());
    QVERIFY(window.organizeMenu_->isEnabled());

    for (const auto id : admittedIds) {
        QVERIFY(window.jobScheduler_.cancel(id));
    }
    QTRY_COMPARE_WITH_TIMEOUT(window.jobScheduler_.runningJobCount(), 0, 2'000);
    QCOMPARE(window.jobScheduler_.queuedJobCount(), 0);
    QCOMPARE(window.jobScheduler_.pendingCompletionCount(), 0);
}

void MainWindowTest::organizerSchedulerJoinsOnWindowDestruction() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    Preferences preferences(directory.filePath(QStringLiteral("preferences.ini")));
    QSemaphore started;
    std::atomic_bool observedCancellation{false};

    {
        auto window = std::make_unique<MainWindow>(state, preferences);
        QVERIFY(window->jobScheduler_.submit([&](const std::atomic_bool& cancelled) {
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
}

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
