#include "LocalState.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr int kMaximumRecentFiles = 50;
constexpr int kMaximumErrorLength = 256;

bool setError(const QString& message, QString* errorMessage) {
    if (errorMessage != nullptr) {
        *errorMessage = message.left(kMaximumErrorLength);
    }
    return false;
}

#ifdef Q_OS_UNIX
constexpr mode_t kPrivateDirectoryMode = S_IRWXU;
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

bool validateAndRepairDescriptor(
    int descriptor,
    bool expectDirectory,
    mode_t requiredMode,
    bool requireSingleLink,
    QString* errorMessage) {
    struct stat status {};
    if (::fstat(descriptor, &status) != 0) {
        return setError(QStringLiteral("Could not verify private local state."), errorMessage);
    }
    const bool expectedType = expectDirectory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode);
    if (!expectedType || status.st_uid != ::geteuid() ||
        (requireSingleLink && status.st_nlink != 1) ||
        (status.st_mode & kSpecialMode) != 0) {
        return setError(QStringLiteral("Local state has unsafe ownership or file type."), errorMessage);
    }
    if ((status.st_mode & kUnsafeWriteMode) != 0) {
        return setError(QStringLiteral("Local state is writable by another user."), errorMessage);
    }
    if ((status.st_mode & 0777) != requiredMode &&
        ::fchmod(descriptor, requiredMode) != 0) {
        return setError(QStringLiteral("Could not restrict local state permissions."), errorMessage);
    }
    struct stat verifiedStatus {};
    if (::fstat(descriptor, &verifiedStatus) != 0 ||
        verifiedStatus.st_dev != status.st_dev || verifiedStatus.st_ino != status.st_ino ||
        verifiedStatus.st_uid != ::geteuid() ||
        (expectDirectory ? !S_ISDIR(verifiedStatus.st_mode) : !S_ISREG(verifiedStatus.st_mode)) ||
        (verifiedStatus.st_mode & 07777) != requiredMode ||
        (requireSingleLink && verifiedStatus.st_nlink != 1)) {
        return setError(QStringLiteral("Could not verify private local state permissions."), errorMessage);
    }
    return true;
}

