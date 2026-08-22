#include "DocumentSession.h"

#include <QDir>
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QtTest>

#include <limits>
#include <memory>
#include <utility>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

class DocumentSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void tracksCleanDirtyUndoAndRedo();
    void savedRevisionFollowsUndoAndBranching();
    void boundsUndoHistory();
    void enforcesRetainedByteAdmission();
    void accountsForBranchingAndUndoRedo();
    void releasesOldestEvictedCost();
    void disablesCommandMerging();
    void handlesObsoleteCommands();
    void mirrorsActionTextTransitions();
    void ownsCommandsAndRejectsNull();
    void tracksSourceRevisionAndDetectsRestoredMtimeEdit();
    void distinguishesSourceReplacementDeletionAndRestoration();
    void tracksSymlinkEntryAndTargetIdentity();
    void neverAdoptsAnInitiallyMissingSource();
    void classifiesSourceStatFailure();
};

namespace {
class AddCommand final : public QUndoCommand {
public:
    AddCommand(int* value, int amount, QString label = QStringLiteral("Change"))
        : QUndoCommand(std::move(label)), value_(value), amount_(amount) {}

    void redo() override { *value_ += amount_; }
    void undo() override { *value_ -= amount_; }

private:
    int* value_;
    int amount_;
};

class TrackedCommand final : public QUndoCommand {
public:
    TrackedCommand(int* destructions, int* executions = nullptr)
        : destructions_(destructions), executions_(executions) {}
    ~TrackedCommand() override { ++*destructions_; }

    void redo() override {
        if (executions_ != nullptr) {
            ++*executions_;
        }
    }
    void undo() override {}

private:
    int* destructions_;
    int* executions_;
};

class MergeCommand final : public QUndoCommand {
public:
    explicit MergeCommand(int* mergeCalls) : mergeCalls_(mergeCalls) {}

    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand*) override {
        ++*mergeCalls_;
        return true;
    }
    void redo() override {}
    void undo() override {}

private:
    int* mergeCalls_;
};

class ObsoleteCommand final : public QUndoCommand {
public:
    enum class Timing { BeforePush, DuringRedo, DuringUndo, DuringSecondRedo };

    ObsoleteCommand(int* value, int* destructions, Timing timing)
        : value_(value), destructions_(destructions), timing_(timing) {
        if (timing_ == Timing::BeforePush) {
            setObsolete(true);
        }
    }
    ~ObsoleteCommand() override { ++*destructions_; }

    void redo() override {
        ++*value_;
        ++redoCount_;
        if (timing_ == Timing::DuringRedo
            || (timing_ == Timing::DuringSecondRedo && redoCount_ == 2)) {
            setObsolete(true);
        }
    }
    void undo() override {
        --*value_;
        if (timing_ == Timing::DuringUndo) {
            setObsolete(true);
        }
    }

private:
    int* value_;
    int* destructions_;
    Timing timing_;
    int redoCount_ = 0;
};

class RelabelingCommand final : public QUndoCommand {
public:
    RelabelingCommand() : QUndoCommand(QStringLiteral("Initial")) {}

    void redo() override { setText(QStringLiteral("Applied")); }
    void undo() override { setText(QStringLiteral("Reverted")); }
};
}

void DocumentSessionTest::tracksCleanDirtyUndoAndRedo() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    DocumentSession session(path);
    QSignalSpy stateChanges(&session, &DocumentSession::stateChanged);
    int value = 0;

    QCOMPARE(session.filePath(), path);
    QCOMPARE(session.displayName(), QStringLiteral("document.pdf"));
    QVERIFY(!session.isDirty());
    QVERIFY(!session.canUndo());
    QVERIFY(!session.canRedo());

    QVERIFY(session.push(
        std::make_unique<AddCommand>(&value, 3, QStringLiteral("Add pages")), 16));
    QCOMPARE(value, 3);
    QVERIFY(session.isDirty());
    QVERIFY(session.canUndo());
    QCOMPARE(session.undoText(), QStringLiteral("Add pages"));

    session.undo();
    QCOMPARE(value, 0);
    QVERIFY(!session.isDirty());
    QVERIFY(session.canRedo());

    session.redo();
    QCOMPARE(value, 3);
    QVERIFY(session.isDirty());
    QVERIFY(stateChanges.count() > 0);
}

