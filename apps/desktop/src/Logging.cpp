#include "Logging.h"

#include "LocalState.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr qint64 kMinimumLogBytes = 16 * 1024;
constexpr auto kActiveLogName = "zenpdf.log";
constexpr auto kRotatedLogName = "zenpdf.log.1";
QMutex logMutex;
QString logPath;
qint64 maximumLogBytes = Logging::defaultMaximumLogBytes;

#ifdef Q_OS_UNIX
constexpr mode_t kPrivateFileMode = S_IRUSR | S_IWUSR;
constexpr auto kDirectoryLockTimeout = std::chrono::milliseconds(100);
int logDirectoryDescriptor = -1;

void closeDescriptor(int& descriptor) {
    if (descriptor >= 0) {
        ::close(descriptor);
        descriptor = -1;
    }
}

class DirectoryLock final {
public:
    explicit DirectoryLock(int descriptor)
        : descriptor_(descriptor) {
        const auto deadline = std::chrono::steady_clock::now() + kDirectoryLockTimeout;
        bool firstAttempt = true;
        while (firstAttempt || std::chrono::steady_clock::now() < deadline) {
            firstAttempt = false;
            if (::flock(descriptor_, LOCK_EX | LOCK_NB) == 0) {
                locked_ = true;
                return;
            }
            const int error = errno;
            if (error != EINTR && error != EWOULDBLOCK && error != EAGAIN) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now < deadline) {
                std::this_thread::sleep_for(std::min(
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::milliseconds(1)),
                    deadline - now));
            }
        }
        timedOut_ = true;
    }

    ~DirectoryLock() {
        if (locked_) {
            ::flock(descriptor_, LOCK_UN);
        }
    }

    [[nodiscard]] bool isLocked() const {
        return locked_;
    }

    [[nodiscard]] bool timedOut() const {
        return timedOut_;
    }

    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;

private:
    int descriptor_;
    bool locked_{false};
    bool timedOut_{false};
};

bool isPrivateRegularFile(const struct stat& status) {
    return S_ISREG(status.st_mode) && status.st_uid == ::geteuid() && status.st_nlink == 1 &&
           (status.st_mode & 07777) == kPrivateFileMode;
}

enum class EntryState {
    Missing,
    Valid,
    Invalid,
};

