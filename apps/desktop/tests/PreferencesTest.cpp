#include "Preferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QCryptographicHash>
#include <QDateTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>
#include <memory>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

class PreferencesTest final : public QObject {
    Q_OBJECT

private slots:
    void missingUsesDefaultsWithoutCreatingFile();
    void savesAndReloadsCurrentSchema();
    void migratesVersionlessLegacyValues();
    void rejectsFutureSchema();
    void preservesRecognizableFutureSchemaWithCorruptTail();
    void rejectsMalformedAndWrongTypeValues_data();
    void rejectsMalformedAndWrongTypeValues();
    void rejectsOversizedValuesAndFiles();
    void boundedQtLockContentionAndRecovery();
    void rejectsEncodedOverflowWithoutReplacingPriorState();
    void concurrentProcessesPublishCompleteSnapshots();
#ifdef Q_OS_UNIX
    void rejectsDanglingAliases();
    void validatesQtLockSidecars_data();
    void validatesQtLockSidecars();
    void recoversAbandonedRecognizableQtLock();
    void recoversQtLockWriterGrammar_data();
    void recoversQtLockWriterGrammar();
    void preservesActiveQtLockWriterGrammar();
    void preservesForeignQtLockWriterGrammar_data();
    void preservesForeignQtLockWriterGrammar();
    void importAndSavePublishOneCompleteCurrentSnapshot();
    void boundedLegacyWriterContentionAndRecovery();
    void missingLegacyFinalHonorsActiveWriter();
    void missingLegacyParentUsesDefaultsWithoutCreating();
    void missingLegacyFinalRejectsHostileSidecars_data();
    void missingLegacyFinalRejectsHostileSidecars();
    void missingLegacyFinalRecoversAbandonedSidecar();
    void saveRejectsUnresolvedLegacy_data();
    void saveRejectsUnresolvedLegacy();
    void rejectsUnsafeLegacyParents_data();
    void rejectsUnsafeLegacyParents();
    void rejectsForeignLegacyParentInspection();
    void validatesLegacyLockSidecars_data();
    void validatesLegacyLockSidecars();
    void recoversAbandonedLegacyLock();
    void publicationInspectionFailurePreservesPriorSnapshot();
    void rejectsUnsafeLegacyLeaves_data();
    void rejectsUnsafeLegacyLeaves();
    void boundedCoordinatorContentionAndRecovery();
    void rejectsCoordinatorWithoutOwnerRead();
    void repairsSafePrivateLeafModes();
    void rejectsUnsafeUnixLeaves_data();
    void rejectsUnsafeUnixLeaves();
#endif
};

namespace {
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
}

void writeSettings(
    const QString& path,
    const QVariant& geometry,
    const QVariant& state,
    const std::optional<int> schema = std::nullopt) {
    QSettings settings(path, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    if (schema.has_value()) {
        settings.setValue(QStringLiteral("schema/version"), *schema);
    }
    settings.setValue(QStringLiteral("window/geometry"), geometry);
    settings.setValue(QStringLiteral("window/state"), state);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
}

void verifyPathFreeError(const QString& error, const QString& path) {
    QVERIFY(!error.isEmpty());
    QVERIFY(error.size() <= 256);
    QVERIFY(!error.contains(path));
    QVERIFY(!error.contains(QFileInfo(path).fileName()));
}

QByteArray fileHash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
}

void PreferencesTest::missingUsesDefaultsWithoutCreatingFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("state/preferences.ini"));
    Preferences preferences(path);
    WindowPreferences loaded{QByteArrayLiteral("old"), QByteArrayLiteral("old")};
    QString error;

    QVERIFY(preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    QVERIFY(!QFileInfo::exists(path));
}

void PreferencesTest::savesAndReloadsCurrentSchema() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    const WindowPreferences expected{QByteArrayLiteral("geometry"), QByteArrayLiteral("state")};
    QString error;

    QVERIFY(preferences.saveWindowPreferences(expected, &error));
    QVERIFY(error.isEmpty());
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded, &error));
    QCOMPARE(loaded.geometry, expected.geometry);
    QCOMPARE(loaded.state, expected.state);
    QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("schema/version")).toInt(), Preferences::currentSchemaVersion);
}

void PreferencesTest::migratesVersionlessLegacyValues() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    writeSettings(
        legacyPath,
        QByteArrayLiteral("legacy-geometry"),
        QByteArrayLiteral("legacy-state"));
    const auto encodedLegacyDirectory = QFile::encodeName(QFileInfo(legacyPath).absolutePath());
    const auto encodedLegacyPath = QFile::encodeName(legacyPath);
#ifdef Q_OS_UNIX
    QVERIFY(::chmod(encodedLegacyDirectory.constData(), 0755) == 0);
    QVERIFY(::chmod(encodedLegacyPath.constData(), 0644) == 0);
    struct stat legacyBefore {};
    QVERIFY(::lstat(encodedLegacyPath.constData(), &legacyBefore) == 0);
#endif
    const auto inventoryBefore = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    const auto legacyHash = fileHash(legacyPath);
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;

    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("legacy-geometry"));
    QCOMPARE(loaded.state, QByteArrayLiteral("legacy-state"));
    QCOMPARE(fileHash(legacyPath), legacyHash);
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventoryBefore);
#ifdef Q_OS_UNIX
    struct stat legacyAfter {};
    QVERIFY(::lstat(encodedLegacyPath.constData(), &legacyAfter) == 0);
    QCOMPARE(legacyAfter.st_dev, legacyBefore.st_dev);
    QCOMPARE(legacyAfter.st_ino, legacyBefore.st_ino);
    QCOMPARE(legacyAfter.st_mode, legacyBefore.st_mode);
    QCOMPARE(legacyAfter.st_size, legacyBefore.st_size);
    QCOMPARE(legacyAfter.st_mtim.tv_sec, legacyBefore.st_mtim.tv_sec);
    QCOMPARE(legacyAfter.st_mtim.tv_nsec, legacyBefore.st_mtim.tv_nsec);
    QCOMPARE(legacyAfter.st_ctim.tv_sec, legacyBefore.st_ctim.tv_sec);
    QCOMPARE(legacyAfter.st_ctim.tv_nsec, legacyBefore.st_ctim.tv_nsec);
#endif
    QVERIFY(QFileInfo::exists(path));
    QSettings migrated(path, QSettings::IniFormat);
    QCOMPARE(migrated.value(QStringLiteral("schema/version")).toInt(), Preferences::currentSchemaVersion);
}

void PreferencesTest::preservesRecognizableFutureSchemaWithCorruptTail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    const QByteArray bytes = QByteArrayLiteral(
        "[schema]\nversion=2147483647\n[window]\ngeometry=@ByteArray(ok)\n[broken\n");
    writeBytes(path, bytes);
    const auto before = fileHash(path);
    Preferences preferences(path);
    WindowPreferences loaded;
    QString error;

    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    verifyPathFreeError(error, path);
    error.clear();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("replacement"), QByteArrayLiteral("replacement")}, &error));
    verifyPathFreeError(error, path);
    QCOMPARE(fileHash(path), before);
}

void PreferencesTest::rejectsFutureSchema() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    writeSettings(
        path,
        QByteArrayLiteral("geometry"),
        QByteArrayLiteral("state"),
        Preferences::currentSchemaVersion + 1);
    Preferences preferences(path);
    WindowPreferences loaded{QByteArrayLiteral("old"), QByteArrayLiteral("old")};
    QString error;
    QFile beforeFile(path);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const auto before = beforeFile.readAll();
    beforeFile.close();

    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    verifyPathFreeError(error, path);
    error.clear();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("new geometry"), QByteArrayLiteral("new state")}, &error));
    verifyPathFreeError(error, path);
    QFile afterFile(path);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterFile.readAll(), before);
}

void PreferencesTest::rejectsMalformedAndWrongTypeValues_data() {
    QTest::addColumn<QByteArray>("contents");
    QTest::newRow("corrupt-section") << QByteArrayLiteral("[General\nwindow\\geometry=@ByteArray(ok)\n");
    QTest::newRow("truncated-byte-array") << QByteArrayLiteral(
        "[schema]\nversion=1\n[window]\ngeometry=@ByteArray(unclosed\nstate=@ByteArray(ok)\n");
    QTest::newRow("wrong-type") << QByteArrayLiteral(
        "[schema]\nversion=1\n[window]\ngeometry=plain-text\nstate=@ByteArray(ok)\n");
}

