#include "DocumentWidget.h"

#include <QAccessible>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPdfWriter>
#include <QListView>
#include <QProcess>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class DocumentWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void clearsPasswordAfterUnlock();
    void exposesAccessibleThumbnailNames();
    void findsGeneratedTextAndActivatesResult();
    void searchesUnicodeAndHandlesNoText();
    void supersedesLongSearchAndShutsDownCleanly();
    void rejectsMalformedPdfBeforeSearchStarts();
    void spaceActivatesOnlyTheFocusedThumbnailList();
};

namespace {
void createPdf(const QString& path, int pageCount = 1) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    QPainter painter(&writer);
    for (auto page = 0; page < pageCount; ++page) {
        painter.drawText(QPointF(72, 72), QStringLiteral("Private page %1").arg(page + 1));
        if (page + 1 < pageCount) {
            QVERIFY(writer.newPage());
        }
    }
    painter.end();
}

QListView* thumbnailList(DocumentWidget& widget) {
    for (auto* candidate : widget.findChildren<QListView*>()) {
        if (candidate->accessibleName() == QStringLiteral("Page thumbnails")) {
            return candidate;
        }
    }
    return nullptr;
}

QListView* searchResultList(DocumentWidget& widget) {
    for (auto* candidate : widget.findChildren<QListView*>()) {
        if (candidate->accessibleName() == QStringLiteral("Document search results")) {
            return candidate;
        }
    }
    return nullptr;
}

QLineEdit* searchLineEdit(DocumentWidget& widget) {
    for (auto* candidate : widget.findChildren<QLineEdit*>()) {
        if (candidate->placeholderText() == QStringLiteral("Search document")) {
            return candidate;
        }
    }
    return nullptr;
}

QLabel* searchStatusLabel(DocumentWidget& widget) {
    for (auto* candidate : widget.findChildren<QLabel*>()) {
        if (candidate->objectName() == QStringLiteral("searchStatus")) {
            return candidate;
        }
    }
    return nullptr;
}
}

