#include "ThumbnailModel.h"

#include <QFile>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <limits>
#include <memory>

class ThumbnailModelTest final : public QObject {
    Q_OBJECT

private slots:
    void boundsRenderSizes_data();
    void boundsRenderSizes();
    void defersDeduplicatesAndRendersFifo();
    void capsCancelsAndReadmitsLongDocumentRequests();
    void boundsCacheAndEvictsTheLeastRecentlyUsedThumbnail();
    void suppressesFailedRenderRetries();
    void rejectsOversizedRendererOutput();
    void resetsSafelyWhenTheDocumentChanges();
    void discardsAResultWhenTheDocumentResetsDuringRender();
    void destructionCancelsQueuedCallbacks();
};

namespace {
void createPdf(const QString& path, int pageCount) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    for (auto page = 0; page < pageCount; ++page) {
        painter.drawText(QPointF(72, 72), QStringLiteral("Thumbnail page %1").arg(page + 1));
        if (page + 1 < pageCount) {
            QVERIFY(writer.newPage());
        }
    }
    painter.end();
}

std::unique_ptr<QPdfDocument> loadPdf(const QString& path) {
    auto document = std::make_unique<QPdfDocument>();
    if (document->load(path) != QPdfDocument::Error::None) {
        return {};
    }
    return document;
}
}

void ThumbnailModelTest::boundsRenderSizes_data() {
    QTest::addColumn<QSizeF>("points");
    QTest::addColumn<QSize>("expected");
    QTest::addColumn<bool>("valid");

    QTest::newRow("standard") << QSizeF(612, 792) << QSize(128, 166) << true;
    QTest::newRow("minimum-height") << QSizeF(1'000'000, 1) << QSize(128, 1) << true;
    QTest::newRow("maximum-height") << QSizeF(1, 1'000'000) << QSize(128, 512) << true;
    QTest::newRow("zero-width") << QSizeF(0, 1) << QSize{} << false;
    QTest::newRow("negative-height") << QSizeF(1, -1) << QSize{} << false;
    QTest::newRow("infinite-width")
        << QSizeF(std::numeric_limits<qreal>::infinity(), 1) << QSize{} << false;
    QTest::newRow("nan-height")
        << QSizeF(1, std::numeric_limits<qreal>::quiet_NaN()) << QSize{} << false;
}

void ThumbnailModelTest::boundsRenderSizes() {
    QFETCH(QSizeF, points);
    QFETCH(QSize, expected);
    QFETCH(bool, valid);

    const auto result = ThumbnailModel::boundedRenderSize(points);
    QCOMPARE(result.has_value(), valid);
    if (valid) {
        QCOMPARE(*result, expected);
    }
}

void ThumbnailModelTest::defersDeduplicatesAndRendersFifo() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("four-pages.pdf"));
    createPdf(path, 4);
    auto document = loadPdf(path);
    QVERIFY(document != nullptr);
    ThumbnailModel model(document.get());
    QSignalSpy finished(&model, &ThumbnailModel::thumbnailFinished);
    QList<int> renderOrder;
    bool wrongThread = false;
    model.renderer_ = [&](QPdfDocument&, int page, const QSize& size) {
        renderOrder.append(page);
        wrongThread = wrongThread || QThread::currentThread() != model.thread();
        return QImage(size, QImage::Format_ARGB32_Premultiplied);
    };

    QVERIFY(!model.data(model.index(2), Qt::DecorationRole).isValid());
    QVERIFY(!model.data(model.index(0), Qt::DecorationRole).isValid());
    QVERIFY(!model.data(model.index(2), Qt::DecorationRole).isValid());
    QVERIFY(!model.data(model.index(1), Qt::DecorationRole).isValid());
    QCOMPARE(model.pendingRequestCount(), 3);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(model.data(model.index(2), Qt::AccessibleTextRole).toString(),
             QStringLiteral("Page 3"));

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 2'000);
    QCOMPARE(renderOrder, QList<int>({2, 0, 1}));
    QVERIFY(!wrongThread);
    QCOMPARE(model.pendingRequestCount(), 0);
    QVERIFY(model.cacheCostBytes() > 0);
    QVERIFY(model.cacheCostBytes() <= ThumbnailModel::maximumCacheBytes);
    QVERIFY(model.data(model.index(2), Qt::DecorationRole).canConvert<QPixmap>());
    QCOMPARE(finished.count(), 3);
}