void PreferencesTest::rejectsMalformedAndWrongTypeValues() {
    QFETCH(QByteArray, contents);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    writeBytes(path, contents);
    Preferences preferences(path);
    WindowPreferences loaded{QByteArrayLiteral("old"), QByteArrayLiteral("old")};
    QString error;

    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    verifyPathFreeError(error, path);
}

void PreferencesTest::rejectsOversizedValuesAndFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    QString error;
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArray(Preferences::maximumValueBytes + 1, 'g'), {}}, &error));
    QVERIFY(!QFileInfo::exists(path));
    verifyPathFreeError(error, path);

    writeSettings(
        path,
        QByteArray(Preferences::maximumValueBytes + 1, 'g'),
        QByteArrayLiteral("state"),
        Preferences::currentSchemaVersion);
    WindowPreferences loaded;
    error.clear();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    verifyPathFreeError(error, path);

    writeBytes(path, QByteArray(Preferences::maximumFileBytes + 1, 'x'));
    const auto before = QFileInfo(path).size();
    error.clear();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QCOMPARE(QFileInfo(path).size(), before);
    verifyPathFreeError(error, path);
}

void PreferencesTest::boundedQtLockContentionAndRecovery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    const WindowPreferences original{QByteArrayLiteral("original"), QByteArrayLiteral("original")};
    QVERIFY(preferences.saveWindowPreferences(original));
    const auto before = fileHash(path);
    QLockFile held(path + QStringLiteral(".lock"));
    held.setStaleLockTime(0);
    QVERIFY(held.tryLock(0));

    QElapsedTimer timer;
    timer.start();
    WindowPreferences loaded;
    QString error;
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, path);
    timer.restart();
    error.clear();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    QVERIFY(timer.elapsed() < 500);
    QCOMPARE(fileHash(path), before);
    verifyPathFreeError(error, path);

    held.unlock();
    const WindowPreferences recovered{QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")};
    QVERIFY(preferences.saveWindowPreferences(recovered));
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, recovered.geometry);
    QCOMPARE(loaded.state, recovered.state);

    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    const auto importedPath = directory.filePath(QStringLiteral("imported/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    QVERIFY(QDir().mkpath(QFileInfo(importedPath).absolutePath()));
    writeSettings(legacyPath, QByteArrayLiteral("legacy"), QByteArrayLiteral("legacy"));
    const auto legacyHash = fileHash(legacyPath);
    QLockFile importLock(importedPath + QStringLiteral(".lock"));
    importLock.setStaleLockTime(0);
    QVERIFY(importLock.tryLock(0));
    Preferences importing(importedPath, legacyPath);
    timer.restart();
    error.clear();
    QVERIFY(!importing.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    QVERIFY(!QFileInfo::exists(importedPath));
    QCOMPARE(fileHash(legacyPath), legacyHash);
    importLock.unlock();
    QVERIFY(importing.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("legacy"));
    QCOMPARE(fileHash(legacyPath), legacyHash);
}

void PreferencesTest::rejectsEncodedOverflowWithoutReplacingPriorState() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    const WindowPreferences original{QByteArrayLiteral("original"), QByteArrayLiteral("original")};
    QVERIFY(preferences.saveWindowPreferences(original));
    const WindowPreferences bounded{
        QByteArray(Preferences::maximumValueBytes, 'a'),
        QByteArray(Preferences::maximumValueBytes, 'a')};
    QVERIFY(preferences.saveWindowPreferences(bounded));
    QVERIFY(QFileInfo(path).size() <= Preferences::maximumFileBytes);
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, bounded.geometry);
    QCOMPARE(loaded.state, bounded.state);
    QVERIFY(preferences.saveWindowPreferences(original));
    const auto priorHash = fileHash(path);

    for (const char byte : {'\\', static_cast<char>(0xff)}) {
        QString error;
        const WindowPreferences escaped{
            QByteArray(Preferences::maximumValueBytes, byte),
            QByteArray(Preferences::maximumValueBytes, byte)};
        QVERIFY(!preferences.saveWindowPreferences(escaped, &error));
        verifyPathFreeError(error, path);
        QCOMPARE(fileHash(path), priorHash);
        QVERIFY(QFileInfo(path).size() <= Preferences::maximumFileBytes);
    }
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, original.geometry);
    QCOMPARE(loaded.state, original.state);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("after-overflow"), QByteArrayLiteral("after-overflow")}));
}

void PreferencesTest::concurrentProcessesPublishCompleteSnapshots() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("initial"), QByteArrayLiteral("initial")}));
    const auto startWriter = [&](const QString& marker) {
        auto process = std::make_unique<QProcess>();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PROBE"), QStringLiteral("1"));
        environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), path);
        environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_MARKER"), marker);
        process->setProcessEnvironment(environment);
        process->setProgram(QCoreApplication::applicationFilePath());
        process->start();
        if (!process->waitForStarted()) {
            return std::unique_ptr<QProcess>{};
        }
        return process;
    };
    auto first = startWriter(QStringLiteral("first"));
    auto second = startWriter(QStringLiteral("second"));
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    QVERIFY(first->waitForFinished(5'000));
    QVERIFY(second->waitForFinished(5'000));
    QCOMPARE(first->exitStatus(), QProcess::NormalExit);
    QCOMPARE(second->exitStatus(), QProcess::NormalExit);
    QCOMPARE(first->exitCode(), 0);
    QCOMPARE(second->exitCode(), 0);

    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QVERIFY(loaded.geometry == QByteArrayLiteral("first") ||
            loaded.geometry == QByteArrayLiteral("second"));
    QCOMPARE(loaded.state, loaded.geometry);
    QVERIFY(QDir(directory.path()).entryList(
        {QStringLiteral("preferences.ini.stage-*")}, QDir::Files).isEmpty());
}

#ifdef Q_OS_UNIX
void PreferencesTest::rejectsDanglingAliases() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto missingTarget = directory.filePath(QStringLiteral("missing-target"));
    const auto finalPath = directory.filePath(QStringLiteral("preferences.ini"));
    const auto encodedTarget = QFile::encodeName(missingTarget);
    const auto encodedFinal = QFile::encodeName(finalPath);
    QVERIFY(::symlink(encodedTarget.constData(), encodedFinal.constData()) == 0);
    Preferences preferences(finalPath);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, finalPath);
    error.clear();
    timer.restart();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("new"), QByteArrayLiteral("new")}, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, finalPath);
    struct stat aliasStatus {};
    QVERIFY(::lstat(encodedFinal.constData(), &aliasStatus) == 0);
    QVERIFY(S_ISLNK(aliasStatus.st_mode));
    QVERIFY(::lstat(encodedTarget.constData(), &aliasStatus) != 0);
    QCOMPARE(errno, ENOENT);

    const auto currentPath = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    const auto encodedLegacy = QFile::encodeName(legacyPath);
    QVERIFY(::symlink(encodedTarget.constData(), encodedLegacy.constData()) == 0);
    Preferences importing(currentPath, legacyPath);
    error.clear();
    timer.restart();
    QVERIFY(!importing.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(::lstat(encodedLegacy.constData(), &aliasStatus) == 0);
    QVERIFY(S_ISLNK(aliasStatus.st_mode));
    QVERIFY(!QFileInfo(currentPath).exists());
}

void PreferencesTest::validatesQtLockSidecars_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("fifo") << QStringLiteral("fifo");
    QTest::newRow("malformed") << QStringLiteral("malformed");
    QTest::newRow("oversize") << QStringLiteral("oversize");
    QTest::newRow("symlink") << QStringLiteral("symlink");
    QTest::newRow("hardlink") << QStringLiteral("hardlink");
    QTest::newRow("unsafe-mode") << QStringLiteral("unsafe-mode");
    QTest::newRow("owner-read-missing") << QStringLiteral("owner-read-missing");
    QTest::newRow("one-field") << QStringLiteral("one-field");
    QTest::newRow("two-field") << QStringLiteral("two-field");
    QTest::newRow("four-field") << QStringLiteral("four-field");
    QTest::newRow("extra-field") << QStringLiteral("extra-field");
    QTest::newRow("empty-pid") << QStringLiteral("empty-pid");
    QTest::newRow("zero-pid") << QStringLiteral("zero-pid");
    QTest::newRow("negative-pid") << QStringLiteral("negative-pid");
    QTest::newRow("plus-pid") << QStringLiteral("plus-pid");
    QTest::newRow("leading-zero-pid") << QStringLiteral("leading-zero-pid");
    QTest::newRow("overflow-pid") << QStringLiteral("overflow-pid");
    QTest::newRow("nondigit-pid") << QStringLiteral("nondigit-pid");
    QTest::newRow("oversized-field") << QStringLiteral("oversized-field");
    QTest::newRow("double-trailing-newline") << QStringLiteral("double-trailing-newline");
    QTest::newRow("nul") << QStringLiteral("nul");
}

