#pragma once

#include <QMainWindow>
#include <QStringList>

class DocumentWidget;
class LocalState;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QTabWidget;

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
    void buildMenus();
    void ensureWelcomeTab();
    void openFileDialog();
    void openPdf(const QString& path);
    void rebuildRecentMenu();
    void mergeDocuments();
    void extractPages();
    void rotatePages();
    void togglePresentationMode();
    [[nodiscard]] DocumentWidget* currentDocument() const;
    [[nodiscard]] QString chooseOutputPath(const QString& suggestedName) const;

    LocalState& localState_;
    QTabWidget* tabs_;
    QMenu* recentMenu_;
};
