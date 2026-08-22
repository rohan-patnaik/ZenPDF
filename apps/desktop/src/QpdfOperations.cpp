#include "QpdfOperations.h"
#include "QpdfPublication.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cstdio>
#include <cerrno>
#include <algorithm>
#include <optional>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace {
constexpr int kMaximumInputs = 100;
constexpr int kStartTimeoutMs = 5'000;
constexpr int kProcessPollMs = 100;
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

struct FileIdentity final {
    QString path;
    QString canonicalPath;
    qint64 size{-1};
    qint64 modifiedMs{-1};
    bool directory{false};
#ifdef Q_OS_UNIX
    dev_t device{};
    ino_t inode{};
#ifdef Q_OS_LINUX
    qint64 modifiedNs{-1};
    qint64 changedNs{-1};
#endif
#endif
};

std::optional<FileIdentity> fileIdentity(const QString& path, bool directory = false) {
    const QFileInfo info(path);
    if (!info.exists() || (directory ? !info.isDir() : !info.isFile())) {
        return std::nullopt;
    }
    FileIdentity identity{
        normalizedPath(path), info.canonicalFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch(), directory};
#ifdef Q_OS_UNIX
    struct stat metadata {};
    const auto encoded = QFile::encodeName(identity.path);
    if (::stat(encoded.constData(), &metadata) != 0 ||
        (directory ? !S_ISDIR(metadata.st_mode) : !S_ISREG(metadata.st_mode))) {
        return std::nullopt;
    }
    identity.device = metadata.st_dev;
    identity.inode = metadata.st_ino;
#ifdef Q_OS_LINUX
    identity.modifiedNs = static_cast<qint64>(metadata.st_mtim.tv_sec) * 1'000'000'000LL +
                          metadata.st_mtim.tv_nsec;
    identity.changedNs = static_cast<qint64>(metadata.st_ctim.tv_sec) * 1'000'000'000LL +
                         metadata.st_ctim.tv_nsec;
#endif
#endif
    return identity;
}

bool sameFile(const FileIdentity& left, const FileIdentity& right) {
#ifdef Q_OS_UNIX
    return left.device == right.device && left.inode == right.inode;
#else
    return !left.canonicalPath.isEmpty() && left.canonicalPath == right.canonicalPath;
#endif
}

bool unchangedFile(const FileIdentity& expected) {
    const auto current = fileIdentity(expected.path, expected.directory);
    if (!current.has_value() || !sameFile(expected, *current) || expected.directory) {
        return current.has_value() && expected.directory && sameFile(expected, *current);
    }
    if (current->size != expected.size || current->modifiedMs != expected.modifiedMs) {
        return false;
    }
#ifdef Q_OS_LINUX
    return current->modifiedNs == expected.modifiedNs && current->changedNs == expected.changedNs;
#else
    return true;
#endif
}

enum class SnapshotResult { Succeeded, Cancelled, TimedOut, Failed };