void ThumbnailModelTest::capsCancelsAndReadmitsLongDocumentRequests() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("long.pdf"));
    createPdf(path, ThumbnailModel::maximumPendingRequests + 16);
    const auto acceptanceFixture = qEnvironmentVariable("ZENPDF_THUMBNAIL_ACCEPTANCE_FIXTURE");
    if (!acceptanceFixture.isEmpty()) {
        QVERIFY2(!QFile::exists(acceptanceFixture), "acceptance fixture path must not exist");
        QVERIFY(QFile::copy(path, acceptanceFixture));
    }
    auto document = loadPdf(path);
    QVERIFY(document != nullptr);
    ThumbnailModel model(document.get());
    QSignalSpy finished(&model, &ThumbnailModel::thumbnailFinished);
    int renderCalls = 0;
    model.renderer_ = [&](QPdfDocument&, int, const QSize& size) {
        ++renderCalls;
        return QImage(size, QImage::Format_ARGB32_Premultiplied);
    };

    for (auto page = 0; page < model.rowCount(); ++page) {
        (void)model.data(model.index(page), Qt::DecorationRole);
    }
    QCOMPARE(model.pendingRequestCount(), ThumbnailModel::maximumPendingRequests);
    QCOMPARE(model.cancelPendingRequests(), ThumbnailModel::maximumPendingRequests);
    QCOMPARE(model.pendingRequestCount(), 0);
    QTest::qWait(20);
    QCOMPARE(renderCalls, 0);
    QCOMPARE(finished.count(), 0);

    const auto lastPage = model.rowCount() - 1;
    (void)model.data(model.index(lastPage), Qt::DecorationRole);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.at(0).at(0).toInt(), lastPage);
    QCOMPARE(renderCalls, 1);
}

void ThumbnailModelTest::boundsCacheAndEvictsTheLeastRecentlyUsedThumbnail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr auto pageCount = ThumbnailModel::maximumCacheBytes
        / (ThumbnailModel::thumbnailWidth * ThumbnailModel::maximumThumbnailHeight * 4) + 1;
    static_assert(pageCount == 129);
    const auto path = directory.filePath(QStringLiteral("cache-boundary.pdf"));
    createPdf(path, pageCount);
    auto document = loadPdf(path);
    QVERIFY(document != nullptr);
    ThumbnailModel model(document.get());
    model.renderer_ = [](QPdfDocument&, int, const QSize&) {
        return QImage(ThumbnailModel::thumbnailWidth,
                      ThumbnailModel::maximumThumbnailHeight,
                      QImage::Format_ARGB32_Premultiplied);
    };

    for (auto page = 0; page < pageCount - 1; ++page) {
        (void)model.data(model.index(page), Qt::DecorationRole);
        model.renderNext();
    }
    QCOMPARE(model.cacheCostBytes(), ThumbnailModel::maximumCacheBytes);
    QVERIFY(model.cache_.contains(0));
    QVERIFY(model.cache_.contains(pageCount - 2));

    (void)model.data(model.index(pageCount - 1), Qt::DecorationRole);
    model.renderNext();
    QCOMPARE(model.cacheCostBytes(), ThumbnailModel::maximumCacheBytes);
    QVERIFY(!model.cache_.contains(0));
    QVERIFY(model.cache_.contains(pageCount - 1));
}

void ThumbnailModelTest::suppressesFailedRenderRetries() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("failure.pdf"));
    createPdf(path, 1);
    auto document = loadPdf(path);
    QVERIFY(document != nullptr);
    ThumbnailModel model(document.get());
    QSignalSpy finished(&model, &ThumbnailModel::thumbnailFinished);
    int renderCalls = 0;
    model.renderer_ = [&](QPdfDocument&, int, const QSize&) {
        ++renderCalls;
        return QImage{};
    };

    (void)model.data(model.index(0), Qt::DecorationRole);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QCOMPARE(model.failedPageCount(), 1);
    for (auto attempt = 0; attempt < 10; ++attempt) {
        (void)model.data(model.index(0), Qt::DecorationRole);
    }
    QTest::qWait(20);
    QCOMPARE(renderCalls, 1);
    QCOMPARE(finished.count(), 1);
}

