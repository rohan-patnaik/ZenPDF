#include "PrintPolicy.h"

#include <Qt>

#include <cmath>

std::optional<QSize> PrintPolicy::boundedRenderSize(QSizeF pagePoints, QSizeF targetPixels) {
    const auto isValidDimension = [](qreal value) {
        return std::isfinite(value) && value > 0.0;
    };
    if (!isValidDimension(pagePoints.width()) || !isValidDimension(pagePoints.height()) ||
        pagePoints.width() > maximumPagePointDimension ||
        pagePoints.height() > maximumPagePointDimension ||
        !isValidDimension(targetPixels.width()) || !isValidDimension(targetPixels.height())) {
        return std::nullopt;
    }

    QSize renderSize = pagePoints.scaled(targetPixels, Qt::KeepAspectRatio).toSize();
    if (renderSize.isEmpty()) {
        return std::nullopt;
    }
    if (renderSize.width() > maximumRenderDimension || renderSize.height() > maximumRenderDimension) {
        renderSize.scale(maximumRenderDimension, maximumRenderDimension, Qt::KeepAspectRatio);
    }
    return renderSize;
}

PrintRenderDecision PrintPolicy::renderIfNotCancelled(
    bool cancelled,
    const std::function<bool()>& render) {
    if (cancelled) {
        return PrintRenderDecision::Cancelled;
    }
    return render() ? PrintRenderDecision::Rendered : PrintRenderDecision::Failed;
}
