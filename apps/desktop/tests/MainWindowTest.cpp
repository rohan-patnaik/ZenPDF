#include "MainWindow.h"

#include "DocumentSession.h"
#include "DocumentWidget.h"
#include "LocalState.h"
#include "QpdfOperations.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoCommand>
#include <QtTest>

#include <memory>
#include <utility>

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesKeyboardAccessibleDeleteAction();
    void routesUndoForFirstDocumentImmediately();
    void routesUndoAndDirtyStateToActiveDocument();
    void dirtyTabRequiresExplicitDiscard();
    void dirtyWindowRequiresExplicitDiscard();
    void successfulOrganizerOutputOpensAsCleanTab();
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
    MainWindow window(state);

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
    MainWindow window(state);
    window.openFiles({path});
    auto* undoAction = window.findChild<QAction*>(QStringLiteral("undoAction"));
    QVERIFY(undoAction != nullptr);
    int value = 0;

    QVERIFY(window.currentDocument()->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("First change"))));
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
    MainWindow window(state);
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
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("First change"))));
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

void MainWindowTest::dirtyTabRequiresExplicitDiscard() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    createPdf(path, QStringLiteral("Document"));
    LocalState state(directory.filePath(QStringLiteral("state.sqlite")));
    QVERIFY(state.initialize());
    MainWindow window(state);
    window.openFiles({path});
    auto* document = window.currentDocument();
    QVERIFY(document != nullptr);
    int value = 0;
    QVERIFY(document->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("Unsaved change"))));

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
    MainWindow window(state);
    window.openFiles({path});
    int value = 0;
    QVERIFY(window.currentDocument()->session().push(
        std::make_unique<AddCommand>(&value, 1, QStringLiteral("Unsaved change"))));

    QCloseEvent cancelEvent;
    answerMessageBox(QMessageBox::Cancel);
    window.closeEvent(&cancelEvent);
    QVERIFY(!cancelEvent.isAccepted());

    QCloseEvent discardEvent;
    answerMessageBox(QMessageBox::Discard);
    window.closeEvent(&discardEvent);
    QVERIFY(discardEvent.isAccepted());
}

void MainWindowTest::successfulOrganizerOutputOpensAsCleanTab() {
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
    MainWindow window(state);
    window.openFiles({source});
    QCOMPARE(window.tabs_->count(), 1);
    auto* sourceDocument = window.currentDocument();
    QVERIFY(sourceDocument != nullptr);
    QVERIFY(!sourceDocument->isWindowModified());
    QVERIFY(!window.tabs_->tabText(0).contains('*'));

    window.finishOrganizerTask(
        output,
        QpdfResult{true, QStringLiteral("Saved output")},
        QStringLiteral("Operation failed"));

    QCOMPARE(window.tabs_->count(), 2);
    QCOMPARE(window.currentDocument()->filePath(), output);
    QVERIFY(!window.currentDocument()->isWindowModified());
    QVERIFY(!sourceDocument->isWindowModified());
    QVERIFY(!window.tabs_->tabText(0).contains('*'));
    QVERIFY(!window.tabs_->tabText(1).contains('*'));
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), sourceBytes);
}

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
