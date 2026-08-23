#include "Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <memory>
#include <vector>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr qint64 kTestMaximumBytes = 16 * 1024;
constexpr auto kSecret = "PRIVATE_DOCUMENT_PASSWORD_SENTINEL_91cc4d";

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

#ifdef Q_OS_UNIX
void makeDirectory(const QString& path, mode_t mode) {
    QVERIFY(::mkdir(QFile::encodeName(path).constData(), mode) == 0);
    QVERIFY(::chmod(QFile::encodeName(path).constData(), mode) == 0);
}

void writeFile(const QString& path, const QByteArray& contents, mode_t mode) {
    const auto encodedPath = QFile::encodeName(path);
    const int descriptor = ::open(
        encodedPath.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    QVERIFY(descriptor >= 0);
    QCOMPARE(
        ::write(descriptor, contents.constData(), static_cast<size_t>(contents.size())),
        static_cast<ssize_t>(contents.size()));
    QVERIFY(::close(descriptor) == 0);
    QVERIFY(::chmod(encodedPath.constData(), mode) == 0);
}

struct stat fileStatus(const QString& path) {
    struct stat status {};
    if (::lstat(QFile::encodeName(path).constData(), &status) != 0) {
        status.st_mode = 0;
    }
    return status;
}

void verifyPrivateBoundedLog(const QString& path, bool requireContent = true) {
    const auto status = fileStatus(path);
    QVERIFY(S_ISREG(status.st_mode));
    QCOMPARE(status.st_uid, ::geteuid());
    QCOMPARE(status.st_nlink, nlink_t{1});
    QCOMPARE(status.st_mode & 07777, mode_t{0600});
    if (requireContent) {
        QVERIFY(status.st_size > 0);
    }
    QVERIFY(status.st_size <= kTestMaximumBytes);
}
#endif

struct ProbeResult {
    QProcess::ExitStatus exitStatus{QProcess::NormalExit};
    int exitCode{-1};
    QByteArray standardError;
    qint64 elapsedMilliseconds{-1};
};

struct WarningOnDestruction final {
    ~WarningOnDestruction() {
        qWarning("%s", kSecret);
    }
};

ProbeResult runProbe(const QString& logDirectory, const QString& mode) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE"), mode);
    environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE_DIRECTORY"), logDirectory);
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QElapsedTimer elapsed;
    elapsed.start();
    process.start();
    if (!process.waitForStarted(2'000) || !process.waitForFinished(3'000)) {
        process.kill();
        process.waitForFinished(1'000);
    }
    return {
        process.exitStatus(),
        process.exitCode(),
        process.readAllStandardError(),
        elapsed.elapsed(),
    };
}

std::unique_ptr<QProcess> startWaitingProbe(
    const QString& logDirectory, const QString& mode, const QString& triggerPath) {
    auto process = std::make_unique<QProcess>();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE"), mode);
    environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE_DIRECTORY"), logDirectory);
    environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE_TRIGGER"), triggerPath);
    process->setProcessEnvironment(environment);
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start();
    return process;
}

bool waitForReady(QProcess& process) {
    QByteArray output;
    QElapsedTimer wait;
    wait.start();
    while (!output.contains("READY\n") && wait.elapsed() < 3'000) {
        process.waitForReadyRead(100);
        output.append(process.readAllStandardOutput());
    }
    return output == QByteArray("READY\n");
}

int runLoggingProbe() {
    const auto directory = qEnvironmentVariable("ZENPDF_LOGGING_PROBE_DIRECTORY");
    const auto mode = qEnvironmentVariable("ZENPDF_LOGGING_PROBE");
#ifdef Q_OS_UNIX
    if (mode.startsWith(QStringLiteral("fatal"))) {
        const rlimit disabledCoreDump{0, 0};
        if (::setrlimit(RLIMIT_CORE, &disabledCoreDump) != 0) {
            return 90;
        }
    }
#endif
    Logging::install(directory, kTestMaximumBytes);
    if (mode.endsWith(QStringLiteral("-contention"))) {
        std::fputs("READY\n", stdout);
        std::fflush(stdout);
        const auto triggerPath = qEnvironmentVariable("ZENPDF_LOGGING_PROBE_TRIGGER");
        QElapsedTimer wait;
        wait.start();
        while (!QFileInfo::exists(triggerPath) && wait.elapsed() < 5'000) {
            QThread::msleep(10);
        }
        if (!QFileInfo::exists(triggerPath)) {
            Logging::shutdown();
            return 92;
        }
        if (mode.startsWith(QStringLiteral("fatal"))) {
            qFatal("%s", kSecret);
        }
        qWarning("%s", kSecret);
        Logging::shutdown();
        return 0;
    }
    if (mode == QStringLiteral("fatal")) {
        qFatal("%s", kSecret);
    }
    if (mode == QStringLiteral("shared")) {
        for (int index = 0; index < 10; ++index) {
            qWarning("%s", kSecret);
        }
        std::fputs("READY\n", stdout);
        std::fflush(stdout);
        const auto releasePath = qEnvironmentVariable("ZENPDF_LOGGING_PROBE_RELEASE");
        QElapsedTimer wait;
        wait.start();
        while (!QFileInfo::exists(releasePath) && wait.elapsed() < 5'000) {
            QThread::msleep(10);
        }
        Logging::shutdown();
        return QFileInfo::exists(releasePath) ? 0 : 91;
    }
    if (mode == QStringLiteral("destructor")) {
        {
            WarningOnDestruction owner;
            Q_UNUSED(owner);
        }
        Logging::shutdown();
        return 0;
    }
    qWarning("%s", kSecret);
    Logging::shutdown();
    return 0;
}
}

