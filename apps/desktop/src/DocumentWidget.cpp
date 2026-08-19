#include "DocumentWidget.h"

#include "PrintPolicy.h"

#include <QAction>
#include <QAbstractListModel>
#include <QApplication>
#include <QCache>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QIdentityProxyModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kThumbnailWidth = 128;
constexpr int kMaximumThumbnailHeight = 512;
constexpr int kThumbnailCacheBytes = 32 * 1024 * 1024;

class ThumbnailModel final : public QAbstractListModel {
public:
    explicit ThumbnailModel(QPdfDocument* document, QObject* parent = nullptr)
        : QAbstractListModel(parent), document_(document), cache_(kThumbnailCacheBytes) {}

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : document_->pageCount();
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= document_->pageCount()) {
            return {};
        }
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
            return QStringLiteral("Page %1").arg(document_->pageLabel(index.row()));
        }
        if (role != Qt::DecorationRole) {
            return {};
        }
        if (const auto* cached = cache_.object(index.row())) {
            return *cached;
        }
        const auto pointSize = document_->pagePointSize(index.row());
        if (pointSize.isEmpty()) {
            return {};
        }
        const int height = std::clamp(
            qRound(kThumbnailWidth * pointSize.height() / pointSize.width()), 1, kMaximumThumbnailHeight);
        auto pixmap = QPixmap::fromImage(document_->render(index.row(), {kThumbnailWidth, height}));
        const int cost = std::max(1, pixmap.width() * pixmap.height() * 4);
        cache_.insert(index.row(), new QPixmap(pixmap), cost);
        return pixmap;
    }

private:
    QPdfDocument* document_;
    mutable QCache<int, QPixmap> cache_;
};

class BookmarkDisplayModel final : public QIdentityProxyModel {
public:
    using QIdentityProxyModel::QIdentityProxyModel;

    QVariant data(const QModelIndex& index, int role) const override {
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
            return QIdentityProxyModel::data(index, static_cast<int>(QPdfBookmarkModel::Role::Title));
        }
        return QIdentityProxyModel::data(index, role);
    }
};

class SearchDisplayModel final : public QIdentityProxyModel {
public:
    using QIdentityProxyModel::QIdentityProxyModel;

    QVariant data(const QModelIndex& index, int role) const override {
        if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
            const auto page = QIdentityProxyModel::data(index, static_cast<int>(QPdfSearchModel::Role::Page)).toInt() + 1;
            const auto before = QIdentityProxyModel::data(index, static_cast<int>(QPdfSearchModel::Role::ContextBefore)).toString();
            const auto after = QIdentityProxyModel::data(index, static_cast<int>(QPdfSearchModel::Role::ContextAfter)).toString();
            return QStringLiteral("Page %1: …%2%3…").arg(page).arg(before.trimmed(), after.trimmed());
        }
        return QIdentityProxyModel::data(index, role);
    }
};

QString pdfErrorMessage(QPdfDocument::Error error) {
    switch (error) {
    case QPdfDocument::Error::None:
        return {};
    case QPdfDocument::Error::FileNotFound:
        return QObject::tr("The file no longer exists.");
    case QPdfDocument::Error::InvalidFileFormat:
        return QObject::tr("The file is not a valid PDF.");
    case QPdfDocument::Error::IncorrectPassword:
        return QObject::tr("This password-protected PDF cannot be opened without its password.");
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return QObject::tr("The PDF uses an unsupported security scheme.");
    case QPdfDocument::Error::DataNotYetAvailable:
        return QObject::tr("The PDF data is not yet available.");
    case QPdfDocument::Error::Unknown:
        return QObject::tr("The PDF engine could not open this document.");
    }
    return QObject::tr("The PDF engine reported an unknown error.");
}
}

