#pragma once

#include <QObject>
#include <QString>
#include <QUndoStack>

#include <memory>

class MainWindow;
class QUndoCommand;

class DocumentSession final : public QObject {
    Q_OBJECT

public:
    static constexpr int maximumUndoCommands = 512;

    explicit DocumentSession(QString filePath, QObject* parent = nullptr);
    ~DocumentSession() override;

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] int undoCommandCount() const;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;

    [[nodiscard]] bool push(std::unique_ptr<QUndoCommand> command);
    void undo();
    void redo();
    void markSaved();

signals:
    void stateChanged();

private:
    friend class MainWindow;

    [[nodiscard]] QUndoStack& undoStack();

    QString filePath_;
    QUndoStack undoStack_;
};
