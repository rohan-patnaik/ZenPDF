#include "MainWindow.h"

#include "DocumentSession.h"
#include "DocumentWidget.h"
#include "LocalState.h"
#include "Preferences.h"
#include "QpdfOperations.h"

#include <QAction>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QShortcut>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUndoGroup>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
constexpr qint64 kMaximumDocumentBytes = 2LL * 1024 * 1024 * 1024;

void clearSensitiveString(QString& value) {
    value.fill(QChar{u'\0'});
    value.clear();
    value.squeeze();
}

bool hasLocalPdf(const QMimeData* mimeData) {
    for (const auto& url : mimeData->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool confirmDiscard(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox box(
        QMessageBox::Warning,
        title,
        text,
        QMessageBox::Discard | QMessageBox::Cancel,
        parent);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    if (auto* discard = box.button(QMessageBox::Discard)) {
        discard->setAccessibleName(QObject::tr("Discard"));
        discard->setAccessibleDescription(QObject::tr("Continue without saving"));
    }
    if (auto* cancel = box.button(QMessageBox::Cancel)) {
        cancel->setAccessibleName(QObject::tr("Cancel"));
        cancel->setAccessibleDescription(QObject::tr("Return to ZenPDF"));
    }
    return box.exec() == QMessageBox::Discard;
}
}

MainWindow::MainWindow(LocalState& localState, Preferences& preferences, QWidget* parent)
    : QMainWindow(parent),
      localState_(localState),
      preferences_(preferences),
      undoGroup_(new QUndoGroup(this)),
      exitPresentationShortcut_(new QShortcut(QKeySequence(Qt::Key_Escape), this)),
      tabs_(new QTabWidget(this)),
      recentMenu_(nullptr),
      organizeMenu_(nullptr) {
    setWindowTitle(tr("ZenPDF"));
    setMinimumSize(900, 620);
    setAcceptDrops(true);
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);
    tabs_->setTabsClosable(true);
    setCentralWidget(tabs_);
    exitPresentationShortcut_->setContext(Qt::ApplicationShortcut);
    exitPresentationShortcut_->setEnabled(false);
    connect(exitPresentationShortcut_, &QShortcut::activated, this, [this] {
        if (presentationMode_) {
            togglePresentationMode();
        }
    });

    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        auto* document = qobject_cast<DocumentWidget*>(tabs_->widget(index));
        undoGroup_->setActiveStack(document != nullptr ? &document->session().undoStack() : nullptr);
    });

    buildMenus();
    ensureWelcomeTab();
    const auto defaultGeometryRect = geometry();
    const auto defaultState = saveState();
    const auto defaultWindowState = windowState();
    QString statusMessage = tr("Local-only workspace ready");
    WindowPreferences windowPreferences;
    QString preferencesError;
    if (!preferences_.loadWindowPreferences(&windowPreferences, &preferencesError)) {
        statusMessage = preferencesError;
    } else {
        QMainWindow validationWindow;
        const bool geometryValid = windowPreferences.geometry.isEmpty() ||
                                   validationWindow.restoreGeometry(windowPreferences.geometry);
        const bool stateValid = windowPreferences.state.isEmpty() ||
                                validationWindow.restoreState(windowPreferences.state);
        const bool geometryRestored = geometryValid &&
                                      (windowPreferences.geometry.isEmpty() ||
                                       restoreGeometry(windowPreferences.geometry));
        const bool stateRestored = stateValid &&
                                   (windowPreferences.state.isEmpty() ||
                                    restoreState(windowPreferences.state));
        if (!geometryRestored || !stateRestored) {
            setWindowState(defaultWindowState);
            setGeometry(defaultGeometryRect);
            (void)restoreState(defaultState);
            statusMessage = tr("Window preferences were invalid; defaults were used.");
        }
    }
    statusBar()->showMessage(statusMessage);
}

