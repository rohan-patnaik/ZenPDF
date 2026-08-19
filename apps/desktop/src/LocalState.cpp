#include "LocalState.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {
constexpr int kMaximumRecentFiles = 50;
}

LocalState::LocalState(QString databasePath)
    : databasePath_(std::move(databasePath)),
      connectionName_(QStringLiteral("zenpdf-state-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

LocalState::~LocalState() {
    {
        auto database = QSqlDatabase::database(connectionName_, false);
        if (database.isValid()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName_);
}

bool LocalState::initialize(QString* errorMessage) {
    const QFileInfo databaseInfo(databasePath_);
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create the local state directory.");
        }
        return false;
    }

    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database.setDatabaseName(databasePath_);
    database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    if (!database.open()) {
        return reportDatabaseError(QStringLiteral("Could not open the local state database"), errorMessage);
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode=WAL")) ||
        !query.exec(QStringLiteral("PRAGMA foreign_keys=ON")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS recent_files ("
            "path TEXT PRIMARY KEY NOT NULL, "
            "opened_at_utc TEXT NOT NULL)"))) {
        return reportDatabaseError(QStringLiteral("Could not initialize the local state database"), errorMessage);
    }
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
        reportDatabaseError(QStringLiteral("Could not read recent files"), errorMessage);
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
    return true;
}

QString LocalState::normalizedPath(const QString& path) const {
    if (path.trimmed().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool LocalState::reportDatabaseError(const QString& context, QString* errorMessage) const {
    if (errorMessage != nullptr) {
        const auto database = QSqlDatabase::database(connectionName_, false);
        *errorMessage = QStringLiteral("%1: %2").arg(context, database.lastError().text());
    }
    return false;
}
