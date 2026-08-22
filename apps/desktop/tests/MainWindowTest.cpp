#include "MainWindow.h"

#include "DocumentWidget.h"
#include "LocalState.h"
#include "QpdfOperations.h"

#include <QAction>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesKeyboardAccessibleDeleteAction();
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
