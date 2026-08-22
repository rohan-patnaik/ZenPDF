#include "DocumentSession.h"

#include <QFileInfo>
#include <QThread>
#include <QUndoCommand>

#include <cerrno>
#include <limits>
#include <optional>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr int kCommandAlive = 0;
constexpr int kCommandDiscarded = 1;
constexpr int kCommandObsoleteAfterRedo = 2;
constexpr int kCommandObsoleteAfterUndo = 3;

class NonMergingCommand final : public QUndoCommand {
public:
    NonMergingCommand(std::unique_ptr<QUndoCommand> command,
                      std::shared_ptr<int> state)
        : QUndoCommand(command->text()), command_(std::move(command)), state_(std::move(state)) {}
    ~NonMergingCommand() override {
        if (*state_ == kCommandAlive) {
            *state_ = kCommandDiscarded;
        }
    }

    int id() const override { return -1; }
    void redo() override {
        command_->redo();
        synchronizeState(kCommandObsoleteAfterRedo);
    }
    void undo() override {
        command_->undo();
        synchronizeState(kCommandObsoleteAfterUndo);
    }

private:
    void synchronizeState(int obsoleteState) {
        setText(command_->text());
        setObsolete(command_->isObsolete());
        if (isObsolete()) {
            *state_ = obsoleteState;
        }
    }

    std::unique_ptr<QUndoCommand> command_;
    std::shared_ptr<int> state_;
};
}

class DocumentSourceRevision final {
public:
    struct Identity final {
        QString canonicalPath;
        qint64 size{-1};
        qint64 modifiedMs{-1};
        qint64 metadataChangedMs{-1};
#ifdef Q_OS_UNIX
        quint64 pathDevice{0};
        quint64 pathInode{0};
        quint64 sourceDevice{0};
        quint64 sourceInode{0};
        qint64 modifiedNs{-1};
        qint64 changedNs{-1};
        bool pathIsSymlink{false};
#endif
    };

    enum class ProbeFailure { None, Missing, Unavailable, NotRegular };

    struct Probe final {
        std::optional<Identity> identity;
        ProbeFailure failure{ProbeFailure::Unavailable};
    };

    explicit DocumentSourceRevision(const QString& path)
        : path_(path), baseline_(probe(path)) {
        status_ = baseline_.identity.has_value()
                      ? DocumentSession::SourceRevisionStatus::Unchanged
                      : DocumentSession::SourceRevisionStatus::Untracked;
        if (status_ == DocumentSession::SourceRevisionStatus::Untracked) {
            message_ = QObject::tr(
                "No stable regular source was available when this document session began.");
        }
    }

    [[nodiscard]] bool isTracked() const { return baseline_.identity.has_value(); }
    [[nodiscard]] DocumentSession::SourceRevisionStatus status() const { return status_; }
    [[nodiscard]] const QString& message() const { return message_; }

