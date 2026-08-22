#pragma once

#include <QAbstractListModel>
#include <QCache>
#include <QImage>
#include <QMetaObject>
#include <QPixmap>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QSizeF>
#include <QTimer>

#include <functional>
#include <optional>

class QPdfDocument;
class ThumbnailModelTest;

class ThumbnailModel final : public QAbstractListModel {
    Q_OBJECT

public:
    static constexpr int thumbnailWidth = 128;
    static constexpr int maximumThumbnailHeight = 512;
    static constexpr int maximumPendingRequests = 64;
    static constexpr int maximumCacheBytes = 32 * 1024 * 1024;

    explicit ThumbnailModel(QPdfDocument* document, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] int pendingRequestCount() const;
    [[nodiscard]] int failedPageCount() const;
    [[nodiscard]] int cacheCostBytes() const;
    [[nodiscard]] int cancelPendingRequests();
    void setDocument(QPdfDocument* document);

    [[nodiscard]] static std::optional<QSize> boundedRenderSize(const QSizeF& pointSize);

signals:
    void thumbnailFinished(int page, bool succeeded);

private:
    friend class ThumbnailModelTest;
    using Renderer = std::function<QImage(QPdfDocument&, int, const QSize&)>;

    [[nodiscard]] bool requestThumbnail(int page);
    void renderNext();
    void resetForDocumentChange();
    void scheduleNext();

    QPointer<QPdfDocument> document_;
    QCache<int, QPixmap> cache_{maximumCacheBytes};
    QQueue<int> pendingPages_;
    QSet<int> pendingSet_;
    QSet<int> failedPages_;
    QTimer renderTimer_;
    QMetaObject::Connection pageCountConnection_;
    QMetaObject::Connection destroyedConnection_;
    Renderer renderer_;
    quint64 documentGeneration_{0};
};
