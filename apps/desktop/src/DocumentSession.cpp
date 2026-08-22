#include "DocumentSession.h"

#include <QFileInfo>
#include <QUndoCommand>

#include <limits>

namespace {
class NonMergingCommand final : public QUndoCommand {
public:
    explicit NonMergingCommand(std::unique_ptr<QUndoCommand> command)
        : QUndoCommand(command->text()), command_(std::move(command)) {}

    int id() const override { return -1; }
    void redo() override { command_->redo(); }
    void undo() override { command_->undo(); }

private:
    std::unique_ptr<QUndoCommand> command_;
};
}

DocumentSession::DocumentSession(QString filePath,
                                 quint64 retainedByteLimit,
                                 QObject* parent)
    : QObject(parent),
      filePath_(QFileInfo(filePath).absoluteFilePath()),
      undoStack_(),
      retainedByteLimit_(retainedByteLimit) {
    undoStack_.setUndoLimit(maximumUndoCommands);
    connect(&undoStack_, &QUndoStack::cleanChanged, this, &DocumentSession::stateChanged);
    connect(&undoStack_, &QUndoStack::canUndoChanged, this, &DocumentSession::stateChanged);
    connect(&undoStack_, &QUndoStack::canRedoChanged, this, &DocumentSession::stateChanged);
    connect(&undoStack_, &QUndoStack::undoTextChanged, this, &DocumentSession::stateChanged);
    connect(&undoStack_, &QUndoStack::redoTextChanged, this, &DocumentSession::stateChanged);
}

DocumentSession::~DocumentSession() {
    disconnect(&undoStack_, nullptr, this, nullptr);
}

QString DocumentSession::filePath() const {
    return filePath_;
}

QString DocumentSession::displayName() const {
    return QFileInfo(filePath_).fileName();
}

bool DocumentSession::isDirty() const {
    return !undoStack_.isClean();
}

bool DocumentSession::canUndo() const {
    return undoStack_.canUndo();
}

bool DocumentSession::canRedo() const {
    return undoStack_.canRedo();
}

int DocumentSession::undoCommandCount() const {
    return undoStack_.count();
}

QString DocumentSession::undoText() const {
    return undoStack_.undoText();
}

QString DocumentSession::redoText() const {
    return undoStack_.redoText();
}

quint64 DocumentSession::retainedBytes() const {
    return retainedBytes_;
}

quint64 DocumentSession::retainedByteLimit() const {
    return retainedByteLimit_;
}

quint64 DocumentSession::remainingRetainedBytes() const {
    return retainedByteLimit_ - retainedBytes_;
}

DocumentSession::PushRejection DocumentSession::lastPushRejection() const {
    return lastPushRejection_;
}

QString DocumentSession::lastPushRejectionMessage() const {
    return lastPushRejectionMessage_;
}

QUndoStack& DocumentSession::undoStack() {
    return undoStack_;
}

bool DocumentSession::push(std::unique_ptr<QUndoCommand> command,
                           quint64 retainedBytes) {
    const auto reject = [this](PushRejection reason, QString message) {
        lastPushRejection_ = reason;
        lastPushRejectionMessage_ = std::move(message);
        emit commandRejected(reason, lastPushRejectionMessage_);
        return false;
    };

    if (command == nullptr) {
        return reject(PushRejection::NullCommand,
                      tr("The change could not be recorded because its undo command is missing."));
    }
    if (retainedBytes > retainedByteLimit_) {
        return reject(PushRejection::CommandExceedsRetainedByteLimit,
                      tr("The change needs %1 undo bytes, above this document's %2-byte limit.")
                          .arg(retainedBytes)
                          .arg(retainedByteLimit_));
    }

    const auto retainedCount = undoStack_.index();
    const auto firstRetained = retainedCount == maximumUndoCommands ? 1 : 0;
    quint64 projectedBytes = 0;
    for (auto index = firstRetained; index < retainedCount; ++index) {
        projectedBytes += retainedCosts_.at(index);
    }
    if (retainedBytes > std::numeric_limits<quint64>::max() - projectedBytes) {
        return reject(PushRejection::RetainedByteAdditionOverflow,
                      tr("The change's declared undo size overflows the accounting range."));
    }
    projectedBytes += retainedBytes;
    if (projectedBytes > retainedByteLimit_) {
        return reject(PushRejection::RetainedByteLimitExceeded,
                      tr("The change needs %1 undo bytes, but only %2 bytes remain for this document.")
                          .arg(retainedBytes)
                          .arg(retainedByteLimit_ - (projectedBytes - retainedBytes)));
    }

    QVector<quint64> projectedCosts;
    projectedCosts.reserve(qMin(retainedCount + 1, maximumUndoCommands));
    for (auto index = firstRetained; index < retainedCount; ++index) {
        projectedCosts.append(retainedCosts_.at(index));
    }
    projectedCosts.append(retainedBytes);

    auto wrappedCommand = std::make_unique<NonMergingCommand>(std::move(command));
    retainedCosts_ = std::move(projectedCosts);
    retainedBytes_ = projectedBytes;
    lastPushRejection_ = PushRejection::None;
    lastPushRejectionMessage_.clear();
    undoStack_.push(wrappedCommand.release());
    emit retainedBytesChanged(retainedBytes_);
    return true;
}

void DocumentSession::undo() {
    undoStack_.undo();
}

void DocumentSession::redo() {
    undoStack_.redo();
}

void DocumentSession::markSaved() {
    undoStack_.setClean();
}
