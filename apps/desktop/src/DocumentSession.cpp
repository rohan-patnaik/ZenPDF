#include "DocumentSession.h"

#include <QFileInfo>

DocumentSession::DocumentSession(QString filePath, QObject* parent)
    : QObject(parent),
      filePath_(QFileInfo(filePath).absoluteFilePath()),
      undoStack_() {
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

QUndoStack& DocumentSession::undoStack() {
    return undoStack_;
}

void DocumentSession::push(QUndoCommand* command) {
    undoStack_.push(command);
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