SnapshotResult snapshotInput(
    const FileIdentity& expected,
    const QString& snapshotPath,
    const std::atomic_bool* cancelled,
    const QElapsedTimer& elapsed,
    int timeoutMs) {
    QFile source(expected.path);
    if (!source.open(QIODevice::ReadOnly)) {
        return SnapshotResult::Failed;
    }
#ifdef Q_OS_UNIX
    struct stat before {};
    if (::fstat(source.handle(), &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_dev != expected.device || before.st_ino != expected.inode) {
        return SnapshotResult::Failed;
    }
#endif
    QFile snapshot(snapshotPath);
    if (!snapshot.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return SnapshotResult::Failed;
    }
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    qint64 total = 0;
    while (true) {
        if (cancelled != nullptr && cancelled->load()) {
            return SnapshotResult::Cancelled;
        }
        if (elapsed.elapsed() >= timeoutMs) {
            return SnapshotResult::TimedOut;
        }
        const auto bytesRead = source.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return SnapshotResult::Failed;
        }
        if (bytesRead == 0) {
            break;
        }
        total += bytesRead;
        if (total > QpdfOperations::maximumInputBytes ||
            snapshot.write(buffer.constData(), bytesRead) != bytesRead) {
            return SnapshotResult::Failed;
        }
    }
    if (!snapshot.setPermissions(kOwnerFilePermissions) || !snapshot.flush()) {
        return SnapshotResult::Failed;
    }
#ifdef Q_OS_UNIX
    if (::fsync(snapshot.handle()) != 0) {
        return SnapshotResult::Failed;
    }
    struct stat after {};
    if (::fstat(source.handle(), &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size) {
        return SnapshotResult::Failed;
    }
#ifdef Q_OS_LINUX
    if (
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
        return SnapshotResult::Failed;
    }
#else
    if (before.st_mtime != after.st_mtime || before.st_ctime != after.st_ctime) {
        return SnapshotResult::Failed;
    }
#endif
#endif
    return total == expected.size ? SnapshotResult::Succeeded : SnapshotResult::Failed;
}

int selectedPageOccurrenceCount(const QString& range) {
    int count = 0;
    for (const auto& part : range.split(',')) {
        const auto bounds = part.split('-');
        const int first = bounds.at(0).toInt();
        const int last = bounds.size() == 2 ? bounds.at(1).toInt() : first;
        count += last - first + 1;
    }
    return count;
}

int uniqueSelectedPageCount(const QString& range, int pageCount) {
    QList<bool> selected(pageCount + 1, false);
    int count = 0;
    for (const auto& part : range.split(',')) {
        const auto bounds = part.split('-');
        const int first = bounds.at(0).toInt();
        const int last = bounds.size() == 2 ? bounds.at(1).toInt() : first;
        for (int page = first; page <= last; ++page) {
            if (!selected.at(page)) {
                selected[page] = true;
                ++count;
            }
        }
    }
    return count;
}

QString retainedPageRange(const QString& deletedRange, int pageCount) {
    QList<bool> deleted(pageCount + 1, false);
    for (const auto& part : deletedRange.split(',')) {
        const auto bounds = part.split('-');
        const int first = bounds.at(0).toInt();
        const int last = bounds.size() == 2 ? bounds.at(1).toInt() : first;
        for (int page = first; page <= last; ++page) {
            deleted[page] = true;
        }
    }

    QStringList retained;
    for (int first = 1; first <= pageCount;) {
        while (first <= pageCount && deleted.at(first)) {
            ++first;
        }
        if (first > pageCount) {
            break;
        }
        int last = first;
        while (last + 1 <= pageCount && !deleted.at(last + 1)) {
            ++last;
        }
        retained.append(first == last ? QString::number(first)
                                      : QStringLiteral("%1-%2").arg(first).arg(last));
        first = last + 1;
    }
    return retained.join(',');
}

}

QpdfPublication::Result QpdfPublication::publishNoReplace(
    const QString& source, const QString& destination) {
    const auto sourceBytes = QFile::encodeName(source);
    const auto destinationBytes = QFile::encodeName(destination);
#ifdef Q_OS_LINUX
    if (::syscall(SYS_renameat2,
                  AT_FDCWD,
                  sourceBytes.constData(),
                  AT_FDCWD,
                  destinationBytes.constData(),
                  RENAME_NOREPLACE) == 0) {
        return Result::Succeeded;
    }
    if (errno == EEXIST) {
        return Result::DestinationExists;
    }
    if (errno != ENOSYS && errno != EINVAL) {
        return Result::Failed;
    }
#endif
#ifdef Q_OS_UNIX
    if (::link(sourceBytes.constData(), destinationBytes.constData()) == 0) {
        (void)::unlink(sourceBytes.constData());
        return Result::Succeeded;
    }
    return errno == EEXIST ? Result::DestinationExists : Result::Failed;
#else
    if (QFile::rename(source, destination)) {
        return Result::Succeeded;
    }
    return QFileInfo::exists(destination) ? Result::DestinationExists : Result::Failed;
#endif
}