void PreferencesTest::validatesQtLockSidecars() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    const auto sidecar = path + QStringLiteral(".lock");
    const auto target = directory.filePath(QStringLiteral("target"));
    const auto encodedSidecar = QFile::encodeName(sidecar);
    const auto encodedTarget = QFile::encodeName(target);
    const QByteArray sentinel = QByteArrayLiteral("lock-sidecar-target");
    writeBytes(target, sentinel);
    if (kind == QStringLiteral("fifo")) {
        QVERIFY(::mkfifo(encodedSidecar.constData(), 0600) == 0);
    } else if (kind == QStringLiteral("symlink")) {
        QVERIFY(::symlink(encodedTarget.constData(), encodedSidecar.constData()) == 0);
    } else if (kind == QStringLiteral("hardlink")) {
        QVERIFY(::link(encodedTarget.constData(), encodedSidecar.constData()) == 0);
    } else {
        QByteArray contents = QByteArrayLiteral("not-a-qt-lock\n");
        if (kind == QStringLiteral("oversize")) {
            contents = QByteArray(4097, 'x');
        } else if (kind == QStringLiteral("one-field")) {
            contents = QByteArrayLiteral("999999999\n");
        } else if (kind == QStringLiteral("two-field")) {
            contents = QByteArrayLiteral("999999999\napplication\n");
        } else if (kind == QStringLiteral("four-field")) {
            contents = QByteArrayLiteral("999999999\napplication\nhost\nmachine\n");
        } else if (kind == QStringLiteral("extra-field")) {
            contents = QByteArrayLiteral("999999999\napplication\nhost\nmachine\nboot\nextra\n");
        } else if (kind == QStringLiteral("empty-pid")) {
            contents = QByteArrayLiteral("\napplication\nhost\n");
        } else if (kind == QStringLiteral("zero-pid")) {
            contents = QByteArrayLiteral("0\napplication\nhost\n");
        } else if (kind == QStringLiteral("negative-pid")) {
            contents = QByteArrayLiteral("-1\napplication\nhost\n");
        } else if (kind == QStringLiteral("plus-pid")) {
            contents = QByteArrayLiteral("+1\napplication\nhost\n");
        } else if (kind == QStringLiteral("leading-zero-pid")) {
            contents = QByteArrayLiteral("01\napplication\nhost\n");
        } else if (kind == QStringLiteral("overflow-pid")) {
            contents = QByteArrayLiteral("9223372036854775808\napplication\nhost\n");
        } else if (kind == QStringLiteral("nondigit-pid")) {
            contents = QByteArrayLiteral("1x\napplication\nhost\n");
        } else if (kind == QStringLiteral("oversized-field")) {
            contents = QByteArrayLiteral("999999999\n") + QByteArray(1025, 'a') +
                QByteArrayLiteral("\nhost\n");
        } else if (kind == QStringLiteral("double-trailing-newline")) {
            contents = QByteArrayLiteral("999999999\napplication\nhost\n\n");
        } else if (kind == QStringLiteral("nul")) {
            contents = QByteArrayLiteral("999999999\napplication\nhost");
            contents.append('\0');
            contents.append('\n');
        }
        writeBytes(sidecar, contents);
        if (kind == QStringLiteral("unsafe-mode")) {
            QVERIFY(::chmod(encodedSidecar.constData(), 0666) == 0);
        } else if (kind == QStringLiteral("owner-read-missing")) {
            writeBytes(
                sidecar,
                QByteArrayLiteral("999999999\napplication\nhost\n\n\n"));
            QVERIFY(::chmod(encodedSidecar.constData(), 0200) == 0);
        }
    }
    struct stat before {};
    QVERIFY(::lstat(encodedSidecar.constData(), &before) == 0);
    const auto sidecarHash = S_ISREG(before.st_mode) ? fileHash(sidecar) : QByteArray{};
    Preferences preferences(path);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, path);
    const QStringList malformedKinds{
        QStringLiteral("malformed"),
        QStringLiteral("one-field"),
        QStringLiteral("two-field"),
        QStringLiteral("four-field"),
        QStringLiteral("extra-field"),
        QStringLiteral("empty-pid"),
        QStringLiteral("zero-pid"),
        QStringLiteral("negative-pid"),
        QStringLiteral("plus-pid"),
        QStringLiteral("leading-zero-pid"),
        QStringLiteral("overflow-pid"),
        QStringLiteral("nondigit-pid"),
        QStringLiteral("oversized-field"),
        QStringLiteral("double-trailing-newline"),
        QStringLiteral("nul"),
    };
    if (malformedKinds.contains(kind)) {
        QCOMPARE(error, QStringLiteral("Preferences have a malformed lock sidecar."));
    }
    error.clear();
    timer.restart();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, path);
    if (malformedKinds.contains(kind)) {
        QCOMPARE(error, QStringLiteral("Preferences have a malformed lock sidecar."));
    }
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(target), QCryptographicHash::hash(sentinel, QCryptographicHash::Sha256));
    struct stat after {};
    QVERIFY(::lstat(encodedSidecar.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_nlink, before.st_nlink);
    if (S_ISREG(before.st_mode)) {
        QCOMPARE(fileHash(sidecar), sidecarHash);
    }
    QVERIFY(QFile::remove(sidecar));
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}));
    WindowPreferences recovered;
    QVERIFY(preferences.loadWindowPreferences(&recovered));
    QCOMPARE(recovered.geometry, QByteArrayLiteral("recovered"));
}

void PreferencesTest::recoversQtLockWriterGrammar_data() {
    QTest::addColumn<QString>("kind");
    QTest::addColumn<int>("trailingNewlines");
    QTest::newRow("three-field-same-host") << QStringLiteral("three-field") << 1;
    QTest::newRow("five-field-no-trailing-newline") << QStringLiteral("five-field") << 0;
    QTest::newRow("five-field-one-trailing-newline") << QStringLiteral("five-field") << 1;
    QTest::newRow("five-field-empty-application") << QStringLiteral("empty-application") << 1;
    QTest::newRow("five-field-empty-host") << QStringLiteral("empty-host") << 1;
    QTest::newRow("five-field-empty-machine-boot") << QStringLiteral("empty-machine-boot") << 1;
    QTest::newRow("renamed-host-current-machine") << QStringLiteral("renamed-host") << 1;
    QTest::newRow("current-machine-previous-boot") << QStringLiteral("previous-boot") << 1;
}

