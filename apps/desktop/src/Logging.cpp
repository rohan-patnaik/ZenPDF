#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {
constexpr qsizetype kMaximumMessageCharacters = 4096;
constexpr qint64 kMinimumLogBytes = 16 * 1024;
constexpr auto kOwnerFilePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto kOwnerDirectoryPermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
QMutex logMutex;
std::unique_ptr<QFile> logFile;
QString logPath;
qint64 maximumLogBytes = Logging::defaultMaximumLogBytes;

bool openLogFile() {
    logFile = std::make_unique<QFile>(logPath);
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logFile.reset();
        return false;
    }
    if (!QFile::setPermissions(logPath, kOwnerFilePermissions)) {
        logFile->close();
        logFile.reset();
        return false;
    }
    return true;
}

bool rotateLog() {
    if (logFile != nullptr) {
        logFile->close();
        logFile.reset();
    }
    const auto rotatedPath = logPath + QStringLiteral(".1");
    QFile::remove(rotatedPath);
    const bool rotated = QFileInfo(logPath).size() <= maximumLogBytes &&
                         QFile::rename(logPath, rotatedPath);
    if (rotated) {
        QFile::setPermissions(rotatedPath, kOwnerFilePermissions);
    } else if (QFileInfo::exists(logPath) && !QFile::remove(logPath)) {
        return false;
    }
    return openLogFile();
}

QByteArray boundedLogLine(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QString boundedMessage = message.left(kMaximumMessageCharacters);
    if (message.size() > kMaximumMessageCharacters) {
        boundedMessage.append(QStringLiteral(" [truncated]"));
    }
    const auto formatted = qFormatLogMessage(type, context, boundedMessage);
    auto line = QStringLiteral("%1 %2\n")
                    .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), formatted)
                    .toUtf8();
    if (line.size() > maximumLogBytes) {
        line.truncate(static_cast<qsizetype>(maximumLogBytes - 1));
        line.append('\n');
    }
    return line;
}

void writeMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QMutexLocker locker(&logMutex);
    const auto line = boundedLogLine(type, context, message);
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stderr);

    if (logFile == nullptr || !logFile->isOpen()) {
        return;
    }
    if (logFile->size() + line.size() > maximumLogBytes) {
        if (!rotateLog()) {
            return;
        }
    }
    if (logFile != nullptr && logFile->isOpen()) {
        logFile->write(line);
        logFile->flush();
    }
}
}

void Logging::install(const QString& logDirectory, qint64 requestedMaximumLogBytes) {
    QMutexLocker locker(&logMutex);
    if (!QDir().mkpath(logDirectory) || !QFile::setPermissions(logDirectory, kOwnerDirectoryPermissions)) {
        return;
    }
    logPath = QDir(logDirectory).filePath(QStringLiteral("zenpdf.log"));
    maximumLogBytes = std::max(requestedMaximumLogBytes, kMinimumLogBytes);
    if (QFileInfo(logPath).size() > maximumLogBytes) {
        QFile::remove(logPath);
    }
    openLogFile();
    qSetMessagePattern(QStringLiteral("[%{type}] %{category}: %{message}"));
    qInstallMessageHandler(writeMessage);
}

void Logging::shutdown() {
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&logMutex);
    if (logFile != nullptr) {
        logFile->close();
        logFile.reset();
    }
    logPath.clear();
}