void DocumentSessionTest::savedRevisionFollowsUndoAndBranching() {
    DocumentSession session(QStringLiteral("document.pdf"));
    int value = 0;
    QVERIFY(session.push(std::make_unique<AddCommand>(&value, 1), 1));
    session.markSaved();
    QVERIFY(!session.isDirty());

    session.undo();
    QCOMPARE(value, 0);
    QVERIFY(session.isDirty());
    session.redo();
    QCOMPARE(value, 1);
    QVERIFY(!session.isDirty());

    session.undo();
    QVERIFY(session.push(std::make_unique<AddCommand>(&value, 5), 5));
    QCOMPARE(value, 5);
    QVERIFY(session.isDirty());
    QVERIFY(!session.canRedo());
}

void DocumentSessionTest::boundsUndoHistory() {
    DocumentSession session(QStringLiteral("document.pdf"));
    int value = 0;
    for (int index = 0; index < DocumentSession::maximumUndoCommands + 1; ++index) {
        QVERIFY(session.push(std::make_unique<AddCommand>(&value, 1), 1));
    }

    QCOMPARE(session.undoCommandCount(), DocumentSession::maximumUndoCommands);
    QCOMPARE(value, DocumentSession::maximumUndoCommands + 1);
    while (session.canUndo()) {
        session.undo();
    }
    QCOMPARE(value, 1);
    QVERIFY(session.isDirty());
}

void DocumentSessionTest::enforcesRetainedByteAdmission() {
    DocumentSession session(QStringLiteral("document.pdf"), 10);
    QSignalSpy rejections(&session, &DocumentSession::commandRejected);
    QSignalSpy retainedChanges(&session, &DocumentSession::retainedBytesChanged);
    int destructions = 0;
    int executions = 0;

    QCOMPARE(session.retainedByteLimit(), quint64(10));
    QCOMPARE(session.retainedBytes(), quint64(0));
    QCOMPARE(session.remainingRetainedBytes(), quint64(10));
    QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions, &executions), 4));
    QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions, &executions), 6));
    QCOMPARE(session.retainedBytes(), quint64(10));
    QCOMPARE(session.remainingRetainedBytes(), quint64(0));
    QCOMPARE(session.lastPushRejection(), DocumentSession::PushRejection::None);
    QCOMPARE(executions, 2);
    QCOMPARE(retainedChanges.count(), 2);

    QVERIFY(!session.push(std::make_unique<TrackedCommand>(&destructions, &executions), 1));
    QCOMPARE(session.lastPushRejection(),
             DocumentSession::PushRejection::RetainedByteLimitExceeded);
    QVERIFY(session.lastPushRejectionMessage().contains(QStringLiteral("only 0 bytes remain")));
    QCOMPARE(session.undoCommandCount(), 2);
    QCOMPARE(session.retainedBytes(), quint64(10));
    QCOMPARE(executions, 2);
    QCOMPARE(destructions, 1);
    QCOMPARE(rejections.count(), 1);

    DocumentSession single(QStringLiteral("single.pdf"), 10);
    QVERIFY(!single.push(std::make_unique<TrackedCommand>(&destructions, &executions), 11));
    QCOMPARE(single.lastPushRejection(),
             DocumentSession::PushRejection::CommandExceedsRetainedByteLimit);
    QCOMPARE(single.undoCommandCount(), 0);
    QCOMPARE(single.retainedBytes(), quint64(0));
    QCOMPARE(executions, 2);
    QCOMPARE(destructions, 2);

    DocumentSession overflow(QStringLiteral("overflow.pdf"),
                             std::numeric_limits<quint64>::max());
    QVERIFY(overflow.push(std::make_unique<TrackedCommand>(&destructions),
                          std::numeric_limits<quint64>::max() - 5));
    QVERIFY(!overflow.push(std::make_unique<TrackedCommand>(&destructions), 6));
    QCOMPARE(overflow.lastPushRejection(),
             DocumentSession::PushRejection::RetainedByteAdditionOverflow);
    QCOMPARE(overflow.undoCommandCount(), 1);
    QCOMPARE(overflow.retainedBytes(), std::numeric_limits<quint64>::max() - 5);
}