bool QpdfOperations::isValidPageRange(const QString& range, int pageCount) {
    if (pageCount < 1 || pageCount > maximumPageCount || range.isEmpty() || range.size() > 1'000) {
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
        {inputPath, QStringLiteral("--pages"), QStringLiteral("."), pageRange, QStringLiteral("--")},
        outputPath,
        {inputPath},
        cancelled,
        limits,
        selectedPageOccurrenceCount(pageRange));
}

QpdfResult QpdfOperations::deletePages(
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
    const auto retained = retainedPageRange(pageRange, pageCount);
    if (retained.isEmpty()) {
        return {false, QStringLiteral("At least one page must remain in the new PDF.")};
    }
    return run(
        {inputPath, QStringLiteral("--pages"), QStringLiteral("."), retained, QStringLiteral("--")},
        outputPath,
        {inputPath},
        cancelled,
        limits,
        pageCount - uniqueSelectedPageCount(pageRange, pageCount));
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
    return run({inputPath, rotation}, outputPath, {inputPath}, cancelled, limits, pageCount);
}

QpdfResult QpdfOperations::run(
    const QStringList& arguments,
    const QString& outputPath,
    const QStringList& protectedInputPaths,
    const std::atomic_bool* cancelled,
    QpdfLimits limits,
    std::optional<int> expectedPageCount) {
    if (outputPath.trimmed().isEmpty()) {
        return {false, QStringLiteral("Choose an output path.")};
    }
    if (limits.maximumOutputBytes < 8 || limits.maximumOutputBytes > maximumInputBytes ||
        limits.operationTimeoutMs < 1 || limits.operationTimeoutMs > maximumOperationTimeoutMs) {
        return {false, QStringLiteral("Invalid operation safety limits.")};
    }
    if (cancelled != nullptr && cancelled->load()) {
        return {false, QStringLiteral("The operation was cancelled.")};
    }
    QElapsedTimer elapsed;
    elapsed.start();
    const auto cleanOutput = normalizedPath(outputPath);
    QList<FileIdentity> protectedInputs;
    for (const auto& input : protectedInputPaths) {
        const auto identity = fileIdentity(input);
        if (!identity.has_value()) {
            return {false, QStringLiteral("An input PDF changed or became unreadable.")};
        }
        protectedInputs.append(*identity);
        if (cleanOutput == identity->path ||
            (!identity->canonicalPath.isEmpty() && QFileInfo(cleanOutput).canonicalFilePath() == identity->canonicalPath)) {
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
    const auto destinationIdentity = fileIdentity(cleanOutput);
    if (outputInfo.exists()) {
        if (!destinationIdentity.has_value()) {
            return {false, QStringLiteral("The output path is not a stable regular file.")};
        }
        for (const auto& input : protectedInputs) {
            if (sameFile(input, *destinationIdentity)) {
                return {false, QStringLiteral("Choose a new output file; the source is never overwritten.")};
            }
        }
        return {false, QStringLiteral("Choose a new output file; organizer results never replace an existing file.")};
    }
    const auto outputDirectoryIdentity = fileIdentity(outputDirectory.absolutePath(), true);

    QTemporaryDir stagingDirectory(outputDirectory.filePath(QStringLiteral(".zenpdf-XXXXXX")));
    if (!stagingDirectory.isValid() ||
        !QFile::setPermissions(stagingDirectory.path(), kOwnerDirectoryPermissions) ||
        (QFileInfo(stagingDirectory.path()).permissions() &
         (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
          QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther))) {
        return {false, QStringLiteral("Could not create a private temporary output directory.")};
    }
    const auto temporaryPath = stagingDirectory.filePath(QStringLiteral("result.pdf"));

    QHash<QString, QString> snapshotPaths;
    for (qsizetype index = 0; index < protectedInputs.size(); ++index) {
        const auto& input = protectedInputs.at(index);
        const auto snapshotPath = stagingDirectory.filePath(
            QStringLiteral("input-%1.pdf").arg(index));
        const auto snapshotResult = snapshotInput(
            input, snapshotPath, cancelled, elapsed, limits.operationTimeoutMs);
        if (snapshotResult == SnapshotResult::Cancelled) {
            return {false, QStringLiteral("The operation was cancelled.")};
        }
        if (snapshotResult == SnapshotResult::TimedOut) {
            return {false, QStringLiteral("The operation exceeded its time safety limit.")};
        }
        if (snapshotResult != SnapshotResult::Succeeded) {
            return {false, QStringLiteral("An input PDF changed while it was being secured; no output was published.")};
        }
        snapshotPaths.insert(protectedInputPaths.at(index), snapshotPath);
    }

    for (const auto& snapshotPath : snapshotPaths) {
        QProcess encryptionCheck;
        encryptionCheck.setProcessChannelMode(QProcess::SeparateChannels);
        encryptionCheck.start(
            QStringLiteral("qpdf"),
            {QStringLiteral("--is-encrypted"), snapshotPath},
            QIODevice::ReadOnly);
        const int remaining = limits.operationTimeoutMs - static_cast<int>(elapsed.elapsed());
        if (remaining < 1 || !encryptionCheck.waitForStarted(std::min(kStartTimeoutMs, remaining))) {
            return {false, remaining < 1
                               ? QStringLiteral("The operation exceeded its time safety limit.")
                               : QStringLiteral("qpdf is not installed or could not be started.")};
        }
        while (!encryptionCheck.waitForFinished(kProcessPollMs)) {
            if (cancelled != nullptr && cancelled->load()) {
                encryptionCheck.kill();
                encryptionCheck.waitForFinished(kStartTimeoutMs);
                return {false, QStringLiteral("The operation was cancelled.")};
            }
            if (elapsed.elapsed() >= limits.operationTimeoutMs) {
                encryptionCheck.kill();
                encryptionCheck.waitForFinished(kStartTimeoutMs);
                return {false, QStringLiteral("The operation exceeded its time safety limit.")};
            }
        }
        if (encryptionCheck.exitStatus() != QProcess::NormalExit) {
            return {false, QStringLiteral("qpdf could not inspect an input PDF safely.")};
        }
        if (encryptionCheck.exitCode() == 0) {
            return {false, QStringLiteral("Encrypted or permission-restricted PDFs are not supported by organizer commands.")};
        }
        if (encryptionCheck.exitCode() != 2 || !encryptionCheck.readAllStandardError().trimmed().isEmpty()) {
            return {false, QStringLiteral("An input is malformed or unsupported; no output was published.")};
        }
    }

    auto processArguments = arguments;
    for (auto& argument : processArguments) {
        const auto snapshot = snapshotPaths.constFind(argument);
        if (snapshot != snapshotPaths.cend()) {
            argument = *snapshot;
        }
    }
    processArguments << temporaryPath;
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QStringLiteral("qpdf"), processArguments, QIODevice::ReadOnly);
    const int processStartRemaining = limits.operationTimeoutMs - static_cast<int>(elapsed.elapsed());
    if (processStartRemaining < 1 ||
        !process.waitForStarted(std::min(kStartTimeoutMs, processStartRemaining))) {
        return {false, processStartRemaining < 1
                           ? QStringLiteral("The operation exceeded its time safety limit.")
                           : QStringLiteral("qpdf is not installed or could not be started.")};
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
    while (!process.waitForFinished(kProcessPollMs)) {
        collectDiagnostic();
        const bool outputLimitExceeded = exceedsOutputLimit();
        if (outputLimitExceeded || (cancelled != nullptr && cancelled->load()) ||
            elapsed.elapsed() >= limits.operationTimeoutMs) {
            process.kill();
            process.waitForFinished(kStartTimeoutMs);
            if (outputLimitExceeded) {
                return {false, QStringLiteral("The generated output exceeded its size safety limit.")};
            }
            return {false, elapsed.elapsed() >= limits.operationTimeoutMs
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
    if (!result.setPermissions(kOwnerFilePermissions) || !result.flush()) {
        return {false, QStringLiteral("Could not secure the completed output file.")};
    }
#ifdef Q_OS_UNIX
    if (::fsync(result.handle()) != 0) {
        return {false, QStringLiteral("Could not flush the completed output file.")};
    }
#endif
    result.close();

    const auto runValidation = [&](const QStringList& validationArguments, QByteArray* standardOutput)
        -> std::optional<QpdfResult> {
        QProcess validation;
        validation.setProcessChannelMode(QProcess::SeparateChannels);
        validation.start(QStringLiteral("qpdf"), validationArguments, QIODevice::ReadOnly);
        const int remaining = limits.operationTimeoutMs - static_cast<int>(elapsed.elapsed());
        if (remaining < 1 || !validation.waitForStarted(std::min(kStartTimeoutMs, remaining))) {
            return QpdfResult{false, remaining < 1
                                        ? QStringLiteral("The operation exceeded its time safety limit.")
                                        : QStringLiteral("The completed PDF could not be reopened for validation.")};
        }
        while (!validation.waitForFinished(kProcessPollMs)) {
            if (cancelled != nullptr && cancelled->load()) {
                validation.kill();
                validation.waitForFinished(kStartTimeoutMs);
                return QpdfResult{false, QStringLiteral("The operation was cancelled.")};
            }
            if (elapsed.elapsed() >= limits.operationTimeoutMs) {
                validation.kill();
                validation.waitForFinished(kStartTimeoutMs);
                return QpdfResult{false, QStringLiteral("The operation exceeded its time safety limit.")};
            }
        }
        if (validation.exitStatus() != QProcess::NormalExit || validation.exitCode() != 0) {
            return QpdfResult{false, QStringLiteral("The completed PDF did not pass structural reopen validation.")};
        }
        if (standardOutput != nullptr) {
            *standardOutput = validation.readAllStandardOutput();
        }
        return std::nullopt;
    };

    if (const auto error = runValidation(
            {QStringLiteral("--check"), temporaryPath}, nullptr);
        error.has_value()) {
        return *error;
    }
    if (expectedPageCount.has_value()) {
        QByteArray pageCountOutput;
        if (const auto error = runValidation(
                {QStringLiteral("--show-npages"), temporaryPath}, &pageCountOutput);
            error.has_value()) {
            return *error;
        }
        bool pageCountOk = false;
        const int actualPageCount = QString::fromLatin1(pageCountOutput).trimmed().toInt(&pageCountOk);
        if (!pageCountOk || actualPageCount != *expectedPageCount) {
            return {false, QStringLiteral("The completed PDF did not preserve the expected page structure.")};
        }
    }

    for (const auto& input : protectedInputs) {
        if (!unchangedFile(input)) {
            return {false, QStringLiteral("An input PDF changed while the operation was running; no output was published.")};
        }
    }
    const QFileInfo currentOutputInfo(cleanOutput);
    if (currentOutputInfo.isSymLink()) {
        return {false, QStringLiteral("The output path changed to a symbolic link; no output was published.")};
    }
    const auto currentDestinationIdentity = fileIdentity(cleanOutput);
    if (destinationIdentity.has_value()) {
        if (!currentDestinationIdentity.has_value() ||
            !sameFile(*destinationIdentity, *currentDestinationIdentity) ||
            currentDestinationIdentity->size != destinationIdentity->size ||
            currentDestinationIdentity->modifiedMs != destinationIdentity->modifiedMs) {
            return {false, QStringLiteral("The output file changed while the operation was running; no output was published.")};
        }
    } else if (currentOutputInfo.exists()) {
        return {false, QStringLiteral("The output path appeared while the operation was running; no output was published.")};
    }
    if (outputDirectoryIdentity.has_value() && !unchangedFile(*outputDirectoryIdentity)) {
        return {false, QStringLiteral("The output directory changed while the operation was running; no output was published.")};
    }

    const auto publication = QpdfPublication::publishNoReplace(temporaryPath, cleanOutput);
    if (publication == QpdfPublication::Result::DestinationExists) {
        return {false, QStringLiteral("The output path appeared while the operation was running; no output was replaced.")};
    }
    if (publication != QpdfPublication::Result::Succeeded) {
        return {false, QStringLiteral("Could not atomically place the completed output file.")};
    }
    return {true, QStringLiteral("Saved %1").arg(QDir::toNativeSeparators(cleanOutput))};
}