    [[nodiscard]] bool revalidate() {
        const auto previousStatus = status_;
        const auto previousMessage = message_;
        if (!baseline_.identity.has_value()) {
            status_ = DocumentSession::SourceRevisionStatus::Untracked;
            message_ = QObject::tr(
                "No stable regular source was available when this document session began.");
            return status_ != previousStatus || message_ != previousMessage;
        }

        const auto current = probe(path_);
        if (!current.identity.has_value()) {
            switch (current.failure) {
            case ProbeFailure::Missing:
                status_ = DocumentSession::SourceRevisionStatus::Missing;
                message_ = QObject::tr(
                    "The source file is no longer available. Reopen it or choose a new destination before saving.");
                break;
            case ProbeFailure::NotRegular:
                status_ = DocumentSession::SourceRevisionStatus::Replaced;
                message_ = QObject::tr(
                    "The source path no longer refers to the regular file that was opened. Reopen it before saving.");
                break;
            case ProbeFailure::Unavailable:
            case ProbeFailure::None:
                status_ = DocumentSession::SourceRevisionStatus::Unavailable;
                message_ = QObject::tr(
                    "The source identity could not be verified. Restore access and retry before saving.");
                break;
            }
            return status_ != previousStatus || message_ != previousMessage;
        }

        const auto& expected = *baseline_.identity;
        const auto& actual = *current.identity;
        if (!sameSource(expected, actual)) {
            status_ = DocumentSession::SourceRevisionStatus::Replaced;
            message_ = QObject::tr(
                "The source path now refers to a different file. Reopen it before saving.");
        } else if (!sameRevision(expected, actual)) {
            status_ = DocumentSession::SourceRevisionStatus::Modified;
            message_ = QObject::tr(
                "The source file changed after it was opened. Reopen it before saving.");
        } else {
            status_ = DocumentSession::SourceRevisionStatus::Unchanged;
            message_.clear();
        }
        return status_ != previousStatus || message_ != previousMessage;
    }

private:
    static ProbeFailure classifyErrno(int error) {
        return error == ENOENT || error == ENOTDIR ? ProbeFailure::Missing
                                                   : ProbeFailure::Unavailable;
    }

    static Probe probe(const QString& path) {
        const QFileInfo info(path);
#ifdef Q_OS_UNIX
        struct stat pathMetadata {};
        const auto encodedPath = QFile::encodeName(path);
        if (::lstat(encodedPath.constData(), &pathMetadata) != 0) {
            return {{}, classifyErrno(errno)};
        }
        if (!S_ISREG(pathMetadata.st_mode) && !S_ISLNK(pathMetadata.st_mode)) {
            return {{}, ProbeFailure::NotRegular};
        }

        struct stat sourceMetadata {};
        if (::stat(encodedPath.constData(), &sourceMetadata) != 0) {
            return {{}, classifyErrno(errno)};
        }
        if (!S_ISREG(sourceMetadata.st_mode)) {
            return {{}, ProbeFailure::NotRegular};
        }
        const auto canonicalPath = info.canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            return {{}, ProbeFailure::Unavailable};
        }