void DocumentSessionTest::accountsForBranchingAndUndoRedo() {
    DocumentSession session(QStringLiteral("document.pdf"), 10);
    int destructions = 0;
    int value = 0;
    QVERIFY(session.push(std::make_unique<AddCommand>(&value, 1), 3));
    QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions), 7));
    QCOMPARE(session.retainedBytes(), quint64(10));

    session.undo();
    QCOMPARE(session.retainedBytes(), quint64(10));
    session.redo();
    QCOMPARE(session.retainedBytes(), quint64(10));
    session.undo();
    QVERIFY(!session.push(std::make_unique<TrackedCommand>(&destructions), 8));
    QCOMPARE(destructions, 1);
    QCOMPARE(session.retainedBytes(), quint64(10));
    QCOMPARE(session.undoCommandCount(), 2);
    QVERIFY(session.canRedo());

    QVERIFY(session.push(std::make_unique<AddCommand>(&value, 5), 7));
    QCOMPARE(destructions, 2);
    QCOMPARE(session.retainedBytes(), quint64(10));
    QCOMPARE(session.undoCommandCount(), 2);
    QVERIFY(!session.canRedo());
    QVERIFY(session.isDirty());
}

void DocumentSessionTest::releasesOldestEvictedCost() {
    DocumentSession session(QStringLiteral("document.pdf"), 10);
    int destructions = 0;
    QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions), 10));
    for (int index = 1; index < DocumentSession::maximumUndoCommands; ++index) {
        QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions), 0));
    }
    QCOMPARE(session.retainedBytes(), quint64(10));
    QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions), 1));

    QCOMPARE(session.undoCommandCount(), DocumentSession::maximumUndoCommands);
    QCOMPARE(session.retainedBytes(), quint64(1));
    QCOMPARE(destructions, 1);
}

void DocumentSessionTest::disablesCommandMerging() {
    DocumentSession session(QStringLiteral("document.pdf"), 10);
    int mergeCalls = 0;
    QVERIFY(session.push(std::make_unique<MergeCommand>(&mergeCalls), 2));
    QVERIFY(session.push(std::make_unique<MergeCommand>(&mergeCalls), 3));
    QCOMPARE(mergeCalls, 0);
    QCOMPARE(session.undoCommandCount(), 2);
    QCOMPARE(session.retainedBytes(), quint64(5));
}

