#pragma once

#include <QMainWindow>
#include <QStringList>

class DocumentWidget;
class LocalState;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QTabWidget;
class QUndoGroup;
struct QpdfResult;
class MainWindowTest;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(LocalState& localState, QWidget* parent = nullptr);
    void openFiles(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    friend class MainWindowTest;

    void buildMenus();
    void ensureWelcomeTab();
    void openFileDialog();
    void openPdf(const QString& path);
    void closeTab(int index);
    void updateDocumentState(DocumentWidget& document);
    void rebuildRecentMenu();
    void mergeDocuments();
    void extractPages();
    void deletePages();
    void rotatePages();
    void finishOrganizerTask(
        const QString& outputPath,
        const QpdfResult& result,
        const QString& failureTitle);
    void togglePresentationMode();
    [[nodiscard]] DocumentWidget* currentDocument() const;
    [[nodiscard]] QString chooseOutputPath(const QString& suggestedName) const;

    LocalState& localState_;
    QUndoGroup* undoGroup_;
    QTabWidget* tabs_;
    QMenu* recentMenu_;
};