bool validateExistingPrivateFile(const QString& path, bool allowMissing, QString* errorMessage) {
    const auto encodedPath = QFile::encodeName(path);
    struct stat pathStatus {};
    if (::lstat(encodedPath.constData(), &pathStatus) != 0) {
        if (allowMissing && errno == ENOENT) {
            return true;
        }
        return setError(QStringLiteral("Could not inspect the local state database."), errorMessage);
    }
    if (!S_ISREG(pathStatus.st_mode)) {
        return setError(QStringLiteral("The local state database has an unsafe file type."), errorMessage);
    }
    FileDescriptor descriptor(::open(
        encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (!descriptor.isValid()) {
        return setError(QStringLiteral("Could not safely open the local state database."), errorMessage);
    }
    return validateAndRepairDescriptor(
        descriptor.get(), false, kPrivateFileMode, true, errorMessage);
}

bool prepareDatabaseFile(const QString& path, QString* errorMessage) {
    const auto encodedPath = QFile::encodeName(path);
    struct stat pathStatus {};
    if (::lstat(encodedPath.constData(), &pathStatus) == 0) {
        return validateExistingPrivateFile(path, false, errorMessage);
    }
    if (errno != ENOENT) {
        return setError(QStringLiteral("Could not inspect the local state database."), errorMessage);
    }
    FileDescriptor descriptor(::open(
        encodedPath.constData(),
        O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
        kPrivateFileMode));
    if (!descriptor.isValid()) {
        if (errno == EEXIST) {
            return validateExistingPrivateFile(path, false, errorMessage);
        }
        return setError(QStringLiteral("Could not create the private local state database."), errorMessage);
    }
    return validateAndRepairDescriptor(
        descriptor.get(), false, kPrivateFileMode, true, errorMessage);
}

bool validateSqliteFiles(const QString& databasePath, bool createDatabase, QString* errorMessage) {
    if (createDatabase) {
        if (!validateExistingPrivateFile(databasePath, true, errorMessage) ||
            !validateExistingPrivateFile(databasePath + QStringLiteral("-wal"), true, errorMessage) ||
            !validateExistingPrivateFile(databasePath + QStringLiteral("-shm"), true, errorMessage) ||
            !prepareDatabaseFile(databasePath, errorMessage)) {
            return false;
        }
        return true;
    }
    return validateExistingPrivateFile(databasePath, false, errorMessage) &&
           validateExistingPrivateFile(databasePath + QStringLiteral("-wal"), true, errorMessage) &&
           validateExistingPrivateFile(databasePath + QStringLiteral("-shm"), true, errorMessage);
}
#endif
}

LocalState::LocalState(QString databasePath)
    : databasePath_(QDir::cleanPath(QFileInfo(databasePath).absoluteFilePath())),
      connectionName_(QStringLiteral("zenpdf-state-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

LocalState::~LocalState() {
    closeAndRemoveConnection();
}

bool LocalState::preparePrivateApplicationDirectory(
    const QString& directoryPath,
    QString* errorMessage) {
    if (directoryPath.isEmpty()) {
        return setError(QStringLiteral("The private local state directory is unavailable."), errorMessage);
    }
#ifdef Q_OS_UNIX
    const QFileInfo directoryInfo(directoryPath);
    if (!QDir().mkpath(directoryInfo.absolutePath())) {
        return setError(QStringLiteral("Could not prepare the local state parent directory."), errorMessage);
    }
    const auto encodedPath = QFile::encodeName(directoryInfo.absoluteFilePath());
    if (::mkdir(encodedPath.constData(), kPrivateDirectoryMode) != 0 && errno != EEXIST) {
        return setError(QStringLiteral("Could not create the private local state directory."), errorMessage);
    }
    FileDescriptor descriptor(::open(
        encodedPath.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!descriptor.isValid()) {
        return setError(QStringLiteral("Could not safely open the local state directory."), errorMessage);
    }
    return validateAndRepairDescriptor(
        descriptor.get(), true, kPrivateDirectoryMode, false, errorMessage);
#else
    if (!QDir().mkpath(directoryPath)) {
        return setError(QStringLiteral("Could not create the local state directory."), errorMessage);
    }
    return true;
#endif
}

bool LocalState::initialize(QString* errorMessage) {
    closeAndRemoveConnection();
    const QFileInfo databaseInfo(databasePath_);
    if (!preparePrivateApplicationDirectory(databaseInfo.absolutePath(), errorMessage)) {
        return false;
    }
#ifdef Q_OS_UNIX
    if (!validateSqliteFiles(databasePath_, true, errorMessage)) {
        return false;
    }
#endif

    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database.setDatabaseName(databasePath_);
    database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    bool initialized = false;
    QString failureMessage;
    if (!database.open()) {
        failureMessage = QStringLiteral("Could not open the private local state database.");
    } else {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("PRAGMA journal_mode=WAL")) ||
            !query.exec(QStringLiteral("PRAGMA secure_delete=ON")) ||
            !query.exec(QStringLiteral("PRAGMA foreign_keys=ON")) ||
            !query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS recent_files ("
                "path TEXT PRIMARY KEY NOT NULL, "
                "opened_at_utc TEXT NOT NULL)"))) {
            failureMessage = QStringLiteral("Could not initialize the private local state database.");
        } else if (!query.exec(QStringLiteral("PRAGMA secure_delete")) ||
                   !query.next() || query.value(0).toInt() != 1) {
            failureMessage = QStringLiteral("Could not enable private local state deletion.");
        } else {
            initialized = true;
        }
    }
    if (!initialized) {
        database.close();
        database = {};
        QSqlDatabase::removeDatabase(connectionName_);
        return setError(failureMessage, errorMessage);
    }

#ifdef Q_OS_UNIX
    if (!validateSqliteFiles(databasePath_, false, errorMessage)) {
        database.close();
        database = {};
        QSqlDatabase::removeDatabase(connectionName_);
        return false;
    }
#endif
    return true;
}

bool LocalState::recordRecentFile(const QString& path, QString* errorMessage) {
    const auto cleanPath = normalizedPath(path);
    if (cleanPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("A non-empty local file path is required.");
        }
        return false;
    }

    auto database = QSqlDatabase::database(connectionName_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO recent_files(path, opened_at_utc) VALUES(:path, :opened_at) "
        "ON CONFLICT(path) DO UPDATE SET opened_at_utc = excluded.opened_at_utc"));
    query.bindValue(QStringLiteral(":path"), cleanPath);
    query.bindValue(QStringLiteral(":opened_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        return reportDatabaseError(QStringLiteral("Could not update recent files"), errorMessage);
    }

    QSqlQuery trimQuery(database);
    trimQuery.prepare(QStringLiteral(
        "DELETE FROM recent_files WHERE path NOT IN ("
        "SELECT path FROM recent_files ORDER BY opened_at_utc DESC LIMIT :limit)"));
    trimQuery.bindValue(QStringLiteral(":limit"), kMaximumRecentFiles);
    if (!trimQuery.exec()) {
        return reportDatabaseError(QStringLiteral("Could not bound recent files"), errorMessage);
    }
    return true;
}

QList<RecentFile> LocalState::recentFiles(int limit, QString* errorMessage) const {
    QList<RecentFile> result;
    auto database = QSqlDatabase::database(connectionName_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT path, opened_at_utc FROM recent_files "
        "ORDER BY opened_at_utc DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), std::clamp(limit, 0, kMaximumRecentFiles));
    if (!query.exec()) {
        (void)reportDatabaseError(QStringLiteral("Could not read recent files"), errorMessage);
        return result;
    }

    while (query.next()) {
        result.append({query.value(0).toString(), QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs)});
    }
    return result;
}

bool LocalState::clearRecentFiles(QString* errorMessage) {
    auto database = QSqlDatabase::database(connectionName_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("DELETE FROM recent_files"))) {
        return reportDatabaseError(QStringLiteral("Could not clear recent files"), errorMessage);
    }
    query.finish();
    const auto truncateWal = [&query] {
        return query.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")) &&
               query.next() && query.value(0).toInt() == 0;
    };
    if (!truncateWal() || !query.exec(QStringLiteral("VACUUM")) || !truncateWal()) {
        return reportDatabaseError(QStringLiteral("Could not purge cleared recent files"), errorMessage);
    }
    return true;
}

QString LocalState::normalizedPath(const QString& path) const {
    if (path.trimmed().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool LocalState::reportDatabaseError(const QString& context, QString* errorMessage) const {
    return setError(context.endsWith(u'.') ? context : context + u'.', errorMessage);
}

void LocalState::closeAndRemoveConnection() {
    if (!QSqlDatabase::contains(connectionName_)) {
        return;
    }
    {
        auto database = QSqlDatabase::database(connectionName_, false);
        if (database.isValid()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName_);
}
