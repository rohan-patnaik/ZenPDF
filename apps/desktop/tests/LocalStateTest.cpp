#include "LocalState.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class LocalStateTest final : public QObject {
    Q_OBJECT

private slots:
    void rejectsEmptyPaths();
    void storesMostRecentFirstAndDeduplicates();
    void boundsAndClearsHistory();
    void clearingPurgesPathsFromDatabaseFiles();
};

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
    const auto databasePath = directory.filePath(QStringLiteral("state.sqlite3"));
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

QTEST_GUILESS_MAIN(LocalStateTest)
#include "LocalStateTest.moc"