void DocumentSessionTest::handlesObsoleteCommands() {
    int value = 0;
    int destructions = 0;
    DocumentSession session(QStringLiteral("document.pdf"), 10);
    QSignalSpy rejections(&session, &DocumentSession::commandRejected);

    QVERIFY(!session.push(std::make_unique<ObsoleteCommand>(
                              &value, &destructions, ObsoleteCommand::Timing::BeforePush),
                          4));
    QCOMPARE(session.lastPushRejection(), DocumentSession::PushRejection::ObsoleteCommand);
    QCOMPARE(value, 0);
    QCOMPARE(destructions, 1);
    QCOMPARE(session.undoCommandCount(), 0);
    QCOMPARE(session.retainedBytes(), quint64(0));
    QVERIFY(!session.isDirty());
    QCOMPARE(rejections.count(), 1);

    DocumentSession pushSession(QStringLiteral("push.pdf"), 10);
    QSignalSpy pushDiscarded(&pushSession, &DocumentSession::obsoleteCommandDiscarded);
    QVERIFY(pushSession.push(std::make_unique<ObsoleteCommand>(
                                 &value, &destructions, ObsoleteCommand::Timing::DuringRedo),
                             4));
    QCOMPARE(value, 1);
    QCOMPARE(destructions, 2);
    QCOMPARE(pushSession.undoCommandCount(), 0);
    QCOMPARE(pushSession.retainedBytes(), quint64(0));
    QVERIFY(pushSession.isDirty());
    QCOMPARE(pushDiscarded.count(), 1);
    pushSession.markSaved();
    QVERIFY(!pushSession.isDirty());

    DocumentSession branchSession(QStringLiteral("branch.pdf"), 10);
    int branchValue = 0;
    int branchDestructions = 0;
    QVERIFY(branchSession.push(std::make_unique<AddCommand>(&branchValue, 1), 3));
    QVERIFY(branchSession.push(std::make_unique<AddCommand>(&branchValue, 1), 7));
    branchSession.undo();
    QVERIFY(branchSession.push(std::make_unique<ObsoleteCommand>(
                                   &branchValue,
                                   &branchDestructions,
                                   ObsoleteCommand::Timing::DuringRedo),
                               7));
    QCOMPARE(branchDestructions, 1);
    QCOMPARE(branchSession.retainedBytes(), quint64(3));
    QCOMPARE(branchSession.undoCommandCount(), 1);
    QVERIFY(!branchSession.canRedo());
    QVERIFY(branchSession.isDirty());

    DocumentSession undoSession(QStringLiteral("undo.pdf"), 10);
    QSignalSpy undoDiscarded(&undoSession, &DocumentSession::obsoleteCommandDiscarded);
    QVERIFY(undoSession.push(std::make_unique<ObsoleteCommand>(
                                 &value, &destructions, ObsoleteCommand::Timing::DuringUndo),
                             4));
    QCOMPARE(undoSession.retainedBytes(), quint64(4));
    QVERIFY(undoSession.isDirty());
    undoSession.undo();
    QCOMPARE(value, 1);
    QCOMPARE(destructions, 3);
    QCOMPARE(undoSession.undoCommandCount(), 0);
    QCOMPARE(undoSession.retainedBytes(), quint64(0));
    QVERIFY(!undoSession.isDirty());
    QCOMPARE(undoDiscarded.count(), 1);

    DocumentSession savedUndoSession(QStringLiteral("saved-undo.pdf"), 10);
    int savedUndoValue = 0;
    int savedUndoDestructions = 0;
    QVERIFY(savedUndoSession.push(std::make_unique<ObsoleteCommand>(
                                      &savedUndoValue,
                                      &savedUndoDestructions,
                                      ObsoleteCommand::Timing::DuringUndo),
                                  4));
    savedUndoSession.markSaved();
    QVERIFY(!savedUndoSession.isDirty());
    savedUndoSession.undo();
    QCOMPARE(savedUndoValue, 0);
    QCOMPARE(savedUndoDestructions, 1);
    QCOMPARE(savedUndoSession.retainedBytes(), quint64(0));
    QVERIFY(savedUndoSession.isDirty());

    DocumentSession redoSession(QStringLiteral("redo.pdf"), 10);
    QSignalSpy redoDiscarded(&redoSession, &DocumentSession::obsoleteCommandDiscarded);
    QVERIFY(redoSession.push(std::make_unique<ObsoleteCommand>(
                                &value,
                                &destructions,
                                ObsoleteCommand::Timing::DuringSecondRedo),
                            6));
    redoSession.undo();
    QCOMPARE(redoSession.retainedBytes(), quint64(6));
    QVERIFY(redoSession.canRedo());
    redoSession.redo();
    QCOMPARE(destructions, 4);
    QCOMPARE(redoSession.undoCommandCount(), 0);
    QCOMPARE(redoSession.retainedBytes(), quint64(0));
    QVERIFY(redoSession.isDirty());
    QCOMPARE(redoDiscarded.count(), 1);
}

void DocumentSessionTest::mirrorsActionTextTransitions() {
    DocumentSession session(QStringLiteral("document.pdf"), 10);
    QVERIFY(session.push(std::make_unique<RelabelingCommand>(), 1));
    QCOMPARE(session.undoText(), QStringLiteral("Applied"));
    session.undo();
    QCOMPARE(session.redoText(), QStringLiteral("Reverted"));
    session.redo();
    QCOMPARE(session.undoText(), QStringLiteral("Applied"));
}

void DocumentSessionTest::ownsCommandsAndRejectsNull() {
    int destructions = 0;
    {
        DocumentSession session(QStringLiteral("document.pdf"));
        QSignalSpy stateChanges(&session, &DocumentSession::stateChanged);
        std::unique_ptr<QUndoCommand> empty;
        QVERIFY(!session.push(std::move(empty), 1));
        QCOMPARE(session.undoCommandCount(), 0);
        QCOMPARE(session.retainedBytes(), quint64(0));
        QVERIFY(!session.isDirty());
        QCOMPARE(stateChanges.count(), 0);
        QCOMPARE(session.lastPushRejection(), DocumentSession::PushRejection::NullCommand);

        auto command = std::make_unique<TrackedCommand>(&destructions);
        QVERIFY(session.push(std::move(command), 1));
        QVERIFY(command == nullptr);
        QCOMPARE(destructions, 0);
        QCOMPARE(session.undoCommandCount(), 1);
        QCOMPARE(session.retainedBytes(), quint64(1));
        QVERIFY(session.isDirty());
    }
    QCOMPARE(destructions, 1);
}

