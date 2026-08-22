#pragma once

#include <QObject>
#include <QString>
#include <QUndoStack>
#include <QVector>
#include <QtGlobal>

#include <memory>

class MainWindow;
class DocumentSourceRevision;
class QUndoCommand;

class DocumentSession final : public QObject {
    Q_OBJECT

public:
    static constexpr int maximumUndoCommands = 512;
    static constexpr quint64 defaultRetainedByteLimit = 64ULL * 1024ULL * 1024ULL;

    enum class PushRejection {
        None,
        NullCommand,
        ObsoleteCommand,
        CommandExceedsRetainedByteLimit,
        RetainedByteAdditionOverflow,
        RetainedByteLimitExceeded,
    };
    Q_ENUM(PushRejection)

    enum class SourceRevisionStatus {
        Untracked,
        Unchanged,
        Modified,
        Replaced,
        Missing,
        Unavailable,
    };
    Q_ENUM(SourceRevisionStatus)

    explicit DocumentSession(QString filePath,
                             quint64 retainedByteLimit = defaultRetainedByteLimit,
                             QObject* parent = nullptr);
    ~DocumentSession() override;

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] int undoCommandCount() const;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;
    [[nodiscard]] quint64 retainedBytes() const;
    [[nodiscard]] quint64 retainedByteLimit() const;
    [[nodiscard]] quint64 remainingRetainedBytes() const;
    [[nodiscard]] PushRejection lastPushRejection() const;
    [[nodiscard]] QString lastPushRejectionMessage() const;
    [[nodiscard]] bool hasTrackedSourceRevision() const;
    [[nodiscard]] SourceRevisionStatus sourceRevisionStatus() const;
    [[nodiscard]] QString sourceRevisionMessage() const;

    [[nodiscard]] bool push(std::unique_ptr<QUndoCommand> command,
                            quint64 retainedBytes);
    [[nodiscard]] SourceRevisionStatus revalidateSourceRevision();
    void undo();
    void redo();
    void markSaved();

signals:
    void stateChanged();
    void retainedBytesChanged(quint64 retainedBytes);
    void commandRejected(DocumentSession::PushRejection reason, const QString& message);
    void obsoleteCommandDiscarded(const QString& message);
    void sourceRevisionStatusChanged(DocumentSession::SourceRevisionStatus status,
                                     const QString& message);

private:
    friend class MainWindow;

    [[nodiscard]] QUndoStack& undoStack();
    void reconcileCommandLifetimes();

    QString filePath_;
    std::unique_ptr<DocumentSourceRevision> sourceRevision_;
    QUndoStack undoStack_;
    QVector<quint64> retainedCosts_;
    QVector<std::shared_ptr<int>> commandStates_;
    quint64 retainedBytes_ = 0;
    quint64 retainedByteLimit_;
    bool suppressRetainedBytesSignal_ = false;
    bool hasUntrackedExecutedChange_ = false;
    PushRejection lastPushRejection_ = PushRejection::None;
    QString lastPushRejectionMessage_;
};