        struct stat confirmedPathMetadata {};
        struct stat confirmedSourceMetadata {};
        if (::lstat(encodedPath.constData(), &confirmedPathMetadata) != 0
            || ::stat(encodedPath.constData(), &confirmedSourceMetadata) != 0) {
            return {{}, classifyErrno(errno)};
        }
        const bool stablePath = pathMetadata.st_dev == confirmedPathMetadata.st_dev
                                && pathMetadata.st_ino == confirmedPathMetadata.st_ino
                                && pathMetadata.st_mode == confirmedPathMetadata.st_mode;
        const bool stableSource = sourceMetadata.st_dev == confirmedSourceMetadata.st_dev
                                  && sourceMetadata.st_ino == confirmedSourceMetadata.st_ino
                                  && sourceMetadata.st_size == confirmedSourceMetadata.st_size;
#ifdef Q_OS_LINUX
        const bool stableRevision =
            sourceMetadata.st_mtim.tv_sec == confirmedSourceMetadata.st_mtim.tv_sec
            && sourceMetadata.st_mtim.tv_nsec == confirmedSourceMetadata.st_mtim.tv_nsec
            && sourceMetadata.st_ctim.tv_sec == confirmedSourceMetadata.st_ctim.tv_sec
            && sourceMetadata.st_ctim.tv_nsec == confirmedSourceMetadata.st_ctim.tv_nsec;
#else
        const bool stableRevision = true;
#endif
        if (!stablePath || !stableSource || !stableRevision
            || (!S_ISREG(confirmedPathMetadata.st_mode)
                && !S_ISLNK(confirmedPathMetadata.st_mode))
            || !S_ISREG(confirmedSourceMetadata.st_mode)) {
            return {{}, ProbeFailure::Unavailable};
        }
        Identity identity{
            canonicalPath,
            static_cast<qint64>(confirmedSourceMetadata.st_size),
            info.lastModified().toMSecsSinceEpoch(),
            info.fileTime(QFileDevice::FileMetadataChangeTime).toMSecsSinceEpoch(),
            static_cast<quint64>(confirmedPathMetadata.st_dev),
            static_cast<quint64>(confirmedPathMetadata.st_ino),
            static_cast<quint64>(confirmedSourceMetadata.st_dev),
            static_cast<quint64>(confirmedSourceMetadata.st_ino),
#ifdef Q_OS_LINUX
            static_cast<qint64>(confirmedSourceMetadata.st_mtim.tv_sec) * 1'000'000'000LL
                + confirmedSourceMetadata.st_mtim.tv_nsec,
            static_cast<qint64>(confirmedSourceMetadata.st_ctim.tv_sec) * 1'000'000'000LL
                + confirmedSourceMetadata.st_ctim.tv_nsec,
#else
            info.lastModified().toMSecsSinceEpoch() * 1'000'000LL,
            info.fileTime(QFileDevice::FileMetadataChangeTime).toMSecsSinceEpoch()
                * 1'000'000LL,
#endif
            S_ISLNK(confirmedPathMetadata.st_mode)};
        return {identity, ProbeFailure::None};
#else
        if (!info.exists()) {
            return {{}, ProbeFailure::Missing};
        }
        if (!info.isFile()) {
            return {{}, ProbeFailure::NotRegular};
        }
        const auto canonicalPath = info.canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            return {{}, ProbeFailure::Unavailable};
        }
        return {Identity{canonicalPath,
                         info.size(),
                         info.lastModified().toMSecsSinceEpoch(),
                         info.fileTime(QFileDevice::FileMetadataChangeTime).toMSecsSinceEpoch()},
                ProbeFailure::None};
#endif
    }

    static bool sameSource(const Identity& expected, const Identity& actual) {
#ifdef Q_OS_UNIX
        return expected.pathDevice == actual.pathDevice
               && expected.pathInode == actual.pathInode
               && expected.sourceDevice == actual.sourceDevice
               && expected.sourceInode == actual.sourceInode
               && expected.pathIsSymlink == actual.pathIsSymlink
               && expected.canonicalPath == actual.canonicalPath;
#else
        return expected.canonicalPath == actual.canonicalPath;
#endif
    }

    static bool sameRevision(const Identity& expected, const Identity& actual) {
#ifdef Q_OS_UNIX
        return expected.size == actual.size && expected.modifiedNs == actual.modifiedNs
               && expected.changedNs == actual.changedNs;
#else
        return expected.size == actual.size && expected.modifiedMs == actual.modifiedMs
               && expected.metadataChangedMs == actual.metadataChangedMs;
#endif
    }

    QString path_;
    Probe baseline_;
    DocumentSession::SourceRevisionStatus status_;
    QString message_;
};

DocumentSession::DocumentSession(QString filePath,
                                 quint64 retainedByteLimit,
                                 QObject* parent)
    : QObject(parent),
      filePath_(QFileInfo(filePath).absoluteFilePath()),
      sourceRevision_(std::make_unique<DocumentSourceRevision>(filePath_)),
      undoStack_(),
      retainedByteLimit_(retainedByteLimit) {
    undoStack_.setUndoLimit(maximumUndoCommands);
    const auto reconcile = [this] { reconcileCommandLifetimes(); };
    connect(&undoStack_, &QUndoStack::indexChanged, this, reconcile);
    connect(&undoStack_, &QUndoStack::cleanChanged, this, reconcile);
    connect(&undoStack_, &QUndoStack::canUndoChanged, this, reconcile);
    connect(&undoStack_, &QUndoStack::canRedoChanged, this, reconcile);
    connect(&undoStack_, &QUndoStack::undoTextChanged, this, reconcile);
    connect(&undoStack_, &QUndoStack::redoTextChanged, this, reconcile);
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

bool DocumentSession::hasTrackedSourceRevision() const {
    Q_ASSERT(QThread::currentThread() == thread());
    return sourceRevision_->isTracked();
}

DocumentSession::SourceRevisionStatus DocumentSession::sourceRevisionStatus() const {
    Q_ASSERT(QThread::currentThread() == thread());
    return sourceRevision_->status();
}

QString DocumentSession::sourceRevisionMessage() const {
    Q_ASSERT(QThread::currentThread() == thread());
    return sourceRevision_->message();
}

DocumentSession::SourceRevisionStatus DocumentSession::revalidateSourceRevision() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (sourceRevision_->revalidate()) {
        emit sourceRevisionStatusChanged(sourceRevision_->status(), sourceRevision_->message());
    }
    return sourceRevision_->status();
}

