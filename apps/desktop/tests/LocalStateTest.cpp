#include "LocalState.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

class LocalStateTest final : public QObject {
    Q_OBJECT

private slots:
    void rejectsEmptyPaths();
    void storesMostRecentFirstAndDeduplicates();
    void boundsAndClearsHistory();
    void clearingPurgesPathsFromDatabaseFiles();
    void createsPrivateStateUnderExposedUmask();
    void repairsSafeExposedState();
    void rejectsApplicationDirectorySymlink();
    void rejectsDatabaseSymlink();
    void rejectsHardlinkedDatabase();
    void rejectsNonRegularDatabasePromptly();
    void rejectsWritableApplicationDirectory();
    void rejectsWritableStateFile_data();
    void rejectsWritableStateFile();
    void repairsExistingSqliteSidecars();
    void removesFailedConnectionAndPermitsRetry();
    void sanitizesHostilePathErrors();
};

namespace {
#ifdef Q_OS_UNIX
class ScopedUmask final {
public:
    explicit ScopedUmask(mode_t mask) : previous_(::umask(mask)) {}
    ~ScopedUmask() { ::umask(previous_); }

    ScopedUmask(const ScopedUmask&) = delete;
    ScopedUmask& operator=(const ScopedUmask&) = delete;

private:
    mode_t previous_;
};

mode_t objectMode(const QString& path) {
    struct stat status {};
    if (::lstat(QFile::encodeName(path).constData(), &status) != 0) {
        return 0;
    }
    return status.st_mode & 07777;
}

void setObjectMode(const QString& path, mode_t mode) {
    QVERIFY(::chmod(QFile::encodeName(path).constData(), mode) == 0);
}

void writeFile(const QString& path, const QByteArray& contents, mode_t mode) {
    const auto encodedPath = QFile::encodeName(path);
    const int descriptor = ::open(
        encodedPath.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    QVERIFY(descriptor >= 0);
    QCOMPARE(::write(descriptor, contents.constData(), static_cast<size_t>(contents.size())),
             static_cast<ssize_t>(contents.size()));
    QVERIFY(::close(descriptor) == 0);
    setObjectMode(path, mode);
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void makeDirectory(const QString& path, mode_t mode) {
    QVERIFY(::mkdir(QFile::encodeName(path).constData(), mode) == 0);
    setObjectMode(path, mode);
}

void verifyPrivateSqliteFiles(const QString& databasePath, bool requireSidecars) {
    QCOMPARE(objectMode(databasePath), mode_t{0600});
    for (const auto& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const auto path = databasePath + suffix;
        if (requireSidecars) {
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        }
        if (QFileInfo::exists(path)) {
            QCOMPARE(objectMode(path), mode_t{0600});
        }
    }
}
#endif
}

void LocalStateTest::rejectsEmptyPaths() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite3")));
    QVERIFY(state.initialize());
    QString error;
    QVERIFY(!state.recordRecentFile(QStringLiteral("  "), &error));
    QVERIFY(!error.isEmpty());
}

void LocalStateTest::storesMostRecentFirstAndDeduplicates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite3")));
    QVERIFY(state.initialize());
    QVERIFY(state.recordRecentFile(directory.filePath(QStringLiteral("one.pdf"))));
    QTest::qSleep(2);
    QVERIFY(state.recordRecentFile(directory.filePath(QStringLiteral("two.pdf"))));
    QTest::qSleep(2);
    QVERIFY(state.recordRecentFile(directory.filePath(QStringLiteral("one.pdf"))));

    const auto recent = state.recentFiles(10);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.at(0).path, QDir::cleanPath(directory.filePath(QStringLiteral("one.pdf"))));
    QCOMPARE(recent.at(1).path, QDir::cleanPath(directory.filePath(QStringLiteral("two.pdf"))));
}

void LocalStateTest::boundsAndClearsHistory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalState state(directory.filePath(QStringLiteral("state.sqlite3")));
    QVERIFY(state.initialize());
    for (int index = 0; index < 60; ++index) {
        QVERIFY(state.recordRecentFile(directory.filePath(QStringLiteral("%1.pdf").arg(index))));
    }
    QCOMPARE(state.recentFiles(100).size(), 50);
    QVERIFY(state.clearRecentFiles());
    QVERIFY(state.recentFiles().isEmpty());
}

