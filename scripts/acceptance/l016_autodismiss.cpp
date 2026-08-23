#include <QByteArray>
#include <QMessageBox>
#include <QString>

#include <cstdio>

QMessageBox::StandardButton QMessageBox::critical(
    QWidget *,
    const QString &title,
    const QString &message,
    QMessageBox::StandardButtons,
    QMessageBox::StandardButton) {
    const QByteArray encoded = message.toUtf8();
    const bool titleOk = title == QStringLiteral("ZenPDF could not start");
    const bool bounded = encoded.size() <= 256;
    const bool pathFree = !message.contains(QLatin1Char('/'));
    const bool basenameFree = !message.contains(QStringLiteral("state.sqlite3"), Qt::CaseInsensitive);
    const bool driverFree = !message.contains(QStringLiteral("sqlite"), Qt::CaseInsensitive)
        && !message.contains(QStringLiteral("driver"), Qt::CaseInsensitive);
    std::fprintf(
        stderr,
        "L016_AUTODISMISS title_ok=%s message_bytes=%lld bounded=%s path_free=%s basename_free=%s driver_free=%s\n",
        titleOk ? "yes" : "no",
        static_cast<long long>(encoded.size()),
        bounded ? "yes" : "no",
        pathFree ? "yes" : "no",
        basenameFree ? "yes" : "no",
        driverFree ? "yes" : "no");
    std::fflush(stderr);
    return QMessageBox::Ok;
}