DocumentWidget::DocumentWidget(QString filePath, QWidget* parent)
    : QWidget(parent),
      filePath_(QFileInfo(filePath).absoluteFilePath()),
      document_(new QPdfDocument(this)),
      view_(new QPdfView(this)),
      searchModel_(new QPdfSearchModel(this)),
      pageSelector_(new QSpinBox(this)) {
    updateLoadState(document_->load(filePath_));
}

QString DocumentWidget::filePath() const {
    return filePath_;
}

QString DocumentWidget::displayName() const {
    return QFileInfo(filePath_).fileName();
}

int DocumentWidget::pageCount() const {
    return document_->pageCount();
}

bool DocumentWidget::isReady() const {
    return errorMessage_.isEmpty() && document_->pageCount() > 0;
}

bool DocumentWidget::needsPassword() const {
    return document_->error() == QPdfDocument::Error::IncorrectPassword;
}

QString DocumentWidget::errorMessage() const {
    return errorMessage_;
}

bool DocumentWidget::unlock(const QString& password) {
    if (password.isEmpty()) {
        return false;
    }
    document_->setPassword(password);
    const auto error = document_->load(filePath_);
    document_->setPassword({});
    updateLoadState(error);
    return isReady();
}

void DocumentWidget::buildInterface() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* toolbar = new QToolBar(this);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* previous = toolbar->addAction(tr("Previous"));
    previous->setShortcut(QKeySequence::MoveToPreviousPage);
    auto* next = toolbar->addAction(tr("Next"));
    next->setShortcut(QKeySequence::MoveToNextPage);
    pageSelector_->setRange(1, document_->pageCount());
    pageSelector_->setAccessibleName(tr("Current page"));
    toolbar->addWidget(pageSelector_);
    toolbar->addWidget(new QLabel(tr(" of %1").arg(document_->pageCount()), toolbar));
    toolbar->addSeparator();
    auto* zoomOut = toolbar->addAction(tr("−"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    auto* zoomIn = toolbar->addAction(tr("+"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    auto* fitWidth = toolbar->addAction(tr("Fit width"));
    auto* fitPage = toolbar->addAction(tr("Fit page"));
    auto* pageMode = toolbar->addAction(tr("Single page"));
    pageMode->setCheckable(true);
    toolbar->addSeparator();
    auto* metadata = toolbar->addAction(tr("Details"));
    auto* print = toolbar->addAction(tr("Print"));
    print->setShortcut(QKeySequence::Print);
    rootLayout->addWidget(toolbar);

    auto* sideTabs = new QTabWidget(this);
    sideTabs->setMinimumWidth(220);
    sideTabs->setMaximumWidth(360);
    auto* thumbnails = new QListView(sideTabs);
    thumbnails->setModel(new ThumbnailModel(document_, thumbnails));
    thumbnails->setIconSize({kThumbnailWidth, 180});
    thumbnails->setViewMode(QListView::ListMode);
    thumbnails->setAccessibleName(tr("Page thumbnails"));
    sideTabs->addTab(thumbnails, tr("Pages"));

    auto* bookmarkModel = new QPdfBookmarkModel(sideTabs);
    bookmarkModel->setDocument(document_);
    auto* bookmarkDisplay = new BookmarkDisplayModel(sideTabs);
    bookmarkDisplay->setSourceModel(bookmarkModel);
    auto* bookmarks = new QTreeView(sideTabs);
    bookmarks->setHeaderHidden(true);
    bookmarks->setModel(bookmarkDisplay);
    bookmarks->setAccessibleName(tr("Document bookmarks"));
    sideTabs->addTab(bookmarks, tr("Outline"));

    auto* searchPage = new QWidget(sideTabs);
    auto* searchLayout = new QVBoxLayout(searchPage);
    auto* searchInput = new QLineEdit(searchPage);
    searchInput->setPlaceholderText(tr("Search document"));
    searchInput->setClearButtonEnabled(true);
    auto* searchDisplay = new SearchDisplayModel(searchPage);
    searchModel_->setDocument(document_);
    searchDisplay->setSourceModel(searchModel_);
    auto* searchResults = new QListView(searchPage);
    searchResults->setModel(searchDisplay);
    searchResults->setWordWrap(true);
    searchResults->setAccessibleName(tr("Document search results"));
    searchLayout->addWidget(searchInput);
    searchLayout->addWidget(searchResults);
    sideTabs->addTab(searchPage, tr("Search"));

    view_->setDocument(document_);
    view_->setPageMode(QPdfView::PageMode::MultiPage);
    view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    view_->setAccessibleName(tr("PDF document view"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    view_->setSearchModel(searchModel_);
#endif

    auto* splitter = new QSplitter(this);
    splitter->addWidget(sideTabs);
    splitter->addWidget(view_);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter, 1);

    connect(previous, &QAction::triggered, this, [this] { jumpToPage(pageSelector_->value() - 1); });
    connect(next, &QAction::triggered, this, [this] { jumpToPage(pageSelector_->value() + 1); });
    connect(pageSelector_, &QSpinBox::valueChanged, this, &DocumentWidget::jumpToPage);
    connect(view_->pageNavigator(), &QPdfPageNavigator::currentPageChanged, pageSelector_, [this](int page) {
        const QSignalBlocker blocker(pageSelector_);
        pageSelector_->setValue(page + 1);
    });
    connect(zoomOut, &QAction::triggered, this, [this] { setCustomZoom(view_->zoomFactor() / 1.2); });
    connect(zoomIn, &QAction::triggered, this, [this] { setCustomZoom(view_->zoomFactor() * 1.2); });
    connect(fitWidth, &QAction::triggered, this, [this] { view_->setZoomMode(QPdfView::ZoomMode::FitToWidth); });
    connect(fitPage, &QAction::triggered, this, [this] { view_->setZoomMode(QPdfView::ZoomMode::FitInView); });
    connect(pageMode, &QAction::toggled, this, [this, pageMode](bool singlePage) {
        view_->setPageMode(singlePage ? QPdfView::PageMode::SinglePage : QPdfView::PageMode::MultiPage);
        pageMode->setText(singlePage ? tr("Continuous") : tr("Single page"));
    });
    connect(metadata, &QAction::triggered, this, &DocumentWidget::showMetadata);
    connect(print, &QAction::triggered, this, &DocumentWidget::printDocument);
    connect(thumbnails, &QListView::activated, this, [this](const QModelIndex& index) { jumpToPage(index.row() + 1); });
    connect(bookmarks, &QTreeView::activated, this, [this, bookmarkDisplay](const QModelIndex& index) {
        const auto source = bookmarkDisplay->mapToSource(index);
        const int page = source.data(static_cast<int>(QPdfBookmarkModel::Role::Page)).toInt();
        const auto location = source.data(static_cast<int>(QPdfBookmarkModel::Role::Location)).toPointF();
        const auto zoom = source.data(static_cast<int>(QPdfBookmarkModel::Role::Zoom)).toReal();
        view_->pageNavigator()->jump(page, location, zoom);
    });
    connect(searchInput, &QLineEdit::textChanged, searchModel_, &QPdfSearchModel::setSearchString);
    connect(searchResults, &QListView::activated, this, [this, searchDisplay](const QModelIndex& index) {
        const auto source = searchDisplay->mapToSource(index);
        view_->pageNavigator()->jump(searchModel_->resultAtIndex(source.row()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        view_->setCurrentSearchResultIndex(source.row());
#endif
    });
}

void DocumentWidget::printDocument() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(displayName());
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print local PDF"));
    dialog.setMinMax(1, document_->pageCount());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int firstPage = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    const int lastPage = printer.toPage() > 0 ? printer.toPage() - 1 : document_->pageCount() - 1;
    if (lastPage - firstPage + 1 > PrintPolicy::maximumPagesPerJob) {
        QMessageBox::warning(
            this,
            tr("Print range is too large"),
            tr("Choose at most %1 pages per print job.").arg(PrintPolicy::maximumPagesPerJob));
        return;
    }
    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(this, tr("Could not print"), tr("The selected print device could not be started."));
        return;
    }

    QProgressDialog progress(
        tr("Rendering locally; cancellation applies between pages…"),
        tr("Cancel"),
        firstPage,
        lastPage + 1,
        this);
    progress.setWindowTitle(tr("Printing"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const QRectF paintRect = printer.pageLayout().paintRectPixels(printer.resolution());
    for (int page = firstPage; page <= lastPage; ++page) {
        progress.setValue(page);
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            printer.abort();
            break;
        }
        if (page != firstPage && !printer.newPage()) {
            QMessageBox::warning(this, tr("Print stopped"), tr("The print device could not start the next page."));
            break;
        }
        const QSizeF points = document_->pagePointSize(page);
        const auto renderSize = PrintPolicy::boundedRenderSize(points, paintRect.size());
        if (!renderSize.has_value()) {
            QMessageBox::warning(this, tr("Print stopped"), tr("A page exceeds the safe print dimensions."));
            break;
        }
        QImage image;
        const auto decision = PrintPolicy::renderIfNotCancelled(progress.wasCanceled(), [&] {
            image = document_->render(page, *renderSize);
            return !image.isNull();
        });
        if (decision == PrintRenderDecision::Cancelled) {
            printer.abort();
            break;
        }
        if (decision == PrintRenderDecision::Failed) {
            QMessageBox::warning(this, tr("Print stopped"), tr("A page could not be rendered safely."));
            break;
        }
        QSizeF destinationSize = QSizeF(image.size()).scaled(paintRect.size(), Qt::KeepAspectRatio);
        const QRectF destination(
            paintRect.center().x() - destinationSize.width() / 2,
            paintRect.center().y() - destinationSize.height() / 2,
            destinationSize.width(),
            destinationSize.height());
        painter.drawImage(destination, image);
    }
    progress.setValue(lastPage + 1);
    painter.end();
}

void DocumentWidget::showMetadata() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Document details"));
    auto* layout = new QFormLayout(&dialog);
    const auto addField = [this, layout](const QString& label, QPdfDocument::MetaDataField field) {
        const auto value = document_->metaData(field).toString().trimmed();
        auto* text = new QLabel(value.isEmpty() ? tr("Not provided") : value, this);
        text->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
        text->setWordWrap(true);
        layout->addRow(label, text);
    };
    addField(tr("Title"), QPdfDocument::MetaDataField::Title);
    addField(tr("Author"), QPdfDocument::MetaDataField::Author);
    addField(tr("Subject"), QPdfDocument::MetaDataField::Subject);
    addField(tr("Keywords"), QPdfDocument::MetaDataField::Keywords);
    addField(tr("Creator"), QPdfDocument::MetaDataField::Creator);
    addField(tr("Producer"), QPdfDocument::MetaDataField::Producer);
    layout->addRow(tr("Pages"), new QLabel(QString::number(document_->pageCount()), &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.exec();
}

void DocumentWidget::setCustomZoom(qreal factor) {
    view_->setZoomMode(QPdfView::ZoomMode::Custom);
    view_->setZoomFactor(std::clamp(factor, 0.25, 5.0));
}

void DocumentWidget::jumpToPage(int oneBasedPage) {
    const int page = std::clamp(oneBasedPage, 1, document_->pageCount()) - 1;
    view_->pageNavigator()->jump(page, QPointF{}, 0);
}

void DocumentWidget::updateLoadState(QPdfDocument::Error error) {
    errorMessage_ = pdfErrorMessage(error);
    if (error == QPdfDocument::Error::None && document_->pageCount() < 1) {
        errorMessage_ = tr("The document contains no readable pages.");
    } else if (document_->pageCount() > 100'000) {
        errorMessage_ = tr("The document exceeds the 100,000-page safety limit.");
    }
    if (errorMessage_.isEmpty() && !interfaceBuilt_) {
        interfaceBuilt_ = true;
        buildInterface();
    }
}