QUndoStack& DocumentSession::undoStack() {
    return undoStack_;
}

void DocumentSession::reconcileCommandLifetimes() {
    const auto previousBytes = retainedBytes_;
    auto becameDirtyWithoutUndo = false;
    for (auto index = commandStates_.size() - 1; index >= 0; --index) {
        const auto state = *commandStates_.at(index);
        if (state == kCommandAlive) {
            continue;
        }

        retainedBytes_ -= retainedCosts_.at(index);
        retainedCosts_.removeAt(index);
        commandStates_.removeAt(index);
        if (state == kCommandObsoleteAfterRedo) {
            hasUntrackedExecutedChange_ = true;
            becameDirtyWithoutUndo = true;
            emit obsoleteCommandDiscarded(
                tr("The change became obsolete while redoing and was removed from history."));
        } else if (state == kCommandObsoleteAfterUndo) {
            emit obsoleteCommandDiscarded(
                tr("The change became obsolete while undoing and was removed from history."));
        }
    }

    if (!suppressRetainedBytesSignal_ && retainedBytes_ != previousBytes) {
        emit retainedBytesChanged(retainedBytes_);
    }
    if (becameDirtyWithoutUndo) {
        emit stateChanged();
    }
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
    QVector<std::shared_ptr<int>> projectedStates;
    projectedCosts.reserve(qMin(retainedCount + 1, maximumUndoCommands));
    projectedStates.reserve(qMin(retainedCount + 1, maximumUndoCommands));
    for (auto index = firstRetained; index < retainedCount; ++index) {
        projectedCosts.append(retainedCosts_.at(index));
        projectedStates.append(commandStates_.at(index));
    }
    projectedCosts.append(retainedBytes);

    auto commandState = std::make_shared<int>(kCommandAlive);
    projectedStates.append(commandState);
    auto wrappedCommand =
        std::make_unique<NonMergingCommand>(std::move(command), commandState);
    const auto previousBytes = retainedBytes_;
    retainedCosts_ = std::move(projectedCosts);
    commandStates_ = std::move(projectedStates);
    retainedBytes_ = projectedBytes;
    lastPushRejection_ = PushRejection::None;
    lastPushRejectionMessage_.clear();
    suppressRetainedBytesSignal_ = true;
    undoStack_.push(wrappedCommand.release());
    reconcileCommandLifetimes();
    suppressRetainedBytesSignal_ = false;
    if (retainedBytes_ != previousBytes) {
        emit retainedBytesChanged(retainedBytes_);
    }
    return true;
}

void DocumentSession::undo() {
    undoStack_.undo();
    reconcileCommandLifetimes();
}

void DocumentSession::redo() {
    undoStack_.redo();
    reconcileCommandLifetimes();
}

void DocumentSession::markSaved() {
    const auto hadUntrackedChange = hasUntrackedExecutedChange_;
    hasUntrackedExecutedChange_ = false;
    undoStack_.setClean();
    if (hadUntrackedChange) {
        emit stateChanged();
    }
}
