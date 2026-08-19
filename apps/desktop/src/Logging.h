#pragma once

#include <QtGlobal>
#include <QString>

class Logging final {
public:
    static constexpr qint64 defaultMaximumLogBytes = 1024 * 1024;

    static void install(const QString& logDirectory, qint64 maximumLogBytes = defaultMaximumLogBytes);
    static void shutdown();

    Logging() = delete;
};
