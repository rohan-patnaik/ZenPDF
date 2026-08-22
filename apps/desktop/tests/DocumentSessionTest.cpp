#include "DocumentSession.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QtTest>

#include <utility>

class DocumentSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void tracksCleanDirtyUndoAndRedo();
    void savedRevisionFollowsUndoAndBranching();
    void boundsUndoHistory();
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

    session.push(new AddCommand(&value, 3, QStringLiteral("Add pages")));
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
    session.push(new AddCommand(&value, 1));
    session.markSaved();
    QVERIFY(!session.isDirty());

    session.undo();
    QCOMPARE(value, 0);
    QVERIFY(session.isDirty());
    session.redo();
    QCOMPARE(value, 1);
    QVERIFY(!session.isDirty());

    session.undo();
    session.push(new AddCommand(&value, 5));
    QCOMPARE(value, 5);
    QVERIFY(session.isDirty());
    QVERIFY(!session.canRedo());
}

void DocumentSessionTest::boundsUndoHistory() {
    DocumentSession session(QStringLiteral("document.pdf"));
    int value = 0;
    for (int index = 0; index < DocumentSession::maximumUndoCommands + 1; ++index) {
        session.push(new AddCommand(&value, 1));
    }

    QCOMPARE(session.undoCommandCount(), DocumentSession::maximumUndoCommands);
    QCOMPARE(value, DocumentSession::maximumUndoCommands + 1);
    while (session.canUndo()) {
        session.undo();
    }
    QCOMPARE(value, 1);
    QVERIFY(session.isDirty());
}

QTEST_MAIN(DocumentSessionTest)
#include "DocumentSessionTest.moc"
