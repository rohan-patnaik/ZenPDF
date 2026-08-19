#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>
#include <memory>

namespace {
constexpr qint64 kMaximumLogBytes = 1024 * 1024;
QMutex logMutex;
std::unique_ptr<QFile> logFile;

void writeMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QMutexLocker locker(&logMutex);
    const auto formatted = qFormatLogMessage(type, context, message);
    std::fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());

    if (logFile == nullptr || !logFile->isOpen()) {
        return;
    }
    QTextStream stream(logFile.get());
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ' ' << formatted << '\n';
    stream.flush();
}
}

void Logging::install(const QString& logDirectory) {
    QMutexLocker locker(&logMutex);
    QDir().mkpath(logDirectory);
    const auto path = QDir(logDirectory).filePath(QStringLiteral("zenpdf.log"));
    if (QFileInfo(path).size() > kMaximumLogBytes) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
    logFile = std::make_unique<QFile>(path);
    logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
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
}
