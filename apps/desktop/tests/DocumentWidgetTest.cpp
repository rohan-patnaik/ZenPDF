#include "DocumentWidget.h"

#include <QFileInfo>
#include <QLineEdit>
#include <QPainter>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPdfWriter>
#include <QListView>
#include <QProcess>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

class DocumentWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void clearsPasswordAfterUnlock();
    void exposesAccessibleThumbnailNames();
    void findsGeneratedTextAndActivatesResult();
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
    auto* sideTabs = widget.findChild<QTabWidget*>();
    QVERIFY(searchInput != nullptr);
    QVERIFY(searchResults != nullptr);
    QVERIFY(sideTabs != nullptr);

    searchInput->setText(QStringLiteral("quokka"));
    QTRY_COMPARE(searchResults->model()->rowCount(), 1);
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
