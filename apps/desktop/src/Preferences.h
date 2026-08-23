#pragma once

#include <QByteArray>
#include <QString>

struct WindowPreferences final {
    QByteArray geometry;
    QByteArray state;
};

class Preferences final {
public:
    static constexpr int currentSchemaVersion = 1;
    static constexpr qsizetype maximumValueBytes = 32 * 1024;
    static constexpr qint64 maximumFileBytes = 96 * 1024;

    explicit Preferences(QString filePath, QString legacyFilePath = {});

    [[nodiscard]] bool loadWindowPreferences(
        WindowPreferences* preferences,
        QString* errorMessage = nullptr);
    [[nodiscard]] bool saveWindowPreferences(
        const WindowPreferences& preferences,
        QString* errorMessage = nullptr);

private:
    [[nodiscard]] bool loadFromCurrentPath(
        WindowPreferences* preferences,
        bool* exists,
        bool migrateVersionless,
        QString* errorMessage);
    [[nodiscard]] bool loadCurrentPathWhileLocked(
        WindowPreferences* preferences,
        bool* exists,
        bool migrateVersionless,
        QString* errorMessage);
    [[nodiscard]] bool importLegacyIfCurrentAbsent(
        const WindowPreferences& legacyPreferences,
        WindowPreferences* preferences,
        QString* errorMessage);
    [[nodiscard]] bool loadLegacyPathWhileLocked(
        WindowPreferences* preferences,
        bool* exists,
        QString* errorMessage);
    [[nodiscard]] bool saveCurrentPathWhileLocked(
        const WindowPreferences& preferences,
        QString* errorMessage);
    [[nodiscard]] bool prepareFile(QString* errorMessage) const;
    [[nodiscard]] bool validateFile(QString* errorMessage) const;
    [[nodiscard]] bool validateFileWithoutMutation(QString* errorMessage) const;

    QString filePath_;
    QString legacyFilePath_;
};
