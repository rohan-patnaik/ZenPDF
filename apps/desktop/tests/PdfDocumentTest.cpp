#include <QFile>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class PdfDocumentTest final : public QObject {
    Q_OBJECT

private slots:
    void opensPasswordProtectedDocument();
    void rejectsInvalidPdf();
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

void PdfDocumentTest::opensPasswordProtectedDocument() {
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

    QPdfDocument document;
    QCOMPARE(document.load(encrypted), QPdfDocument::Error::IncorrectPassword);
    document.setPassword(QStringLiteral("reader"));
    QCOMPARE(document.load(encrypted), QPdfDocument::Error::None);
    QCOMPARE(document.pageCount(), 1);
}

void PdfDocumentTest::rejectsInvalidPdf() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("invalid.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("not a pdf") > 0);
    file.close();
    QPdfDocument document;
    QCOMPARE(document.load(path), QPdfDocument::Error::InvalidFileFormat);
}

QTEST_MAIN(PdfDocumentTest)
#include "PdfDocumentTest.moc"
