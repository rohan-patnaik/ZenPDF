#pragma once

#include <QMainWindow>

class LocalState;
class QTabWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(LocalState& localState, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void addWorkspaceTab();
    void restoreWindowState();

    LocalState& localState_;
    QTabWidget* tabs_;
};
