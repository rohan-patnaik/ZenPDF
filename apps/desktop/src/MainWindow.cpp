#include "MainWindow.h"

#include "DocumentWidget.h"
#include "LocalState.h"
#include "QpdfOperations.h"

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <atomic>
#include <functional>
#include <utility>

namespace {
constexpr qint64 kMaximumDocumentBytes = 2LL * 1024 * 1024 * 1024;

QpdfResult runOrganizerTask(
    QWidget* parent,
    const QString& title,
    std::function<QpdfResult(const std::atomic_bool*)> operation) {
    std::atomic_bool cancelled{false};
    QFutureWatcher<QpdfResult> watcher;
    QProgressDialog progress(QObject::tr("Working on a private local copy…"), QObject::tr("Cancel"), 0, 0, parent);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QObject::connect(&progress, &QProgressDialog::canceled, [&cancelled] { cancelled.store(true); });
    QObject::connect(&watcher, &QFutureWatcher<QpdfResult>::finished, &progress, &QDialog::accept);
    watcher.setFuture(QtConcurrent::run([operation = std::move(operation), &cancelled] {
        return operation(&cancelled);
    }));
    progress.exec();
    watcher.waitForFinished();
    return watcher.result();
}

bool hasLocalPdf(const QMimeData* mimeData) {
    for (const auto& url : mimeData->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
}

MainWindow::MainWindow(LocalState& localState, QWidget* parent)
    : QMainWindow(parent),
      localState_(localState),
      tabs_(new QTabWidget(this)),
      recentMenu_(nullptr) {
    setWindowTitle(tr("ZenPDF"));
    setMinimumSize(900, 620);
    setAcceptDrops(true);
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);
    tabs_->setTabsClosable(true);
    setCentralWidget(tabs_);

    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* widget = tabs_->widget(index);
        tabs_->removeTab(index);
        widget->deleteLater();
        ensureWelcomeTab();
    });

    buildMenus();
    ensureWelcomeTab();
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    statusBar()->showMessage(tr("Local-only workspace ready"));
}

void MainWindow::openFiles(const QStringList& paths) {
    for (const auto& path : paths) {
        openPdf(path);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (hasLocalPdf(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            paths.append(url.toLocalFile());
        }
    }
    openFiles(paths);
    event->acceptProposedAction();
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open PDFs…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    recentMenu_ = fileMenu->addMenu(tr("Open &recent"));
    rebuildRecentMenu();
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* organizeMenu = menuBar()->addMenu(tr("&Organize"));
    auto* mergeAction = organizeMenu->addAction(tr("&Merge PDFs into a new file…"));
    connect(mergeAction, &QAction::triggered, this, &MainWindow::mergeDocuments);
    auto* extractAction = organizeMenu->addAction(tr("&Extract pages to a new file…"));
    connect(extractAction, &QAction::triggered, this, &MainWindow::extractPages);
    auto* rotateAction = organizeMenu->addAction(tr("&Rotate pages into a new file…"));
    connect(rotateAction, &QAction::triggered, this, &MainWindow::rotatePages);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* presentation = viewMenu->addAction(tr("&Presentation mode"));
    presentation->setShortcut(QKeySequence(Qt::Key_F11));
    connect(presentation, &QAction::triggered, this, &MainWindow::togglePresentationMode);
}

void MainWindow::ensureWelcomeTab() {
    if (tabs_->count() != 0) {
        return;
    }
    auto* page = new QWidget(tabs_);
    auto* layout = new QVBoxLayout(page);
    auto* message = new QLabel(
        tr("ZenPDF Desktop\n\nOpen or drop local PDF files here.\nDocuments stay on this device."), page);
    message->setAlignment(Qt::AlignCenter);
    message->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    message->setAccessibleName(tr("ZenPDF local workspace introduction"));
    layout->addWidget(message);
    tabs_->addTab(page, tr("Welcome"));
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
}

void MainWindow::openFileDialog() {
    const auto paths = QFileDialog::getOpenFileNames(
        this, tr("Open local PDFs"), {}, tr("PDF documents (*.pdf);;All files (*)"));
    openFiles(paths);
}

void MainWindow::openPdf(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        QMessageBox::warning(this, tr("Could not open PDF"), tr("The selected file is missing or unreadable."));
        return;
    }
    if (info.size() > kMaximumDocumentBytes) {
        QMessageBox::warning(this, tr("PDF exceeds safety limit"), tr("ZenPDF does not open documents larger than 2 GiB."));
        return;
    }
    const auto cleanPath = info.absoluteFilePath();
    for (int index = 0; index < tabs_->count(); ++index) {
        if (auto* document = qobject_cast<DocumentWidget*>(tabs_->widget(index));
            document != nullptr && document->filePath() == cleanPath) {
            tabs_->setCurrentIndex(index);
            return;
        }
    }

    auto* document = new DocumentWidget(cleanPath, tabs_);
    if (!document->isReady()) {
        QMessageBox::warning(this, tr("Could not open PDF"), document->errorMessage());
        document->deleteLater();
        return;
    }
    if (tabs_->count() == 1 && qobject_cast<DocumentWidget*>(tabs_->widget(0)) == nullptr) {
        auto* welcome = tabs_->widget(0);
        tabs_->removeTab(0);
        welcome->deleteLater();
    }
    const int index = tabs_->addTab(document, document->displayName());
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(cleanPath));
    tabs_->setCurrentIndex(index);
    QString stateError;
    if (!localState_.recordRecentFile(cleanPath, &stateError)) {
        qWarning("Could not record recent file: %s", qPrintable(stateError));
    }
    rebuildRecentMenu();
    statusBar()->showMessage(tr("Opened %1 pages locally").arg(document->pageCount()), 4'000);
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    const auto recentFiles = localState_.recentFiles();
    for (const auto& recent : recentFiles) {
        auto* action = recentMenu_->addAction(QFileInfo(recent.path).fileName());
        action->setToolTip(QDir::toNativeSeparators(recent.path));
        connect(action, &QAction::triggered, this, [this, path = recent.path] { openPdf(path); });
    }
    if (recentFiles.isEmpty()) {
        auto* empty = recentMenu_->addAction(tr("No recent files"));
        empty->setEnabled(false);
    } else {
        recentMenu_->addSeparator();
        auto* clear = recentMenu_->addAction(tr("Clear recent history"));
        connect(clear, &QAction::triggered, this, [this] {
            if (!localState_.clearRecentFiles()) {
                QMessageBox::warning(this, tr("Could not clear history"), tr("The local history database could not be updated."));
            }
            rebuildRecentMenu();
        });
    }
}