void MainWindow::openFiles(const QStringList& paths) {
    for (const auto& path : paths) {
        openPdf(path);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    int dirtyDocuments = 0;
    for (int index = 0; index < tabs_->count(); ++index) {
        if (const auto* document = qobject_cast<DocumentWidget*>(tabs_->widget(index));
            document != nullptr && document->session().isDirty()) {
            ++dirtyDocuments;
        }
    }
    if (dirtyDocuments > 0 &&
        !confirmDiscard(
            this,
            tr("Discard unsaved changes?"),
            tr("%n open document(s) contain unsaved changes. Closing ZenPDF will discard them.",
               nullptr,
               dirtyDocuments))) {
        event->ignore();
        return;
    }
    QString preferencesError;
    if (!preferences_.saveWindowPreferences(
            {saveGeometry(), saveState()}, &preferencesError)) {
        statusBar()->showMessage(preferencesError);
        if (!confirmDiscard(
                this,
                tr("Window preferences not saved"),
                tr("ZenPDF could not save private window preferences. Close without saving them?"))) {
            event->ignore();
            return;
        }
    }
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

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction = undoGroup_->createUndoAction(this, tr("&Undo"));
    undoAction->setObjectName(QStringLiteral("undoAction"));
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setStatusTip(tr("Undo the last change in the active document"));
    editMenu->addAction(undoAction);
    auto* redoAction = undoGroup_->createRedoAction(this, tr("&Redo"));
    redoAction->setObjectName(QStringLiteral("redoAction"));
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setStatusTip(tr("Redo the next change in the active document"));
    editMenu->addAction(redoAction);

    organizeMenu_ = menuBar()->addMenu(tr("&Organize"));
    auto* mergeAction = organizeMenu_->addAction(tr("&Merge PDFs into a new file…"));
    connect(mergeAction, &QAction::triggered, this, &MainWindow::mergeDocuments);
    auto* extractAction = organizeMenu_->addAction(tr("&Extract pages to a new file…"));
    connect(extractAction, &QAction::triggered, this, &MainWindow::extractPages);
    auto* deleteAction = organizeMenu_->addAction(tr("&Delete selected pages into a new file…"));
    deleteAction->setObjectName(QStringLiteral("deletePagesAction"));
    deleteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    deleteAction->setStatusTip(tr("Create a new PDF without the selected pages; the open document is unchanged"));
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deletePages);
    auto* rotateAction = organizeMenu_->addAction(tr("&Rotate pages into a new file…"));
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
    for (int attempt = 0; document->needsPassword() && attempt < 3; ++attempt) {
        bool accepted = false;
        auto password = QInputDialog::getText(
            this,
            tr("Password required"),
            tr("Enter the document password:"),
            QLineEdit::Password,
            {},
            &accepted);
        if (!accepted) {
            clearSensitiveString(password);
            document->deleteLater();
            return;
        }
        const bool unlocked = document->unlock(password);
        clearSensitiveString(password);
        if (unlocked) {
            break;
        }
        if (attempt < 2) {
            QMessageBox::warning(this, tr("Incorrect password"), tr("The document did not accept that password."));
        }
    }
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
    undoGroup_->addStack(&document->session().undoStack());
    connect(&document->session(), &DocumentSession::stateChanged, this, [this, document] {
        updateDocumentState(*document);
    });
    const int index = tabs_->addTab(document, document->displayName());
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(cleanPath));
    updateDocumentState(*document);
    tabs_->setCurrentIndex(index);
    undoGroup_->setActiveStack(&document->session().undoStack());
    QString stateError;
    if (!localState_.recordRecentFile(cleanPath, &stateError)) {
        qWarning("Could not record recent file: %s", qPrintable(stateError));
    }
    rebuildRecentMenu();
    statusBar()->showMessage(tr("Opened %1 pages locally").arg(document->pageCount()), 4'000);
}

void MainWindow::closeTab(int index) {
    auto* document = qobject_cast<DocumentWidget*>(tabs_->widget(index));
    if (document != nullptr && document->session().isDirty() &&
        !confirmDiscard(
            this,
            tr("Discard unsaved changes?"),
            tr("%1 contains unsaved changes.").arg(document->displayName()))) {
        return;
    }
    auto* widget = tabs_->widget(index);
    if (document != nullptr) {
        undoGroup_->removeStack(&document->session().undoStack());
    }
    tabs_->removeTab(index);
    widget->deleteLater();
    ensureWelcomeTab();
}

void MainWindow::updateDocumentState(DocumentWidget& document) {
    const int index = tabs_->indexOf(&document);
    if (index < 0) {
        return;
    }
    const bool dirty = document.session().isDirty();
    document.setWindowModified(dirty);
    tabs_->setTabText(index, document.displayName() + (dirty ? QStringLiteral("*") : QString{}));
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
    const auto result = runOrganizerTask(tr("Merging PDFs"), [inputs, output](const std::atomic_bool* cancelled) {
        return QpdfOperations::merge(inputs, output, cancelled);
    });
    finishOrganizerTask(output, result, tr("Merge did not complete"));
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
    const auto result = runOrganizerTask(tr("Extracting pages"), [input, range, pageCount, output](const std::atomic_bool* cancelled) {
        return QpdfOperations::extract(input, range, pageCount, output, cancelled);
    });
    finishOrganizerTask(output, result, tr("Extraction did not complete"));
}

