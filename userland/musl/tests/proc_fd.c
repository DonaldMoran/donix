/*
 * proc_fd.c -- regression test for /proc/self/fd/N and /dev/console.
 *
 * The seam's third consumer, and the first positive readlink in the
 * tree.  Verifies the three facts that make busybox `tty` print a
 * path instead of "not a tty":
 *
 *   1. readlink("/proc/self/fd/0") returns "/dev/console".
 *      Same for fd 1 and fd 2.
 *   2. stat("/dev/console") succeeds and reports a character
 *      device whose (st_dev, st_ino) equals fstat(0)'s.
 *   3. readlink on a non-console fd returns -EINVAL.
 *
 * ttyname_r(3)'s three gates are exactly these: isatty (already
 * passes), readlink (1), and the device-identity comparison (2).
 * If this test passes, tty should print a path; the canary row
 * added in commit 5 proves that end to end.
 *
 * Exit status: 0 on all-pass, 1 on any failure.
 */
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define BUF_SIZE 256

static void puts_raw(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    write(1, s, n);
}

static void print_int(long v) {
    char buf[32];
    int i = 0;
    if (v == 0) { buf[i++] = '0'; }
    else {
        int neg = v < 0;
        unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
        char tmp[24];
        int j = 0;
        while (u > 0) { tmp[j++] = '0' + (u % 10); u /= 10; }
        if (neg) buf[i++] = '-';
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    write(1, buf, i);
}

static int failures = 0;

static void ok(const char* what) {
    puts_raw("ok   ");
    puts_raw(what);
    puts_raw("\n");
}

static void fail(const char* what) {
    puts_raw("FAIL ");
    puts_raw(what);
    puts_raw("\n");
    failures++;
}

static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* readlink into a NUL-terminated buffer; returns 0 on success. */
static int do_readlink(const char* path, char* out, size_t cap) {
    ssize_t n = readlink(path, out, cap - 1);
    if (n < 0) return -1;
    out[n] = '\0';
    return 0;
}

int main(void) {
    puts_raw("=== /proc/self/fd and /dev/console regression ===\n");

    /* 1. readlink on the three console fds -> "/dev/console". */
    const char* fdpaths[] = {
        "/proc/self/fd/0",
        "/proc/self/fd/1",
        "/proc/self/fd/2",
    };
    for (size_t i = 0; i < sizeof(fdpaths) / sizeof(fdpaths[0]); i++) {
        char target[BUF_SIZE];
        if (do_readlink(fdpaths[i], target, sizeof(target)) == 0
            && streq(target, "/dev/console")) {
            char msg[128];
            size_t m = 0;
            const char* p1 = "readlink(";
            while (p1[m]) { msg[m] = p1[m]; m++; }
            for (size_t j = 0; fdpaths[i][j]; j++) msg[m++] = fdpaths[i][j];
            const char* p2 = ") -> /dev/console";
            for (size_t j = 0; p2[j]; j++) msg[m++] = p2[j];
            msg[m] = '\0';
            ok(msg);
        } else {
            char msg[128];
            size_t m = 0;
            const char* p1 = "readlink(";
            while (p1[m]) { msg[m] = p1[m]; m++; }
            for (size_t j = 0; fdpaths[i][j]; j++) msg[m++] = fdpaths[i][j];
            const char* p2 = ") -> /dev/console";
            for (size_t j = 0; p2[j]; j++) msg[m++] = p2[j];
            msg[m] = '\0';
            fail(msg);
        }
    }

    /* 2. /dev/console is a character device whose identity matches
     *    fstat(0)'s.  This is ttyname_r's gate 3. */
    struct stat st_console, st_fd0;
    if (stat("/dev/console", &st_console) == 0) {
        ok("stat(/dev/console) succeeded");
        if (S_ISCHR(st_console.st_mode)) {
            ok("stat(/dev/console) reports S_IFCHR");
        } else {
            fail("stat(/dev/console) reports S_IFCHR");
        }
        if (fstat(0, &st_fd0) == 0
            && st_console.st_dev == st_fd0.st_dev
            && st_console.st_ino == st_fd0.st_ino) {
            ok("stat(/dev/console) matches fstat(0) on (st_dev, st_ino)");
        } else {
            fail("stat(/dev/console) matches fstat(0) on (st_dev, st_ino)");
            puts_raw("       console st_dev=");
            print_int((long)st_console.st_dev);
            puts_raw(" st_ino=");
            print_int((long)st_console.st_ino);
            puts_raw("\n");
            puts_raw("       fd0     st_dev=");
            print_int((long)st_fd0.st_dev);
            puts_raw(" st_ino=");
            print_int((long)st_fd0.st_ino);
            puts_raw("\n");
        }
    } else {
        fail("stat(/dev/console) succeeded");
    }

    /* 3. readlink on a non-console fd returns -EINVAL.  This is
     *    the boundary: /proc/self/fd/5 is a /proc path, but donix
     *    provides no target for it. */
    char target[BUF_SIZE];
    ssize_t r = readlink("/proc/self/fd/5", target, sizeof(target) - 1);
    if (r < 0 && errno == EINVAL) {
        ok("readlink(/proc/self/fd/5) -> EINVAL");
    } else {
        fail("readlink(/proc/self/fd/5) -> EINVAL");
    }

    puts_raw("---\n");
    if (failures == 0) {
        puts_raw("ALL PASS (0 failures)\n");
        return 0;
    }
    print_int(failures);
    puts_raw(" failure(s)\n");
    return 1;
}
