#include "ThumbnailModel.h"

#include <QPdfDocument>
#include <QThread>

#include <algorithm>
#include <cmath>

ThumbnailModel::ThumbnailModel(QPdfDocument* document, QObject* parent)
    : QAbstractListModel(parent),
      renderer_([](QPdfDocument& source, int page, const QSize& size) {
          return source.render(page, size);
      }) {
    renderTimer_.setSingleShot(true);
    connect(&renderTimer_, &QTimer::timeout, this, &ThumbnailModel::renderNext);
    setDocument(document);
}

int ThumbnailModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || document_.isNull() ? 0 : document_->pageCount();
}

QVariant ThumbnailModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || document_.isNull() || index.row() < 0
        || index.row() >= document_->pageCount()) {
        return {};
    }
    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
        auto label = document_->pageLabel(index.row());
        if (label.isEmpty()) {
            label = QString::number(index.row() + 1);
        }
        return tr("Page %1").arg(label);
    }
    if (role != Qt::DecorationRole) {
        return {};
    }
    if (const auto* cached = cache_.object(index.row())) {
        return *cached;
    }
    (void)const_cast<ThumbnailModel*>(this)->requestThumbnail(index.row());
    return {};
}

int ThumbnailModel::pendingRequestCount() const {
    return static_cast<int>(pendingPages_.size());
}

int ThumbnailModel::failedPageCount() const {
    return static_cast<int>(failedPages_.size());
}

int ThumbnailModel::cacheCostBytes() const {
    return static_cast<int>(cache_.totalCost());
}

int ThumbnailModel::cancelPendingRequests() {
    const auto cancelled = static_cast<int>(pendingPages_.size());
    renderTimer_.stop();
    pendingPages_.clear();
    pendingSet_.clear();
    return cancelled;
}

void ThumbnailModel::setDocument(QPdfDocument* document) {
    if (document_.data() == document) {
        return;
    }
    beginResetModel();
    disconnect(pageCountConnection_);
    disconnect(destroyedConnection_);
    resetForDocumentChange();
    document_ = document;
    if (document != nullptr) {
        pageCountConnection_ = connect(document, &QPdfDocument::pageCountChanged, this, [this] {
            beginResetModel();
            resetForDocumentChange();
            endResetModel();
        });
        destroyedConnection_ = connect(document, &QObject::destroyed, this, [this] {
            beginResetModel();
            resetForDocumentChange();
            document_ = nullptr;
            endResetModel();
        });
    }
    endResetModel();
}

std::optional<QSize> ThumbnailModel::boundedRenderSize(const QSizeF& pointSize) {
    const auto width = pointSize.width();
    const auto height = pointSize.height();
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
        return std::nullopt;
    }
    const auto scaledHeight = static_cast<double>(thumbnailWidth) * height / width;
    if (!std::isfinite(scaledHeight) || scaledHeight <= 0.0) {
        return std::nullopt;
    }
    const auto boundedHeight = scaledHeight >= maximumThumbnailHeight
        ? maximumThumbnailHeight
        : std::clamp(qRound(scaledHeight), 1, maximumThumbnailHeight);
    return QSize{thumbnailWidth, boundedHeight};
}

bool ThumbnailModel::requestThumbnail(int page) {
    if (document_.isNull() || page < 0 || page >= document_->pageCount()
        || cache_.contains(page) || failedPages_.contains(page) || pendingSet_.contains(page)
        || pendingPages_.size() >= static_cast<qsizetype>(maximumPendingRequests)) {
        return false;
    }
    pendingPages_.enqueue(page);
    pendingSet_.insert(page);
    scheduleNext();
    return true;
}

void ThumbnailModel::renderNext() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (pendingPages_.isEmpty()) {
        return;
    }
    const auto page = pendingPages_.dequeue();
    pendingSet_.remove(page);
    const QPointer<QPdfDocument> source = document_;
    const auto generation = documentGeneration_;

    bool succeeded = false;
    if (!source.isNull() && source->thread() == QThread::currentThread()
        && page >= 0 && page < source->pageCount()) {
        const auto renderSize = boundedRenderSize(source->pagePointSize(page));
        if (renderSize.has_value()) {
            const QPointer<ThumbnailModel> self(this);
            const auto image = renderer_(*source, page, *renderSize);
            if (self.isNull()) {
                return;
            }
            if (source != document_ || generation != documentGeneration_) {
                scheduleNext();
                return;
            }
            if (!image.isNull() && image.width() <= thumbnailWidth
                && image.height() <= maximumThumbnailHeight) {
                auto pixmap = QPixmap::fromImage(image);
                const auto pixelBytes = static_cast<qint64>(pixmap.width())
                    * static_cast<qint64>(pixmap.height()) * 4;
                const auto cost = static_cast<int>(std::clamp<qint64>(
                    pixelBytes, 1, maximumCacheBytes));
                cache_.insert(page, new QPixmap(std::move(pixmap)), cost);
                succeeded = true;
                emit dataChanged(index(page), index(page), {Qt::DecorationRole});
                if (self.isNull()) {
                    return;
                }
                if (source != document_ || generation != documentGeneration_) {
                    scheduleNext();
                    return;
                }
            }
        }
    }
    if (!succeeded) {
        failedPages_.insert(page);
    }
    const QPointer<ThumbnailModel> self(this);
    emit thumbnailFinished(page, succeeded);
    if (!self.isNull()) {
        scheduleNext();
    }
}

void ThumbnailModel::resetForDocumentChange() {
    ++documentGeneration_;
    (void)cancelPendingRequests();
    cache_.clear();
    failedPages_.clear();
}

void ThumbnailModel::scheduleNext() {
    if (!pendingPages_.isEmpty() && !renderTimer_.isActive()) {
        renderTimer_.start(0);
    }
}
