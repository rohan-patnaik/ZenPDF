#include "Preferences.h"

#include "LocalState.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>
#include <QVariant>

#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr int kMaximumErrorLength = 256;
constexpr auto kLockTimeout = std::chrono::milliseconds(100);

bool setError(const QString& message, QString* errorMessage) {
    if (errorMessage != nullptr) {
        *errorMessage = message.left(kMaximumErrorLength);
    }
    return false;
}

bool isBoundedByteArray(const QVariant& value) {
    return value.metaType().id() == QMetaType::QByteArray &&
           value.toByteArray().size() <= Preferences::maximumValueBytes;
}

#ifdef Q_OS_UNIX
constexpr mode_t kPrivateFileMode = S_IRUSR | S_IWUSR;
constexpr mode_t kUnsafeWriteMode = S_IWGRP | S_IWOTH;
constexpr mode_t kSpecialMode = S_ISUID | S_ISGID | S_ISVTX;

class FileDescriptor final {
public:
    explicit FileDescriptor(int descriptor = -1) : descriptor_(descriptor) {}
    ~FileDescriptor() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    [[nodiscard]] int get() const { return descriptor_; }
    [[nodiscard]] bool isValid() const { return descriptor_ >= 0; }

private:
    int descriptor_;
};

bool validateDescriptor(int descriptor, bool enforceSize, QString* errorMessage) {
    struct stat status {};
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_uid != ::geteuid() || status.st_nlink != 1 ||
        (status.st_mode & S_IRUSR) == 0 ||
        (status.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0) {
        return setError(QStringLiteral("Preferences have unsafe local storage."), errorMessage);
    }
    if ((status.st_mode & 0777) != kPrivateFileMode &&
        ::fchmod(descriptor, kPrivateFileMode) != 0) {
        return setError(QStringLiteral("Could not restrict preference permissions."), errorMessage);
    }
    struct stat verified {};
    if (::fstat(descriptor, &verified) != 0 ||
        verified.st_dev != status.st_dev || verified.st_ino != status.st_ino ||
        !S_ISREG(verified.st_mode) || verified.st_uid != ::geteuid() ||
        verified.st_nlink != 1 || (verified.st_mode & 07777) != kPrivateFileMode) {
        return setError(QStringLiteral("Could not verify private preferences."), errorMessage);
    }
    if (enforceSize && verified.st_size > Preferences::maximumFileBytes) {
        return setError(QStringLiteral("Preferences exceed the local size limit."), errorMessage);
    }
    return true;
}

bool validatePublicationDescriptor(
    int descriptor,
    qint64 expectedSize,
    QString* errorMessage) {
    struct stat before {};
    if (::fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_uid != ::geteuid() || before.st_nlink > 1 ||
        (before.st_mode & 07777) != kPrivateFileMode ||
        before.st_size != expectedSize || before.st_size > Preferences::maximumFileBytes) {
        return setError(QStringLiteral("Could not validate preference publication."), errorMessage);
    }
    struct stat after {};
    if (::fstat(descriptor, &after) != 0 || after.st_dev != before.st_dev ||
        after.st_ino != before.st_ino || after.st_mode != before.st_mode ||
        after.st_uid != before.st_uid || after.st_nlink != before.st_nlink ||
        after.st_size != before.st_size) {
        return setError(QStringLiteral("Could not verify preference publication."), errorMessage);
    }
    return true;
}

