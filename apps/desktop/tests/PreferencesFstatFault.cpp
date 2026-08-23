#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using FstatFunction = int (*)(int, struct stat*);
bool injected = false;
}

extern "C" int fstat(int descriptor, struct stat* status) {
    static const auto realFstat = reinterpret_cast<FstatFunction>(dlsym(RTLD_NEXT, "fstat"));
    const int result = realFstat(descriptor, status);
    if (result == 0 && std::getenv("ZENPDF_L004_FAKE_FOREIGN_LEGACY_PARENT") != nullptr &&
        S_ISDIR(status->st_mode)) {
        char linkPath[64] {};
        char target[4096] {};
        const int length = std::snprintf(
            linkPath, sizeof(linkPath), "/proc/self/fd/%d", descriptor);
        const ssize_t targetLength = length > 0
            ? ::readlink(linkPath, target, sizeof(target) - 1)
            : -1;
        if (targetLength > 0) {
            target[targetLength] = '\0';
            if (std::strstr(target, "/legacy-foreign-parent") != nullptr) {
                status->st_uid = status->st_uid + 1;
                return result;
            }
        }
    }
    if (!injected && std::getenv("ZENPDF_L004_FAIL_PUBLICATION_FSTAT") != nullptr) {
        char linkPath[64] {};
        char target[4096] {};
        const int length = std::snprintf(
            linkPath, sizeof(linkPath), "/proc/self/fd/%d", descriptor);
        const ssize_t targetLength = length > 0
            ? ::readlink(linkPath, target, sizeof(target) - 1)
            : -1;
        if (targetLength > 0) {
            target[targetLength] = '\0';
            const char* name = std::strrchr(target, '/');
            name = name == nullptr ? target : name + 1;
            if ((std::strstr(name, "preferences.ini.") != nullptr ||
                 (result == 0 && status->st_nlink == 0 && status->st_size > 0)) &&
                std::strstr(name, ".stage-") == nullptr &&
                std::strstr(name, ".lock") == nullptr &&
                std::strstr(name, ".zenpdf-") == nullptr) {
                injected = true;
                errno = EIO;
                return -1;
            }
        }
    }
    return result;
}
