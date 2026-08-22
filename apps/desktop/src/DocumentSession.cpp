#include "DocumentSession.h"

#include <QFileInfo>
#include <QUndoCommand>

#include <limits>

namespace {
class NonMergingCommand final : public QUndoCommand {
public:
    NonMergingCommand(std::unique_ptr<QUndoCommand> command,
                      std::shared_ptr<bool> alive)
        : QUndoCommand(command->text()), command_(std::move(command)), alive_(std::move(alive)) {}
    ~NonMergingCommand() override { *alive_ = false; }

    int id() const override { return -1; }
    void redo() override {
        command_->redo();
        synchronizeState();
    }
    void undo() override {
        command_->undo();
        synchronizeState();
    }

private:
    void synchronizeState() {
        setText(command_->text());
        setObsolete(command_->isObsolete());
    }

    std::unique_ptr<QUndoCommand> command_;
    std::shared_ptr<bool> alive_;
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
    return hasUntrackedExecutedChange_ || !undoStack_.isClean();
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
    if (command->isObsolete()) {
        return reject(PushRejection::ObsoleteCommand,
                      tr("The change was already obsolete and was not executed."));
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

    auto commandAlive = std::make_shared<bool>(true);
    auto wrappedCommand =
        std::make_unique<NonMergingCommand>(std::move(command), commandAlive);
    const auto previousBytes = retainedBytes_;
    lastPushRejection_ = PushRejection::None;
    lastPushRejectionMessage_.clear();
    undoStack_.push(wrappedCommand.release());

    const auto commandWasRetained = *commandAlive;
    if (commandWasRetained) {
        retainedCosts_ = std::move(projectedCosts);
        retainedBytes_ = projectedBytes;
    } else {
        retainedCosts_.resize(undoStack_.count());
        retainedBytes_ = 0;
        for (const auto cost : retainedCosts_) {
            retainedBytes_ += cost;
        }
        hasUntrackedExecutedChange_ = true;
        emit obsoleteCommandDiscarded(
            tr("The change became obsolete while executing and was not retained for undo."));
        emit stateChanged();
    }
    if (retainedBytes_ != previousBytes) {
        emit retainedBytesChanged(retainedBytes_);
    }
    return true;
}

void DocumentSession::undo() {
    const auto commandIndex = undoStack_.index() - 1;
    const auto previousCount = undoStack_.count();
    undoStack_.undo();
    if (undoStack_.count() < previousCount) {
        retainedBytes_ -= retainedCosts_.at(commandIndex);
        retainedCosts_.removeAt(commandIndex);
        emit retainedBytesChanged(retainedBytes_);
        emit obsoleteCommandDiscarded(
            tr("The change became obsolete while undoing and was removed from history."));
    }
}

void DocumentSession::redo() {
    const auto commandIndex = undoStack_.index();
    const auto previousCount = undoStack_.count();
    undoStack_.redo();
    if (undoStack_.count() < previousCount) {
        retainedBytes_ -= retainedCosts_.at(commandIndex);
        retainedCosts_.removeAt(commandIndex);
        hasUntrackedExecutedChange_ = true;
        emit retainedBytesChanged(retainedBytes_);
        emit obsoleteCommandDiscarded(
            tr("The change became obsolete while redoing and was removed from history."));
        emit stateChanged();
    }
}

void DocumentSession::markSaved() {
    const auto hadUntrackedChange = hasUntrackedExecutedChange_;
    hasUntrackedExecutedChange_ = false;
    undoStack_.setClean();
    if (hadUntrackedChange) {
        emit stateChanged();
    }
}
