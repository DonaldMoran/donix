/*
 * proc_stat.c -- /proc/<pid>/stat is readable and well-formed.
 *
 * Commit 3 of the /proc work.  /proc/self/stat and
 * /proc/<pid>/stat now open, read, and stat.  This test checks the
 * shape of the text, not its values (most columns are deliberate
 * zeros; see proc_build_stat's comment in user_syscall.c).
 *
 * It does NOT assert comm == the test's own name: when a donix
 * binary runs as a busybox applet the process name is "busybox",
 * and a test that asserted its own name would be fragile.  The
 * checks are about structure.
 *
 *   1. open("/proc/self/stat") succeeds
 *   2. read() returns data
 *   3. the text begins with this pid followed by " ("
 *   4. it contains a ')' and the state char after ") " is R/S/Z/T
 *   5. it has at least 20 whitespace-separated fields
 *   6. stat("/proc/self/stat") is S_IFREG with nonzero size
 *   7. open("/proc/999999/stat") fails ENOENT
 *
 * Read-only, fast.  Not a canary row.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static int fails = 0;

int main(void) {
    char buf[1024];

    /* 1. open("/proc/self/stat") succeeds. */
    errno = 0;
    int fd = open("/proc/self/stat", O_RDONLY);
    if (fd < 0) {
        printf("FAIL 1: open(/proc/self/stat) failed: %s\n",
               strerror(errno));
        return 1;
    }
    printf("ok 1: open(/proc/self/stat) succeeds\n");

    /* 2. read() returns data. */
    errno = 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        printf("FAIL 2: read() returned %ld: %s\n",
               (long)n, strerror(errno));
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("ok 2: read() returned %ld bytes\n", (long)n);

    /* 3. the text begins with this pid followed by " (". */
    char want[24];
    snprintf(want, sizeof(want), "%d (", (int)getpid());
    if (strncmp(buf, want, strlen(want)) == 0) {
        printf("ok 3: text begins with \"%s\"\n", want);
    } else {
        printf("FAIL 3: text does not begin with \"%s\": %.40s\n",
               want, buf);
        fails++;
    }

    /* 4. contains ')' and the char after ") " is a state letter. */
    char* rp = strrchr(buf, ')');
    if (!rp) {
        printf("FAIL 4: no ')' in the text\n");
        fails++;
    } else if (rp[1] != ' ') {
        printf("FAIL 4: no space after ')'\n");
        fails++;
    } else {
        char sc = rp[2];
        if (sc=='R' || sc=='S' || sc=='Z' || sc=='T') {
            printf("ok 4: state char is '%c'\n", sc);
        } else {
            printf("FAIL 4: state char is '%c', want R/S/Z/T\n", sc);
            fails++;
        }
    }

    /* 5. at least 20 whitespace-separated fields. */
    {
        int fields = 0;
        for (const char* q = buf; *q; q++) {
            if (*q != ' ' && *q != '\n' && *q != '\t'
                && (q == buf || q[-1] == ' ' || q[-1] == '\n' || q[-1] == '\t'))
                fields++;
        }
        if (fields >= 20) {
            printf("ok 5: %d whitespace-separated fields\n", fields);
        } else {
            printf("FAIL 5: only %d fields, want >= 20\n", fields);
            fails++;
        }
    }

    close(fd);

    /* 6. stat("/proc/self/stat") is S_IFREG with nonzero size. */
    errno = 0;
    struct stat st;
    if (stat("/proc/self/stat", &st) != 0) {
        printf("FAIL 6: stat() failed: %s\n", strerror(errno));
        fails++;
    } else if (!S_ISREG(st.st_mode)) {
        printf("FAIL 6: mode 0%o is not a regular file\n",
               (unsigned)st.st_mode);
        fails++;
    } else if (st.st_size <= 0) {
        printf("FAIL 6: size is %ld, want nonzero\n", (long)st.st_size);
        fails++;
    } else {
        printf("ok 6: stat() reports S_IFREG, size %ld\n", (long)st.st_size);
    }

    /* 7. open("/proc/999999/stat") fails ENOENT. */
    errno = 0;
    int bad = open("/proc/999999/stat", O_RDONLY);
    if (bad >= 0) {
        printf("FAIL 7: open(/proc/999999/stat) succeeded\n");
        close(bad);
        fails++;
    } else if (errno != ENOENT) {
        printf("FAIL 7: errno is %d (%s), want ENOENT\n",
               errno, strerror(errno));
        fails++;
    } else {
        printf("ok 7: open(/proc/999999/stat) fails ENOENT\n");
    }

    if (fails == 0) {
        printf("PROC_STAT-ALL-PASS\n");
        return 0;
    }
    printf("PROC_STAT: %d failed\n", fails);
    return 1;
}