bool validateDescriptorWithoutMutation(
    int descriptor,
    bool enforceSize,
    QString* errorMessage) {
    struct stat before {};
    if (::fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_uid != ::geteuid() || before.st_nlink != 1 ||
        (before.st_mode & S_IRUSR) == 0 ||
        (before.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0 ||
        (enforceSize && before.st_size > Preferences::maximumFileBytes)) {
        return setError(QStringLiteral("Preferences have unsafe local storage."), errorMessage);
    }
    struct stat after {};
    if (::fstat(descriptor, &after) != 0 || after.st_dev != before.st_dev ||
        after.st_ino != before.st_ino || after.st_mode != before.st_mode ||
        after.st_uid != before.st_uid || after.st_nlink != before.st_nlink ||
        after.st_size != before.st_size) {
        return setError(QStringLiteral("Could not verify private preferences."), errorMessage);
    }
    return true;
}

enum class PathKind { Missing, Regular, Unsafe, Error };

PathKind inspectPathNoFollow(const QString& path, struct stat* status = nullptr) {
    struct stat inspected {};
    const auto encodedPath = QFile::encodeName(path);
    if (::lstat(encodedPath.constData(), &inspected) != 0) {
        return errno == ENOENT ? PathKind::Missing : PathKind::Error;
    }
    if (status != nullptr) {
        *status = inspected;
    }
    return S_ISREG(inspected.st_mode) ? PathKind::Regular : PathKind::Unsafe;
}

bool isRecognizableQtLock(const QByteArray& bytes) {
    if (bytes.isEmpty() || bytes.size() > 4096 || bytes.contains('\0')) {
        return false;
    }
    auto lines = bytes.split('\n');
    if (!lines.isEmpty() && lines.back().isEmpty()) {
        lines.removeLast();
    }
    if ((lines.size() != 3 && lines.size() != 5) || lines.at(0).isEmpty()) {
        return false;
    }
    const auto pidBytes = lines.at(0);
    if (pidBytes.front() == '0') {
        return false;
    }
    for (const char byte : pidBytes) {
        if (byte < '0' || byte > '9') {
            return false;
        }
    }
    bool pidOk = false;
    const qlonglong pid = pidBytes.toLongLong(&pidOk);
    if (!pidOk || pid <= 0 || QByteArray::number(pid) != pidBytes) {
        return false;
    }
    for (qsizetype index = 1; index < lines.size(); ++index) {
        if (lines.at(index).size() > 1024) {
            return false;
        }
    }
    return true;
}

bool validateQtLockSidecar(const QString& path, QString* errorMessage) {
    struct stat before {};
    const auto kind = inspectPathNoFollow(path, &before);
    if (kind == PathKind::Missing) {
        return true;
    }
    if (kind != PathKind::Regular || before.st_uid != ::geteuid() || before.st_nlink != 1 ||
        (before.st_mode & S_IRUSR) == 0 ||
        (before.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0 || before.st_size > 4096) {
        return setError(QStringLiteral("Preferences have an unsafe lock sidecar."), errorMessage);
    }
    const auto encodedPath = QFile::encodeName(path);
    FileDescriptor descriptor(::open(
        encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (!descriptor.isValid()) {
        return setError(QStringLiteral("Could not inspect the preference lock sidecar."), errorMessage);
    }
    struct stat opened {};
    if (::fstat(descriptor.get(), &opened) != 0 || opened.st_dev != before.st_dev ||
        opened.st_ino != before.st_ino || opened.st_size != before.st_size) {
        return setError(QStringLiteral("The preference lock sidecar changed during inspection."), errorMessage);
    }
    QByteArray bytes(static_cast<qsizetype>(opened.st_size), Qt::Uninitialized);
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(
            descriptor.get(), bytes.data() + offset, static_cast<size_t>(bytes.size() - offset));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return setError(QStringLiteral("Could not read the preference lock sidecar."), errorMessage);
        }
        offset += count;
    }
    struct stat after {};
    if (::fstat(descriptor.get(), &after) != 0 || after.st_dev != opened.st_dev ||
        after.st_ino != opened.st_ino || after.st_size != opened.st_size ||
        after.st_mtim.tv_sec != opened.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != opened.st_mtim.tv_nsec || !isRecognizableQtLock(bytes)) {
        return setError(QStringLiteral("Preferences have a malformed lock sidecar."), errorMessage);
    }
    return true;
}
#endif

class QtSettingsLockLease final {
public:
    QtSettingsLockLease() = default;

    [[nodiscard]] bool acquire(const QString& settingsPath, QString* errorMessage) {
#ifdef Q_OS_UNIX
        if (!validateQtLockSidecar(settingsPath + QStringLiteral(".lock"), errorMessage)) {
            return false;
        }
#endif
        lock_ = std::make_unique<QLockFile>(settingsPath + QStringLiteral(".lock"));
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) {
            lock_.reset();
            return setError(QStringLiteral("Preferences are busy; try again."), errorMessage);
        }
        return true;
    }

    ~QtSettingsLockLease() {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }

    QtSettingsLockLease(const QtSettingsLockLease&) = delete;
    QtSettingsLockLease& operator=(const QtSettingsLockLease&) = delete;

private:
    std::unique_ptr<QLockFile> lock_;
};

class PreferenceLock final {
public:
    explicit PreferenceLock(QString finalPath)
        : finalPath_(std::move(finalPath)) {}

    [[nodiscard]] bool acquire(QString* errorMessage) {
#ifdef Q_OS_UNIX
        const auto lockPath = QFile::encodeName(finalPath_ + QStringLiteral(".zenpdf-lock"));
        descriptor_ = std::make_unique<FileDescriptor>(::open(
            lockPath.constData(),
            O_RDWR | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC,
            kPrivateFileMode));
        if (!descriptor_->isValid() ||
            !validateDescriptor(descriptor_->get(), false, errorMessage)) {
            return setError(QStringLiteral("Could not open the preference lock."), errorMessage);
        }
        const auto deadline = std::chrono::steady_clock::now() + kLockTimeout;
        for (;;) {
            if (::flock(descriptor_->get(), LOCK_EX | LOCK_NB) == 0) {
                locked_ = true;
                break;
            }
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
                return setError(QStringLiteral("Could not acquire the preference lock."), errorMessage);
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return setError(QStringLiteral("Preferences are busy; try again."), errorMessage);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
#else
        portableLock_ = std::make_unique<QLockFile>(
            finalPath_ + QStringLiteral(".zenpdf-coordinate.lock"));
        portableLock_->setStaleLockTime(0);
        if (!portableLock_->tryLock(static_cast<int>(kLockTimeout.count()))) {
            return setError(QStringLiteral("Preferences are busy; try again."), errorMessage);
        }
#endif
        return qtSettingsLock_.acquire(finalPath_, errorMessage);
    }

    ~PreferenceLock() {
#ifdef Q_OS_UNIX
        if (locked_) {
            (void)::flock(descriptor_->get(), LOCK_UN);
        }
#else
        if (portableLock_ != nullptr) {
            portableLock_->unlock();
        }
#endif
    }

private:
    QString finalPath_;
    QtSettingsLockLease qtSettingsLock_;
#ifdef Q_OS_UNIX
    std::unique_ptr<FileDescriptor> descriptor_;
    bool locked_ = false;
#else
    std::unique_ptr<QLockFile> portableLock_;
#endif
};

class StagingFile final {
public:
    explicit StagingFile(const QString& finalPath)
        : path_(finalPath + QStringLiteral(".stage-") +
                QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

    ~StagingFile() {
        QFile::remove(path_ + QStringLiteral(".lock"));
        QFile::remove(path_);
    }

    [[nodiscard]] const QString& path() const { return path_; }

    [[nodiscard]] bool create(QString* errorMessage) const {
        QFile file(path_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            return setError(QStringLiteral("Could not create preference staging."), errorMessage);
        }
        if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return setError(QStringLiteral("Could not secure preference staging."), errorMessage);
        }
        return true;
    }

private:
    QString path_;
};

bool readBoundedFile(const QString& path, QByteArray* bytes, QString* errorMessage) {
#ifdef Q_OS_UNIX
    const auto encodedPath = QFile::encodeName(path);
    FileDescriptor descriptor(::open(
        encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    struct stat status {};
    if (!descriptor.isValid() || ::fstat(descriptor.get(), &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_uid != ::geteuid() || status.st_nlink != 1 ||
        (status.st_mode & S_IRUSR) == 0 ||
        (status.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0) {
        return setError(QStringLiteral("Could not safely read private preferences."), errorMessage);
    }
    if (status.st_size < 0 || status.st_size > Preferences::maximumFileBytes) {
        return setError(QStringLiteral("Preferences exceed the local size limit."), errorMessage);
    }
    bytes->resize(static_cast<qsizetype>(status.st_size));
    qsizetype offset = 0;
    while (offset < bytes->size()) {
        const ssize_t count = ::read(
            descriptor.get(), bytes->data() + offset, static_cast<size_t>(bytes->size() - offset));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            bytes->clear();
            return setError(QStringLiteral("Could not read private preferences."), errorMessage);
        }
        offset += count;
    }
    struct stat verified {};
    if (::fstat(descriptor.get(), &verified) != 0 || verified.st_dev != status.st_dev ||
        verified.st_ino != status.st_ino || verified.st_size != status.st_size ||
        verified.st_mtim.tv_sec != status.st_mtim.tv_sec ||
        verified.st_mtim.tv_nsec != status.st_mtim.tv_nsec) {
        bytes->clear();
        return setError(QStringLiteral("Preferences changed while being read."), errorMessage);
    }
    return true;
#else
    if (QFileInfo(path).size() > Preferences::maximumFileBytes) {
        return setError(QStringLiteral("Preferences exceed the local size limit."), errorMessage);
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return setError(QStringLiteral("Could not read private preferences."), errorMessage);
    }
    *bytes = file.read(Preferences::maximumFileBytes + 1);
    if (bytes->size() > Preferences::maximumFileBytes || !file.atEnd()) {
        bytes->clear();
        return setError(QStringLiteral("Preferences exceed the local size limit."), errorMessage);
    }
    return true;
#endif
}

std::optional<int> recognizableSchemaVersion(const QByteArray& bytes) {
    QByteArray section;
    for (QByteArray line : bytes.split('\n')) {
        line = line.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const auto equals = line.indexOf('=');
        if (equals <= 0) {
            continue;
        }
        const auto key = line.left(equals).trimmed();
        if (!((section == QByteArrayLiteral("schema") && key == QByteArrayLiteral("version")) ||
              (section == QByteArrayLiteral("General") &&
               key == QByteArrayLiteral("schema/version")))) {
            continue;
        }
        bool ok = false;
        const qlonglong version = line.mid(equals + 1).trimmed().toLongLong(&ok);
        if (ok && version >= 1 && version <= std::numeric_limits<int>::max()) {
            return static_cast<int>(version);
        }
    }
    return std::nullopt;
}

bool parseSettingsBytes(
    const QString& finalPath,
    const QByteArray& bytes,
    WindowPreferences* preferences,
    bool* versionless,
    int* schemaVersion,
    QString* errorMessage) {
    StagingFile staging(finalPath);
    if (!staging.create(errorMessage)) {
        return false;
    }
    QFile file(staging.path());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(bytes) != bytes.size()) {
        return setError(QStringLiteral("Could not stage private preferences."), errorMessage);
    }
    file.close();

    QSettings settings(staging.path(), QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    (void)settings.allKeys();
    if (settings.status() != QSettings::NoError) {
        return setError(QStringLiteral("Preferences are corrupt; defaults were used."), errorMessage);
    }
    *versionless = !settings.contains(QStringLiteral("schema/version"));
    *schemaVersion = 0;
    if (!*versionless) {
        bool converted = false;
        *schemaVersion = settings.value(QStringLiteral("schema/version")).toInt(&converted);
        if (!converted || *schemaVersion < 1) {
            return setError(QStringLiteral("Preferences have an invalid schema."), errorMessage);
        }
        if (*schemaVersion > Preferences::currentSchemaVersion) {
            return setError(
                QStringLiteral("Preferences use a newer unsupported schema."),
                errorMessage);
        }
    }
    const auto geometryValue = settings.value(QStringLiteral("window/geometry"), QByteArray{});
    const auto stateValue = settings.value(QStringLiteral("window/state"), QByteArray{});
    if (!isBoundedByteArray(geometryValue) || !isBoundedByteArray(stateValue)) {
        return setError(QStringLiteral("Preferences contain invalid window state."), errorMessage);
    }
    preferences->geometry = geometryValue.toByteArray();
    preferences->state = stateValue.toByteArray();
    return true;
}

bool serializeSettings(
    const QString& finalPath,
    const WindowPreferences& preferences,
    QByteArray* bytes,
    QString* errorMessage) {
    StagingFile staging(finalPath);
    if (!staging.create(errorMessage)) {
        return false;
    }
    {
        QSettings settings(staging.path(), QSettings::IniFormat);
        settings.setFallbacksEnabled(false);
        settings.setValue(
            QStringLiteral("schema/version"), Preferences::currentSchemaVersion);
        settings.setValue(QStringLiteral("window/geometry"), preferences.geometry);
        settings.setValue(QStringLiteral("window/state"), preferences.state);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            return setError(QStringLiteral("Could not serialize private preferences."), errorMessage);
        }
    }
    return readBoundedFile(staging.path(), bytes, errorMessage);
}

bool publishSettingsBytes(
    const QString& finalPath,
    const QByteArray& bytes,
    QString* errorMessage) {
    if (bytes.size() > Preferences::maximumFileBytes) {
        return setError(QStringLiteral("Preferences exceed the local size limit."), errorMessage);
    }
#ifdef Q_OS_UNIX
    const auto kind = inspectPathNoFollow(finalPath);
    if (kind == PathKind::Unsafe || kind == PathKind::Error) {
        return setError(QStringLiteral("Preferences have an unsafe file type."), errorMessage);
    }
#else
    const QFileInfo destination(finalPath);
    if (destination.isSymLink() || (destination.exists() && !destination.isFile())) {
        return setError(QStringLiteral("Preferences have an unsafe file type."), errorMessage);
    }
#endif
    QSaveFile output(finalPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        !output.flush()) {
        output.cancelWriting();
        return setError(QStringLiteral("Could not save private preferences."), errorMessage);
    }
#ifdef Q_OS_UNIX
    if (output.handle() < 0 ||
        !validatePublicationDescriptor(
            output.handle(), static_cast<qint64>(bytes.size()), errorMessage)) {
        output.cancelWriting();
        return false;
    }
#else
    if (output.size() < 0 || output.size() > Preferences::maximumFileBytes) {
        output.cancelWriting();
        return setError(QStringLiteral("Preferences exceed the local size limit."), errorMessage);
    }
#endif
    if (!output.commit()) {
        output.cancelWriting();
        return setError(QStringLiteral("Could not save private preferences."), errorMessage);
    }
    return true;
}

bool inspectPreferencePresence(
    const QString& path,
    bool* exists,
    QString* errorMessage) {
    *exists = false;
#ifdef Q_OS_UNIX
    const auto kind = inspectPathNoFollow(path);
    if (kind == PathKind::Missing) {
        return true;
    }
    if (kind != PathKind::Regular) {
        return setError(QStringLiteral("Preferences have an unsafe file type."), errorMessage);
    }
    *exists = true;
    return true;
#else
    const QFileInfo info(path);
    if (info.isSymLink()) {
        return setError(QStringLiteral("Preferences have an unsafe file type."), errorMessage);
    }
    *exists = info.exists();
    if (*exists && !info.isFile()) {
        return setError(QStringLiteral("Preferences have an unsafe file type."), errorMessage);
    }
    return true;
#endif
}

bool inspectLegacyParentPresence(
    const QString& path,
    bool* exists,
    QString* errorMessage) {
    *exists = false;
    const auto parentPath = QFileInfo(path).absolutePath();
#ifdef Q_OS_UNIX
    const auto encodedParent = QFile::encodeName(parentPath);
    struct stat before {};
    if (::lstat(encodedParent.constData(), &before) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        return setError(QStringLiteral("Could not inspect the legacy preference directory."), errorMessage);
    }
    constexpr mode_t requiredOwnerDirectoryMode = S_IRUSR | S_IWUSR | S_IXUSR;
    if (!S_ISDIR(before.st_mode) || before.st_uid != ::geteuid() ||
        (before.st_mode & requiredOwnerDirectoryMode) != requiredOwnerDirectoryMode ||
        (before.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0) {
        return setError(QStringLiteral("Legacy preferences have an unsafe parent."), errorMessage);
    }
    FileDescriptor descriptor(::open(
        encodedParent.constData(),
        O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    struct stat opened {};
    struct stat verified {};
    if (!descriptor.isValid() || ::fstat(descriptor.get(), &opened) != 0 ||
        !S_ISDIR(opened.st_mode) || opened.st_uid != ::geteuid() ||
        (opened.st_mode & requiredOwnerDirectoryMode) != requiredOwnerDirectoryMode ||
        (opened.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0 ||
        opened.st_dev != before.st_dev || opened.st_ino != before.st_ino ||
        opened.st_mode != before.st_mode || ::fstat(descriptor.get(), &verified) != 0 ||
        verified.st_dev != opened.st_dev || verified.st_ino != opened.st_ino ||
        verified.st_mode != opened.st_mode || verified.st_uid != opened.st_uid) {
        return setError(QStringLiteral("Legacy preferences have an unsafe parent."), errorMessage);
    }
#else
    const QFileInfo parent(parentPath);
    if (parent.isSymLink()) {
        return setError(QStringLiteral("Legacy preferences have an unsafe parent."), errorMessage);
    }
    if (!parent.exists()) {
        return true;
    }
    if (!parent.isDir()) {
        return setError(QStringLiteral("Legacy preferences have an unsafe parent."), errorMessage);
    }
#endif
    *exists = true;
    return true;
}

bool readImmutableLegacySnapshot(
    const QString& path,
    QByteArray* bytes,
    bool* exists,
    QString* errorMessage) {
    *exists = false;
#ifdef Q_OS_UNIX
    struct stat before {};
    const auto kind = inspectPathNoFollow(path, &before);
    if (kind == PathKind::Missing) {
        return true;
    }
    if (kind != PathKind::Regular || before.st_uid != ::geteuid() || before.st_nlink != 1 ||
        (before.st_mode & S_IRUSR) == 0 ||
        (before.st_mode & (kUnsafeWriteMode | kSpecialMode)) != 0 ||
        before.st_size < 0 || before.st_size > Preferences::maximumFileBytes) {
        return setError(QStringLiteral("Legacy preferences have unsafe local storage."), errorMessage);
    }
    if (!readBoundedFile(path, bytes, errorMessage)) {
        return false;
    }
    struct stat after {};
    if (inspectPathNoFollow(path, &after) != PathKind::Regular ||
        after.st_dev != before.st_dev || after.st_ino != before.st_ino ||
        after.st_mode != before.st_mode || after.st_uid != before.st_uid ||
        after.st_nlink != before.st_nlink || after.st_size != before.st_size ||
        after.st_mtim.tv_sec != before.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != before.st_mtim.tv_nsec ||
        after.st_ctim.tv_sec != before.st_ctim.tv_sec ||
        after.st_ctim.tv_nsec != before.st_ctim.tv_nsec) {
        bytes->clear();
        return setError(QStringLiteral("Legacy preferences changed while being read."), errorMessage);
    }
#else
    const QFileInfo before(path);
    if (before.isSymLink()) {
        return setError(QStringLiteral("Legacy preferences have unsafe local storage."), errorMessage);
    }
    if (!before.exists()) {
        return true;
    }
    if (!before.isFile() || before.size() > Preferences::maximumFileBytes ||
        !readBoundedFile(path, bytes, errorMessage)) {
        return setError(QStringLiteral("Legacy preferences have unsafe local storage."), errorMessage);
    }
    const QFileInfo after(path);
    if (!after.exists() || after.isSymLink() || after.size() != before.size() ||
        after.lastModified() != before.lastModified()) {
        bytes->clear();
        return setError(QStringLiteral("Legacy preferences changed while being read."), errorMessage);
    }
#endif
    *exists = true;
    return true;
}
}

Preferences::Preferences(QString filePath, QString legacyFilePath)
    : filePath_(QDir::cleanPath(QFileInfo(filePath).absoluteFilePath())),
      legacyFilePath_(legacyFilePath.isEmpty()
                          ? QString{}
                          : QDir::cleanPath(QFileInfo(legacyFilePath).absoluteFilePath())) {}

bool Preferences::prepareFile(QString* errorMessage) const {
    const QFileInfo info(filePath_);
    if (!LocalState::preparePrivateApplicationDirectory(info.absolutePath(), errorMessage)) {
        return false;
    }
#ifdef Q_OS_UNIX
    const auto encodedPath = QFile::encodeName(filePath_);
    struct stat pathStatus {};
    if (::lstat(encodedPath.constData(), &pathStatus) != 0) {
        if (errno != ENOENT) {
            return setError(QStringLiteral("Could not inspect private preferences."), errorMessage);
        }
        return true;
    }
    if (!S_ISREG(pathStatus.st_mode)) {
        return setError(QStringLiteral("Preferences have an unsafe file type."), errorMessage);
    }
    FileDescriptor descriptor(::open(
        encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (!descriptor.isValid()) {
        return setError(QStringLiteral("Could not safely open private preferences."), errorMessage);
    }
    return validateDescriptor(descriptor.get(), true, errorMessage);
#else
    if (info.isSymLink()) {
        return setError(QStringLiteral("Preferences have unsafe local storage."), errorMessage);
    }
    if (info.exists()) {
        if (!info.isFile() || info.size() > maximumFileBytes) {
            return setError(QStringLiteral("Preferences have unsafe local storage."), errorMessage);
        }
    }
    return true;
#endif
}

bool Preferences::validateFile(QString* errorMessage) const {
    return prepareFile(errorMessage);
}

bool Preferences::validateFileWithoutMutation(QString* errorMessage) const {
#ifdef Q_OS_UNIX
    const auto encodedPath = QFile::encodeName(filePath_);
    FileDescriptor descriptor(::open(
        encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (!descriptor.isValid()) {
        return setError(QStringLiteral("Could not safely open private preferences."), errorMessage);
    }
    return validateDescriptorWithoutMutation(descriptor.get(), true, errorMessage);
#else
    const QFileInfo info(filePath_);
    if (info.isSymLink() || !info.isFile() || info.size() > maximumFileBytes) {
        return setError(QStringLiteral("Preferences have unsafe local storage."), errorMessage);
    }
    return true;
#endif
}

bool Preferences::loadFromCurrentPath(
    WindowPreferences* preferences,
    bool* exists,
    bool migrateVersionless,
    QString* errorMessage) {
    *preferences = {};
    *exists = false;
    if (!LocalState::preparePrivateApplicationDirectory(
            QFileInfo(filePath_).absolutePath(), errorMessage)) {
        return false;
    }
    PreferenceLock lock(filePath_);
    if (!lock.acquire(errorMessage)) {
        return false;
    }
    return loadCurrentPathWhileLocked(
        preferences, exists, migrateVersionless, errorMessage);
}

bool Preferences::loadCurrentPathWhileLocked(
    WindowPreferences* preferences,
    bool* exists,
    bool migrateVersionless,
    QString* errorMessage) {
    *preferences = {};
    if (!inspectPreferencePresence(filePath_, exists, errorMessage)) {
        return false;
    }
    if (!*exists) {
        return true;
    }
    if (!validateFileWithoutMutation(errorMessage)) {
        return false;
    }
    QByteArray bytes;
    if (!readBoundedFile(filePath_, &bytes, errorMessage)) {
        return false;
    }
    bool versionless = false;
    int schemaVersion = 0;
    if (const auto recognizable = recognizableSchemaVersion(bytes);
        recognizable.has_value() && *recognizable > currentSchemaVersion) {
        return setError(
            QStringLiteral("Preferences use a newer unsupported schema."), errorMessage);
    }
    if (!parseSettingsBytes(
            filePath_, bytes, preferences, &versionless, &schemaVersion, errorMessage)) {
        *preferences = {};
        return false;
    }
    if (versionless && migrateVersionless) {
        QByteArray migratedBytes;
        if (!serializeSettings(filePath_, *preferences, &migratedBytes, errorMessage) ||
            !publishSettingsBytes(filePath_, migratedBytes, errorMessage)) {
            *preferences = {};
            return false;
        }
    } else if (!validateFile(errorMessage)) {
        *preferences = {};
        return false;
    }
    return true;
}

bool Preferences::loadWindowPreferences(
    WindowPreferences* preferences,
    QString* errorMessage) {
    if (preferences == nullptr) {
        return setError(QStringLiteral("Preferences output is unavailable."), errorMessage);
    }
    bool exists = false;
    if (!loadFromCurrentPath(preferences, &exists, true, errorMessage)) {
        return false;
    }
    if (exists) {
        return true;
    }
    if (legacyFilePath_.isEmpty() || legacyFilePath_ == filePath_) {
        return true;
    }
    bool legacyParentPresent = false;
    if (!inspectLegacyParentPresence(
            legacyFilePath_, &legacyParentPresent, errorMessage)) {
        return false;
    }
    if (!legacyParentPresent) {
        return true;
    }
    QtSettingsLockLease legacyLock;
    if (!legacyLock.acquire(legacyFilePath_, errorMessage)) {
        return false;
    }
    WindowPreferences legacyWindowPreferences;
    bool legacyExists = false;
    if (!loadLegacyPathWhileLocked(
            &legacyWindowPreferences, &legacyExists, errorMessage)) {
        return false;
    }
    if (!legacyExists) {
        return true;
    }
    return importLegacyIfCurrentAbsent(
        legacyWindowPreferences, preferences, errorMessage);
}

bool Preferences::loadLegacyPathWhileLocked(
    WindowPreferences* preferences,
    bool* exists,
    QString* errorMessage) {
    *preferences = {};
    if (!inspectPreferencePresence(legacyFilePath_, exists, errorMessage)) {
        return false;
    }
    if (!*exists) {
        return true;
    }
    QByteArray bytes;
    bool snapshotExists = false;
    if (!readImmutableLegacySnapshot(
            legacyFilePath_, &bytes, &snapshotExists, errorMessage)) {
        return false;
    }
    *exists = snapshotExists;
    if (!snapshotExists) {
        return true;
    }
    bool versionless = false;
    int schemaVersion = 0;
    if (!parseSettingsBytes(
            filePath_, bytes, preferences, &versionless, &schemaVersion, errorMessage)) {
        *preferences = {};
        return false;
    }
    if (!versionless) {
        *preferences = {};
        return setError(QStringLiteral("Legacy preferences have an invalid schema."), errorMessage);
    }
    return true;
}

bool Preferences::importLegacyIfCurrentAbsent(
    const WindowPreferences& legacyPreferences,
    WindowPreferences* preferences,
    QString* errorMessage) {
    if (!LocalState::preparePrivateApplicationDirectory(
            QFileInfo(filePath_).absolutePath(), errorMessage)) {
        return false;
    }
    PreferenceLock lock(filePath_);
    if (!lock.acquire(errorMessage)) {
        return false;
    }
    bool exists = false;
    WindowPreferences currentPreferences;
    if (!loadCurrentPathWhileLocked(
            &currentPreferences, &exists, true, errorMessage)) {
        return false;
    }
    if (exists) {
        *preferences = std::move(currentPreferences);
        return true;
    }
    QByteArray serialized;
    bool stillExists = false;
    if (!serializeSettings(filePath_, legacyPreferences, &serialized, errorMessage) ||
        !inspectPreferencePresence(filePath_, &stillExists, errorMessage) || stillExists ||
        !publishSettingsBytes(filePath_, serialized, errorMessage)) {
        if (stillExists) {
            return setError(QStringLiteral("Preferences changed during legacy import."), errorMessage);
        }
        return false;
    }
    *preferences = legacyPreferences;
    return true;
}

bool Preferences::saveWindowPreferences(
    const WindowPreferences& preferences,
    QString* errorMessage) {
    if (preferences.geometry.size() > maximumValueBytes ||
        preferences.state.size() > maximumValueBytes) {
        return setError(QStringLiteral("Window preferences exceed the size limit."), errorMessage);
    }
    if (!LocalState::preparePrivateApplicationDirectory(
            QFileInfo(filePath_).absolutePath(), errorMessage)) {
        return false;
    }
    bool currentExists = false;
    if (!inspectPreferencePresence(filePath_, &currentExists, errorMessage)) {
        return false;
    }
    if (currentExists) {
        PreferenceLock currentLock(filePath_);
        if (!currentLock.acquire(errorMessage)) {
            return false;
        }
        if (!inspectPreferencePresence(filePath_, &currentExists, errorMessage)) {
            return false;
        }
        if (currentExists) {
            return saveCurrentPathWhileLocked(preferences, errorMessage);
        }
    }

    if (!legacyFilePath_.isEmpty() && legacyFilePath_ != filePath_) {
        bool legacyParentPresent = false;
        if (!inspectLegacyParentPresence(
                legacyFilePath_, &legacyParentPresent, errorMessage)) {
            return false;
        }
        if (legacyParentPresent) {
            QtSettingsLockLease legacyLock;
            if (!legacyLock.acquire(legacyFilePath_, errorMessage)) {
                return false;
            }
            PreferenceLock currentLock(filePath_);
            if (!currentLock.acquire(errorMessage)) {
                return false;
            }
            if (!inspectPreferencePresence(filePath_, &currentExists, errorMessage)) {
                return false;
            }
            if (currentExists) {
                return saveCurrentPathWhileLocked(preferences, errorMessage);
            }
            WindowPreferences legacyPreferences;
            bool legacyExists = false;
            if (!loadLegacyPathWhileLocked(
                    &legacyPreferences, &legacyExists, errorMessage)) {
                return false;
            }
            if (legacyExists) {
                return setError(
                    QStringLiteral("Legacy preferences must be loaded before saving."),
                    errorMessage);
            }
            return saveCurrentPathWhileLocked(preferences, errorMessage);
        }
    }

    PreferenceLock currentLock(filePath_);
    if (!currentLock.acquire(errorMessage)) {
        return false;
    }
    return saveCurrentPathWhileLocked(preferences, errorMessage);
}

bool Preferences::saveCurrentPathWhileLocked(
    const WindowPreferences& preferences,
    QString* errorMessage) {
    bool exists = false;
    if (!inspectPreferencePresence(filePath_, &exists, errorMessage)) {
        return false;
    }
    if (exists) {
        if (!validateFileWithoutMutation(errorMessage)) {
            return false;
        }
        QByteArray existingBytes;
        WindowPreferences existingPreferences;
        bool versionless = false;
        int schemaVersion = 0;
        if (!readBoundedFile(filePath_, &existingBytes, errorMessage)) {
            return false;
        }
        if (const auto recognizable = recognizableSchemaVersion(existingBytes);
            recognizable.has_value() && *recognizable > currentSchemaVersion) {
            return setError(
                QStringLiteral("Preferences use a newer unsupported schema."), errorMessage);
        }
        if (!parseSettingsBytes(
            filePath_,
            existingBytes,
            &existingPreferences,
            &versionless,
            &schemaVersion,
            errorMessage)) {
            return false;
        }
    }
    QByteArray serialized;
    bool publishExists = false;
    if (!serializeSettings(filePath_, preferences, &serialized, errorMessage) ||
        !inspectPreferencePresence(filePath_, &publishExists, errorMessage) ||
        (publishExists && !validateFileWithoutMutation(errorMessage)) ||
        !publishSettingsBytes(filePath_, serialized, errorMessage)) {
        return false;
    }
    return true;
}
