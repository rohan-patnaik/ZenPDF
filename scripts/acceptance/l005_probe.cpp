#include <QApplication>
#include <QByteArray>
#include <QLoggingCategory>
#include <QThread>

#include <chrono>
#include <cstdio>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
Q_LOGGING_CATEGORY(privateCategory, "L005-PRIVATE-CATEGORY-8d1317")

QByteArray sensitiveBody() {
    return qgetenv("ZENPDF_L005_SENSITIVE_BODY");
}

bool createMarker(const QByteArray& path) {
#ifdef Q_OS_UNIX
    if (path.isEmpty()) {
        return false;
    }
    const int descriptor = ::open(
        path.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        return false;
    }
    constexpr char value[] = "ready\n";
    const bool written = ::write(descriptor, value, sizeof(value) - 1) ==
        static_cast<ssize_t>(sizeof(value) - 1);
    return ::close(descriptor) == 0 && written;
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool waitForPath(const QByteArray& path) {
#ifdef Q_OS_UNIX
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::access(path.constData(), F_OK) == 0) {
            return true;
        }
        QThread::msleep(10);
    }
#else
    Q_UNUSED(path);
#endif
    return false;
}

void emitSensitiveWarning() {
    const auto body = sensitiveBody();
    qCWarning(privateCategory, "%s", body.constData());
}

void emitDestructorWarning() {
    emitSensitiveWarning();
}
}

int QApplication::exec() {
    const auto mode = qgetenv("ZENPDF_L005_MODE");

    if (mode == "fatal" || mode == "fatal-wait") {
#ifdef Q_OS_UNIX
        const rlimit disabledCore{0, 0};
        if (::setrlimit(RLIMIT_CORE, &disabledCore) != 0) {
            return 90;
        }
        const auto fatalName = qgetenv("ZENPDF_L005_FATAL_NAME");
        if (fatalName.isEmpty() || fatalName.size() > 15 ||
            ::prctl(PR_SET_NAME, fatalName.constData(), 0, 0, 0) != 0) {
            return 97;
        }
#endif
        if (mode == "fatal-wait" &&
            (!createMarker(qgetenv("ZENPDF_L005_READY")) ||
             !waitForPath(qgetenv("ZENPDF_L005_TRIGGER")))) {
            return 96;
        }
        const auto body = sensitiveBody();
        qCFatal(privateCategory, "%s", body.constData());
    }

    if (mode == "setup") {
        if (!createMarker(qgetenv("ZENPDF_L005_READY")) ||
            !waitForPath(qgetenv("ZENPDF_L005_TRIGGER"))) {
            return 91;
        }
        emitSensitiveWarning();
        return 0;
    }

    if (mode == "runtime") {
        if (!createMarker(qgetenv("ZENPDF_L005_READY")) ||
            !waitForPath(qgetenv("ZENPDF_L005_TRIGGER_ONE"))) {
            return 92;
        }
        emitSensitiveWarning();
        if (!createMarker(qgetenv("ZENPDF_L005_EMITTED")) ||
            !waitForPath(qgetenv("ZENPDF_L005_TRIGGER_TWO"))) {
            return 93;
        }
        emitSensitiveWarning();
        return 0;
    }

    if (mode == "multi") {
        bool countOk = false;
        const int count = qEnvironmentVariableIntValue("ZENPDF_L005_COUNT", &countOk);
        if (!countOk || count < 1 || count > 1000) {
            return 94;
        }
        for (int index = 0; index < count; ++index) {
            emitSensitiveWarning();
        }
        if (!createMarker(qgetenv("ZENPDF_L005_READY")) ||
            !waitForPath(qgetenv("ZENPDF_L005_TRIGGER"))) {
            return 95;
        }
        return 0;
    }

    if (mode == "none") {
        return 0;
    }

    emitSensitiveWarning();
    qAddPostRoutine(emitDestructorWarning);
    return 0;
}