void MainWindow::deletePages() {
    auto* document = currentDocument();
    if (document == nullptr) {
        QMessageBox::information(this, tr("Open a PDF first"), tr("Page deletion works on the active document."));
        return;
    }
    bool accepted = false;
    const auto range = QInputDialog::getText(
        this,
        tr("Delete selected pages into a new file"),
        tr("Pages to remove (for example 1-3,5):"),
        QLineEdit::Normal,
        {},
        &accepted);
    if (!accepted) {
        return;
    }
    if (!QpdfOperations::isValidPageRange(range, document->pageCount())) {
        QMessageBox::warning(
            this,
            tr("Invalid page range"),
            tr("Enter pages within 1–%1, such as 1-3,5.").arg(document->pageCount()));
        return;
    }
    const auto output = chooseOutputPath(
        QFileInfo(document->filePath()).completeBaseName() + QStringLiteral("-pages-removed.pdf"));
    if (output.isEmpty()) {
        return;
    }
    const auto input = document->filePath();
    const int pageCount = document->pageCount();
    const auto result = runOrganizerTask(
        tr("Creating PDF without selected pages"),
        [input, range, pageCount, output](const std::atomic_bool* cancelled) {
            return QpdfOperations::deletePages(input, range, pageCount, output, cancelled);
        });
    finishOrganizerTask(output, result, tr("Page deletion did not complete"));
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
    const auto result = runOrganizerTask(tr("Rotating pages"), [input, range, pageCount, clockwise, output](const std::atomic_bool* cancelled) {
        return QpdfOperations::rotate(input, range, pageCount, clockwise, output, cancelled);
    });
    finishOrganizerTask(output, result, tr("Rotation did not complete"));
}

void MainWindow::setOrganizerActionsEnabled(bool enabled) {
    organizeMenu_->setEnabled(enabled);
    for (auto* action : organizeMenu_->actions()) {
        action->setEnabled(enabled);
    }
}

QpdfResult MainWindow::runOrganizerTask(
    const QString& title,
    std::function<QpdfResult(const std::atomic_bool*)> operation) {
    if (organizerJobActive_) {
        return {false, tr("Another organizer job is already running. Cancel it or wait for it to finish.")};
    }
    organizerJobActive_ = true;
    setOrganizerActionsEnabled(false);

    QProgressDialog progress(
        tr("Working on a private local copy…"), tr("Cancel"), 0, 0, this);
    progress.setWindowTitle(title);
    progress.setAccessibleName(tr("Organizer job progress"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);

    QEventLoop completionLoop;
    QpdfResult result{false, tr("The organizer job ended without a result.")};
    bool terminal = false;
    quint64 jobId = 0;
    connect(
        &jobScheduler_,
        &DesktopJobScheduler::jobFinished,
        &progress,
        [&](quint64 completedId,
            DesktopJobScheduler::Completion completion,
            const QVariant& value,
            const QString& message) {
            if (completedId != jobId) {
                return;
            }
            if (completion == DesktopJobScheduler::Completion::Succeeded
                && value.canConvert<QpdfResult>()) {
                result = value.value<QpdfResult>();
            } else if (completion == DesktopJobScheduler::Completion::Cancelled) {
                result = {false, tr("The organizer job was cancelled.")};
            } else {
                result = {false,
                          message.isEmpty() ? tr("The organizer job failed.") : message};
            }
            terminal = true;
            progress.accept();
            completionLoop.quit();
        });

    const auto submission = jobScheduler_.submit(
        [operation = std::move(operation)](const std::atomic_bool& cancelled) {
            return QVariant::fromValue(operation(&cancelled));
        });
    if (!submission.accepted) {
        organizerJobActive_ = false;
        setOrganizerActionsEnabled(true);
        return {false, submission.message};
    }
    jobId = submission.id;
    connect(&progress, &QProgressDialog::canceled, &progress, [&] {
        if (!terminal) {
            (void)jobScheduler_.cancel(jobId);
            QTimer::singleShot(0, &progress, [&] {
                if (!terminal) {
                    progress.setLabelText(tr("Cancelling the local organizer job…"));
                    progress.setCancelButton(nullptr);
                    progress.show();
                }
            });
        }
    });

    progress.show();
    if (!terminal) {
        completionLoop.exec();
    }
    if (!terminal) {
        (void)jobScheduler_.cancel(jobId);
        result = {false, tr("The organizer job was cancelled during shutdown.")};
    }
    organizerJobActive_ = false;
    setOrganizerActionsEnabled(true);
    return result;
}

void MainWindow::finishOrganizerTask(
    const QString& outputPath,
    const QpdfResult& result,
    const QString& failureTitle) {
    if (result.succeeded) {
        openPdf(outputPath);
        statusBar()->showMessage(result.message, 5'000);
    } else if (!result.message.contains(tr("cancelled"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, failureTitle, result.message);
    }
}

void MainWindow::togglePresentationMode() {
    presentationMode_ = !presentationMode_;
    exitPresentationShortcut_->setEnabled(presentationMode_);
    if (!presentationMode_) {
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
