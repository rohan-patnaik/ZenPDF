#include "QpdfOperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <cstdio>

namespace {
constexpr int kMaximumInputs = 100;
constexpr int kStartTimeoutMs = 5'000;
constexpr int kOperationTimeoutMs = 120'000;

QpdfResult validateInput(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        return {false, QStringLiteral("An input PDF is missing or unreadable.")};
    }
    if (info.size() > QpdfOperations::maximumInputBytes) {
        return {false, QStringLiteral("An input exceeds the 2 GiB safety limit.")};
    }
    return {true, {}};
}

QString normalizedPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
}

bool QpdfOperations::isValidPageRange(const QString& range, int pageCount) {
    if (pageCount < 1 || range.isEmpty() || range.size() > 1'000) {
        return false;
    }
    static const QRegularExpression syntax(QStringLiteral("^[1-9][0-9]*(?:-[1-9][0-9]*)?(?:,[1-9][0-9]*(?:-[1-9][0-9]*)?)*$"));
    if (!syntax.match(range).hasMatch()) {
        return false;
    }
    const auto parts = range.split(',');
    for (const auto& part : parts) {
        const auto bounds = part.split('-');
        bool firstOk = false;
        const int first = bounds.at(0).toInt(&firstOk);
        int last = first;
        bool lastOk = firstOk;
        if (bounds.size() == 2) {
            last = bounds.at(1).toInt(&lastOk);
        }
        if (!firstOk || !lastOk || first < 1 || last < first || last > pageCount) {
            return false;
        }
    }
    return true;
}

QpdfResult QpdfOperations::merge(
    const QStringList& inputPaths,
    const QString& outputPath,
    const std::atomic_bool* cancelled) {
    if (inputPaths.size() < 2 || inputPaths.size() > kMaximumInputs) {
        return {false, QStringLiteral("Choose between 2 and 100 input PDFs.")};
    }
    QStringList arguments{QStringLiteral("--empty"), QStringLiteral("--pages")};
    for (const auto& path : inputPaths) {
        const auto validation = validateInput(path);
        if (!validation.succeeded) {
            return validation;
        }
        arguments << path << QStringLiteral("1-z");
    }
    arguments << QStringLiteral("--");
    return run(arguments, outputPath, inputPaths, cancelled);
}

QpdfResult QpdfOperations::extract(
    const QString& inputPath,
    const QString& pageRange,
    int pageCount,
    const QString& outputPath,
    const std::atomic_bool* cancelled) {
    const auto validation = validateInput(inputPath);
    if (!validation.succeeded) {
        return validation;
    }
    if (!isValidPageRange(pageRange, pageCount)) {
        return {false, QStringLiteral("Enter pages such as 1-3,5 within this document.")};
    }
    return run(
        {QStringLiteral("--empty"), QStringLiteral("--pages"), inputPath, pageRange, QStringLiteral("--")},
        outputPath,
        {inputPath},
        cancelled);
}

QpdfResult QpdfOperations::rotate(
    const QString& inputPath,
    const QString& pageRange,
    int pageCount,
    bool clockwise,
    const QString& outputPath,
    const std::atomic_bool* cancelled) {
    const auto validation = validateInput(inputPath);
    if (!validation.succeeded) {
        return validation;
    }
    if (!isValidPageRange(pageRange, pageCount)) {
        return {false, QStringLiteral("Enter pages such as 1-3,5 within this document.")};
    }
    const auto rotation = QStringLiteral("--rotate=%1:%2")
                              .arg(clockwise ? QStringLiteral("+90") : QStringLiteral("-90"), pageRange);
    return run({inputPath, rotation}, outputPath, {inputPath}, cancelled);
}

QpdfResult QpdfOperations::run(
    const QStringList& arguments,
    const QString& outputPath,
    const QStringList& protectedInputPaths,
    const std::atomic_bool* cancelled) {
    if (outputPath.trimmed().isEmpty()) {
        return {false, QStringLiteral("Choose an output path.")};
    }
    const auto cleanOutput = normalizedPath(outputPath);
    for (const auto& input : protectedInputPaths) {
        if (cleanOutput == normalizedPath(input)) {
            return {false, QStringLiteral("Choose a new output file; the source is never overwritten.")};
        }
    }

    const QFileInfo outputInfo(cleanOutput);
    QDir outputDirectory(outputInfo.absolutePath());
    if (!outputDirectory.exists()) {
        return {false, QStringLiteral("The output directory does not exist.")};
    }
    QTemporaryFile temporary(outputDirectory.filePath(QStringLiteral(".zenpdf-XXXXXX.pdf")));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        return {false, QStringLiteral("Could not create a private temporary output file.")};
    }
    const auto temporaryPath = temporary.fileName();
    temporary.close();
    QFile::remove(temporaryPath);

    auto processArguments = arguments;
    processArguments << temporaryPath;
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("qpdf"), processArguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(kStartTimeoutMs)) {
        return {false, QStringLiteral("qpdf is not installed or could not be started.")};
    }

    int elapsedMs = 0;
    while (!process.waitForFinished(100)) {
        elapsedMs += 100;
        if ((cancelled != nullptr && cancelled->load()) || elapsedMs >= kOperationTimeoutMs) {
            process.kill();
            process.waitForFinished();
            QFile::remove(temporaryPath);
            return {false, elapsedMs >= kOperationTimeoutMs
                               ? QStringLiteral("The operation exceeded the two-minute safety limit.")
                               : QStringLiteral("The operation was cancelled.")};
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QFile::remove(temporaryPath);
        auto detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (detail.size() > 800) {
            detail = detail.left(800) + QStringLiteral("…");
        }
        return {false, detail.isEmpty() ? QStringLiteral("qpdf could not complete the operation.") : detail};
    }

    QFile result(temporaryPath);
    if (!result.open(QIODevice::ReadOnly) || result.size() < 8 ||
        result.size() > maximumInputBytes || result.read(5) != QByteArrayLiteral("%PDF-")) {
        QFile::remove(temporaryPath);
        return {false, QStringLiteral("The generated file did not pass basic PDF validation.")};
    }
    result.close();

    const auto sourceBytes = QFile::encodeName(temporaryPath);
    const auto destinationBytes = QFile::encodeName(cleanOutput);
    if (std::rename(sourceBytes.constData(), destinationBytes.constData()) != 0) {
        QFile::remove(temporaryPath);
        return {false, QStringLiteral("Could not atomically place the completed output file.")};
    }
    temporary.setAutoRemove(false);
    return {true, QStringLiteral("Saved %1").arg(QDir::toNativeSeparators(cleanOutput))};
}