void PreferencesTest::recoversQtLockWriterGrammar() {
    QFETCH(QString, kind);
    QFETCH(int, trailingNewlines);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    qint64 testedPid = QCoreApplication::applicationPid();
    QProcess exitedProcess;
    if (kind != QStringLiteral("previous-boot")) {
        exitedProcess.setProgram(QStringLiteral("/usr/bin/true"));
        exitedProcess.start();
        QVERIFY(exitedProcess.waitForStarted());
        testedPid = exitedProcess.processId();
        QVERIFY(testedPid > 0);
        QVERIFY(exitedProcess.waitForFinished(5'000));
    }
    const auto application = QCoreApplication::applicationName().toUtf8();
    const auto host = QSysInfo::machineHostName().toUtf8();
    const auto machine = QSysInfo::machineUniqueId();
    const auto boot = QSysInfo::bootUniqueId();
    if (qEnvironmentVariableIsSet("ZENPDF_L004_EXPECT_EMPTY_MACHINE_ID")) {
        QVERIFY(machine.isEmpty());
    }
    QByteArrayList fields{QByteArray::number(testedPid)};
    if (kind == QStringLiteral("three-field")) {
        fields << application << host;
    } else if (kind == QStringLiteral("five-field")) {
        fields << application << host << machine << boot;
    } else if (kind == QStringLiteral("empty-application")) {
        fields << QByteArray{} << host << machine << boot;
    } else if (kind == QStringLiteral("empty-host")) {
        fields << application << QByteArray{} << machine << boot;
    } else if (kind == QStringLiteral("empty-machine-boot")) {
        fields << application << host << QByteArray{} << QByteArray{};
    } else if (kind == QStringLiteral("renamed-host")) {
        fields << application << (host + QByteArrayLiteral("-renamed")) << machine << boot;
    } else {
        fields << application << host << machine << (boot + QByteArrayLiteral("-previous"));
    }
    QByteArray lockBytes = fields.join('\n');
    lockBytes.append(QByteArray(trailingNewlines, '\n'));
    const auto lockPath = path + QStringLiteral(".lock");
    writeBytes(lockPath, lockBytes);
    const auto lockHash = fileHash(lockPath);
    const auto encodedLockPath = QFile::encodeName(lockPath);
    struct stat lockStatusBefore {};
    QVERIFY(::lstat(encodedLockPath.constData(), &lockStatusBefore) == 0);
    Preferences preferences(path);
    QString error;

    if (kind == QStringLiteral("renamed-host") && machine.isEmpty()) {
        QVERIFY(!preferences.saveWindowPreferences(
            {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
        QCOMPARE(error, QStringLiteral("Preferences are busy; try again."));
        verifyPathFreeError(error, path);
        QVERIFY(!QFileInfo(path).exists());
        QCOMPARE(fileHash(lockPath), lockHash);
        struct stat lockStatusAfter {};
        QVERIFY(::lstat(encodedLockPath.constData(), &lockStatusAfter) == 0);
        QCOMPARE(lockStatusAfter.st_dev, lockStatusBefore.st_dev);
        QCOMPARE(lockStatusAfter.st_ino, lockStatusBefore.st_ino);
        QCOMPARE(lockStatusAfter.st_mode, lockStatusBefore.st_mode);
        QCOMPARE(lockStatusAfter.st_size, lockStatusBefore.st_size);
        QCOMPARE(lockStatusAfter.st_mtim.tv_sec, lockStatusBefore.st_mtim.tv_sec);
        QCOMPARE(lockStatusAfter.st_mtim.tv_nsec, lockStatusBefore.st_mtim.tv_nsec);
        return;
    }

    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}));
    QVERIFY(!QFileInfo(path + QStringLiteral(".lock")).exists());
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("recovered"));
}

void PreferencesTest::preservesActiveQtLockWriterGrammar() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    const QByteArray lockBytes = QByteArray::number(QCoreApplication::applicationPid()) +
        QByteArrayLiteral("\n") + QCoreApplication::applicationName().toUtf8() +
        QByteArrayLiteral("\n") + QSysInfo::machineHostName().toUtf8() +
        QByteArrayLiteral("\n") + QSysInfo::machineUniqueId() + QByteArrayLiteral("\n") +
        QSysInfo::bootUniqueId() + QByteArrayLiteral("\n");
    const auto lockPath = path + QStringLiteral(".lock");
    writeBytes(lockPath, lockBytes);
    QFile lockFile(lockPath);
    QVERIFY(lockFile.open(QIODevice::ReadWrite));
    QVERIFY(lockFile.setFileTime(
        QDateTime::currentDateTimeUtc().addSecs(-31), QFileDevice::FileModificationTime));
    lockFile.close();
    const auto before = fileHash(lockPath);
    const auto encodedLockPath = QFile::encodeName(lockPath);
    struct stat beforeStatus {};
    QVERIFY(::lstat(encodedLockPath.constData(), &beforeStatus) == 0);
    Preferences preferences(path);
    QString error;

    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    verifyPathFreeError(error, path);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(lockPath), before);
    struct stat afterStatus {};
    QVERIFY(::lstat(encodedLockPath.constData(), &afterStatus) == 0);
    QCOMPARE(afterStatus.st_dev, beforeStatus.st_dev);
    QCOMPARE(afterStatus.st_ino, beforeStatus.st_ino);
    QCOMPARE(afterStatus.st_mode, beforeStatus.st_mode);
    QCOMPARE(afterStatus.st_size, beforeStatus.st_size);
    QCOMPARE(afterStatus.st_mtim.tv_sec, beforeStatus.st_mtim.tv_sec);
    QCOMPARE(afterStatus.st_mtim.tv_nsec, beforeStatus.st_mtim.tv_nsec);
}

void PreferencesTest::preservesForeignQtLockWriterGrammar_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("machine") << QStringLiteral("machine");
    QTest::newRow("host-without-machine") << QStringLiteral("host-without-machine");
}

void PreferencesTest::preservesForeignQtLockWriterGrammar() {
    QFETCH(QString, kind);
    if (qEnvironmentVariableIsSet("ZENPDF_L004_EXPECT_EMPTY_MACHINE_ID")) {
        QVERIFY(QSysInfo::machineUniqueId().isEmpty());
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    QProcess exitedProcess;
    exitedProcess.setProgram(QStringLiteral("/usr/bin/true"));
    exitedProcess.start();
    QVERIFY(exitedProcess.waitForStarted());
    const auto deadPid = exitedProcess.processId();
    QVERIFY(deadPid > 0);
    QVERIFY(exitedProcess.waitForFinished(5'000));
    QByteArrayList fields{
        QByteArray::number(deadPid), QCoreApplication::applicationName().toUtf8()};
    if (kind == QStringLiteral("machine")) {
        fields << QSysInfo::machineHostName().toUtf8()
               << (QSysInfo::machineUniqueId() + QByteArrayLiteral("-foreign"))
               << QSysInfo::bootUniqueId();
    } else {
        fields << (QSysInfo::machineHostName().toUtf8() + QByteArrayLiteral("-foreign"));
    }
    const QByteArray lockBytes = fields.join('\n') + '\n';
    const auto lockPath = path + QStringLiteral(".lock");
    writeBytes(lockPath, lockBytes);
    const auto before = fileHash(lockPath);
    const auto encodedLockPath = QFile::encodeName(lockPath);
    struct stat beforeStatus {};
    QVERIFY(::lstat(encodedLockPath.constData(), &beforeStatus) == 0);
    Preferences preferences(path);
    QString error;

    if (kind == QStringLiteral("machine") && QSysInfo::machineUniqueId().isEmpty()) {
        QVERIFY(preferences.saveWindowPreferences(
            {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo(lockPath).exists());
        WindowPreferences loaded;
        QVERIFY(preferences.loadWindowPreferences(&loaded));
        QCOMPARE(loaded.geometry, QByteArrayLiteral("recovered"));
        return;
    }

    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    QCOMPARE(error, QStringLiteral("Preferences are busy; try again."));
    verifyPathFreeError(error, path);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(lockPath), before);
    struct stat afterStatus {};
    QVERIFY(::lstat(encodedLockPath.constData(), &afterStatus) == 0);
    QCOMPARE(afterStatus.st_dev, beforeStatus.st_dev);
    QCOMPARE(afterStatus.st_ino, beforeStatus.st_ino);
    QCOMPARE(afterStatus.st_mode, beforeStatus.st_mode);
    QCOMPARE(afterStatus.st_size, beforeStatus.st_size);
    QCOMPARE(afterStatus.st_mtim.tv_sec, beforeStatus.st_mtim.tv_sec);
    QCOMPARE(afterStatus.st_mtim.tv_nsec, beforeStatus.st_mtim.tv_nsec);
}

void PreferencesTest::recoversAbandonedRecognizableQtLock() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ZENPDF_L004_LOCK_PROBE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), path);
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.start();
    QVERIFY(process.waitForFinished(5'000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(QFileInfo(path + QStringLiteral(".lock")).exists());
    Preferences preferences(path);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}));
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("recovered"));
}

