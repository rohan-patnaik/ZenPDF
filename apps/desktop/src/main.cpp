#include "LocalState.h"
#include "Logging.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_UNIX
    ::umask(S_IRWXG | S_IRWXO);
#endif
    int result = 1;
    bool loggingInstalled = false;
    {
        QApplication application(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("ZenPDF"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.rohan-patnaik"));
        QCoreApplication::setApplicationName(QStringLiteral("ZenPDF"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
        QApplication::setDesktopFileName(QStringLiteral("io.github.rohan-patnaik.zenpdf"));

        const auto stateDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QString stateError;
        if (!LocalState::preparePrivateApplicationDirectory(stateDirectory, &stateError)) {
            QMessageBox::critical(nullptr, QObject::tr("ZenPDF could not start"), stateError);
            return 1;
        }
        Logging::install(QDir(stateDirectory).filePath(QStringLiteral("logs")));
        loggingInstalled = true;
        qInfo("Starting ZenPDF Desktop");

        LocalState localState(QDir(stateDirectory).filePath(QStringLiteral("state.sqlite3")));
        if (!localState.initialize(&stateError)) {
            QMessageBox::critical(nullptr, QObject::tr("ZenPDF could not start"), stateError);
        } else {
            MainWindow window(localState);
            window.show();
            window.openFiles(application.arguments().mid(1));
            result = application.exec();
        }
    }

    if (loggingInstalled) {
        Logging::shutdown();
    }
    return result;
}
