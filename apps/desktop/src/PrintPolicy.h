#pragma once

#include <QSize>
#include <QSizeF>

#include <functional>
#include <optional>

enum class PrintRenderDecision {
    Cancelled,
    Rendered,
    Failed,
};

class PrintPolicy final {
public:
    static constexpr int maximumPagesPerJob = 100;
    static constexpr int maximumRenderDimension = 2048;
    static constexpr qreal maximumPagePointDimension = 14'400.0;

    [[nodiscard]] static std::optional<QSize> boundedRenderSize(
        QSizeF pagePoints,
        QSizeF targetPixels);
    [[nodiscard]] static PrintRenderDecision renderIfNotCancelled(
        bool cancelled,
        const std::function<bool()>& render);

    PrintPolicy() = delete;
};
