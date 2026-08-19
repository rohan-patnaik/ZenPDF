#include "LocalState.h"
#include "Logging.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ZenPDF"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.rohan-patnaik"));
    QCoreApplication::setApplicationName(QStringLiteral("ZenPDF"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.rohan-patnaik.zenpdf"));

    const auto stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    Logging::install(QDir(stateDirectory).filePath(QStringLiteral("logs")));
    qInfo("Starting ZenPDF Desktop");

    LocalState localState(QDir(stateDirectory).filePath(QStringLiteral("state.sqlite3")));
    QString stateError;
    if (!localState.initialize(&stateError)) {
        QMessageBox::critical(nullptr, QObject::tr("ZenPDF could not start"), stateError);
        Logging::shutdown();
        return 1;
    }

    MainWindow window(localState);
    window.show();
    const int result = application.exec();
    Logging::shutdown();
    return result;
}