void DocumentSessionTest::tracksSourceRevisionAndDetectsRestoredMtimeEdit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write("original"), qint64{8});
    file.close();
    const auto originalModified = QFileInfo(path).lastModified();

    DocumentSession session(path);
    QSignalSpy revisionChanges(&session, &DocumentSession::sourceRevisionStatusChanged);
    QVERIFY(session.hasTrackedSourceRevision());
    QCOMPARE(session.sourceRevisionStatus(), DocumentSession::SourceRevisionStatus::Unchanged);
    QVERIFY(session.sourceRevisionMessage().isEmpty());
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Unchanged);
    QCOMPARE(revisionChanges.count(), 0);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("original"));
    file.close();

    QVERIFY(file.open(QIODevice::ReadWrite));
    QCOMPARE(file.write("modified"), qint64{8});
    QVERIFY(file.flush());
    QVERIFY(file.setFileTime(originalModified, QFileDevice::FileModificationTime));
    file.close();

    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Modified);
    QVERIFY(session.sourceRevisionMessage().contains(QStringLiteral("changed after")));
    QCOMPARE(revisionChanges.count(), 1);
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Modified);
    QCOMPARE(revisionChanges.count(), 1);
    QCOMPARE(session.filePath(), path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("modified"));
}

void DocumentSessionTest::distinguishesSourceReplacementDeletionAndRestoration() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    const auto heldPath = directory.filePath(QStringLiteral("held.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write("source"), qint64{6});
    file.close();

    DocumentSession session(path);
    QSignalSpy revisionChanges(&session, &DocumentSession::sourceRevisionStatusChanged);
    QVERIFY(QFile::rename(path, heldPath));
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Missing);
    QCOMPARE(revisionChanges.count(), 1);

    QVERIFY(QFile::rename(heldPath, path));
    // Restoring the same inode still changes ctime, so the guard stays conservative.
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Modified);
    QCOMPARE(revisionChanges.count(), 2);

    QVERIFY(QFile::rename(path, heldPath));
    file.setFileName(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write("source"), qint64{6});
    file.close();
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Replaced);
    QCOMPARE(revisionChanges.count(), 3);

    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkdir(path));
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Replaced);
    QCOMPARE(revisionChanges.count(), 4);
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Replaced);
    QCOMPARE(revisionChanges.count(), 4);
    QCOMPARE(session.filePath(), path);
}

void DocumentSessionTest::tracksSymlinkEntryAndTargetIdentity() {
#ifndef Q_OS_UNIX
    QSKIP("Symbolic-link identity requires Unix");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto firstTarget = directory.filePath(QStringLiteral("first.pdf"));
    const auto secondTarget = directory.filePath(QStringLiteral("second.pdf"));
    const auto alias = directory.filePath(QStringLiteral("alias.pdf"));
    for (const auto& target : {firstTarget, secondTarget}) {
        QFile file(target);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(file.write("source"), qint64{6});
    }
    QVERIFY(QFile::link(firstTarget, alias));

    DocumentSession session(alias);
    QVERIFY(session.hasTrackedSourceRevision());
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Unchanged);
    QVERIFY(QFile::remove(alias));
    QVERIFY(QFile::link(secondTarget, alias));
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Replaced);
    QCOMPARE(session.filePath(), alias);
#endif
}

void DocumentSessionTest::neverAdoptsAnInitiallyMissingSource() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("future.pdf"));
    DocumentSession session(path);
    QSignalSpy revisionChanges(&session, &DocumentSession::sourceRevisionStatusChanged);
    QVERIFY(!session.hasTrackedSourceRevision());
    QCOMPARE(session.sourceRevisionStatus(), DocumentSession::SourceRevisionStatus::Untracked);

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write("later"), qint64{5});
    file.close();
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Untracked);
    QCOMPARE(revisionChanges.count(), 0);
    QVERIFY(!session.hasTrackedSourceRevision());
}

void DocumentSessionTest::classifiesSourceStatFailure() {
#ifndef Q_OS_UNIX
    QSKIP("Deterministic stat-failure fixture requires Unix symbolic links");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write("source"), qint64{6});
    file.close();
    DocumentSession session(path);

    QVERIFY(QFile::remove(path));
    const auto encodedPath = QFile::encodeName(path);
    QCOMPARE(::symlink("document.pdf", encodedPath.constData()), 0);
    QCOMPARE(session.revalidateSourceRevision(), DocumentSession::SourceRevisionStatus::Unavailable);
    QVERIFY(session.sourceRevisionMessage().contains(QStringLiteral("could not be verified")));
#endif
}

QTEST_MAIN(DocumentSessionTest)
#include "DocumentSessionTest.moc"
