#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

/*
 * unlinkat(2) and the file-vs-directory type check.
 *
 * Creates its own fixtures under /, so it MUTATES THE DISK.  It is
 * not a canary row: run it explicitly.  It cleans up after itself
 * on the success path; on a failure it may leave a fixture behind.
 *
 * Every check resets errno to 0 and captures the return value, so a
 * stale errno cannot make a check pass or fail for the wrong reason.
 */

static int fails = 0;

static void report(const char* what, int ok, int r, int e) {
    if (ok) {
        printf("ok %s\n", what);
    } else {
        printf("FAIL %s: r=%d errno=%d (%s)\n",
               what, r, e, strerror(e));
        fails++;
    }
}

int main(void) {
    int r;

    /* --- Fixtures: one file, one directory. --- */
    int fd = open("/atstep2_file", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        printf("FATAL: cannot create /atstep2_file: %s\n",
               strerror(errno));
        return 1;
    }
    close(fd);

    if (mkdir("/atstep2_dir", 0755) != 0) {
        printf("FATAL: cannot mkdir /atstep2_dir: %s\n",
               strerror(errno));
        return 1;
    }

    /* 1. unlinkat(AT_FDCWD, file, 0) removes a file. */
    errno = 0;
    r = unlinkat(AT_FDCWD, "/atstep2_file", 0);
    report("1 unlinkat file -> 0", r == 0, r, errno);

    /* 2. unlinkat(AT_FDCWD, dir, 0) -> EISDIR. */
    errno = 0;
    r = unlinkat(AT_FDCWD, "/atstep2_dir", 0);
    report("2 unlinkat dir -> EISDIR", r == -1 && errno == EISDIR,
           r, errno);

    /* 3. unlinkat(AT_FDCWD, dir, AT_REMOVEDIR) removes an empty dir. */
    errno = 0;
    r = unlinkat(AT_FDCWD, "/atstep2_dir", AT_REMOVEDIR);
    report("3 unlinkat dir AT_REMOVEDIR -> 0", r == 0, r, errno);

    /* --- Rebuild fixtures for the dirfd and flag checks. --- */
    fd = open("/atstep2_file", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { printf("FATAL: recreate file: %s\n",
                         strerror(errno)); return 1; }
    close(fd);
    if (mkdir("/atstep2_dir", 0755) != 0) {
        printf("FATAL: recreate dir: %s\n", strerror(errno));
        return 1;
    }

    /* 4. unlinkat(AT_FDCWD, file, AT_REMOVEDIR) -> ENOTDIR. */
    errno = 0;
    r = unlinkat(AT_FDCWD, "/atstep2_file", AT_REMOVEDIR);
    report("4 unlinkat file AT_REMOVEDIR -> ENOTDIR",
           r == -1 && errno == ENOTDIR, r, errno);

    /* 5. unlinkat(dirfd, relative) with a real dirfd. */
    int dfd = open("/", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        printf("FATAL: open /: %s\n", strerror(errno));
        return 1;
    }
    errno = 0;
    r = unlinkat(dfd, "atstep2_file", 0);
    report("5 unlinkat(dirfd, rel) -> 0", r == 0, r, errno);

    /* 6. unknown flag -> EINVAL. */
    errno = 0;
    r = unlinkat(AT_FDCWD, "/atstep2_dir", 0x4000);
    report("6 unlinkat bad flag -> EINVAL", r == -1 && errno == EINVAL,
           r, errno);

    /* 7. unlink(2) on a missing path -> ENOENT (regression). */
    errno = 0;
    r = unlink("/atstep2_nope");
    report("7 unlink missing -> ENOENT", r == -1 && errno == ENOENT,
           r, errno);

    /* 8. rmdir(2) on an empty dir -> 0 (regression). */
    errno = 0;
    r = rmdir("/atstep2_dir");
    report("8 rmdir empty dir -> 0", r == 0, r, errno);

    /* Clean up: it is empty, so AT_REMOVEDIR works whether check 8
     * removed it or not.  Check 6 did not remove it (bad flag). */
    errno = 0;
    r = unlinkat(AT_FDCWD, "/atstep2_dir", AT_REMOVEDIR);
    if (r != 0 && errno != ENOENT) {
        printf("note: cleanup left /atstep2_dir: %s\n", strerror(errno));
    }

    close(dfd);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
