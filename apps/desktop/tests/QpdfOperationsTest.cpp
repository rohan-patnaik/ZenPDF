#include "QpdfOperations.h"

#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class QpdfOperationsTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesPageRanges_data();
    void validatesPageRanges();
    void refusesSourceOverwrite();
    void mergesExtractsAndRotates();
};

namespace {
void createPdf(const QString& path, const QString& text) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.drawText(QPointF(72, 72), text);
    painter.end();
}

int pageCount(const QString& path) {
    QProcess process;
    process.start(QStringLiteral("qpdf"), {QStringLiteral("--show-npages"), path});
    if (!process.waitForFinished(5'000) || process.exitCode() != 0) {
        return -1;
    }
    return QString::fromLatin1(process.readAllStandardOutput()).trimmed().toInt();
}
}

void QpdfOperationsTest::validatesPageRanges_data() {
    QTest::addColumn<QString>("range");
    QTest::addColumn<int>("pageCount");
    QTest::addColumn<bool>("valid");
    QTest::newRow("single") << QStringLiteral("1") << 5 << true;
    QTest::newRow("ranges") << QStringLiteral("1-3,5") << 5 << true;
    QTest::newRow("zero") << QStringLiteral("0") << 5 << false;
    QTest::newRow("reverse") << QStringLiteral("4-2") << 5 << false;
    QTest::newRow("past-end") << QStringLiteral("1-6") << 5 << false;
    QTest::newRow("open-range") << QStringLiteral("1-z") << 5 << false;
    QTest::newRow("whitespace") << QStringLiteral("1, 2") << 5 << false;
    QTest::newRow("empty-document") << QStringLiteral("1") << 0 << false;
}

void QpdfOperationsTest::validatesPageRanges() {
    QFETCH(QString, range);
    QFETCH(int, pageCount);
    QFETCH(bool, valid);
    QCOMPARE(QpdfOperations::isValidPageRange(range, pageCount), valid);
}

void QpdfOperationsTest::refusesSourceOverwrite() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("source.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("%PDF-placeholder"), qint64{16});
    file.close();
    const auto result = QpdfOperations::extract(
        path, QStringLiteral("1"), 1, path);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("never overwritten")));
}

void QpdfOperationsTest::mergesExtractsAndRotates() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto first = directory.filePath(QStringLiteral("first.pdf"));
    const auto second = directory.filePath(QStringLiteral("second.pdf"));
    const auto merged = directory.filePath(QStringLiteral("merged.pdf"));
    const auto extracted = directory.filePath(QStringLiteral("extracted.pdf"));
    const auto rotated = directory.filePath(QStringLiteral("rotated.pdf"));
    createPdf(first, QStringLiteral("First"));
    createPdf(second, QStringLiteral("Second"));

    const auto mergeResult = QpdfOperations::merge({first, second}, merged);
    QVERIFY2(mergeResult.succeeded, qPrintable(mergeResult.message));
    QCOMPARE(pageCount(merged), 2);

    const auto extractResult = QpdfOperations::extract(merged, QStringLiteral("2"), 2, extracted);
    QVERIFY2(extractResult.succeeded, qPrintable(extractResult.message));
    QCOMPARE(pageCount(extracted), 1);

    const auto rotateResult = QpdfOperations::rotate(extracted, QStringLiteral("1"), 1, true, rotated);
    QVERIFY2(rotateResult.succeeded, qPrintable(rotateResult.message));
    QCOMPARE(pageCount(rotated), 1);
}

QTEST_MAIN(QpdfOperationsTest)
#include "QpdfOperationsTest.moc"