void PreferencesTest::importAndSavePublishOneCompleteCurrentSnapshot() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    writeSettings(legacyPath, QByteArrayLiteral("legacy"), QByteArrayLiteral("legacy"));
    const auto legacyHash = fileHash(legacyPath);
    const auto startProbe = [&](const QString& mode, const QString& marker) {
        auto process = std::make_unique<QProcess>();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(mode, QStringLiteral("1"));
        environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), path);
        environment.insert(QStringLiteral("ZENPDF_L004_LEGACY_PATH"), legacyPath);
        environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_MARKER"), marker);
        process->setProcessEnvironment(environment);
        process->setProgram(QCoreApplication::applicationFilePath());
        process->start();
        if (!process->waitForStarted()) {
            return std::unique_ptr<QProcess>{};
        }
        return process;
    };
    auto importer = startProbe(
        QStringLiteral("ZENPDF_L004_IMPORT_PROBE"), QStringLiteral("unused"));
    auto writer = startProbe(
        QStringLiteral("ZENPDF_L004_PREFERENCES_PROBE"), QStringLiteral("writer"));
    QVERIFY(importer != nullptr);
    QVERIFY(writer != nullptr);
    QVERIFY(importer->waitForFinished(5'000));
    QVERIFY(writer->waitForFinished(5'000));
    QCOMPARE(importer->exitCode(), 0);
    QCOMPARE(writer->exitCode(), 0);
    Preferences preferences(path);
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("writer"));
    QCOMPARE(loaded.state, loaded.geometry);
    QCOMPARE(fileHash(legacyPath), legacyHash);
}

void PreferencesTest::boundedLegacyWriterContentionAndRecovery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    writeSettings(legacyPath, QByteArrayLiteral("old"), QByteArrayLiteral("old"));
    const auto legacyHash = fileHash(legacyPath);
    QLockFile writerLock(legacyPath + QStringLiteral(".lock"));
    writerLock.setStaleLockTime(0);
    QVERIFY(writerLock.tryLock(0));
    const auto inventoryWhileHeld = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(legacyPath), legacyHash);
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventoryWhileHeld);

    writerLock.unlock();
    writeSettings(legacyPath, QByteArrayLiteral("new"), QByteArrayLiteral("new"));
    const auto recoveryInventory = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    error.clear();
    QVERIFY(preferences.loadWindowPreferences(&loaded, &error));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("new"));
    QCOMPARE(loaded.state, loaded.geometry);
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        recoveryInventory);
}

void PreferencesTest::missingLegacyFinalHonorsActiveWriter() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    const auto serializedPath = directory.filePath(QStringLiteral("serialized.ini"));
    writeSettings(
        serializedPath,
        QByteArrayLiteral("writer-geometry"),
        QByteArrayLiteral("writer-state"));
    QFile serializedFile(serializedPath);
    QVERIFY(serializedFile.open(QIODevice::ReadOnly));
    const auto serialized = serializedFile.readAll();
    serializedFile.close();

    QLockFile writerLock(legacyPath + QStringLiteral(".lock"));
    writerLock.setStaleLockTime(0);
    QVERIFY(writerLock.tryLock(0));
    const auto inventoryWhileHeld = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QVERIFY(!QFileInfo(legacyPath).exists());
    error.clear();
    timer.restart();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("close-geometry"), QByteArrayLiteral("close-state")}, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QVERIFY(!QFileInfo(legacyPath).exists());
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventoryWhileHeld);

    writeBytes(legacyPath, serialized);
    const auto publishedHash = fileHash(legacyPath);
    writerLock.unlock();
    const auto inventoryAfterPublish = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    error.clear();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("must-load-first"), QByteArrayLiteral("must-load-first")}, &error));
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(legacyPath), publishedHash);
    error.clear();
    QVERIFY(preferences.loadWindowPreferences(&loaded, &error));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("writer-geometry"));
    QCOMPARE(loaded.state, QByteArrayLiteral("writer-state"));
    QCOMPARE(fileHash(legacyPath), publishedHash);
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventoryAfterPublish);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("after-import"), QByteArrayLiteral("after-import")}));
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("after-import"));
    QCOMPARE(loaded.state, loaded.geometry);
}

void PreferencesTest::missingLegacyParentUsesDefaultsWithoutCreating() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyParent = directory.filePath(QStringLiteral("absent-legacy"));
    const auto legacyPath = QDir(legacyParent).filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded{QByteArrayLiteral("old"), QByteArrayLiteral("old")};
    QString error;

    QVERIFY(preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    QVERIFY(!QFileInfo(legacyParent).exists());
    QVERIFY(!QFileInfo(path).exists());
}

void PreferencesTest::missingLegacyFinalRejectsHostileSidecars_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("fifo") << QStringLiteral("fifo");
    QTest::newRow("malformed") << QStringLiteral("malformed");
    QTest::newRow("oversize") << QStringLiteral("oversize");
    QTest::newRow("symlink") << QStringLiteral("symlink");
    QTest::newRow("hardlink") << QStringLiteral("hardlink");
    QTest::newRow("unsafe-mode") << QStringLiteral("unsafe-mode");
}

void PreferencesTest::missingLegacyFinalRejectsHostileSidecars() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    const auto sidecar = legacyPath + QStringLiteral(".lock");
    const auto target = directory.filePath(QStringLiteral("target"));
    const auto encodedSidecar = QFile::encodeName(sidecar);
    const auto encodedTarget = QFile::encodeName(target);
    const QByteArray sentinel = QByteArrayLiteral("missing-legacy-lock-target");
    writeBytes(target, sentinel);
    if (kind == QStringLiteral("fifo")) {
        QVERIFY(::mkfifo(encodedSidecar.constData(), 0600) == 0);
    } else if (kind == QStringLiteral("symlink")) {
        QVERIFY(::symlink(encodedTarget.constData(), encodedSidecar.constData()) == 0);
    } else if (kind == QStringLiteral("hardlink")) {
        QVERIFY(::link(encodedTarget.constData(), encodedSidecar.constData()) == 0);
    } else {
        writeBytes(
            sidecar,
            kind == QStringLiteral("oversize") ? QByteArray(4097, 'x')
                                                : QByteArrayLiteral("invalid-lock\n"));
        if (kind == QStringLiteral("unsafe-mode")) {
            QVERIFY(::chmod(encodedSidecar.constData(), 0666) == 0);
        }
    }
    struct stat before {};
    QVERIFY(::lstat(encodedSidecar.constData(), &before) == 0);
    const auto beforeHash = S_ISREG(before.st_mode) ? fileHash(sidecar) : QByteArray{};
    const auto inventory = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QVERIFY(!QFileInfo(legacyPath).exists());
    error.clear();
    timer.restart();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked-save"), QByteArrayLiteral("blocked-save")}, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(target), QCryptographicHash::hash(sentinel, QCryptographicHash::Sha256));
    struct stat after {};
    QVERIFY(::lstat(encodedSidecar.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_nlink, before.st_nlink);
    if (S_ISREG(before.st_mode)) {
        QCOMPARE(fileHash(sidecar), beforeHash);
    }
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventory);
}

void PreferencesTest::missingLegacyFinalRecoversAbandonedSidecar() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ZENPDF_L004_LOCK_PROBE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), legacyPath);
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.start();
    QVERIFY(process.waitForFinished(5'000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(QFileInfo(legacyPath + QStringLiteral(".lock")).exists());
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded{QByteArrayLiteral("old"), QByteArrayLiteral("old")};
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    QVERIFY(!QFileInfo(path).exists());
    QVERIFY(!QFileInfo(legacyPath).exists());
    QVERIFY(!QFileInfo(legacyPath + QStringLiteral(".lock")).exists());
}

void PreferencesTest::saveRejectsUnresolvedLegacy_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("corrupt") << QStringLiteral("corrupt");
    QTest::newRow("future") << QStringLiteral("future");
    QTest::newRow("wrong-type") << QStringLiteral("wrong-type");
}

void PreferencesTest::saveRejectsUnresolvedLegacy() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    if (kind == QStringLiteral("corrupt")) {
        writeBytes(legacyPath, QByteArrayLiteral("[broken\nvalue=1\n"));
    } else if (kind == QStringLiteral("future")) {
        writeSettings(
            legacyPath,
            QByteArrayLiteral("future"),
            QByteArrayLiteral("future"),
            Preferences::currentSchemaVersion + 1);
    } else {
        writeSettings(
            legacyPath,
            QStringLiteral("not-bytes"),
            QByteArrayLiteral("state"));
    }
    const auto legacyHash = fileHash(legacyPath);
    const auto inventory = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(path, legacyPath);
    QString error;

    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("must-not-shadow"), QByteArrayLiteral("must-not-shadow")}, &error));
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(legacyPath), legacyHash);
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventory);
}