EntryState inspectLogEntry(const char* name, struct stat* status = nullptr) {
    struct stat inspected {};
    if (::fstatat(logDirectoryDescriptor, name, &inspected, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT ? EntryState::Missing : EntryState::Invalid;
    }
    if (!isPrivateRegularFile(inspected)) {
        return EntryState::Invalid;
    }
    if (status != nullptr) {
        *status = inspected;
    }
    return EntryState::Valid;
}

bool removeOversizedLog(const char* name) {
    struct stat status {};
    const auto state = inspectLogEntry(name, &status);
    if (state == EntryState::Invalid) {
        return false;
    }
    if (state == EntryState::Valid && status.st_size > maximumLogBytes) {
        return ::unlinkat(logDirectoryDescriptor, name, 0) == 0;
    }
    return true;
}

bool validateOpenedLog(int descriptor, const char* name) {
    struct stat descriptorStatus {};
    struct stat pathStatus {};
    return ::fstat(descriptor, &descriptorStatus) == 0 &&
           isPrivateRegularFile(descriptorStatus) &&
           ::fstatat(logDirectoryDescriptor, name, &pathStatus, AT_SYMLINK_NOFOLLOW) == 0 &&
           isPrivateRegularFile(pathStatus) && descriptorStatus.st_dev == pathStatus.st_dev &&
           descriptorStatus.st_ino == pathStatus.st_ino;
}

int openLogFile() {
    const auto state = inspectLogEntry(kActiveLogName);
    if (state == EntryState::Invalid) {
        return -1;
    }

    int descriptor = -1;
    if (state == EntryState::Missing) {
        descriptor = ::openat(
            logDirectoryDescriptor,
            kActiveLogName,
            O_WRONLY | O_APPEND | O_CREAT | O_EXCL | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC,
            kPrivateFileMode);
    } else {
        descriptor = ::openat(
            logDirectoryDescriptor,
            kActiveLogName,
            O_WRONLY | O_APPEND | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    }
    if (descriptor < 0 || !validateOpenedLog(descriptor, kActiveLogName)) {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        return -1;
    }
    return descriptor;
}

qint64 currentLogSize(int descriptor) {
    struct stat status {};
    return descriptor >= 0 && ::fstat(descriptor, &status) == 0
        ? static_cast<qint64>(status.st_size)
        : -1;
}

bool rotateLog(int& descriptor) {
    closeDescriptor(descriptor);
    if (inspectLogEntry(kActiveLogName) != EntryState::Valid) {
        return false;
    }
    const auto rotatedState = inspectLogEntry(kRotatedLogName);
    if (rotatedState == EntryState::Invalid ||
        (rotatedState == EntryState::Valid &&
         ::unlinkat(logDirectoryDescriptor, kRotatedLogName, 0) != 0)) {
        return false;
    }
    if (::renameat(
            logDirectoryDescriptor,
            kActiveLogName,
            logDirectoryDescriptor,
            kRotatedLogName) != 0) {
        return false;
    }
    descriptor = openLogFile();
    return descriptor >= 0;
}

void writeLogLine(int& descriptor, const QByteArray& line) {
    qsizetype written = 0;
    while (descriptor >= 0 && written < line.size()) {
        const auto result = ::write(
            descriptor,
            line.constData() + written,
            static_cast<std::size_t>(line.size() - written));
        if (result > 0) {
            written += static_cast<qsizetype>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            closeDescriptor(descriptor);
        }
    }
}

bool prepareLogDirectory(const QString& logDirectory) {
    if (!LocalState::preparePrivateApplicationDirectory(logDirectory)) {
        return false;
    }
    const auto encodedPath = QFile::encodeName(logDirectory);
    logDirectoryDescriptor = ::open(
        encodedPath.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (logDirectoryDescriptor < 0) {
        return false;
    }
    struct stat status {};
    if (::fstat(logDirectoryDescriptor, &status) != 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != ::geteuid() || (status.st_mode & 07777) != S_IRWXU) {
        closeDescriptor(logDirectoryDescriptor);
        return false;
    }
    return true;
}

enum class InitializationResult {
    Ready,
    Contended,
    Unsafe,
};

InitializationResult initializeLogFiles() {
    DirectoryLock lock(logDirectoryDescriptor);
    if (!lock.isLocked()) {
        return lock.timedOut() ? InitializationResult::Contended : InitializationResult::Unsafe;
    }
    if (!removeOversizedLog(kActiveLogName) || !removeOversizedLog(kRotatedLogName)) {
        return InitializationResult::Unsafe;
    }
    int descriptor = openLogFile();
    const bool opened = descriptor >= 0;
    closeDescriptor(descriptor);
    return opened ? InitializationResult::Ready : InitializationResult::Unsafe;
}

void writeUnixLogLine(const QByteArray& line) {
    if (logDirectoryDescriptor < 0) {
        return;
    }
    DirectoryLock lock(logDirectoryDescriptor);
    if (!lock.isLocked() || !removeOversizedLog(kActiveLogName) ||
        !removeOversizedLog(kRotatedLogName)) {
        return;
    }
    int descriptor = openLogFile();
    const auto size = currentLogSize(descriptor);
    if (size < 0) {
        closeDescriptor(descriptor);
        return;
    }
    if (size + line.size() > maximumLogBytes && !rotateLog(descriptor)) {
        closeDescriptor(descriptor);
        return;
    }
    if (descriptor >= 0) {
        writeLogLine(descriptor, line);
    }
    closeDescriptor(descriptor);
}
#else
constexpr auto kOwnerFilePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto kOwnerDirectoryPermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
std::unique_ptr<QFile> logFile;

void closeLogFile() {
    if (logFile != nullptr) {
        logFile->close();
        logFile.reset();
    }
}

bool openLogFile() {
    logFile = std::make_unique<QFile>(logPath);
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logFile.reset();
        return false;
    }
    if (!QFile::setPermissions(logPath, kOwnerFilePermissions)) {
        closeLogFile();
        return false;
    }
    return true;
}

qint64 currentLogSize() {
    return logFile != nullptr ? logFile->size() : -1;
}

bool rotateLog() {
    closeLogFile();
    const auto rotatedPath = logPath + QStringLiteral(".1");
    QFile::remove(rotatedPath);
    const bool rotated = QFileInfo(logPath).size() <= maximumLogBytes &&
                         QFile::rename(logPath, rotatedPath);
    if (rotated) {
        QFile::setPermissions(rotatedPath, kOwnerFilePermissions);
    } else if (QFileInfo::exists(logPath) && !QFile::remove(logPath)) {
        return false;
    }
    return openLogFile();
}

void writeLogLine(const QByteArray& line) {
    if (logFile != nullptr && logFile->isOpen()) {
        logFile->write(line);
        logFile->flush();
    }
}
#endif

QStringView severityName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return u"debug";
    case QtInfoMsg:
        return u"info";
    case QtWarningMsg:
        return u"warning";
    case QtCriticalMsg:
        return u"critical";
    case QtFatalMsg:
        return u"fatal";
    }
    return u"unknown";
}

