#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <optional>

struct QpdfResult final {
    bool succeeded{false};
    QString message;
};

struct QpdfLimits final {
    qint64 maximumOutputBytes{2LL * 1024 * 1024 * 1024};
    int operationTimeoutMs{120'000};
};

class QpdfOperations final {
public:
    static constexpr qint64 maximumInputBytes = 2LL * 1024 * 1024 * 1024;
    static constexpr int maximumPageCount = 100'000;
    static constexpr int maximumOperationTimeoutMs = 120'000;

    [[nodiscard]] static bool isValidPageRange(const QString& range, int pageCount);
    [[nodiscard]] static QpdfResult merge(
        const QStringList& inputPaths,
        const QString& outputPath,
        const std::atomic_bool* cancelled = nullptr,
        QpdfLimits limits = {});
    [[nodiscard]] static QpdfResult extract(
        const QString& inputPath,
        const QString& pageRange,
        int pageCount,
        const QString& outputPath,
        const std::atomic_bool* cancelled = nullptr,
        QpdfLimits limits = {});
    [[nodiscard]] static QpdfResult deletePages(
        const QString& inputPath,
        const QString& pageRange,
        int pageCount,
        const QString& outputPath,
        const std::atomic_bool* cancelled = nullptr,
        QpdfLimits limits = {});
    [[nodiscard]] static QpdfResult rotate(
        const QString& inputPath,
        const QString& pageRange,
        int pageCount,
        bool clockwise,
        const QString& outputPath,
        const std::atomic_bool* cancelled = nullptr,
        QpdfLimits limits = {});

    QpdfOperations() = delete;

private:
    [[nodiscard]] static QpdfResult run(
        const QStringList& arguments,
        const QString& outputPath,
        const QStringList& protectedInputPaths,
        const std::atomic_bool* cancelled,
        QpdfLimits limits,
        std::optional<int> expectedPageCount = std::nullopt);
};