void PreferencesTest::rejectsUnsafeLegacyParents_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("writable") << QStringLiteral("writable");
    QTest::newRow("special") << QStringLiteral("special");
    QTest::newRow("symlink") << QStringLiteral("symlink");
    QTest::newRow("wrong-type") << QStringLiteral("wrong-type");
    QTest::newRow("owner-read-missing") << QStringLiteral("owner-read-missing");
    QTest::newRow("owner-write-missing") << QStringLiteral("owner-write-missing");
    QTest::newRow("owner-execute-missing") << QStringLiteral("owner-execute-missing");
}

void PreferencesTest::rejectsUnsafeLegacyParents() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto parentPath = directory.filePath(QStringLiteral("legacy-parent"));
    const auto legacyPath = QDir(parentPath).filePath(QStringLiteral("preferences.ini"));
    const auto encodedParent = QFile::encodeName(parentPath);
    const auto targetParent = directory.filePath(QStringLiteral("target-parent"));
    if (kind == QStringLiteral("symlink")) {
        QVERIFY(QDir().mkpath(targetParent));
        const auto encodedTarget = QFile::encodeName(targetParent);
        QVERIFY(::symlink(encodedTarget.constData(), encodedParent.constData()) == 0);
    } else if (kind == QStringLiteral("wrong-type")) {
        writeBytes(parentPath, QByteArrayLiteral("not-a-directory"));
    } else {
        QVERIFY(QDir().mkpath(parentPath));
        const mode_t mode = kind == QStringLiteral("writable") ? 0777
            : kind == QStringLiteral("special") ? 01700
            : kind == QStringLiteral("owner-read-missing") ? 0300
            : kind == QStringLiteral("owner-write-missing") ? 0500
                                                             : 0600;
        QVERIFY(::chmod(encodedParent.constData(), mode) == 0);
    }
    struct stat before {};
    QVERIFY(::lstat(encodedParent.constData(), &before) == 0);
    const auto inventory = QDir(parentPath).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;
    QString error;
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    verifyPathFreeError(error, legacyPath);
    error.clear();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    struct stat after {};
    QVERIFY(::lstat(encodedParent.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_uid, before.st_uid);
    QCOMPARE(
        QDir(parentPath).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventory);
}

void PreferencesTest::rejectsForeignLegacyParentInspection() {
#ifndef ZENPDF_PREFERENCES_FSTAT_FAULT_LIBRARY
    QSKIP("Legacy-parent ownership interposer is unavailable on this platform.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto parentPath = directory.filePath(QStringLiteral("legacy-foreign-parent"));
    const auto legacyPath = QDir(parentPath).filePath(QStringLiteral("preferences.ini"));
    QVERIFY(QDir().mkpath(parentPath));
    const auto inventory = QDir(parentPath).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(
        QStringLiteral("LD_PRELOAD"),
        QString::fromUtf8(ZENPDF_PREFERENCES_FSTAT_FAULT_LIBRARY));
    environment.insert(QStringLiteral("ZENPDF_L004_FAKE_FOREIGN_LEGACY_PARENT"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_FOREIGN_PARENT_PROBE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), path);
    environment.insert(QStringLiteral("ZENPDF_L004_LEGACY_PATH"), legacyPath);
    environment.insert(
        QStringLiteral("ASAN_OPTIONS"),
        environment.value(QStringLiteral("ASAN_OPTIONS")) +
            QStringLiteral(":verify_asan_link_order=0"));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.start();
    QVERIFY(process.waitForFinished(5'000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(
        QDir(parentPath).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventory);
#endif
}

void PreferencesTest::validatesLegacyLockSidecars_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("fifo") << QStringLiteral("fifo");
    QTest::newRow("malformed") << QStringLiteral("malformed");
    QTest::newRow("oversize") << QStringLiteral("oversize");
    QTest::newRow("symlink") << QStringLiteral("symlink");
    QTest::newRow("hardlink") << QStringLiteral("hardlink");
    QTest::newRow("unsafe-mode") << QStringLiteral("unsafe-mode");
    QTest::newRow("owner-read-missing") << QStringLiteral("owner-read-missing");
}

void PreferencesTest::validatesLegacyLockSidecars() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    writeSettings(legacyPath, QByteArrayLiteral("legacy"), QByteArrayLiteral("legacy"));
    const auto legacyHash = fileHash(legacyPath);
    const auto sidecar = legacyPath + QStringLiteral(".lock");
    const auto target = directory.filePath(QStringLiteral("target"));
    const auto encodedSidecar = QFile::encodeName(sidecar);
    const auto encodedTarget = QFile::encodeName(target);
    const QByteArray sentinel = QByteArrayLiteral("legacy-lock-target");
    writeBytes(target, sentinel);
    if (kind == QStringLiteral("fifo")) {
        QVERIFY(::mkfifo(encodedSidecar.constData(), 0600) == 0);
    } else if (kind == QStringLiteral("symlink")) {
        QVERIFY(::symlink(encodedTarget.constData(), encodedSidecar.constData()) == 0);
    } else if (kind == QStringLiteral("hardlink")) {
        QVERIFY(::link(encodedTarget.constData(), encodedSidecar.constData()) == 0);
    } else {
        writeBytes(
            sidecar,
            kind == QStringLiteral("oversize") ? QByteArray(4097, 'x')
                                                : QByteArrayLiteral("invalid-lock\n"));
        if (kind == QStringLiteral("unsafe-mode")) {
            QVERIFY(::chmod(encodedSidecar.constData(), 0666) == 0);
        } else if (kind == QStringLiteral("owner-read-missing")) {
            writeBytes(
                sidecar,
                QByteArrayLiteral("999999999\napplication\nhost\n\n\n"));
            QVERIFY(::chmod(encodedSidecar.constData(), 0200) == 0);
        }
    }
    struct stat before {};
    QVERIFY(::lstat(encodedSidecar.constData(), &before) == 0);
    const auto sidecarHash = S_ISREG(before.st_mode) ? fileHash(sidecar) : QByteArray{};
    const auto inventory = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(path).exists());
    QCOMPARE(fileHash(legacyPath), legacyHash);
    QCOMPARE(fileHash(target), QCryptographicHash::hash(sentinel, QCryptographicHash::Sha256));
    struct stat after {};
    QVERIFY(::lstat(encodedSidecar.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_nlink, before.st_nlink);
    if (S_ISREG(before.st_mode)) {
        QCOMPARE(fileHash(sidecar), sidecarHash);
    }
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventory);

    QVERIFY(QFile::remove(sidecar));
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("legacy"));
}

void PreferencesTest::recoversAbandonedLegacyLock() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    writeSettings(legacyPath, QByteArrayLiteral("legacy"), QByteArrayLiteral("legacy"));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ZENPDF_L004_LOCK_PROBE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), legacyPath);
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.start();
    QVERIFY(process.waitForFinished(5'000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(QFileInfo(legacyPath + QStringLiteral(".lock")).exists());
    Preferences preferences(path, legacyPath);
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("legacy"));
    QVERIFY(!QFileInfo(legacyPath + QStringLiteral(".lock")).exists());
}