QByteArray boundedLogLine(QtMsgType type, const QString& message) {
    const auto event = message == QStringLiteral("Starting ZenPDF Desktop")
        ? QStringLiteral("application-start")
        : QStringLiteral("message-suppressed");
    auto line = QStringLiteral("%1 [%2] zenpdf: %3\n")
                    .arg(
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                        severityName(type),
                        event)
                    .toUtf8();
    if (line.size() > maximumLogBytes) {
        line.truncate(static_cast<qsizetype>(maximumLogBytes - 1));
        line.append('\n');
    }
    return line;
}

void writeMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    Q_UNUSED(context);
    QMutexLocker locker(&logMutex);
    const auto line = boundedLogLine(type, message);
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stderr);

#ifdef Q_OS_UNIX
    writeUnixLogLine(line);
#else
    const auto size = currentLogSize();
    if (size < 0) {
        return;
    }
    if (size + line.size() > maximumLogBytes && !rotateLog()) {
        return;
    }
    writeLogLine(line);
#endif
}
}

void Logging::install(const QString& logDirectory, qint64 requestedMaximumLogBytes) {
    QMutexLocker locker(&logMutex);
#ifdef Q_OS_UNIX
    closeDescriptor(logDirectoryDescriptor);
#else
    closeLogFile();
#endif
    logPath.clear();
    maximumLogBytes = std::clamp(
        requestedMaximumLogBytes, kMinimumLogBytes, Logging::defaultMaximumLogBytes);

#ifdef Q_OS_UNIX
    if (prepareLogDirectory(logDirectory)) {
        const auto result = initializeLogFiles();
        if (result == InitializationResult::Unsafe) {
            closeDescriptor(logDirectoryDescriptor);
        }
    }
#else
    if (QDir().mkpath(logDirectory) &&
        QFile::setPermissions(logDirectory, kOwnerDirectoryPermissions)) {
        logPath = QDir(logDirectory).filePath(QString::fromLatin1(kActiveLogName));
        if (QFileInfo(logPath).size() > maximumLogBytes) {
            QFile::remove(logPath);
        }
        openLogFile();
    }
#endif
    qSetMessagePattern(QStringLiteral("[%{type}] %{category}: %{message}"));
    qInstallMessageHandler(writeMessage);
}

void Logging::shutdown() {
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&logMutex);
#ifdef Q_OS_UNIX
    closeDescriptor(logDirectoryDescriptor);
#else
    closeLogFile();
#endif
    logPath.clear();
}