void ThumbnailModelTest::rejectsOversizedRendererOutput() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("oversized-render.pdf"));
    createPdf(path, 1);
    auto document = loadPdf(path);
    QVERIFY(document != nullptr);
    ThumbnailModel model(document.get());
    QSignalSpy finished(&model, &ThumbnailModel::thumbnailFinished);
    model.renderer_ = [](QPdfDocument&, int, const QSize&) {
        return QImage(ThumbnailModel::thumbnailWidth + 1, 1,
                      QImage::Format_ARGB32_Premultiplied);
    };

    (void)model.data(model.index(0), Qt::DecorationRole);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2'000);
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QCOMPARE(model.cacheCostBytes(), 0);
    QCOMPARE(model.failedPageCount(), 1);
}

void ThumbnailModelTest::resetsSafelyWhenTheDocumentChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto firstPath = directory.filePath(QStringLiteral("first.pdf"));
    const auto secondPath = directory.filePath(QStringLiteral("second.pdf"));
    createPdf(firstPath, 3);
    createPdf(secondPath, 2);
    auto first = loadPdf(firstPath);
    auto second = loadPdf(secondPath);
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    ThumbnailModel model(first.get());
    int renderCalls = 0;
    model.renderer_ = [&](QPdfDocument&, int, const QSize& size) {
        ++renderCalls;
        return QImage(size, QImage::Format_ARGB32_Premultiplied);
    };

    (void)model.data(model.index(0), Qt::DecorationRole);
    QCOMPARE(model.pendingRequestCount(), 1);
    model.setDocument(second.get());
    QCOMPARE(model.pendingRequestCount(), 0);
    QCOMPARE(model.rowCount(), 2);
    QTest::qWait(20);
    QCOMPARE(renderCalls, 0);

    (void)model.data(model.index(1), Qt::DecorationRole);
    QCOMPARE(model.pendingRequestCount(), 1);
    second.reset();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.pendingRequestCount(), 0);
    QTest::qWait(20);
    QCOMPARE(renderCalls, 0);
}

void ThumbnailModelTest::discardsAResultWhenTheDocumentResetsDuringRender() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto firstPath = directory.filePath(QStringLiteral("render-first.pdf"));
    const auto secondPath = directory.filePath(QStringLiteral("render-second.pdf"));
    createPdf(firstPath, 1);
    createPdf(secondPath, 2);
    auto first = loadPdf(firstPath);
    QVERIFY(first != nullptr);
    ThumbnailModel model(first.get());
    QSignalSpy finished(&model, &ThumbnailModel::thumbnailFinished);
    auto reloadError = QPdfDocument::Error::Unknown;
    model.renderer_ = [&](QPdfDocument&, int, const QSize& size) {
        first->close();
        reloadError = first->load(secondPath);
        return QImage(size, QImage::Format_ARGB32_Premultiplied);
    };

    (void)model.data(model.index(0), Qt::DecorationRole);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 2'000);
    QCOMPARE(reloadError, QPdfDocument::Error::None);
    QTest::qWait(20);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(model.cacheCostBytes(), 0);
    QCOMPARE(model.failedPageCount(), 0);
    QCOMPARE(model.pendingRequestCount(), 0);
}

void ThumbnailModelTest::destructionCancelsQueuedCallbacks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("destroy.pdf"));
    createPdf(path, 2);
    auto document = loadPdf(path);
    QVERIFY(document != nullptr);
    int renderCalls = 0;
    {
        auto model = std::make_unique<ThumbnailModel>(document.get());
        model->renderer_ = [&](QPdfDocument&, int, const QSize& size) {
            ++renderCalls;
            return QImage(size, QImage::Format_ARGB32_Premultiplied);
        };
        (void)model->data(model->index(0), Qt::DecorationRole);
        QCOMPARE(model->pendingRequestCount(), 1);
    }
    QTest::qWait(20);
    QCOMPARE(renderCalls, 0);
}

QTEST_MAIN(ThumbnailModelTest)
#include "ThumbnailModelTest.moc"
