#include "DocumentWidget.h"

#include <QPainter>
#include <QPdfWriter>
#include <QListView>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class DocumentWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void clearsPasswordAfterUnlock();
    void exposesAccessibleThumbnailNames();
};

namespace {
void createPdf(const QString& path) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.drawText(QPointF(72, 72), QStringLiteral("Private local fixture"));
    painter.end();
}
}

void DocumentWidgetTest::clearsPasswordAfterUnlock() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto encrypted = directory.filePath(QStringLiteral("encrypted.pdf"));
    createPdf(source);

    QProcess qpdf;
    qpdf.start(
        QStringLiteral("qpdf"),
        {QStringLiteral("--encrypt"), QStringLiteral("reader"), QStringLiteral("owner"),
         QStringLiteral("256"), QStringLiteral("--"), source, encrypted});
    QVERIFY(qpdf.waitForFinished(5'000));
    QCOMPARE(qpdf.exitCode(), 0);

    DocumentWidget widget(encrypted);
    QVERIFY(widget.needsPassword());
    QVERIFY(widget.unlock(QStringLiteral("reader")));
    QCOMPARE(widget.document_->password(), QString{});
}

void DocumentWidgetTest::exposesAccessibleThumbnailNames() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("accessible.pdf"));
    createPdf(source);

    DocumentWidget widget(source);
    QVERIFY(widget.isReady());
    QListView* thumbnails = nullptr;
    for (auto* candidate : widget.findChildren<QListView*>()) {
        if (candidate->accessibleName() == QStringLiteral("Page thumbnails")) {
            thumbnails = candidate;
            break;
        }
    }
    QVERIFY(thumbnails != nullptr);
    QCOMPARE(thumbnails->model()->rowCount(), 1);
    QCOMPARE(thumbnails->model()->index(0, 0).data(Qt::AccessibleTextRole).toString(),
             QStringLiteral("Page 1"));
}

QTEST_MAIN(DocumentWidgetTest)
#include "DocumentWidgetTest.moc"
