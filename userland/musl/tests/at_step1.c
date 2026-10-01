#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

int main(void) {
    int fails = 0;
    struct stat st;

    /* 1. fstatat(AT_FDCWD, absolute) -- the common case. */
    if (fstatat(AT_FDCWD, "/hello-world.txt", &st, 0) != 0) {
        printf("FAIL 1: fstatat absolute: %s\n", strerror(errno));
        fails++;
    } else {
        printf("ok 1: /hello-world.txt size=%lld\n", (long long)st.st_size);
    }

    /* 2. open a directory, use it as a dirfd. */
    int dfd = open("/bin", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        printf("FAIL 2: open /bin: %s\n", strerror(errno));
        return 1;
    }
    printf("ok 2: dfd=%d\n", dfd);

    /* 3. fstatat(real dirfd, relative) -- the new path. */
    if (fstatat(dfd, "busybox", &st, 0) != 0) {
        printf("FAIL 3: fstatat(dirfd, \"busybox\"): %s\n",
               strerror(errno));
        fails++;
    } else {
        printf("ok 3: /bin/busybox size=%lld\n", (long long)st.st_size);
    }

    /* 4. openat(real dirfd, relative). */
    int fd = openat(dfd, "busybox", O_RDONLY);
    if (fd < 0) {
        printf("FAIL 4: openat(dfd, \"busybox\"): %s\n", strerror(errno));
        fails++;
    } else {
        char buf[16];
        ssize_t n = read(fd, buf, sizeof(buf));
        /* ELF magic: 0x7f 'E' 'L' 'F' */
        if (n >= 4 && buf[0] == 0x7f && buf[1] == 'E' &&
            buf[2] == 'L' && buf[3] == 'F') {
            printf("ok 4: openat read %zd bytes, ELF magic\n", n);
        } else {
            printf("FAIL 4: openat read %zd bytes, bad magic\n", n);
            fails++;
        }
        close(fd);
    }

    /* 5. AT_EMPTY_PATH with empty path == fstat. */
    fd = open("/hello-world.txt", O_RDONLY);
    if (fd >= 0) {
        struct stat st2;
        if (fstatat(fd, "", &st2, AT_EMPTY_PATH) != 0) {
            printf("FAIL 5: fstatat AT_EMPTY_PATH: %s\n", strerror(errno));
            fails++;
        } else {
            printf("ok 5: fstat form size=%lld\n", (long long)st2.st_size);
        }
        close(fd);
    }

    /* 6. empty path WITHOUT AT_EMPTY_PATH -> ENOENT. */
    errno = 0;
    int r6 = fstatat(AT_FDCWD, "", &st, 0);
    if (r6 == -1 && errno == ENOENT) {
        printf("ok 6: empty path -> ENOENT\n");
    } else {
        printf("FAIL 6: empty path gave r=%d errno=%d (%s)\n",
               r6, errno, strerror(errno));
        fails++;
    }

    /* 7. unknown flag -> EINVAL. */
    errno = 0;
    int r7 = fstatat(AT_FDCWD, "/hello-world.txt", &st, 0x4000);
    if (r7 == -1 && errno == EINVAL) {
        printf("ok 7: bad flag -> EINVAL\n");
    } else {
        printf("FAIL 7: bad flag gave r=%d errno=%d (%s)\n",
               r7, errno, strerror(errno));
        fails++;
    }

    /*
     * 8. faccessat(real dirfd, relative) -- resolves against the
     *    dirfd, not the cwd.  dfd still names /bin and the cwd is
     *    still /, so a bug that ignored dirfd and used the cwd
     *    would look for /busybox and fail.
     *
     *    NOTE: this test relies on the staging layout -- busybox
     *    lives at /bin/busybox, nothing lives at /busybox, and the
     *    process cwd is / because nothing here chdir'd.  If that
     *    layout changes, section 9 stops being a control (it would
     *    pass even if dirfd were ignored).  Keep them together.
     */
    if (faccessat(dfd, "busybox", F_OK, 0) == 0) {
        printf("ok 8: faccessat(dfd, \"busybox\") -> exists\n");
    } else {
        printf("FAIL 8: faccessat(dfd, \"busybox\"): %s\n",
               strerror(errno));
        fails++;
    }

    /*
     * 9. Control for section 8: the same name against AT_FDCWD
     *    resolves against the cwd, where /busybox does not exist.
     *    Must fail with ENOENT.
     */
    errno = 0;
    if (faccessat(AT_FDCWD, "busybox", F_OK, 0) == -1 && errno == ENOENT) {
        printf("ok 9: faccessat(AT_FDCWD, \"busybox\") -> ENOENT\n");
    } else {
        printf("FAIL 9: faccessat(AT_FDCWD, \"busybox\"): errno=%d (%s)\n",
               errno, strerror(errno));
        fails++;
    }

    /*
     * (section 10 removed: it asserted faccessat rejects unknown
     *  flags with EINVAL, but that EINVAL comes from musl itself
     *  -- third_party/musl-src/src/unistd/faccessat.c returns
     *  -EINVAL for `flag & ~AT_EACCESS` before issuing any
     *  syscall -- so the test never reached the kernel.  The
     *  kernel deliberately does not validate faccessat's flags;
     *  see the comment on sys_faccessat.  A test that cannot fail
     *  is not a test.)
     */

    /*
     * 11. utimensat(real dirfd, relative) -- the same resolution
     *     rule, exercised through the other *at syscall this
     *     session touches.  donix does not store timestamps, so
     *     success just means the path resolved.
     *
     *     This FAILS against the pre-commit-2 sys_utimensat, which
     *     ignores dirfd and resolves "busybox" against the cwd (/),
     *     where it does not exist.  It passes once sys_utimensat
     *     routes through resolve_at.  Seeing it flip is the point.
     */
    if (utimensat(dfd, "busybox", NULL, 0) == 0) {
        printf("ok 11: utimensat(dfd, \"busybox\") -> 0\n");
    } else {
        printf("FAIL 11: utimensat(dfd, \"busybox\"): %s\n",
               strerror(errno));
        fails++;
    }

    close(dfd);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
