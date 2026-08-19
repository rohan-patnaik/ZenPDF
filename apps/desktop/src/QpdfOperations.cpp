#include "QpdfOperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cstdio>
#include <optional>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
constexpr int kMaximumInputs = 100;
constexpr int kStartTimeoutMs = 5'000;
constexpr qsizetype kMaximumDiagnosticBytes = 8 * 1024;
constexpr auto kOwnerDirectoryPermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
constexpr auto kOwnerFilePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

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
    const std::atomic_bool* cancelled,
    QpdfLimits limits) {
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
    return run(arguments, outputPath, inputPaths, cancelled, limits);
}

QpdfResult QpdfOperations::extract(
    const QString& inputPath,
    const QString& pageRange,
    int pageCount,
    const QString& outputPath,
    const std::atomic_bool* cancelled,
    QpdfLimits limits) {
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
        cancelled,
        limits);
}

QpdfResult QpdfOperations::rotate(
    const QString& inputPath,
    const QString& pageRange,
    int pageCount,
    bool clockwise,
    const QString& outputPath,
    const std::atomic_bool* cancelled,
    QpdfLimits limits) {
    const auto validation = validateInput(inputPath);
    if (!validation.succeeded) {
        return validation;
    }
    if (!isValidPageRange(pageRange, pageCount)) {
        return {false, QStringLiteral("Enter pages such as 1-3,5 within this document.")};
    }
    const auto rotation = QStringLiteral("--rotate=%1:%2")
                              .arg(clockwise ? QStringLiteral("+90") : QStringLiteral("-90"), pageRange);
    return run({inputPath, rotation}, outputPath, {inputPath}, cancelled, limits);
}

QpdfResult QpdfOperations::run(
    const QStringList& arguments,
    const QString& outputPath,
    const QStringList& protectedInputPaths,
    const std::atomic_bool* cancelled,
    QpdfLimits limits) {
    if (outputPath.trimmed().isEmpty()) {
        return {false, QStringLiteral("Choose an output path.")};
    }
    if (limits.maximumOutputBytes < 8 || limits.operationTimeoutMs < 1) {
        return {false, QStringLiteral("Invalid operation safety limits.")};
    }
    if (cancelled != nullptr && cancelled->load()) {
        return {false, QStringLiteral("The operation was cancelled.")};
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
    if (outputInfo.isSymLink()) {
        return {false, QStringLiteral("A symbolic link cannot be used as the output file.")};
    }
    if (outputInfo.exists() && !outputInfo.isFile()) {
        return {false, QStringLiteral("The output path is not a regular file.")};
    }
    std::optional<QFileDevice::Permissions> destinationPermissions;
    if (outputInfo.exists()) {
        destinationPermissions = outputInfo.permissions();
    }

    QTemporaryDir stagingDirectory(outputDirectory.filePath(QStringLiteral(".zenpdf-XXXXXX")));
    if (!stagingDirectory.isValid() ||
        !QFile::setPermissions(stagingDirectory.path(), kOwnerDirectoryPermissions) ||
        (QFileInfo(stagingDirectory.path()).permissions() &
         (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
          QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther))) {
        return {false, QStringLiteral("Could not create a private temporary output directory.")};
    }
    const auto temporaryPath = stagingDirectory.filePath(QStringLiteral("result.pdf"));

    auto processArguments = arguments;
    processArguments << temporaryPath;
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("qpdf"), processArguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(kStartTimeoutMs)) {
        return {false, QStringLiteral("qpdf is not installed or could not be started.")};
    }

    QByteArray diagnostic;
    const auto collectDiagnostic = [&process, &diagnostic] {
        const auto available = process.readAllStandardError();
        process.readAllStandardOutput();
        const auto remaining = kMaximumDiagnosticBytes - diagnostic.size();
        if (remaining > 0) {
            diagnostic.append(available.left(remaining));
        }
    };
    const auto exceedsOutputLimit = [&temporaryPath, limits] {
        const QFileInfo temporaryInfo(temporaryPath);
        return temporaryInfo.exists() && temporaryInfo.size() > limits.maximumOutputBytes;
    };
    int elapsedMs = 0;
    while (!process.waitForFinished(100)) {
        elapsedMs += 100;
        collectDiagnostic();
        const bool outputLimitExceeded = exceedsOutputLimit();
        if (outputLimitExceeded || (cancelled != nullptr && cancelled->load()) ||
            elapsedMs >= limits.operationTimeoutMs) {
            process.kill();
            process.waitForFinished(kStartTimeoutMs);
            if (outputLimitExceeded) {
                return {false, QStringLiteral("The generated output exceeded its size safety limit.")};
            }
            return {false, elapsedMs >= limits.operationTimeoutMs
                               ? QStringLiteral("The operation exceeded its time safety limit.")
                               : QStringLiteral("The operation was cancelled.")};
        }
    }
    collectDiagnostic();
    if (cancelled != nullptr && cancelled->load()) {
        return {false, QStringLiteral("The operation was cancelled.")};
    }
    if (exceedsOutputLimit()) {
        return {false, QStringLiteral("The generated output exceeded its size safety limit.")};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        auto detail = QString::fromLocal8Bit(diagnostic).trimmed();
        return {false, detail.isEmpty() ? QStringLiteral("qpdf could not complete the operation.") : detail};
    }

    QFile result(temporaryPath);
    if (!result.open(QIODevice::ReadWrite) || result.size() < 8 ||
        result.size() > limits.maximumOutputBytes || result.read(5) != QByteArrayLiteral("%PDF-")) {
        return {false, QStringLiteral("The generated file did not pass basic PDF validation.")};
    }
    const auto permissions = destinationPermissions.value_or(kOwnerFilePermissions);
    if (!result.setPermissions(permissions) || !result.flush()) {
        return {false, QStringLiteral("Could not secure the completed output file.")};
    }
#ifdef Q_OS_UNIX
    if (::fsync(result.handle()) != 0) {
        return {false, QStringLiteral("Could not flush the completed output file.")};
    }
#endif
    result.close();

    const auto sourceBytes = QFile::encodeName(temporaryPath);
    const auto destinationBytes = QFile::encodeName(cleanOutput);
    if (std::rename(sourceBytes.constData(), destinationBytes.constData()) != 0) {
        return {false, QStringLiteral("Could not atomically place the completed output file.")};
    }
    return {true, QStringLiteral("Saved %1").arg(QDir::toNativeSeparators(cleanOutput))};
}
