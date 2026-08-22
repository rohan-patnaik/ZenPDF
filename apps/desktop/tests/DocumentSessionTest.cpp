#include "DocumentSession.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QtTest>

#include <limits>
#include <memory>
#include <utility>

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
    void ownsCommandsAndRejectsNull();
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
    DocumentSession session(QStringLiteral("document.pdf"),
                            DocumentSession::maximumUndoCommands);
    int destructions = 0;
    for (int index = 0; index < DocumentSession::maximumUndoCommands + 1; ++index) {
        QVERIFY(session.push(std::make_unique<TrackedCommand>(&destructions), 1));
    }

    QCOMPARE(session.undoCommandCount(), DocumentSession::maximumUndoCommands);
    QCOMPARE(session.retainedBytes(), quint64(DocumentSession::maximumUndoCommands));
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

QTEST_MAIN(DocumentSessionTest)
#include "DocumentSessionTest.moc"