void PreferencesTest::publicationInspectionFailurePreservesPriorSnapshot() {
#ifndef ZENPDF_PREFERENCES_FSTAT_FAULT_LIBRARY
    QSKIP("Publication fstat interposer is unavailable on this platform.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("original"), QByteArrayLiteral("original")}));
    const auto encodedPath = QFile::encodeName(path);
    const auto beforeHash = fileHash(path);
    struct stat before {};
    QVERIFY(::lstat(encodedPath.constData(), &before) == 0);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(
        QStringLiteral("LD_PRELOAD"),
        QString::fromUtf8(ZENPDF_PREFERENCES_FSTAT_FAULT_LIBRARY));
    environment.insert(QStringLiteral("ZENPDF_L004_FAIL_PUBLICATION_FSTAT"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_FSTAT_FAULT_PROBE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), path);
    environment.insert(
        QStringLiteral("ASAN_OPTIONS"),
        environment.value(QStringLiteral("ASAN_OPTIONS")) +
            QStringLiteral(":verify_asan_link_order=0"));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.start();
    QVERIFY(process.waitForFinished(5'000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    struct stat after {};
    QVERIFY(::lstat(encodedPath.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_size, before.st_size);
    QCOMPARE(after.st_mtim.tv_sec, before.st_mtim.tv_sec);
    QCOMPARE(after.st_mtim.tv_nsec, before.st_mtim.tv_nsec);
    QCOMPARE(after.st_ctim.tv_sec, before.st_ctim.tv_sec);
    QCOMPARE(after.st_ctim.tv_nsec, before.st_ctim.tv_nsec);
    QCOMPARE(fileHash(path), beforeHash);

    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}));
    WindowPreferences loaded;
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("recovered"));

    const auto migrationPath = directory.filePath(QStringLiteral("versionless.ini"));
    writeSettings(
        migrationPath,
        QByteArrayLiteral("legacy-geometry"),
        QByteArrayLiteral("legacy-state"));
    const auto encodedMigrationPath = QFile::encodeName(migrationPath);
    const auto migrationHash = fileHash(migrationPath);
    struct stat migrationBefore {};
    QVERIFY(::lstat(encodedMigrationPath.constData(), &migrationBefore) == 0);
    environment.remove(QStringLiteral("ZENPDF_L004_FSTAT_FAULT_PROBE"));
    environment.insert(QStringLiteral("ZENPDF_L004_FSTAT_MIGRATION_PROBE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ZENPDF_L004_PREFERENCES_PATH"), migrationPath);
    QProcess migrationProcess;
    migrationProcess.setProcessEnvironment(environment);
    migrationProcess.setProgram(QCoreApplication::applicationFilePath());
    migrationProcess.start();
    QVERIFY(migrationProcess.waitForFinished(5'000));
    QCOMPARE(migrationProcess.exitStatus(), QProcess::NormalExit);
    QCOMPARE(migrationProcess.exitCode(), 0);
    struct stat migrationAfter {};
    QVERIFY(::lstat(encodedMigrationPath.constData(), &migrationAfter) == 0);
    QCOMPARE(migrationAfter.st_dev, migrationBefore.st_dev);
    QCOMPARE(migrationAfter.st_ino, migrationBefore.st_ino);
    QCOMPARE(migrationAfter.st_mode, migrationBefore.st_mode);
    QCOMPARE(migrationAfter.st_size, migrationBefore.st_size);
    QCOMPARE(migrationAfter.st_mtim.tv_sec, migrationBefore.st_mtim.tv_sec);
    QCOMPARE(migrationAfter.st_mtim.tv_nsec, migrationBefore.st_mtim.tv_nsec);
    QCOMPARE(migrationAfter.st_ctim.tv_sec, migrationBefore.st_ctim.tv_sec);
    QCOMPARE(migrationAfter.st_ctim.tv_nsec, migrationBefore.st_ctim.tv_nsec);
    QCOMPARE(fileHash(migrationPath), migrationHash);
    Preferences migrationPreferences(migrationPath);
    QVERIFY(migrationPreferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("legacy-geometry"));
    QSettings migrated(migrationPath, QSettings::IniFormat);
    QCOMPARE(
        migrated.value(QStringLiteral("schema/version")).toInt(),
        Preferences::currentSchemaVersion);
#endif
}

void PreferencesTest::rejectsUnsafeLegacyLeaves_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("fifo") << QStringLiteral("fifo");
    QTest::newRow("directory") << QStringLiteral("directory");
    QTest::newRow("hardlink") << QStringLiteral("hardlink");
    QTest::newRow("writable") << QStringLiteral("writable");
    QTest::newRow("special") << QStringLiteral("special");
    QTest::newRow("owner-read-missing") << QStringLiteral("owner-read-missing");
}

void PreferencesTest::rejectsUnsafeLegacyLeaves() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto currentPath = directory.filePath(QStringLiteral("current/preferences.ini"));
    const auto legacyPath = directory.filePath(QStringLiteral("legacy/preferences.ini"));
    const auto targetPath = directory.filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
    const auto encodedLegacy = QFile::encodeName(legacyPath);
    const auto encodedTarget = QFile::encodeName(targetPath);
    const QByteArray sentinel = QByteArrayLiteral("legacy-target");
    writeBytes(targetPath, sentinel);
    if (kind == QStringLiteral("fifo")) {
        QVERIFY(::mkfifo(encodedLegacy.constData(), 0600) == 0);
    } else if (kind == QStringLiteral("directory")) {
        QVERIFY(::mkdir(encodedLegacy.constData(), 0700) == 0);
    } else if (kind == QStringLiteral("hardlink")) {
        QVERIFY(::link(encodedTarget.constData(), encodedLegacy.constData()) == 0);
    } else {
        writeSettings(legacyPath, QByteArrayLiteral("legacy"), QByteArrayLiteral("legacy"));
        const mode_t mode = kind == QStringLiteral("writable") ? 0666
            : kind == QStringLiteral("owner-read-missing") ? 0200
                                                           : 04700;
        QVERIFY(::chmod(encodedLegacy.constData(), mode) == 0);
    }
    struct stat before {};
    QVERIFY(::lstat(encodedLegacy.constData(), &before) == 0);
    const auto beforeHash = S_ISREG(before.st_mode) ? fileHash(legacyPath) : QByteArray{};
    const auto inventory = QDir(QFileInfo(legacyPath).absolutePath()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    Preferences preferences(currentPath, legacyPath);
    WindowPreferences loaded;
    QString error;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, legacyPath);
    QVERIFY(!QFileInfo(currentPath).exists());
    QCOMPARE(fileHash(targetPath), QCryptographicHash::hash(sentinel, QCryptographicHash::Sha256));
    struct stat after {};
    QVERIFY(::lstat(encodedLegacy.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QCOMPARE(after.st_nlink, before.st_nlink);
    if (S_ISREG(before.st_mode)) {
        QCOMPARE(fileHash(legacyPath), beforeHash);
    }
    QCOMPARE(
        QDir(QFileInfo(legacyPath).absolutePath()).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name),
        inventory);
}

void PreferencesTest::boundedCoordinatorContentionAndRecovery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    Preferences preferences(path);
    const WindowPreferences original{QByteArrayLiteral("original"), QByteArrayLiteral("original")};
    QVERIFY(preferences.saveWindowPreferences(original));
    const auto before = fileHash(path);
    const auto lockPath = QFile::encodeName(path + QStringLiteral(".zenpdf-lock"));
    const int lockDescriptor = ::open(lockPath.constData(), O_RDWR | O_CLOEXEC);
    QVERIFY(lockDescriptor >= 0);
    QVERIFY(::flock(lockDescriptor, LOCK_EX | LOCK_NB) == 0);

    QElapsedTimer timer;
    timer.start();
    QString error;
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, path);
    QCOMPARE(fileHash(path), before);
    WindowPreferences loaded;
    timer.restart();
    error.clear();
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(timer.elapsed() < 500);
    verifyPathFreeError(error, path);

    QVERIFY(::flock(lockDescriptor, LOCK_UN) == 0);
    QVERIFY(::close(lockDescriptor) == 0);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}));
    QVERIFY(preferences.loadWindowPreferences(&loaded));
    QCOMPARE(loaded.geometry, QByteArrayLiteral("recovered"));
    QCOMPARE(loaded.state, loaded.geometry);

    const auto hostilePath = directory.filePath(QStringLiteral("hostile/preferences.ini"));
    QVERIFY(QDir().mkpath(QFileInfo(hostilePath).absolutePath()));
    const auto targetPath = directory.filePath(QStringLiteral("lock-target"));
    const QByteArray sentinel = QByteArrayLiteral("lock-target-sentinel");
    writeBytes(targetPath, sentinel);
    const auto hostileLock = QFile::encodeName(
        hostilePath + QStringLiteral(".zenpdf-lock"));
    const auto encodedTarget = QFile::encodeName(targetPath);
    QVERIFY(::symlink(encodedTarget.constData(), hostileLock.constData()) == 0);
    Preferences hostile(hostilePath);
    error.clear();
    QVERIFY(!hostile.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    verifyPathFreeError(error, hostilePath);
    QCOMPARE(fileHash(targetPath), QCryptographicHash::hash(sentinel, QCryptographicHash::Sha256));
    struct stat lockStatus {};
    QVERIFY(::lstat(hostileLock.constData(), &lockStatus) == 0);
    QVERIFY(S_ISLNK(lockStatus.st_mode));
}