void DocumentWidgetTest::clearsPasswordAfterUnlock() {
    if (QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty()) {
        QSKIP("qpdf is not installed");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.pdf"));
    const auto encrypted = directory.filePath(QStringLiteral("encrypted.pdf"));
    createPdf(source);

    QProcess qpdf;
    qpdf.start(
        QStringLiteral("qpdf"),
        {QStringLiteral("--encrypt"), QStringLiteral("reader"), QStringLiteral("owner"),
         QStringLiteral("256"), QStringLiteral("--"), source, encrypted});
    QVERIFY(qpdf.waitForFinished(5'000));
    QCOMPARE(qpdf.exitCode(), 0);

    DocumentWidget widget(encrypted);
    QVERIFY(widget.needsPassword());
    QVERIFY(widget.unlock(QStringLiteral("reader")));
    QCOMPARE(widget.document_->password(), QString{});
}

void DocumentWidgetTest::exposesAccessibleThumbnailNames() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("accessible.pdf"));
    createPdf(source);

    DocumentWidget widget(source);
    QVERIFY(widget.isReady());
    auto* thumbnails = thumbnailList(widget);
    QVERIFY(thumbnails != nullptr);
    QCOMPARE(thumbnails->model()->rowCount(), 1);
    QCOMPARE(thumbnails->model()->index(0, 0).data(Qt::AccessibleTextRole).toString(),
             QStringLiteral("Page 1"));
}

void DocumentWidgetTest::findsGeneratedTextAndActivatesResult() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto requestedFixture = qEnvironmentVariable("ZENPDF_SEARCH_ACCEPTANCE_FIXTURE");
    const auto source = requestedFixture.isEmpty()
        ? directory.filePath(QStringLiteral("searchable.pdf"))
        : requestedFixture;
    if (!requestedFixture.isEmpty()) {
        QVERIFY2(!QFileInfo::exists(source), "acceptance fixture destination must not exist");
    }

    QPdfWriter writer(source);
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.drawText(QPointF(72, 72), QStringLiteral("ZenPDF alpha cover"));
    QVERIFY(writer.newPage());
    painter.drawText(QPointF(72, 72), QStringLiteral("Visible search success token: quokka"));
    QVERIFY(writer.newPage());
    painter.drawText(QPointF(72, 72), QStringLiteral("ZenPDF alpha finish"));
    painter.end();

    DocumentWidget widget(source);
    QVERIFY(widget.isReady());
    QTRY_COMPARE(widget.document_->status(), QPdfDocument::Status::Ready);
    QVERIFY(widget.document_->getAllText(1).text().contains(QStringLiteral("quokka")));
    auto* searchInput = searchLineEdit(widget);
    auto* searchResults = searchResultList(widget);
    auto* searchStatus = searchStatusLabel(widget);
    auto* sideTabs = widget.findChild<QTabWidget*>();
    QVERIFY(searchInput != nullptr);
    QVERIFY(searchResults != nullptr);
    QVERIFY(searchStatus != nullptr);
    QVERIFY(sideTabs != nullptr);
    QCOMPARE(searchInput->accessibleName(), QStringLiteral("Search document"));
    QCOMPARE(searchStatus->text(), QStringLiteral("Enter text to search."));
    QCOMPARE(QAccessible::queryAccessibleInterface(searchStatus)->text(QAccessible::Name),
             searchStatus->text());

    searchInput->setText(QStringLiteral("quokka"));
    QTRY_COMPARE(searchResults->model()->rowCount(), 1);
    QCOMPARE(searchStatus->text(), QStringLiteral("1 result(s) found so far."));
    QCOMPARE(QAccessible::queryAccessibleInterface(searchStatus)->text(QAccessible::Name),
             searchStatus->text());
    const auto result = searchResults->model()->index(0, 0);
    QVERIFY(result.data(Qt::DisplayRole).toString().contains(QStringLiteral("Page 2")));

    sideTabs->setCurrentWidget(searchInput->parentWidget());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    searchResults->setCurrentIndex(result);
    searchResults->setFocus();
    QTRY_VERIFY(searchResults->hasFocus());
    QTest::keyClick(searchResults, Qt::Key_Return);
    QTRY_COMPARE(widget.view_->pageNavigator()->currentPage(), 1);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QCOMPARE(widget.view_->currentSearchResultIndex(), 0);
#endif

    searchInput->setText(QStringLiteral("no matching text"));
    QVERIFY(!searchResults->currentIndex().isValid());
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QCOMPARE(widget.view_->currentSearchResultIndex(), -1);
#endif
    QTRY_COMPARE(searchResults->model()->rowCount(), 0);
    QCOMPARE(searchStatus->text(), QStringLiteral("Searching locally; no results yet."));
}

void DocumentWidgetTest::searchesUnicodeAndHandlesNoText() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto unicodeSource = directory.filePath(QStringLiteral("unicode.pdf"));
    const auto blankSource = directory.filePath(QStringLiteral("blank.pdf"));

    {
        QPdfWriter writer(unicodeSource);
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.drawText(QPointF(72, 72), QString::fromUtf8("Local caf\u00e9 r\u00e9sum\u00e9"));
        painter.end();
    }
    {
        QPdfWriter writer(blankSource);
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.end();
    }

    DocumentWidget unicodeWidget(unicodeSource);
    QVERIFY(unicodeWidget.isReady());
    auto* unicodeInput = searchLineEdit(unicodeWidget);
    auto* unicodeResults = searchResultList(unicodeWidget);
    QVERIFY(unicodeInput != nullptr);
    QVERIFY(unicodeResults != nullptr);
    unicodeInput->setText(QString::fromUtf8("caf\u00e9"));
    QTRY_COMPARE(unicodeResults->model()->rowCount(), 1);
    QVERIFY(unicodeResults->model()->index(0, 0).data(Qt::AccessibleTextRole).toString()
                .contains(QString::fromUtf8("caf\u00e9"), Qt::CaseInsensitive));

    DocumentWidget blankWidget(blankSource);
    QVERIFY(blankWidget.isReady());
    QCOMPARE(blankWidget.document_->getAllText(0).text().trimmed(), QString{});
    auto* blankInput = searchLineEdit(blankWidget);
    auto* blankResults = searchResultList(blankWidget);
    auto* blankStatus = searchStatusLabel(blankWidget);
    QVERIFY(blankInput != nullptr);
    QVERIFY(blankResults != nullptr);
    QVERIFY(blankStatus != nullptr);
    blankInput->setText(QStringLiteral("anything"));
    QTest::qWait(150);
    QCOMPARE(blankResults->model()->rowCount(), 0);
    QCOMPARE(blankStatus->text(), QStringLiteral("Searching locally; no results yet."));
}