class LoggingTest final : public QObject {
    Q_OBJECT

private slots:
    void createsPrivateDirectoryAndLog();
    void clampsRequestedMaximumToProductCeiling();
    void boundsProtectsAndSanitizesActiveAndRotatedLogs();
    void removesOversizedPrivateLogsBeforeWriting();
    void rejectsHostileLogChildren_data();
    void rejectsHostileLogChildren();
    void failsClosedWhenRotationTargetBecomesAnAlias();
    void keepsSanitizingWhenLogSetupFails();
    void fatalChildIsBoundedSanitizedAndPrompt();
    void concurrentProcessesShareOneBoundedRotationSet();
    void setupContentionIsBoundedAndRecovers();
    void eventContentionIsBoundedAndRecovers_data();
    void eventContentionIsBoundedAndRecovers();
    void sanitizesWarningsDuringOwnerDestruction();
};

void LoggingTest::createsPrivateDirectoryAndLog() {
#ifndef Q_OS_UNIX
    QSKIP("Unix private-mode contract");
#else
    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    const auto logDirectory = parent.filePath(QStringLiteral("private/logs"));

    Logging::install(logDirectory, kTestMaximumBytes);
    qInfo("Starting ZenPDF Desktop");
    Logging::shutdown();

    const auto directoryStatus = fileStatus(logDirectory);
    QVERIFY(S_ISDIR(directoryStatus.st_mode));
    QCOMPARE(directoryStatus.st_uid, ::geteuid());
    QCOMPARE(directoryStatus.st_mode & 07777, mode_t{0700});
    const auto activePath = QDir(logDirectory).filePath(QStringLiteral("zenpdf.log"));
    verifyPrivateBoundedLog(activePath);
    QCOMPARE(readFile(activePath).count("application-start"), 1);
#endif
}