void LocalStateTest::clearingPurgesPathsFromDatabaseFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto databasePath = directory.filePath(QStringLiteral("app/state.sqlite3"));
    const QByteArray sentinel("ZENPDF_RECENT_PATH_SENTINEL_8f63e104");
    {
        LocalState state(databasePath);
        QVERIFY(state.initialize());
        QVERIFY(state.recordRecentFile(directory.filePath(QString::fromLatin1(sentinel) + QStringLiteral(".pdf"))));
        QVERIFY(state.clearRecentFiles());
        QVERIFY(state.recentFiles().isEmpty());
    }

    for (const auto& path : {databasePath, databasePath + QStringLiteral("-wal"), databasePath + QStringLiteral("-shm")}) {
        QFile file(path);
        if (!file.exists()) {
            continue;
        }
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        QVERIFY2(!file.readAll().contains(sentinel), qPrintable(QStringLiteral("Cleared path remains in %1").arg(path)));
    }
}

void LocalStateTest::createsPrivateStateUnderExposedUmask() {
#ifndef Q_OS_UNIX
    QSKIP("Unix permission contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    ScopedUmask exposedUmask(0022);

    LocalState state(databasePath);
    QVERIFY(state.initialize());
    QVERIFY(state.recordRecentFile(directory.filePath(QStringLiteral("private.pdf"))));
    QCOMPARE(objectMode(applicationDirectory), mode_t{0700});
    verifyPrivateSqliteFiles(databasePath, true);
#endif
}

void LocalStateTest::repairsSafeExposedState() {
#ifndef Q_OS_UNIX
    QSKIP("Unix permission contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0755);
    writeFile(databasePath, {}, 0644);

    LocalState state(databasePath);
    QVERIFY(state.initialize());
    QCOMPARE(objectMode(applicationDirectory), mode_t{0700});
    verifyPrivateSqliteFiles(databasePath, false);
#endif
}

void LocalStateTest::rejectsApplicationDirectorySymlink() {
#ifndef Q_OS_UNIX
    QSKIP("Unix symlink contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto target = directory.filePath(QStringLiteral("target"));
    const auto alias = directory.filePath(QStringLiteral("app"));
    makeDirectory(target, 0755);
    QVERIFY(::symlink(QFile::encodeName(target).constData(), QFile::encodeName(alias).constData()) == 0);

    QString error;
    LocalState state(QDir(alias).filePath(QStringLiteral("state.sqlite3")));
    QVERIFY(!state.initialize(&error));
    QCOMPARE(objectMode(target), mode_t{0755});
    QVERIFY(QDir(target).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    QVERIFY(!error.isEmpty());
#endif
}

void LocalStateTest::rejectsDatabaseSymlink() {
#ifndef Q_OS_UNIX
    QSKIP("Unix symlink contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto target = directory.filePath(QStringLiteral("target.sqlite3"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0700);
    const QByteArray sentinel("SYMLINK_TARGET_SENTINEL");
    writeFile(target, sentinel, 0644);
    QVERIFY(::symlink(QFile::encodeName(target).constData(), QFile::encodeName(databasePath).constData()) == 0);

    QString error;
    LocalState state(databasePath);
    QVERIFY(!state.initialize(&error));
    QCOMPARE(readFile(target), sentinel);
    QCOMPARE(objectMode(target), mode_t{0644});
#endif
}

void LocalStateTest::rejectsHardlinkedDatabase() {
#ifndef Q_OS_UNIX
    QSKIP("Unix hardlink contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto target = directory.filePath(QStringLiteral("target.sqlite3"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0700);
    const QByteArray sentinel("HARDLINK_TARGET_SENTINEL");
    writeFile(target, sentinel, 0600);
    QVERIFY(::link(QFile::encodeName(target).constData(), QFile::encodeName(databasePath).constData()) == 0);

    QString error;
    LocalState state(databasePath);
    QVERIFY(!state.initialize(&error));
    QCOMPARE(readFile(target), sentinel);
    QCOMPARE(readFile(databasePath), sentinel);
    QCOMPARE(objectMode(target), mode_t{0600});
#endif
}

void LocalStateTest::rejectsNonRegularDatabasePromptly() {
#ifndef Q_OS_UNIX
    QSKIP("Unix non-regular file contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0700);
    QVERIFY(::mkfifo(QFile::encodeName(databasePath).constData(), 0600) == 0);
    QElapsedTimer elapsed;
    elapsed.start();

    QString error;
    LocalState state(databasePath);
    QVERIFY(!state.initialize(&error));
    QVERIFY(elapsed.elapsed() < 1'000);
    struct stat status {};
    QVERIFY(::lstat(QFile::encodeName(databasePath).constData(), &status) == 0);
    QVERIFY(S_ISFIFO(status.st_mode));
#endif
}

void LocalStateTest::rejectsWritableApplicationDirectory() {
#ifndef Q_OS_UNIX
    QSKIP("Unix permission contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0777);

    QString error;
    LocalState state(databasePath);
    QVERIFY(!state.initialize(&error));
    QCOMPARE(objectMode(applicationDirectory), mode_t{0777});
    QVERIFY(!QFileInfo::exists(databasePath));
#endif
}

void LocalStateTest::rejectsWritableStateFile_data() {
    QTest::addColumn<QString>("suffix");
    QTest::newRow("database") << QString{};
    QTest::newRow("write-ahead-log") << QStringLiteral("-wal");
    QTest::newRow("shared-memory") << QStringLiteral("-shm");
}

void LocalStateTest::rejectsWritableStateFile() {
#ifndef Q_OS_UNIX
    QSKIP("Unix permission contract");
#else
    QFETCH(QString, suffix);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    const auto unsafePath = databasePath + suffix;
    const QByteArray sentinel("WRITABLE_STATE_SENTINEL");
    makeDirectory(applicationDirectory, 0700);
    writeFile(unsafePath, sentinel, 0666);

    QString error;
    LocalState state(databasePath);
    QVERIFY(!state.initialize(&error));
    QCOMPARE(readFile(unsafePath), sentinel);
    QCOMPARE(objectMode(unsafePath), mode_t{0666});
    if (!suffix.isEmpty()) {
        QVERIFY(!QFileInfo::exists(databasePath));
    }
#endif
}

void LocalStateTest::repairsExistingSqliteSidecars() {
#ifndef Q_OS_UNIX
    QSKIP("Unix permission contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0700);
    const auto rawConnection = QStringLiteral("zenpdf-sidecar-fixture-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        ScopedUmask exposedUmask(0022);
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnection);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode=WAL")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE fixture(value TEXT)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO fixture VALUES('private')")));
        QVERIFY(QFileInfo::exists(databasePath + QStringLiteral("-wal")));
        QVERIFY(QFileInfo::exists(databasePath + QStringLiteral("-shm")));
        setObjectMode(databasePath, 0644);
        setObjectMode(databasePath + QStringLiteral("-wal"), 0644);
        setObjectMode(databasePath + QStringLiteral("-shm"), 0644);

        LocalState state(databasePath);
        QVERIFY(state.initialize());
        verifyPrivateSqliteFiles(databasePath, true);
        query.finish();
        database.close();
        database = {};
    }
    QSqlDatabase::removeDatabase(rawConnection);
#endif
}

void LocalStateTest::removesFailedConnectionAndPermitsRetry() {
#ifndef Q_OS_UNIX
    QSKIP("Unix retry contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto applicationDirectory = directory.filePath(QStringLiteral("app"));
    const auto databasePath = QDir(applicationDirectory).filePath(QStringLiteral("state.sqlite3"));
    makeDirectory(applicationDirectory, 0700);
    writeFile(databasePath, QByteArray("not a sqlite database"), 0600);
    QTest::failOnWarning();

    LocalState state(databasePath);
    QString error;
    QVERIFY(!state.initialize(&error));
    QVERIFY(!error.contains(QStringLiteral("not a database"), Qt::CaseInsensitive));
    QVERIFY(QFile::remove(databasePath));
    QVERIFY(state.initialize(&error));
    QVERIFY(state.recordRecentFile(directory.filePath(QStringLiteral("retry.pdf")), &error));
#endif
}

void LocalStateTest::sanitizesHostilePathErrors() {
#ifndef Q_OS_UNIX
    QSKIP("Unix error privacy contract");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto basename = QStringLiteral("PRIVATE_BASENAME_SENTINEL_4b719e");
    const auto target = directory.filePath(QStringLiteral("target"));
    const auto hostileDirectory = directory.filePath(basename);
    makeDirectory(target, 0700);
    QVERIFY(::symlink(
        QFile::encodeName(target).constData(),
        QFile::encodeName(hostileDirectory).constData()) == 0);

    QString error;
    LocalState state(QDir(hostileDirectory).filePath(QStringLiteral("state.sqlite3")));
    QVERIFY(!state.initialize(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(error.size() <= 256);
    QVERIFY(!error.contains(hostileDirectory));
    QVERIFY(!error.contains(basename));
    QVERIFY(!error.contains(QStringLiteral("driver"), Qt::CaseInsensitive));
    QVERIFY(!error.contains(QStringLiteral("not a database"), Qt::CaseInsensitive));
#endif
}

QTEST_GUILESS_MAIN(LocalStateTest)
#include "LocalStateTest.moc"