void PreferencesTest::rejectsCoordinatorWithoutOwnerRead() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    const auto lockPath = path + QStringLiteral(".zenpdf-lock");
    writeBytes(lockPath, QByteArrayLiteral("coordinator"));
    const auto encodedLock = QFile::encodeName(lockPath);
    QVERIFY(::chmod(encodedLock.constData(), 0200) == 0);
    struct stat before {};
    QVERIFY(::lstat(encodedLock.constData(), &before) == 0);
    Preferences preferences(path);
    QString error;

    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")}, &error));
    verifyPathFreeError(error, path);
    QVERIFY(!QFileInfo(path).exists());
    struct stat after {};
    QVERIFY(::lstat(encodedLock.constData(), &after) == 0);
    QCOMPARE(after.st_dev, before.st_dev);
    QCOMPARE(after.st_ino, before.st_ino);
    QCOMPARE(after.st_mode, before.st_mode);
    QVERIFY(::chmod(encodedLock.constData(), 0600) == 0);
    QVERIFY(preferences.saveWindowPreferences(
        {QByteArrayLiteral("recovered"), QByteArrayLiteral("recovered")}));
}

void PreferencesTest::repairsSafePrivateLeafModes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const mode_t mode : {static_cast<mode_t>(0400), static_cast<mode_t>(0644)}) {
        const auto path = directory.filePath(
            QStringLiteral("preferences-%1.ini").arg(static_cast<unsigned int>(mode)));
        writeSettings(path, QByteArrayLiteral("geometry"), QByteArrayLiteral("state"), 1);
        const auto encoded = QFile::encodeName(path);
        QVERIFY(::chmod(encoded.constData(), mode) == 0);
        Preferences preferences(path);
        WindowPreferences loaded;

        QVERIFY(preferences.loadWindowPreferences(&loaded));
        struct stat status {};
        QVERIFY(::lstat(encoded.constData(), &status) == 0);
        QCOMPARE(status.st_mode & 07777, static_cast<mode_t>(0600));
    }
}

void PreferencesTest::rejectsUnsafeUnixLeaves_data() {
    QTest::addColumn<QString>("kind");
    QTest::newRow("symlink") << QStringLiteral("symlink");
    QTest::newRow("directory") << QStringLiteral("directory");
    QTest::newRow("fifo") << QStringLiteral("fifo");
    QTest::newRow("hardlink") << QStringLiteral("hardlink");
    QTest::newRow("group-world-writable") << QStringLiteral("writable");
    QTest::newRow("group-world-writable-executable") << QStringLiteral("writable-executable");
    QTest::newRow("special-mode") << QStringLiteral("special");
    QTest::newRow("access-denied") << QStringLiteral("access");
}

void PreferencesTest::rejectsUnsafeUnixLeaves() {
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("preferences.ini"));
    const auto target = directory.filePath(QStringLiteral("target"));
    const auto encodedPath = QFile::encodeName(path);
    const auto encodedTarget = QFile::encodeName(target);
    const QByteArray sentinel = QByteArrayLiteral("private-target-sentinel");
    writeBytes(target, sentinel);

    if (kind == QStringLiteral("symlink")) {
        QVERIFY(::symlink(encodedTarget.constData(), encodedPath.constData()) == 0);
    } else if (kind == QStringLiteral("directory")) {
        QVERIFY(::mkdir(encodedPath.constData(), 0700) == 0);
    } else if (kind == QStringLiteral("fifo")) {
        QVERIFY(::mkfifo(encodedPath.constData(), 0600) == 0);
    } else if (kind == QStringLiteral("hardlink")) {
        QVERIFY(::link(encodedTarget.constData(), encodedPath.constData()) == 0);
    } else {
        writeSettings(path, QByteArrayLiteral("geometry"), QByteArrayLiteral("state"), 1);
        const mode_t mode = kind == QStringLiteral("writable") ? 0666 :
                            kind == QStringLiteral("writable-executable") ? 0777 :
                            kind == QStringLiteral("special") ? 04700 : 0200;
        QVERIFY(::chmod(encodedPath.constData(), mode) == 0);
    }

    QByteArray leafBefore;
    if (QFileInfo(path).isFile() && !QFileInfo(path).isSymLink()) {
        QFile leaf(path);
        if (leaf.open(QIODevice::ReadOnly)) {
            leafBefore = leaf.readAll();
        }
    }
    struct stat leafStatusBefore {};
    QVERIFY(::lstat(encodedPath.constData(), &leafStatusBefore) == 0);
    Preferences preferences(path);
    WindowPreferences loaded{QByteArrayLiteral("old"), QByteArrayLiteral("old")};
    QString error;
    QVERIFY(!preferences.loadWindowPreferences(&loaded, &error));
    QVERIFY(loaded.geometry.isEmpty());
    QVERIFY(loaded.state.isEmpty());
    verifyPathFreeError(error, path);
    error.clear();
    QVERIFY(!preferences.saveWindowPreferences(
        {QByteArrayLiteral("new geometry"), QByteArrayLiteral("new state")}, &error));
    verifyPathFreeError(error, path);

    QFile targetFile(target);
    QVERIFY(targetFile.open(QIODevice::ReadOnly));
    QCOMPARE(targetFile.readAll(), sentinel);
    if (!leafBefore.isEmpty()) {
        QFile leaf(path);
        if (leaf.open(QIODevice::ReadOnly)) {
            QCOMPARE(leaf.readAll(), leafBefore);
        }
    }
    struct stat leafStatusAfter {};
    QVERIFY(::lstat(encodedPath.constData(), &leafStatusAfter) == 0);
    QCOMPARE(leafStatusAfter.st_dev, leafStatusBefore.st_dev);
    QCOMPARE(leafStatusAfter.st_ino, leafStatusBefore.st_ino);
    QCOMPARE(leafStatusAfter.st_mode, leafStatusBefore.st_mode);
    QCOMPARE(leafStatusAfter.st_nlink, leafStatusBefore.st_nlink);
}
#endif

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
#ifdef Q_OS_UNIX
    if (qEnvironmentVariableIsSet("ZENPDF_L004_LOCK_PROBE")) {
        const auto path = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_PATH");
        auto* lock = new QLockFile(path + QStringLiteral(".lock"));
        lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) {
            return 4;
        }
        ::_exit(0);
    }
#endif
    if (qEnvironmentVariableIsSet("ZENPDF_L004_IMPORT_PROBE")) {
        const auto path = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_PATH");
        const auto legacyPath = qEnvironmentVariable("ZENPDF_L004_LEGACY_PATH");
        WindowPreferences loaded;
        Preferences preferences(path, legacyPath);
        return preferences.loadWindowPreferences(&loaded) ? 0 : 5;
    }
    if (qEnvironmentVariableIsSet("ZENPDF_L004_FSTAT_FAULT_PROBE")) {
        const auto path = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_PATH");
        Preferences preferences(path);
        return preferences.saveWindowPreferences(
            {QByteArrayLiteral("must-not-commit"), QByteArrayLiteral("must-not-commit")})
            ? 6
            : 0;
    }
    if (qEnvironmentVariableIsSet("ZENPDF_L004_FSTAT_MIGRATION_PROBE")) {
        const auto path = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_PATH");
        Preferences preferences(path);
        WindowPreferences loaded;
        return preferences.loadWindowPreferences(&loaded) ? 7 : 0;
    }
    if (qEnvironmentVariableIsSet("ZENPDF_L004_FOREIGN_PARENT_PROBE")) {
        const auto path = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_PATH");
        const auto legacyPath = qEnvironmentVariable("ZENPDF_L004_LEGACY_PATH");
        Preferences preferences(path, legacyPath);
        WindowPreferences loaded;
        const bool loadRejected = !preferences.loadWindowPreferences(&loaded);
        const bool saveRejected = !preferences.saveWindowPreferences(
            {QByteArrayLiteral("blocked"), QByteArrayLiteral("blocked")});
        return loadRejected && saveRejected ? 0 : 8;
    }
    if (qEnvironmentVariableIsSet("ZENPDF_L004_PREFERENCES_PROBE")) {
        const auto path = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_PATH");
        const auto marker = qEnvironmentVariable("ZENPDF_L004_PREFERENCES_MARKER").toUtf8();
        if (path.isEmpty() || marker.isEmpty()) {
            return 2;
        }
        Preferences preferences(path);
        int successes = 0;
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (preferences.saveWindowPreferences({marker, marker})) {
                ++successes;
            }
        }
        return successes > 0 ? 0 : 3;
    }
    PreferencesTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "PreferencesTest.moc"
