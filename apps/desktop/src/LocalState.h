#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct RecentFile final {
    QString path;
    QDateTime openedAt;
};

class LocalState final {
public:
    explicit LocalState(QString databasePath);
    ~LocalState();

    LocalState(const LocalState&) = delete;
    LocalState& operator=(const LocalState&) = delete;
    LocalState(LocalState&&) = delete;
    LocalState& operator=(LocalState&&) = delete;

    [[nodiscard]] bool initialize(QString* errorMessage = nullptr);
    [[nodiscard]] bool recordRecentFile(const QString& path, QString* errorMessage = nullptr);
    [[nodiscard]] QList<RecentFile> recentFiles(int limit = 10, QString* errorMessage = nullptr) const;
    [[nodiscard]] bool clearRecentFiles(QString* errorMessage = nullptr);

private:
    [[nodiscard]] QString normalizedPath(const QString& path) const;
    [[nodiscard]] bool reportDatabaseError(const QString& context, QString* errorMessage) const;

    QString databasePath_;
    QString connectionName_;
};
