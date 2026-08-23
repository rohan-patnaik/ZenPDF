#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPdfDocument>
#include <QPdfSearchModel>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3 || argc > 4) {
        return 64;
    }

    QPdfDocument document;
    if (argc == 4) {
        document.setPassword(QString::fromUtf8(argv[3]));
    }
    const auto path = QString::fromLocal8Bit(argv[1]);
    const auto error = document.load(path);
    QTextStream out(stdout);
    out << "load_error=" << static_cast<int>(error)
        << " pages=" << document.pageCount()
        << " bytes=" << QFileInfo(path).size() << '\n';
    if (error != QPdfDocument::Error::None || document.pageCount() < 1) {
        out.flush();
        return 2;
    }

    QPdfSearchModel model;
    model.setDocument(&document);
    QElapsedTimer elapsed;
    elapsed.start();
    bool durationOk = false;
    const int requestedDuration = qEnvironmentVariableIntValue("ZENPDF_PROBE_MS", &durationOk);
    const int duration = durationOk ? requestedDuration : 1'500;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        if (elapsed.elapsed() < duration) {
            return;
        }
        long maximumRssKiB = 0;
#ifdef Q_OS_UNIX
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        maximumRssKiB = usage.ru_maxrss;
#endif
        out << "query=" << QString::fromUtf8(argv[2])
            << " results=" << model.rowCount({})
            << " elapsed_ms=" << elapsed.elapsed()
            << " maxrss_kib=" << maximumRssKiB
            << " observation_ms=" << duration << '\n';
        out.flush();
        app.quit();
    });
    model.setSearchString(QString::fromUtf8(argv[2]));
    poll.start(50);
    return app.exec();
}
