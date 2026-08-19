#pragma once

#include <QString>

class Logging final {
public:
    static void install(const QString& logDirectory);
    static void shutdown();

    Logging() = delete;
};
