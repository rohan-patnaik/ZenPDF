#include "Logging.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class LoggingTest final : public QObject {
    Q_OBJECT

private slots:
    void boundsAndProtectsActiveAndRotatedLogs();
};

void LoggingTest::boundsAndProtectsActiveAndRotatedLogs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr qint64 maximumBytes = 16 * 1024;
    Logging::install(directory.path(), maximumBytes);
    const QString oversizedMessage(20'000, QChar{u'x'});
    for (int index = 0; index < 8; ++index) {
        qInfo().noquote() << index << oversizedMessage;
    }
    Logging::shutdown();

    const auto activePath = directory.filePath(QStringLiteral("zenpdf.log"));
    const auto rotatedPath = activePath + QStringLiteral(".1");
    for (const auto& path : {activePath, rotatedPath}) {
        const QFileInfo info(path);
        QVERIFY2(info.exists(), qPrintable(QStringLiteral("Missing expected log %1").arg(path)));
        QVERIFY(info.size() <= maximumBytes);
        QVERIFY(info.size() > 0);
#ifdef Q_OS_UNIX
        const auto publicPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                       QFileDevice::ReadOther | QFileDevice::WriteOther;
        QCOMPARE(info.permissions() & publicPermissions, QFileDevice::Permissions{});
#endif
    }
}

QTEST_GUILESS_MAIN(LoggingTest)
#include "LoggingTest.moc"
