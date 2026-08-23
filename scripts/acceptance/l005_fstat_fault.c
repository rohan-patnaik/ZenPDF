#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int target_fstat_count;

int fstat(int descriptor, struct stat *status) {
    static int (*real_fstat)(int, struct stat *);
    if (real_fstat == NULL) {
        real_fstat = dlsym(RTLD_NEXT, "fstat");
        if (real_fstat == NULL) {
            errno = ENOSYS;
            return -1;
        }
    }

    char link_path[64];
    char target[PATH_MAX + 1];
    const int link_length = snprintf(
        link_path, sizeof(link_path), "/proc/self/fd/%d", descriptor);
    const ssize_t target_length = link_length > 0 && link_length < (int)sizeof(link_path)
        ? readlink(link_path, target, PATH_MAX)
        : -1;
    if (target_length > 0) {
        target[target_length] = '\0';
        const char *base = strrchr(target, '/');
        if (base != NULL && strcmp(base + 1, "zenpdf.log") == 0) {
            ++target_fstat_count;
            const char *requested = getenv("ZENPDF_L005_FAIL_LOG_FSTAT");
            if (requested != NULL && atoi(requested) == target_fstat_count) {
                errno = EIO;
                return -1;
            }
        }
    }
    return real_fstat(descriptor, status);
}
