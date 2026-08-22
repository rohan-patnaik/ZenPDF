#include "QpdfOperations.h"
#include "QpdfPublication.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <cstdio>
#include <thread>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

class QpdfOperationsTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesPageRanges_data();
    void validatesPageRanges();
    void rejectsPageCountsAboveSafetyLimit();
    void rejectsOversizedSparseInput();
    void preservesExtractOrderAndUsesDeleteSetSemantics();
    void refusesSourceOverwrite();
    void refusesCanonicalSymlinkSourceOverwrite();
    void refusesHardlinkSourceOverwrite();
    void refusesChangedSourceBeforePublication();
    void refusesSameSizeSourceChangeWithRestoredMtime();
    void publicationNeverReplacesAppearedDestination();
    void publicationNeverReplacesChangedDestination();
    void cancellationLeavesNoOutputOrStaging();
    void cancellationDuringToolLeavesNoOutputOrStaging();
    void timeoutLeavesNoOutputOrStaging();
    void noisyHelperDiagnosticsAreBoundedAndPrivate();
    void oversizedPageCountOutputIsRejected();
    void outputCapLeavesNoOutputOrStaging();
    void refusesInvalidSafetyLimits();
    void refusesExistingDestination();
    void refusesMalformedInput();
    void refusesEncryptedAndRestrictedInputs();
    void refusesDeletingEveryPage();
    void mergesExtractsAndRotates();
    void extractsAndDeletesWithReopenEquivalence();
};

namespace {
void createPdf(const QString& path, const QString& text) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.drawText(QPointF(72, 72), text);
    painter.end();
}

void createPdf(const QString& path, const QStringList& pageTexts) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setTitle(QStringLiteral("Organizer structure fixture"));
    QPainter painter(&writer);
    for (qsizetype index = 0; index < pageTexts.size(); ++index) {
        if (index > 0) {
            writer.newPage();
        }
        painter.drawText(QPointF(72, 72), pageTexts.at(index));
    }
    painter.end();
}

QImage renderPage(QPdfDocument& document, int page) {
    return document.render(page, QSize(612, 792));
}