void DocumentWidgetTest::supersedesLongSearchAndShutsDownCleanly() {
    constexpr int boundedPageCount = 80;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("bounded-long.pdf"));

    QPdfWriter writer(source);
    writer.setResolution(72);
    QPainter painter(&writer);
    for (int page = 0; page < boundedPageCount; ++page) {
        const auto text = page + 1 == boundedPageCount
            ? QStringLiteral("replacement-only")
            : QStringLiteral("superseded-result");
        painter.drawText(QPointF(72, 72), text);
        if (page + 1 < boundedPageCount) {
            QVERIFY(writer.newPage());
        }
    }
    painter.end();

    auto widget = std::make_unique<DocumentWidget>(source);
    QVERIFY(widget->isReady());
    QCOMPARE(widget->pageCount(), boundedPageCount);
    auto* searchInput = searchLineEdit(*widget);
    auto* searchResults = searchResultList(*widget);
    QVERIFY(searchInput != nullptr);
    QVERIFY(searchResults != nullptr);

    searchInput->setText(QStringLiteral("superseded-result"));
    QTRY_VERIFY(searchResults->model()->rowCount() > 0);
    searchResults->setCurrentIndex(searchResults->model()->index(0, 0));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    widget->view_->setCurrentSearchResultIndex(0);
#endif

    searchInput->setText(QStringLiteral("replacement-only"));
    QVERIFY(!searchResults->currentIndex().isValid());
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QCOMPARE(widget->view_->currentSearchResultIndex(), -1);
#endif
    QTRY_COMPARE_WITH_TIMEOUT(searchResults->model()->rowCount(), 1, 15'000);
    QVERIFY(searchResults->model()->index(0, 0).data(Qt::DisplayRole).toString()
                .contains(QStringLiteral("Page 80")));

    searchInput->setText(QStringLiteral("superseded-result"));
    widget.reset();
    QCoreApplication::processEvents();
}

void DocumentWidgetTest::rejectsMalformedPdfBeforeSearchStarts() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("malformed.pdf"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("%PDF-1.7\nmalformed"), qint64{18});
    file.close();

    DocumentWidget widget(source);
    QVERIFY(!widget.isReady());
    QVERIFY(!widget.errorMessage().isEmpty());
    QCOMPARE(searchLineEdit(widget), nullptr);
    QCOMPARE(widget.searchModel_->rowCount({}), 0);
}

void DocumentWidgetTest::spaceActivatesOnlyTheFocusedThumbnailList() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("keyboard.pdf"));
    createPdf(source, 3);

    DocumentWidget widget(source);
    QVERIFY(widget.isReady());
    auto* thumbnails = thumbnailList(widget);
    QVERIFY(thumbnails != nullptr);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    thumbnails->setCurrentIndex(thumbnails->model()->index(1, 0));
    thumbnails->setFocus();
    QTRY_VERIFY(thumbnails->hasFocus());
    QTest::keyClick(thumbnails, Qt::Key_Space);
    QTRY_COMPARE(widget.view_->pageNavigator()->currentPage(), 1);

    thumbnails->setCurrentIndex(thumbnails->model()->index(2, 0));
    widget.pageSelector_->setFocus();
    QTRY_VERIFY(!thumbnails->hasFocus());
    QTest::keyClick(thumbnails, Qt::Key_Space);
    QTest::qWait(20);
    QCOMPARE(widget.view_->pageNavigator()->currentPage(), 1);
}

QTEST_MAIN(DocumentWidgetTest)
#include "DocumentWidgetTest.moc"
