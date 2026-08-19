#include "MainWindow.h"

#include "LocalState.h"

#include <QAction>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(LocalState& localState, QWidget* parent)
    : QMainWindow(parent), localState_(localState), tabs_(new QTabWidget(this)) {
    setWindowTitle(tr("ZenPDF"));
    setMinimumSize(800, 560);
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);
    tabs_->setTabsClosable(true);
    setCentralWidget(tabs_);

    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (tabs_->count() > 1) {
            tabs_->removeTab(index);
        }
    });

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* newWorkspace = fileMenu->addAction(tr("New &workspace"));
    newWorkspace->setShortcut(QKeySequence::New);
    connect(newWorkspace, &QAction::triggered, this, &MainWindow::addWorkspaceTab);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    addWorkspaceTab();
    restoreWindowState();
    statusBar()->showMessage(tr("Local-only workspace ready"));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::addWorkspaceTab() {
    auto* page = new QWidget(tabs_);
    auto* layout = new QVBoxLayout(page);
    auto* message = new QLabel(
        tr("ZenPDF Desktop\n\nOpen and organize local PDFs without uploading them."), page);
    message->setAlignment(Qt::AlignCenter);
    message->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    message->setAccessibleName(tr("ZenPDF local workspace introduction"));
    layout->addWidget(message);
    const int index = tabs_->addTab(page, tr("Workspace %1").arg(tabs_->count() + 1));
    tabs_->setCurrentIndex(index);
}

void MainWindow::restoreWindowState() {
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
}
