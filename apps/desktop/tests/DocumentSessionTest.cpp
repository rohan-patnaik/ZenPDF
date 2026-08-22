#include "DocumentSession.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QtTest>

#include <memory>
#include <utility>

class DocumentSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void tracksCleanDirtyUndoAndRedo();
    void savedRevisionFollowsUndoAndBranching();
    void boundsUndoHistory();
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
    explicit TrackedCommand(int* destructions) : destructions_(destructions) {}
    ~TrackedCommand() override { ++*destructions_; }

    void redo() override {}
    void undo() override {}

private:
    int* destructions_;
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
        std::make_unique<AddCommand>(&value, 3, QStringLiteral("Add pages"))));
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
    QVERIFY(session.push(std::make_unique<AddCommand>(&value, 1)));
    session.markSaved();
    QVERIFY(!session.isDirty());

    session.undo();
    QCOMPARE(value, 0);
    QVERIFY(session.isDirty());
    session.redo();
    QCOMPARE(value, 1);
    QVERIFY(!session.isDirty());

    session.undo();
    QVERIFY(session.push(std::make_unique<AddCommand>(&value, 5)));
    QCOMPARE(value, 5);
    QVERIFY(session.isDirty());
    QVERIFY(!session.canRedo());
}

void DocumentSessionTest::boundsUndoHistory() {
    DocumentSession session(QStringLiteral("document.pdf"));
    int value = 0;
    for (int index = 0; index < DocumentSession::maximumUndoCommands + 1; ++index) {
        QVERIFY(session.push(std::make_unique<AddCommand>(&value, 1)));
    }

    QCOMPARE(session.undoCommandCount(), DocumentSession::maximumUndoCommands);
    QCOMPARE(value, DocumentSession::maximumUndoCommands + 1);
    while (session.canUndo()) {
        session.undo();
    }
    QCOMPARE(value, 1);
    QVERIFY(session.isDirty());
}

void DocumentSessionTest::ownsCommandsAndRejectsNull() {
    int destructions = 0;
    {
        DocumentSession session(QStringLiteral("document.pdf"));
        QSignalSpy stateChanges(&session, &DocumentSession::stateChanged);
        std::unique_ptr<QUndoCommand> empty;
        QVERIFY(!session.push(std::move(empty)));
        QCOMPARE(session.undoCommandCount(), 0);
        QVERIFY(!session.isDirty());
        QCOMPARE(stateChanges.count(), 0);

        auto command = std::make_unique<TrackedCommand>(&destructions);
        QVERIFY(session.push(std::move(command)));
        QVERIFY(command == nullptr);
        QCOMPARE(destructions, 0);
        QCOMPARE(session.undoCommandCount(), 1);
        QVERIFY(session.isDirty());
    }
    QCOMPARE(destructions, 1);
}

QTEST_MAIN(DocumentSessionTest)
#include "DocumentSessionTest.moc"
