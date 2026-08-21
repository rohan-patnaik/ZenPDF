#include "QpdfOperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <thread>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

class QpdfOperationsTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesPageRanges_data();
    void validatesPageRanges();
    void refusesSourceOverwrite();
    void refusesCanonicalSymlinkSourceOverwrite();
    void refusesHardlinkSourceOverwrite();
    void refusesChangedSourceBeforePublication();
    void cancellationLeavesNoOutputOrStaging();
    void outputCapLeavesNoOutputOrStaging();
    void preservesDestinationPermissions();
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

QStringList stagingDirectories(const QTemporaryDir& directory) {
    return QDir(directory.path()).entryList(
        {QStringLiteral(".zenpdf-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
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

void QpdfOperationsTest::refusesCanonicalSymlinkSourceOverwrite() {
#ifndef Q_OS_UNIX
    QSKIP("Symbolic-link identity test requires Unix");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto alias = directory.filePath(QStringLiteral("source-alias.pdf"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();
    QVERIFY(QFile::link(source, alias));

    const auto result = QpdfOperations::extract(alias, QStringLiteral("1"), 1, source);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("never overwritten")));
#endif
}

void QpdfOperationsTest::refusesHardlinkSourceOverwrite() {
#ifndef Q_OS_UNIX
    QSKIP("Hard-link identity test requires Unix");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("hardlink.pdf"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();
    const auto sourceBytes = QFile::encodeName(source);
    const auto outputBytes = QFile::encodeName(output);
    QCOMPARE(::link(sourceBytes.constData(), outputBytes.constData()), 0);

    const auto result = QpdfOperations::extract(source, QStringLiteral("1"), 1, output);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("never overwritten")));
#endif
}

void QpdfOperationsTest::refusesChangedSourceBeforePublication() {
#ifndef Q_OS_UNIX
    QSKIP("Race regression test requires Unix process fixtures");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    const auto binDirectory = directory.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkdir(binDirectory));
    const auto fakeQpdf = QDir(binDirectory).filePath(QStringLiteral("qpdf"));
    QFile script(fakeQpdf);
    QVERIFY(script.open(QIODevice::WriteOnly));
    const QByteArray body = "#!/bin/sh\ncp \"$3\" \"$6\"\nsleep 0.3\n";
    QCOMPARE(script.write(body), body.size());
    script.close();
    QVERIFY(QFile::setPermissions(
        fakeQpdf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-original-fixture") > 0);
    file.close();

    const auto originalPath = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    std::thread modifier([source] {
        QThread::msleep(100);
        QFile changed(source);
        if (changed.open(QIODevice::Append)) {
            changed.write("-changed");
        }
    });
    const auto result = QpdfOperations::extract(source, QStringLiteral("1"), 1, output);
    modifier.join();
    qputenv("PATH", originalPath);

    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("changed while")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
#endif
}

void QpdfOperationsTest::cancellationLeavesNoOutputOrStaging() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();
    std::atomic_bool cancelled{true};

    const auto result = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, output, &cancelled);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("cancelled")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
}

void QpdfOperationsTest::outputCapLeavesNoOutputOrStaging() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    createPdf(source, QStringLiteral("Output cap fixture"));

    const auto result = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, output, nullptr, QpdfLimits{64, 5'000});
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("size safety limit")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
}

void QpdfOperationsTest::preservesDestinationPermissions() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    createPdf(source, QStringLiteral("Permission fixture"));
    QFile existing(output);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QVERIFY(existing.write("old") > 0);
    existing.close();
    QVERIFY(QFile::setPermissions(
        output, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup));
    const auto expected = QFileInfo(output).permissions();

    const auto result = QpdfOperations::extract(source, QStringLiteral("1"), 1, output);
    QVERIFY2(result.succeeded, qPrintable(result.message));
    QCOMPARE(QFileInfo(output).permissions(), expected);
    QVERIFY(stagingDirectories(directory).isEmpty());
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