bool runQpdf(const QStringList& arguments) {
    QProcess process;
    process.start(QStringLiteral("qpdf"), arguments);
    return process.waitForFinished(5'000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

void createStructuredPdf(const QString& path) {
    const auto streamObject = [](const QByteArray& text) {
        const auto stream = QByteArray("BT /F1 18 Tf 72 720 Td (") + text + ") Tj ET\n";
        return QByteArray("<< /Length ") + QByteArray::number(stream.size()) +
               " >>\nstream\n" + stream + "endstream";
    };
    const QList<QByteArray> objects{
        "<< /Type /Catalog /Pages 2 0 R /Outlines 10 0 R /PageMode /UseOutlines >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 7 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 8 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 9 0 R /Annots [11 0 R] >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        streamObject("Page one"),
        streamObject("Page two"),
        streamObject("Page three"),
        "<< /Type /Outlines /First 12 0 R /Last 12 0 R /Count 1 >>",
        "<< /Type /Annot /Subtype /Text /Rect [72 680 90 700] /Contents (Retained note) /P 5 0 R >>",
        "<< /Title (Third page) /Parent 10 0 R /Dest [5 0 R /Fit] >>",
        "<< /Title (Organizer structure fixture) >>",
    };

    QByteArray pdf("%PDF-1.7\n%âãÏÓ\n");
    QList<qint64> offsets{0};
    for (qsizetype index = 0; index < objects.size(); ++index) {
        offsets.append(pdf.size());
        pdf.append(QByteArray::number(index + 1));
        pdf.append(" 0 obj\n");
        pdf.append(objects.at(index));
        pdf.append("\nendobj\n");
    }
    const qint64 xrefOffset = pdf.size();
    pdf.append("xref\n0 ");
    pdf.append(QByteArray::number(objects.size() + 1));
    pdf.append("\n0000000000 65535 f \n");
    for (qsizetype index = 1; index < offsets.size(); ++index) {
        pdf.append(QByteArray::number(offsets.at(index)).rightJustified(10, '0'));
        pdf.append(" 00000 n \n");
    }
    pdf.append("trailer\n<< /Size ");
    pdf.append(QByteArray::number(objects.size() + 1));
    pdf.append(" /Root 1 0 R /Info 13 0 R >>\nstartxref\n");
    pdf.append(QByteArray::number(xrefOffset));
    pdf.append("\n%%EOF\n");

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write(pdf), pdf.size());
}

QByteArray qpdfJson(const QString& path) {
    QProcess process;
    process.start(
        QStringLiteral("qpdf"),
        {QStringLiteral("--json"), QStringLiteral("--json-stream-data=none"), path});
    if (!process.waitForFinished(5'000) || process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
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
    QTest::newRow("maximum-page-count")
        << QStringLiteral("100000") << QpdfOperations::maximumPageCount << true;
    QTest::newRow("above-maximum-page-count")
        << QStringLiteral("1") << QpdfOperations::maximumPageCount + 1 << false;
    QTest::newRow("maximum-range-length")
        << QStringLiteral("10") + QStringLiteral(",1").repeated(499) << 10 << true;
    QTest::newRow("above-maximum-range-length")
        << QStringLiteral("10") + QStringLiteral(",1").repeated(500) << 10 << false;
}

void QpdfOperationsTest::validatesPageRanges() {
    QFETCH(QString, range);
    QFETCH(int, pageCount);
    QFETCH(bool, valid);
    QCOMPARE(QpdfOperations::isValidPageRange(range, pageCount), valid);
}

void QpdfOperationsTest::rejectsPageCountsAboveSafetyLimit() {
    QVERIFY(!QpdfOperations::isValidPageRange(
        QStringLiteral("1"), QpdfOperations::maximumPageCount + 1));
}

void QpdfOperationsTest::rejectsOversizedSparseInput() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("oversized.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(QpdfOperations::maximumInputBytes + 1));
    file.close();

    const auto result = QpdfOperations::extract(source, QStringLiteral("1"), 1, output);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("2 GiB safety limit")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
}

void QpdfOperationsTest::preservesExtractOrderAndUsesDeleteSetSemantics() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto extracted = directory.filePath(QStringLiteral("extracted.pdf"));
    const auto deleted = directory.filePath(QStringLiteral("deleted.pdf"));
    createPdf(source, {QStringLiteral("One"), QStringLiteral("Two"),
                       QStringLiteral("Three"), QStringLiteral("Four")});

    const auto extractResult = QpdfOperations::extract(
        source, QStringLiteral("3,1-2,2"), 4, extracted);
    QVERIFY2(extractResult.succeeded, qPrintable(extractResult.message));
    QCOMPARE(pageCount(extracted), 4);
    QPdfDocument sourceDocument;
    QPdfDocument extractedDocument;
    QCOMPARE(sourceDocument.load(source), QPdfDocument::Error::None);
    QCOMPARE(extractedDocument.load(extracted), QPdfDocument::Error::None);
    for (int outputPage = 0; outputPage < 4; ++outputPage) {
        const int expectedSourcePage[] = {2, 0, 1, 1};
        QCOMPARE(renderPage(extractedDocument, outputPage),
                 renderPage(sourceDocument, expectedSourcePage[outputPage]));
    }

    const auto deleteResult = QpdfOperations::deletePages(
        source, QStringLiteral("3,1-2,2"), 4, deleted);
    QVERIFY2(deleteResult.succeeded, qPrintable(deleteResult.message));
    QCOMPARE(pageCount(deleted), 1);
    QPdfDocument deletedDocument;
    QCOMPARE(deletedDocument.load(deleted), QPdfDocument::Error::None);
    QCOMPARE(renderPage(deletedDocument, 0), renderPage(sourceDocument, 3));

    const auto allPages = QpdfOperations::deletePages(
        source,
        QStringLiteral("2-4,1-2,3"),
        4,
        directory.filePath(QStringLiteral("all-pages.pdf")));
    QVERIFY(!allPages.succeeded);
    QVERIFY(allPages.message.contains(QStringLiteral("At least one page")));
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
    const QByteArray body =
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  --is-encrypted) exit 2 ;;\n"
        "  --check) exit 0 ;;\n"
        "  --show-npages) echo 1; exit 0 ;;\n"
        "esac\n"
        "cp \"$1\" \"$6\"\n"
        "sleep 0.3\n";
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

void QpdfOperationsTest::refusesSameSizeSourceChangeWithRestoredMtime() {
#ifndef Q_OS_LINUX
    QSKIP("Nanosecond change-time regression requires Linux");
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
    const QByteArray body =
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  --is-encrypted) exit 2 ;;\n"
        "  --check) exit 0 ;;\n"
        "  --show-npages) echo 1; exit 0 ;;\n"
        "esac\n"
        "cp \"$1\" \"$6\"\n"
        "sleep 0.3\n";
    QCOMPARE(script.write(body), body.size());
    script.close();
    QVERIFY(QFile::setPermissions(
        fakeQpdf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const QByteArray original = "%PDF-AAAA";
    const QByteArray replacement = "%PDF-BBBB";
    QCOMPARE(original.size(), replacement.size());
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), original.size());
    file.close();
    const auto originalMtime = QFileInfo(source).lastModified();

    const auto originalPath = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    std::thread modifier([source, replacement, originalMtime] {
        QThread::msleep(100);
        QFile changed(source);
        if (changed.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
            changed.write(replacement);
            changed.flush();
            changed.setFileTime(originalMtime, QFileDevice::FileModificationTime);
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

void QpdfOperationsTest::publicationNeverReplacesAppearedDestination() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto staging = directory.filePath(QStringLiteral("staging.pdf"));
    const auto destination = directory.filePath(QStringLiteral("destination.pdf"));
    QFile staged(staging);
    QVERIFY(staged.open(QIODevice::WriteOnly));
    QCOMPARE(staged.write("completed"), qint64{9});
    staged.close();
    QFile appeared(destination);
    QVERIFY(appeared.open(QIODevice::WriteOnly));
    QCOMPARE(appeared.write("appeared"), qint64{8});
    appeared.close();

    QCOMPARE(QpdfPublication::publishNoReplace(staging, destination),
             QpdfPublication::Result::DestinationExists);
    QVERIFY(QFileInfo::exists(staging));
    QVERIFY(appeared.open(QIODevice::ReadOnly));
    QCOMPARE(appeared.readAll(), QByteArray("appeared"));
}

void QpdfOperationsTest::publicationNeverReplacesChangedDestination() {
#ifndef Q_OS_UNIX
    QSKIP("Atomic replacement fixture requires Unix");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto staging = directory.filePath(QStringLiteral("staging.pdf"));
    const auto destination = directory.filePath(QStringLiteral("destination.pdf"));
    const auto replacement = directory.filePath(QStringLiteral("replacement.pdf"));
    for (const auto& item : QList<QPair<QString, QByteArray>>{
             {staging, "completed"}, {destination, "first"}, {replacement, "replacement"}}) {
        QFile file(item.first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(item.second), item.second.size());
    }
    const auto replacementBytes = QFile::encodeName(replacement);
    const auto destinationBytes = QFile::encodeName(destination);
    QCOMPARE(std::rename(replacementBytes.constData(), destinationBytes.constData()), 0);

    QCOMPARE(QpdfPublication::publishNoReplace(staging, destination),
             QpdfPublication::Result::DestinationExists);
    QFile current(destination);
    QVERIFY(current.open(QIODevice::ReadOnly));
    QCOMPARE(current.readAll(), QByteArray("replacement"));
    QVERIFY(QFileInfo::exists(staging));
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

void QpdfOperationsTest::cancellationDuringToolLeavesNoOutputOrStaging() {
#ifndef Q_OS_UNIX
    QSKIP("Cancellation fixture requires a Unix helper script");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    const auto marker = directory.filePath(QStringLiteral("descendant-marker"));
    const auto binDirectory = directory.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkdir(binDirectory));
    const auto fakeQpdf = QDir(binDirectory).filePath(QStringLiteral("qpdf"));
    QFile script(fakeQpdf);
    QVERIFY(script.open(QIODevice::WriteOnly));
    const QByteArray body =
        "#!/bin/sh\n"
        "if [ \"$1\" = --is-encrypted ]; then exit 2; fi\n"
        "(sleep 0.4; touch \"$ZENPDF_TEST_MARKER\") &\n"
        "sleep 5\n";
    QCOMPARE(script.write(body), body.size());
    script.close();
    QVERIFY(QFile::setPermissions(
        fakeQpdf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();

    const auto originalPath = qgetenv("PATH");
    const auto originalMarker = qgetenv("ZENPDF_TEST_MARKER");
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    qputenv("ZENPDF_TEST_MARKER", QFile::encodeName(marker));
    std::atomic_bool cancelled{false};
    std::thread cancelThread([&cancelled] {
        QThread::msleep(150);
        cancelled.store(true);
    });
    const auto result = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, output, &cancelled);
    cancelThread.join();
    qputenv("PATH", originalPath);
    if (originalMarker.isNull()) {
        qunsetenv("ZENPDF_TEST_MARKER");
    } else {
        qputenv("ZENPDF_TEST_MARKER", originalMarker);
    }
    QThread::msleep(600);

    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("cancelled")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(!QFileInfo::exists(marker));
    QVERIFY(stagingDirectories(directory).isEmpty());
#endif
}

void QpdfOperationsTest::timeoutLeavesNoOutputOrStaging() {
#ifndef Q_OS_UNIX
    QSKIP("Timeout fixture requires a Unix helper script");
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
    const QByteArray body =
        "#!/bin/sh\n"
        "if [ \"$1\" = --is-encrypted ]; then exit 2; fi\n"
        "exec sleep 5\n";
    QCOMPARE(script.write(body), body.size());
    script.close();
    QVERIFY(QFile::setPermissions(
        fakeQpdf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();

    const auto originalPath = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    const auto result = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, output, nullptr, QpdfLimits{1024, 150});
    qputenv("PATH", originalPath);

    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("time safety limit")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
#endif
}

void QpdfOperationsTest::noisyHelperDiagnosticsAreBoundedAndPrivate() {
#ifndef Q_OS_UNIX
    QSKIP("Noisy helper fixture requires Unix bounded pipes");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    const auto binDirectory = directory.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkdir(binDirectory));
    const auto fakeQpdf = QDir(binDirectory).filePath(QStringLiteral("qpdf"));
    const auto capturedPathFile = directory.filePath(QStringLiteral("captured-helper-path"));
    QFile script(fakeQpdf);
    QVERIFY(script.open(QIODevice::WriteOnly));
    const QByteArray body =
        "#!/bin/sh\n"
        "if [ \"$1\" = --is-encrypted ]; then exit 2; fi\n"
        "printf %s \"$1\" >\"$CAPTURED_QPDF_PATH\"\n"
        "path_bytes=$(printf %s \"$1\" | wc -c)\n"
        "leading=512\n"
        "repetitions=20\n"
        "used=$((leading + repetitions * (path_bytes + 1)))\n"
        "padding=$((8192 - used - path_bytes / 2))\n"
        "head -c \"$leading\" /dev/zero | tr '\\000' ' ' >&2\n"
        "i=0\n"
        "while [ $i -lt $repetitions ]; do\n"
        "  printf '%s\\n' \"$1\" >&2\n"
        "  i=$((i + 1))\n"
        "done\n"
        "head -c \"$padding\" /dev/zero | tr '\\000' x >&2\n"
        "printf '%s\\n' \"$1\" >&2\n"
        "head -c 20000 /dev/zero | tr '\\000' y >&2\n"
        "printf 'untrusted stdout\\n'\n"
        "exit 1\n";
    QCOMPARE(script.write(body), body.size());
    script.close();
    QVERIFY(QFile::setPermissions(
        fakeQpdf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();

    const auto originalPath = qgetenv("PATH");
    const auto originalCapturedPath = qgetenv("CAPTURED_QPDF_PATH");
    qputenv("CAPTURED_QPDF_PATH", QFile::encodeName(capturedPathFile));
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    const auto result = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, output, nullptr, QpdfLimits{1024, 5'000});
    qputenv("PATH", originalPath);
    if (originalCapturedPath.isNull()) {
        qunsetenv("CAPTURED_QPDF_PATH");
    } else {
        qputenv("CAPTURED_QPDF_PATH", originalCapturedPath);
    }

    QVERIFY(!result.succeeded);
    QFile capturedPath(capturedPathFile);
    QVERIFY(capturedPath.open(QIODevice::ReadOnly));
    const auto sensitiveToken = QString::fromLocal8Bit(capturedPath.readAll());
    QVERIFY(!sensitiveToken.isEmpty());
    QVERIFY(result.message.toUtf8().size() <= 8 * 1024);
    QVERIFY(result.message.contains(QStringLiteral("<private temporary directory>")));
    QVERIFY(!result.message.contains(QStringLiteral(".zenpdf-")));
    QVERIFY(!result.message.contains(directory.path()));
    QVERIFY(!result.message.contains(sensitiveToken.left(sensitiveToken.size() / 2)));
    QStringDecoder decoder(QStringDecoder::Utf8);
    (void)decoder.decode(result.message.toUtf8());
    QVERIFY(!decoder.hasError());
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());

    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray exactLimitBody =
        "#!/bin/sh\n"
        "if [ \"$1\" = --is-encrypted ]; then exit 2; fi\n"
        "head -c 20000 /dev/zero | tr '\\000' z >&2\n"
        "exit 1\n";
    QCOMPARE(script.write(exactLimitBody), exactLimitBody.size());
    script.close();
    const auto exactLimitOutput = directory.filePath(QStringLiteral("exact-limit.pdf"));
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    const auto exactLimitResult = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, exactLimitOutput, nullptr, QpdfLimits{1024, 5'000});
    qputenv("PATH", originalPath);
    QVERIFY(!exactLimitResult.succeeded);
    QCOMPARE(exactLimitResult.message.toUtf8().size(), 8 * 1024);
    QCOMPARE(exactLimitResult.message.size(), 8 * 1024);
    QVERIFY(!exactLimitResult.message.contains(directory.path()));
    QVERIFY(!QFileInfo::exists(exactLimitOutput));
    QVERIFY(stagingDirectories(directory).isEmpty());
#endif
}

void QpdfOperationsTest::oversizedPageCountOutputIsRejected() {
#ifndef Q_OS_UNIX
    QSKIP("Noisy helper fixture requires Unix bounded pipes");
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
    const QByteArray body =
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  --is-encrypted) exit 2 ;;\n"
        "  --check) printf 'ignored warning\\n' >&2; exit 0 ;;\n"
        "  --show-npages)\n"
        "    i=0; while [ $i -lt 100 ]; do printf '1234567890'; printf 'noise\\n' >&2; i=$((i + 1)); done\n"
        "    exit 0 ;;\n"
        "esac\n"
        "for last do :; done\n"
        "cp \"$1\" \"$last\"\n";
    QCOMPARE(script.write(body), body.size());
    script.close();
    QVERIFY(QFile::setPermissions(
        fakeQpdf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-placeholder") > 0);
    file.close();

    const auto originalPath = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(binDirectory) + ':' + originalPath);
    const auto result = QpdfOperations::extract(source, QStringLiteral("1"), 1, output);
    qputenv("PATH", originalPath);

    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("oversized page-count response")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
#endif
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

void QpdfOperationsTest::refusesInvalidSafetyLimits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    createPdf(source, QStringLiteral("Safety fixture"));

    const auto oversizedOutput = QpdfOperations::extract(
        source,
        QStringLiteral("1"),
        1,
        output,
        nullptr,
        QpdfLimits{QpdfOperations::maximumInputBytes + 1, 5'000});
    QVERIFY(!oversizedOutput.succeeded);
    QVERIFY(oversizedOutput.message.contains(QStringLiteral("Invalid operation safety limits")));

    const auto oversizedTimeout = QpdfOperations::extract(
        source,
        QStringLiteral("1"),
        1,
        output,
        nullptr,
        QpdfLimits{1024, QpdfOperations::maximumOperationTimeoutMs + 1});
    QVERIFY(!oversizedTimeout.succeeded);
    QVERIFY(oversizedTimeout.message.contains(QStringLiteral("Invalid operation safety limits")));
    QVERIFY(!QFileInfo::exists(output));
}

void QpdfOperationsTest::refusesExistingDestination() {
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
    const auto result = QpdfOperations::extract(source, QStringLiteral("1"), 1, output);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("never replace")));
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("old"));
    QVERIFY(stagingDirectories(directory).isEmpty());
}

void QpdfOperationsTest::refusesMalformedInput() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("malformed.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("%PDF-malformed") > 0);
    file.close();

    const auto result = QpdfOperations::extract(
        source, QStringLiteral("1"), 1, output);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("malformed or unsupported")));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(stagingDirectories(directory).isEmpty());
}

void QpdfOperationsTest::refusesEncryptedAndRestrictedInputs() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto encrypted = directory.filePath(QStringLiteral("encrypted.pdf"));
    const auto restricted = directory.filePath(QStringLiteral("restricted.pdf"));
    createPdf(source, QStringList{QStringLiteral("First"), QStringLiteral("Second")});
    QVERIFY(runQpdf(
        {QStringLiteral("--encrypt"), QStringLiteral("reader"), QStringLiteral("owner"),
         QStringLiteral("256"), QStringLiteral("--"), source, encrypted}));
    QVERIFY(runQpdf(
        {QStringLiteral("--encrypt"), QString{}, QStringLiteral("owner"), QStringLiteral("256"),
         QStringLiteral("--modify=none"), QStringLiteral("--extract=n"), QStringLiteral("--"),
         source, restricted}));

    for (const auto& input : {encrypted, restricted}) {
        const auto output = input + QStringLiteral("-output.pdf");
        const auto result = QpdfOperations::deletePages(
            input, QStringLiteral("1"), 2, output);
        QVERIFY(!result.succeeded);
        QVERIFY(result.message.contains(QStringLiteral("Encrypted or permission-restricted")));
        QVERIFY(!QFileInfo::exists(output));
    }
    QVERIFY(stagingDirectories(directory).isEmpty());
}

void QpdfOperationsTest::refusesDeletingEveryPage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto output = directory.filePath(QStringLiteral("output.pdf"));
    createPdf(source, QStringList{QStringLiteral("First"), QStringLiteral("Second")});

    const auto result = QpdfOperations::deletePages(
        source, QStringLiteral("1-2"), 2, output);
    QVERIFY(!result.succeeded);
    QVERIFY(result.message.contains(QStringLiteral("At least one page")));
    QVERIFY(!QFileInfo::exists(output));
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

void QpdfOperationsTest::extractsAndDeletesWithReopenEquivalence() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto extracted = directory.filePath(QStringLiteral("extracted.pdf"));
    const auto deleted = directory.filePath(QStringLiteral("deleted.pdf"));
    createStructuredPdf(source);
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const auto originalBytes = sourceFile.readAll();
    sourceFile.close();

    const auto extractResult = QpdfOperations::extract(
        source, QStringLiteral("2-3"), 3, extracted);
    QVERIFY2(extractResult.succeeded, qPrintable(extractResult.message));
    const auto deleteResult = QpdfOperations::deletePages(
        source, QStringLiteral("1-2,2"), 3, deleted);
    QVERIFY2(deleteResult.succeeded, qPrintable(deleteResult.message));

    QPdfDocument sourceDocument;
    QPdfDocument extractedDocument;
    QPdfDocument deletedDocument;
    QCOMPARE(sourceDocument.load(source), QPdfDocument::Error::None);
    QCOMPARE(extractedDocument.load(extracted), QPdfDocument::Error::None);
    QCOMPARE(deletedDocument.load(deleted), QPdfDocument::Error::None);
    QCOMPARE(extractedDocument.pageCount(), 2);
    QCOMPARE(deletedDocument.pageCount(), 1);
    QCOMPARE(
        extractedDocument.metaData(QPdfDocument::MetaDataField::Title),
        sourceDocument.metaData(QPdfDocument::MetaDataField::Title));
    QCOMPARE(
        deletedDocument.metaData(QPdfDocument::MetaDataField::Title),
        sourceDocument.metaData(QPdfDocument::MetaDataField::Title));
    QCOMPARE(renderPage(extractedDocument, 0), renderPage(sourceDocument, 1));
    QCOMPARE(renderPage(extractedDocument, 1), renderPage(sourceDocument, 2));
    QCOMPARE(renderPage(deletedDocument, 0), renderPage(sourceDocument, 2));
    const auto extractedStructure = qpdfJson(extracted);
    const auto deletedStructure = qpdfJson(deleted);
    QVERIFY(extractedStructure.contains("Retained note"));
    QVERIFY(extractedStructure.contains("Third page"));
    QVERIFY(deletedStructure.contains("Retained note"));
    QVERIFY(deletedStructure.contains("Third page"));

    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), originalBytes);
}

QTEST_MAIN(QpdfOperationsTest)
#include "QpdfOperationsTest.moc"