void MainWindow::mergeDocuments() {
    const auto inputs = QFileDialog::getOpenFileNames(
        this, tr("Choose PDFs in merge order"), {}, tr("PDF documents (*.pdf)"));
    if (inputs.size() < 2) {
        return;
    }
    const auto output = chooseOutputPath(QStringLiteral("merged.pdf"));
    if (output.isEmpty()) {
        return;
    }
    const auto result = runOrganizerTask(this, tr("Merging PDFs"), [inputs, output](const std::atomic_bool* cancelled) {
        return QpdfOperations::merge(inputs, output, cancelled);
    });
    if (result.succeeded) {
        openPdf(output);
        statusBar()->showMessage(result.message, 5'000);
    } else if (!result.message.contains(tr("cancelled"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Merge did not complete"), result.message);
    }
}

void MainWindow::extractPages() {
    auto* document = currentDocument();
    if (document == nullptr) {
        QMessageBox::information(this, tr("Open a PDF first"), tr("Page extraction works on the active document."));
        return;
    }
    bool accepted = false;
    const auto range = QInputDialog::getText(
        this, tr("Extract pages"), tr("Pages (for example 1-3,5):"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) {
        return;
    }
    if (!QpdfOperations::isValidPageRange(range, document->pageCount())) {
        QMessageBox::warning(this, tr("Invalid page range"), tr("Enter pages within 1–%1, such as 1-3,5.").arg(document->pageCount()));
        return;
    }
    const auto output = chooseOutputPath(QFileInfo(document->filePath()).completeBaseName() + QStringLiteral("-extract.pdf"));
    if (output.isEmpty()) {
        return;
    }
    const auto input = document->filePath();
    const int pageCount = document->pageCount();
    const auto result = runOrganizerTask(this, tr("Extracting pages"), [input, range, pageCount, output](const std::atomic_bool* cancelled) {
        return QpdfOperations::extract(input, range, pageCount, output, cancelled);
    });
    if (result.succeeded) {
        openPdf(output);
        statusBar()->showMessage(result.message, 5'000);
    } else if (!result.message.contains(tr("cancelled"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Extraction did not complete"), result.message);
    }
}

void MainWindow::rotatePages() {
    auto* document = currentDocument();
    if (document == nullptr) {
        QMessageBox::information(this, tr("Open a PDF first"), tr("Page rotation works on the active document."));
        return;
    }
    bool accepted = false;
    const auto range = QInputDialog::getText(
        this, tr("Rotate pages"), tr("Pages (for example 1-3,5):"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) {
        return;
    }
    if (!QpdfOperations::isValidPageRange(range, document->pageCount())) {
        QMessageBox::warning(this, tr("Invalid page range"), tr("Enter pages within 1–%1, such as 1-3,5.").arg(document->pageCount()));
        return;
    }
    const auto directions = QStringList{tr("Clockwise"), tr("Counter-clockwise")};
    const auto direction = QInputDialog::getItem(this, tr("Rotate pages"), tr("Direction:"), directions, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const auto output = chooseOutputPath(QFileInfo(document->filePath()).completeBaseName() + QStringLiteral("-rotated.pdf"));
    if (output.isEmpty()) {
        return;
    }
    const auto input = document->filePath();
    const int pageCount = document->pageCount();
    const bool clockwise = direction == directions.at(0);
    const auto result = runOrganizerTask(this, tr("Rotating pages"), [input, range, pageCount, clockwise, output](const std::atomic_bool* cancelled) {
        return QpdfOperations::rotate(input, range, pageCount, clockwise, output, cancelled);
    });
    if (result.succeeded) {
        openPdf(output);
        statusBar()->showMessage(result.message, 5'000);
    } else if (!result.message.contains(tr("cancelled"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Rotation did not complete"), result.message);
    }
}

void MainWindow::togglePresentationMode() {
    if (isFullScreen()) {
        showNormal();
        menuBar()->show();
    } else {
        menuBar()->hide();
        showFullScreen();
    }
}

DocumentWidget* MainWindow::currentDocument() const {
    return qobject_cast<DocumentWidget*>(tabs_->currentWidget());
}

QString MainWindow::chooseOutputPath(const QString& suggestedName) const {
    return QFileDialog::getSaveFileName(
        const_cast<MainWindow*>(this), tr("Save a new PDF"), suggestedName, tr("PDF documents (*.pdf)"));
}