void LoggingTest::clampsRequestedMaximumToProductCeiling() {
#ifndef Q_OS_UNIX
    QSKIP("Unix descriptor hardening contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    writeFile(activePath, QByteArray(Logging::defaultMaximumLogBytes, 'p'), 0600);

    Logging::install(directory.path(), 8 * Logging::defaultMaximumLogBytes);
    qWarning("%s", kSecret);
    Logging::shutdown();

    const auto rotatedPath = activePath + QStringLiteral(".1");
    QCOMPARE(QFileInfo(rotatedPath).size(), Logging::defaultMaximumLogBytes);
    QVERIFY(QFileInfo(activePath).size() > 0);
    QVERIFY(QFileInfo(activePath).size() <= Logging::defaultMaximumLogBytes);
    QVERIFY(!readFile(activePath).contains(kSecret));
#endif
}

void LoggingTest::boundsProtectsAndSanitizesActiveAndRotatedLogs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString oversizedMessage(20'000, QChar{u'x'});
    Logging::install(directory.path(), kTestMaximumBytes);
    for (int index = 0; index < 800; ++index) {
        qInfo().noquote() << index << oversizedMessage;
    }
    Logging::shutdown();

    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    const auto rotatedPath = activePath + QStringLiteral(".1");
    for (const auto& path : {activePath, rotatedPath}) {
        QVERIFY2(QFileInfo::exists(path), qPrintable(QStringLiteral("Missing expected log %1").arg(path)));
#ifdef Q_OS_UNIX
        verifyPrivateBoundedLog(path);
#else
        const QFileInfo info(path);
        QVERIFY(info.size() > 0);
        QVERIFY(info.size() <= kTestMaximumBytes);
#endif
        const auto contents = readFile(path);
        QVERIFY(!contents.contains(QByteArray(128, 'x')));
        QVERIFY(contents.contains("message-suppressed"));
    }
    QCOMPARE(
        QDir(directory.path()).entryList(QDir::Files | QDir::NoDotAndDotDot),
        QStringList({QStringLiteral("zenpdf.log"), QStringLiteral("zenpdf.log.1")}));
}

void LoggingTest::removesOversizedPrivateLogsBeforeWriting() {
#ifndef Q_OS_UNIX
    QSKIP("Unix descriptor hardening contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    const auto rotatedPath = activePath + QStringLiteral(".1");
    const QByteArray oversized(kTestMaximumBytes + 1, 'o');
    writeFile(activePath, oversized, 0600);
    writeFile(rotatedPath, oversized, 0600);

    Logging::install(directory.path(), kTestMaximumBytes);
    qInfo("fresh bounded event");
    Logging::shutdown();

    verifyPrivateBoundedLog(activePath);
    QVERIFY(!readFile(activePath).contains(QByteArray(128, 'o')));
    QVERIFY(!QFileInfo::exists(rotatedPath));
#endif
}

void LoggingTest::rejectsHostileLogChildren_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("kind");
    for (const auto& name : {QStringLiteral("zenpdf.log"), QStringLiteral("zenpdf.log.1")}) {
        QTest::newRow(qPrintable(name + QStringLiteral("-symlink"))) << name << QStringLiteral("symlink");
        QTest::newRow(qPrintable(name + QStringLiteral("-hardlink"))) << name << QStringLiteral("hardlink");
        QTest::newRow(qPrintable(name + QStringLiteral("-fifo"))) << name << QStringLiteral("fifo");
        QTest::newRow(qPrintable(name + QStringLiteral("-unsafe-mode"))) << name << QStringLiteral("unsafe-mode");
        QTest::newRow(qPrintable(name + QStringLiteral("-unwritable"))) << name << QStringLiteral("unwritable");
    }
}

void LoggingTest::rejectsHostileLogChildren() {
#ifndef Q_OS_UNIX
    QSKIP("Unix descriptor hardening contract");
#else
    QFETCH(QString, name);
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto logDirectory = directory.filePath(QStringLiteral("logs"));
    const auto childPath = QDir(logDirectory).filePath(name);
    const auto targetPath = directory.filePath(QStringLiteral("target"));
    const QByteArray sentinel(kSecret);
    makeDirectory(logDirectory, 0700);
    writeFile(targetPath, sentinel, 0600);

    if (kind == QStringLiteral("symlink")) {
        QVERIFY(::symlink(
                    QFile::encodeName(targetPath).constData(),
                    QFile::encodeName(childPath).constData()) == 0);
    } else if (kind == QStringLiteral("hardlink")) {
        QVERIFY(::link(
                    QFile::encodeName(targetPath).constData(),
                    QFile::encodeName(childPath).constData()) == 0);
    } else if (kind == QStringLiteral("fifo")) {
        QVERIFY(::mkfifo(QFile::encodeName(childPath).constData(), 0600) == 0);
    } else {
        writeFile(childPath, sentinel, kind == QStringLiteral("unsafe-mode") ? 0666 : 0400);
    }

    const auto before = fileStatus(childPath);
    QElapsedTimer elapsed;
    elapsed.start();
    Logging::install(logDirectory, kTestMaximumBytes);
    qWarning("%s", kSecret);
    Logging::shutdown();
    QVERIFY(elapsed.elapsed() < 1'000);

    const auto after = fileStatus(childPath);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_nlink, before.st_nlink);
    QCOMPARE(readFile(targetPath), sentinel);
    if (kind != QStringLiteral("fifo") && kind != QStringLiteral("symlink")) {
        QCOMPARE(readFile(childPath), sentinel);
    }
    const auto entries = QDir(logDirectory).entryList(
        QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    QCOMPARE(entries, QStringList({name}));
#endif
}

void LoggingTest::failsClosedWhenRotationTargetBecomesAnAlias() {
#ifndef Q_OS_UNIX
    QSKIP("Unix descriptor hardening contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto targetPath = directory.filePath(QStringLiteral("target"));
    const auto rotatedPath = directory.filePath(QStringLiteral("zenpdf.log.1"));
    const QByteArray sentinel(kSecret);
    writeFile(targetPath, sentinel, 0600);

    Logging::install(directory.path(), kTestMaximumBytes);
    QVERIFY(::symlink(
                QFile::encodeName(targetPath).constData(),
                QFile::encodeName(rotatedPath).constData()) == 0);
    for (int index = 0; index < 800; ++index) {
        qInfo("rotation event %d %s", index, kSecret);
    }
    Logging::shutdown();

    QCOMPARE(readFile(targetPath), sentinel);
    QVERIFY(S_ISLNK(fileStatus(rotatedPath).st_mode));
    verifyPrivateBoundedLog(directory.filePath(QStringLiteral("zenpdf.log")), false);
#endif
}

void LoggingTest::keepsSanitizingWhenLogSetupFails() {
    const auto result = runProbe(
        QStringLiteral("/proc/zenpdf-l005-unavailable/logs"), QStringLiteral("warning"));
    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);
    QVERIFY(result.elapsedMilliseconds < 3'000);
    QVERIFY(result.standardError.size() < 1024);
    QVERIFY(!result.standardError.contains(kSecret));
    QCOMPARE(result.standardError.count("message-suppressed"), 1);
}

void LoggingTest::fatalChildIsBoundedSanitizedAndPrompt() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto fatalResult = runProbe(directory.path(), QStringLiteral("fatal"));
    QCOMPARE(fatalResult.exitStatus, QProcess::CrashExit);
    QVERIFY(fatalResult.elapsedMilliseconds < 3'000);
    QVERIFY(fatalResult.standardError.size() < 1024);
    QVERIFY(!fatalResult.standardError.contains(kSecret));
    QCOMPARE(fatalResult.standardError.count("[fatal] zenpdf: message-suppressed"), 1);

    const auto restartResult = runProbe(directory.path(), QStringLiteral("warning"));
    QCOMPARE(restartResult.exitStatus, QProcess::NormalExit);
    QCOMPARE(restartResult.exitCode, 0);
    QVERIFY(restartResult.elapsedMilliseconds < 3'000);
    QVERIFY(restartResult.standardError.size() < 1024);
    QVERIFY(!restartResult.standardError.contains(kSecret));
    QCOMPARE(restartResult.standardError.count("[warning] zenpdf: message-suppressed"), 1);

    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    const auto contents = readFile(activePath);
    QVERIFY(!contents.contains(kSecret));
    QCOMPARE(contents.count("[fatal] zenpdf: message-suppressed"), 1);
    QCOMPARE(contents.count("[warning] zenpdf: message-suppressed"), 1);
    QCOMPARE(contents.count('\n'), 2);
    QCOMPARE(
        QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
        QStringList({QStringLiteral("zenpdf.log")}));
#ifdef Q_OS_UNIX
    const auto directoryStatus = fileStatus(directory.path());
    QVERIFY(S_ISDIR(directoryStatus.st_mode));
    QCOMPARE(directoryStatus.st_uid, ::geteuid());
    QCOMPARE(directoryStatus.st_mode & 07777, mode_t{0700});
    verifyPrivateBoundedLog(activePath);
#endif
}

void LoggingTest::concurrentProcessesShareOneBoundedRotationSet() {
#ifndef Q_OS_UNIX
    QSKIP("Unix interprocess-locking contract");
#else
    constexpr int processCount = 32;
    constexpr int eventsPerProcess = 10;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto releasePath = directory.filePath(QStringLiteral("release"));
    std::vector<std::unique_ptr<QProcess>> processes;
    processes.reserve(processCount);

    for (int index = 0; index < processCount; ++index) {
        auto process = std::make_unique<QProcess>();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE"), QStringLiteral("shared"));
        environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE_DIRECTORY"), directory.path());
        environment.insert(QStringLiteral("ZENPDF_LOGGING_PROBE_RELEASE"), releasePath);
        process->setProcessEnvironment(environment);
        process->setProgram(QCoreApplication::applicationFilePath());
        process->setProcessChannelMode(QProcess::SeparateChannels);
        process->start();
        QVERIFY2(process->waitForStarted(2'000), qPrintable(process->errorString()));
        processes.push_back(std::move(process));
    }

    for (const auto& process : processes) {
        QByteArray ready;
        QElapsedTimer wait;
        wait.start();
        while (!ready.contains("READY\n") && wait.elapsed() < 5'000) {
            process->waitForReadyRead(100);
            ready.append(process->readAllStandardOutput());
        }
        QCOMPARE(ready, QByteArray("READY\n"));

        const QDir descriptors(
            QStringLiteral("/proc/%1/fd").arg(process->processId()));
        for (const auto& entry : descriptors.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
            const auto target = QFileInfo(descriptors.filePath(entry)).symLinkTarget();
            QVERIFY2(
                !(target.contains(QStringLiteral("zenpdf.log")) &&
                  target.endsWith(QStringLiteral(" (deleted)"))),
                qPrintable(target));
        }
    }

    writeFile(releasePath, QByteArray("release"), 0600);
    for (const auto& process : processes) {
        QVERIFY(process->waitForFinished(3'000));
        QCOMPARE(process->exitStatus(), QProcess::NormalExit);
        QCOMPARE(process->exitCode(), 0);
        const auto standardError = process->readAllStandardError();
        QVERIFY(!standardError.contains(kSecret));
        QCOMPARE(standardError.count("message-suppressed"), eventsPerProcess);
    }

    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    const auto rotatedPath = activePath + QStringLiteral(".1");
    verifyPrivateBoundedLog(activePath);
    verifyPrivateBoundedLog(rotatedPath);
    const auto retained = readFile(rotatedPath) + readFile(activePath);
    QVERIFY(!retained.contains(kSecret));
    QCOMPARE(retained.count("message-suppressed"), processCount * eventsPerProcess);
    QCOMPARE(retained.count('\n'), processCount * eventsPerProcess);
    QCOMPARE(
        QDir(directory.path()).entryList(QDir::Files | QDir::NoDotAndDotDot),
        QStringList({QStringLiteral("release"), QStringLiteral("zenpdf.log"),
                     QStringLiteral("zenpdf.log.1")}));
#endif
}

void LoggingTest::setupContentionIsBoundedAndRecovers() {
#ifndef Q_OS_UNIX
    QSKIP("Unix interprocess-locking contract");
#else
    QTemporaryDir parent;
    QTemporaryDir control;
    QVERIFY(parent.isValid());
    QVERIFY(control.isValid());
    const auto logDirectory = parent.filePath(QStringLiteral("logs"));
    const auto targetPath = parent.filePath(QStringLiteral("target"));
    const auto triggerPath = control.filePath(QStringLiteral("trigger"));
    makeDirectory(logDirectory, 0700);
    writeFile(targetPath, QByteArray(kSecret), 0600);
    const int lockDescriptor = ::open(
        QFile::encodeName(logDirectory).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    QVERIFY(lockDescriptor >= 0);
    QVERIFY(::flock(lockDescriptor, LOCK_EX | LOCK_NB) == 0);

    QElapsedTimer elapsed;
    elapsed.start();
    auto process =
        startWaitingProbe(logDirectory, QStringLiteral("setup-contention"), triggerPath);
    QVERIFY2(process->waitForStarted(2'000), qPrintable(process->errorString()));
    QVERIFY(waitForReady(*process));
    QVERIFY(elapsed.elapsed() < 1'000);
    QCOMPARE(readFile(targetPath), QByteArray(kSecret));
    QCOMPARE(
        QDir(logDirectory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
        QStringList{});

    QVERIFY(::flock(lockDescriptor, LOCK_UN) == 0);
    QVERIFY(::close(lockDescriptor) == 0);
    writeFile(triggerPath, QByteArray("trigger"), 0600);
    QVERIFY(process->waitForFinished(1'000));
    QCOMPARE(process->exitStatus(), QProcess::NormalExit);
    QCOMPARE(process->exitCode(), 0);
    const auto standardError = process->readAllStandardError();
    QVERIFY(!standardError.contains(kSecret));
    QCOMPARE(standardError.count("message-suppressed"), 1);
    const auto activePath = QDir(logDirectory).filePath(QStringLiteral("zenpdf.log"));
    verifyPrivateBoundedLog(activePath);
    const auto contents = readFile(activePath);
    QVERIFY(!contents.contains(kSecret));
    QCOMPARE(contents.count("message-suppressed"), 1);
#endif
}

void LoggingTest::eventContentionIsBoundedAndRecovers_data() {
    QTest::addColumn<QString>("mode");
    QTest::addColumn<bool>("crashes");
    QTest::newRow("runtime") << QStringLiteral("runtime-contention") << false;
    QTest::newRow("fatal") << QStringLiteral("fatal-contention") << true;
}

void LoggingTest::eventContentionIsBoundedAndRecovers() {
#ifndef Q_OS_UNIX
    QSKIP("Unix interprocess-locking contract");
#else
    QFETCH(QString, mode);
    QFETCH(bool, crashes);
    QTemporaryDir directory;
    QTemporaryDir control;
    QVERIFY(directory.isValid());
    QVERIFY(control.isValid());
    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    const auto targetPath = directory.filePath(QStringLiteral("target"));
    const QByteArray baseline("baseline\n");
    writeFile(activePath, baseline, 0600);
    writeFile(targetPath, QByteArray(kSecret), 0600);
    const auto triggerPath = control.filePath(QStringLiteral("trigger"));
    auto process = startWaitingProbe(directory.path(), mode, triggerPath);
    QVERIFY2(process->waitForStarted(2'000), qPrintable(process->errorString()));
    QVERIFY(waitForReady(*process));

    const int lockDescriptor = ::open(
        QFile::encodeName(directory.path()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    QVERIFY(lockDescriptor >= 0);
    QVERIFY(::flock(lockDescriptor, LOCK_EX | LOCK_NB) == 0);
    QElapsedTimer elapsed;
    elapsed.start();
    writeFile(triggerPath, QByteArray("trigger"), 0600);
    QVERIFY(process->waitForFinished(1'000));
    QVERIFY(elapsed.elapsed() < 1'000);
    QCOMPARE(process->exitStatus(), crashes ? QProcess::CrashExit : QProcess::NormalExit);
    if (!crashes) {
        QCOMPARE(process->exitCode(), 0);
    }
    const auto standardError = process->readAllStandardError();
    QVERIFY(!standardError.contains(kSecret));
    QCOMPARE(standardError.count("message-suppressed"), 1);
    QCOMPARE(readFile(activePath), baseline);
    QCOMPARE(readFile(targetPath), QByteArray(kSecret));
    QVERIFY(!QFileInfo::exists(activePath + QStringLiteral(".1")));

    QVERIFY(::flock(lockDescriptor, LOCK_UN) == 0);
    QVERIFY(::close(lockDescriptor) == 0);
    const auto recovered = runProbe(directory.path(), QStringLiteral("warning"));
    QCOMPARE(recovered.exitStatus, QProcess::NormalExit);
    QCOMPARE(recovered.exitCode, 0);
    const auto contents = readFile(activePath);
    QVERIFY(contents.startsWith(baseline));
    QVERIFY(!contents.mid(baseline.size()).contains(kSecret));
    QCOMPARE(contents.count("message-suppressed"), 1);
    QCOMPARE(readFile(targetPath), QByteArray(kSecret));
    verifyPrivateBoundedLog(activePath);
#endif
}

void LoggingTest::sanitizesWarningsDuringOwnerDestruction() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto result = runProbe(directory.path(), QStringLiteral("destructor"));
    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);
    QVERIFY(result.elapsedMilliseconds < 3'000);
    QVERIFY(!result.standardError.contains(kSecret));
    QCOMPARE(result.standardError.count("[warning] zenpdf: message-suppressed"), 1);

    const auto contents = readFile(directory.filePath(QStringLiteral("zenpdf.log")));
    QVERIFY(!contents.contains(kSecret));
    QCOMPARE(contents.count("[warning] zenpdf: message-suppressed"), 1);
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (qEnvironmentVariableIsSet("ZENPDF_LOGGING_PROBE")) {
        return runLoggingProbe();
    }
    LoggingTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "LoggingTest.moc"
