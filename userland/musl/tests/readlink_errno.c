/*
 * readlink_errno.c -- regression test for sys_readlink's errno.
 *
 * WHY THIS EXISTS.  open-issues.md item 8 described a defect:
 * sys_readlink returned -EINVAL for every input, including paths
 * that do not exist.  Linux distinguishes -ENOENT (the path does
 * not exist) from -EINVAL (the path exists but is not a symlink).
 *
 * The defect is fixed in sys_readlink -- it calls access_resolved
 * first, so a missing path is -ENOENT and an existing one is
 * -EINVAL -- but NOTHING EXERCISED IT.  No applet in the current
 * set calls readlink(2); the only consumer is musl's ttyname(3),
 * which walks /proc/self/fd/N then /dev, neither of which exists,
 * so it can never distinguish the two answers.  A fix with no test
 * is a fix nobody can confirm.
 *
 * This test calls readlink(2) directly, the way a caller that
 * reads errno would, and asserts both answers.
 *
 * THE CHECKS:
 *
 *   1. readlink("/nonexistent")        -> -1, errno == ENOENT
 *   2. readlink("/usr/bin/HELLO")      -> -1, errno == EINVAL
 *   3. readlink("/")                   -> -1, errno == EINVAL
 *
 * Check 2 uses a path that definitely exists on the image (the
 * hello-world applet, staged bare under /usr/bin).  Check 3 uses
 * the root, which also exists.  Both are non-symlinks, and donix
 * has no symlinks, so -EINVAL is the truthful answer for each.
 *
 * Check 1 is the one that was broken: before the fix it returned
 * -EINVAL, because sys_readlink did not resolve the path at all.
 *
 * NOT A CANARY ROW.  It is read-only and fast, so it COULD be one,
 * but it is grouped with the other direct-syscall regression tests
 * (at_step1, fcntl_lowfd) that are run by hand when the relevant
 * syscall is touched.  Run it when changing sys_readlink, the
 * resolve_at family, or access_resolved.
 *
 * BUILD.  Add to USERLAND_ELFS and the mcopy_one chain in
 * 05_boot_kernel64/Makefile, like every other test in this
 * directory.  Staged bare, so the binary is /usr/bin/READLINK_ERRNO.
 *
 * RUN.  From ash or donix>:
 *
 *     readlink_errno
 *
 * Prints one line per check and a summary; exit status 0 if all
 * three pass, 1 otherwise.
 */

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

static int failures = 0;

/* One check: call readlink(path, buf, sizeof buf), require the
 * result to be -1 and errno to be `want_errno`. */
static void check_readlink(const char* path, int want_errno,
                           const char* what) {
    char buf[256];
    errno = 0;

    ssize_t n = readlink(path, buf, sizeof(buf));

    if (n != -1) {
        printf("FAIL %s: readlink(\"%s\") returned %ld, expected -1\n",
               what, path, (long)n);
        failures++;
        return;
    }
    if (errno != want_errno) {
        printf("FAIL %s: readlink(\"%s\") set errno=%d (%s), expected %d\n",
               what, path, errno, strerror(errno), want_errno);
        failures++;
        return;
    }
    printf("ok   %s: readlink(\"%s\") -> -1, errno=%d (%s)\n",
           what, path, errno, strerror(errno));
}

int main(void) {
    printf("=== readlink errno regression ===\n");

    /* 1. A path that does not exist must be ENOENT, not EINVAL.
     * This is the check that was broken before the fix. */
    check_readlink("/nonexistent", ENOENT,
                   "missing path is ENOENT");

    /* 2. A path that exists and is not a symlink is EINVAL.
     * /usr/bin/HELLO is the hello-world applet, staged bare. */
    check_readlink("/usr/bin/HELLO", EINVAL,
                   "existing regular file is EINVAL");

    /* 3. The root exists and is not a symlink.  Exercises the
     * path_is_root alias inside access_resolved. */
    check_readlink("/", EINVAL,
                   "existing directory is EINVAL");

    if (failures == 0) {
        printf("---\n3 passed, 0 failed\n");
        return 0;
    }
    printf("---\n%d failed\n", failures);
    return 1;
}
