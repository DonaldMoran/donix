/*
 * proc_status.c -- regression test for /proc/self/status.
 *
 * The seam's second non-FAT consumer, after /dev/null.  Proves that
 * open(2), read(2), stat(2), and close(2) all work on a synthesized
 * file whose bytes are produced by the kernel, not read from FAT.
 *
 * What this test checks, in order:
 *
 *   1. open("/proc/self/status", O_RDONLY) succeeds and returns a
 *      valid fd.
 *   2. read() returns at least one byte and the text contains the
 *      five expected keys: Name:, Pid:, PPid:, Uid:, Gid:.
 *   3. stat("/proc/self/status") reports S_IFREG and a nonzero
 *      size.
 *   4. close() succeeds.
 *
 * It does NOT check the *values* of Pid:/PPid:, because they are
 * per-run -- the test's own pid and its parent's.  It checks that
 * the keys are present and that the lines are well-formed
 * ("Key:\tValue\n").  A future test can pin the values if it wants
 * to run under a known shell.
 *
 * Exit status: 0 on all-pass, 1 on any failure.
 */
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define PATH "/proc/self/status"
#define BUF_SIZE 4096

/* Manual write, no stdio, matching the other tests. */
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

/* True if `line` begins with `key` followed by ':' and then a tab. */
static int line_has_key(const char* text, size_t len, const char* key) {
    size_t klen = 0;
    while (key[klen]) klen++;

    for (size_t i = 0; i + klen + 1 < len; i++) {
        /* Match at start of text or after a newline. */
        if (i != 0 && text[i - 1] != '\n') continue;
        size_t j = 0;
        while (j < klen && text[i + j] == key[j]) j++;
        if (j != klen) continue;
        /* key matched; expect ':' then '\t'. */
        if (text[i + klen] == ':' && text[i + klen + 1] == '\t') {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    puts_raw("=== /proc/self/status regression ===\n");

    /* 1. open */
    int fd = open(PATH, O_RDONLY);
    if (fd < 0) {
        fail("open(" PATH ") succeeded");
        puts_raw("errno=");
        print_int(errno);
        puts_raw("\n");
        puts_raw("---\n");
        print_int(failures);
        puts_raw(" failure(s)\n");
        return 1;
    }
    ok("open(" PATH ") -> fd");

    /* 2. read */
    char buf[BUF_SIZE];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        fail("read() returned data");
        close(fd);
        puts_raw("---\n");
        print_int(failures);
        puts_raw(" failure(s)\n");
        return 1;
    }
    buf[n] = '\0';
    ok("read() returned data");

    /* Check the five keys.  Each is one ok/FAIL. */
    struct { const char* name; } keys[] = {
        { "Name" },
        { "Pid"  },
        { "PPid" },
        { "Uid"  },
        { "Gid"  },
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (line_has_key(buf, (size_t)n, keys[i].name)) {
            char msg[64];
            size_t m = 0;
            const char* pfx = "contains ";
            while (pfx[m]) { msg[m] = pfx[m]; m++; }
            for (size_t j = 0; keys[i].name[j]; j++) msg[m++] = keys[i].name[j];
            msg[m++] = ':';
            msg[m] = '\0';
            ok(msg);
        } else {
            char msg[64];
            size_t m = 0;
            const char* pfx = "contains ";
            while (pfx[m]) { msg[m] = pfx[m]; m++; }
            for (size_t j = 0; keys[i].name[j]; j++) msg[m++] = keys[i].name[j];
            msg[m++] = ':';
            msg[m] = '\0';
            fail(msg);
        }
    }

    /* 3. stat */
    struct stat st;
    if (stat(PATH, &st) == 0) {
        if (S_ISREG(st.st_mode) && st.st_size > 0) {
            ok("stat() reports S_IFREG with nonzero size");
        } else {
            fail("stat() reports S_IFREG with nonzero size");
        }
    } else {
        fail("stat(" PATH ") succeeded");
    }

    /* 4. close */
    if (close(fd) == 0) {
        ok("close() succeeded");
    } else {
        fail("close() succeeded");
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
