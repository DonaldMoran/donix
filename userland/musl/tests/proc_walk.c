/*
 * proc_walk.c -- walk /proc the way libbb's procps_scan does, and
 * report what each step returns.
 *
 * This is a DIAGNOSTIC, not an assertion test.  It exits 0 and
 * prints what it finds, because we do not yet know which steps are
 * meant to succeed: /proc/<pid>/stat and /proc/<pid>/status exist
 * (commits 3 and 4), /proc/<pid>/cmdline was added in commit 5.
 * A test that failed on a known gap would hide the step that
 * actually breaks.
 *
 * It reproduces, in our own code, the sequence procps_scan runs:
 *
 *   1. opendir("/proc"); readdir each entry
 *   2. for a numeric entry: stat /proc/<n> and /proc/<n>/
 *      (procps_scan does this under PSSCAN_UIDGID, and it
 *      `continue`s the entry if the stat fails)
 *   3. for a numeric entry: open /proc/<n>/stat, read it, and run
 *      THE SAME sscanf procps_scan runs on the tail after ') ',
 *      printing the conversion count n.  procps_scan skips the
 *      entry when n < 11.
 *   4. for a numeric entry: open /proc/<n>/status
 *   5. for a numeric entry: open /proc/<n>/cmdline
 *
 * Step 3 is the one the consumer is strict about: if n < 11 every
 * process is skipped, and the printed tail names the field where
 * the parse stopped.  The sscanf format below is copied verbatim
 * from third_party/busybox/libbb/procps.c (the
 * !ENABLE_FEATURE_FAST_TOP branch), so this checks the kernel's
 * output against the consumer's real format, not against our
 * description of it.
 *
 * Read-only, fast.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

static int read_all(int fd, char* buf, size_t cap) {
    ssize_t n = read(fd, buf, cap - 1);
    if (n < 0) { buf[0] = '\0'; return -1; }
    buf[n] = '\0';
    return (int)n;
}

int main(void) {
    printf("=== proc_walk ===\n");

    DIR* d = opendir("/proc");
    if (!d) {
        printf("opendir(/proc) FAILED: %s\n", strerror(errno));
        printf("PROC_WALK-DONE\n");
        return 0;
    }
    printf("opendir(/proc) ok\n");

    int numeric_seen = 0;

    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        const char* nm = e->d_name;
        int all_digits = (nm[0] != '\0');
        for (const char* q = nm; *q; q++)
            if (*q < '0' || *q > '9') { all_digits = 0; break; }

        printf("readdir: '%s'%s\n", nm, all_digits ? "  [numeric]" : "");

        if (!all_digits) continue;
        numeric_seen++;

        char path[128];
        char buf[1024];

        /* ---- step 2: stat the per-pid DIRECTORY -------------------
         * procps_scan, under PSSCAN_UIDGID (which ps's default flag
         * set includes), builds "/proc/<pid>/" and calls stat() on
         * it to read st_uid/st_gid.  If that stat fails, the entry
         * is `continue`d -- skipped.  This is the one call the test
         * did not reproduce before; the bare pid directory and the
         * trailing-slash form are both checked. */
        {
            struct stat dsb;
            snprintf(path, sizeof(path), "/proc/%s", nm);
            errno = 0;
            int sr = stat(path, &dsb);
            if (sr == 0)
                printf("  stat('%s') ok, mode=0%o uid=%u gid=%u\n",
                       path, (unsigned)dsb.st_mode,
                       (unsigned)dsb.st_uid, (unsigned)dsb.st_gid);
            else
                printf("  stat('%s') FAILED: %s\n", path, strerror(errno));

            snprintf(path, sizeof(path), "/proc/%s/", nm);
            errno = 0;
            sr = stat(path, &dsb);
            if (sr == 0)
                printf("  stat('%s') ok, mode=0%o uid=%u gid=%u\n",
                       path, (unsigned)dsb.st_mode,
                       (unsigned)dsb.st_uid, (unsigned)dsb.st_gid);
            else
                printf("  stat('%s') FAILED: %s  <-- procps_scan would SKIP\n",
                       path, strerror(errno));
        }

        /* ---- step 3: /proc/<n>/stat, then the consumer's sscanf ---- */
        snprintf(path, sizeof(path), "/proc/%s/stat", nm);
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("  stat file: open FAILED: %s\n", strerror(errno));
        } else {
            int n = read_all(fd, buf, sizeof(buf));
            close(fd);
            if (n <= 0) {
                printf("  stat file: read returned %d\n", n);
            } else {
                printf("  stat file: %d bytes: %.80s\n", n, buf);

                /* Same split procps_scan does: last ')'. */
                char* cp = strrchr(buf, ')');
                if (!cp) {
                    printf("  stat file: no ')' -- procps_scan would deref past it\n");
                } else {
                    *cp = '\0';
                    cp += 2;   /* skip ") " */

                    char state[4] = {0};
                    unsigned ppid = 0, pgid = 0, sid = 0;
                    int tty = 0, tasknice = 0;
                    unsigned long utime = 0, stime = 0;
                    unsigned long start_time = 0, vsz = 0, rss = 0;

                    /* verbatim from libbb/procps.c (non-FAST_TOP) */
                    int conv = sscanf(cp,
                        "%c %u "
                        "%u %u %d %*s "
                        "%*s %*s %*s %*s %*s "
                        "%lu %lu "
                        "%*s %*s %*s "
                        "%ld "
                        "%*s %*s "
                        "%lu "
                        "%lu "
                        "%lu ",
                        state, &ppid,
                        &pgid, &sid, &tty,
                        &utime, &stime,
                        &tasknice,
                        &start_time,
                        &vsz,
                        &rss);

                    printf("  sscanf n=%d (need >=11)  tail='%.60s'\n",
                           conv, cp);
                    if (conv < 11)
                        printf("  >>> procps_scan would SKIP this entry\n");
                }
            }
        }

        /* ---- step 4: /proc/<n>/status ---- */
        snprintf(path, sizeof(path), "/proc/%s/status", nm);
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("  status: open FAILED: %s\n", strerror(errno));
        } else {
            int n = read_all(fd, buf, sizeof(buf));
            close(fd);
            printf("  status: open ok, %d bytes, first line: %.40s\n",
                   n, n > 0 ? buf : "(empty)");
        }

        /* ---- step 5: /proc/<n>/cmdline ---- */
        snprintf(path, sizeof(path), "/proc/%s/cmdline", nm);
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("  cmdline: open FAILED: %s\n", strerror(errno));
        } else {
            int n = read_all(fd, buf, sizeof(buf));
            close(fd);
            printf("  cmdline: open ok, %d bytes\n", n);
        }
    }
    closedir(d);

    printf("numeric entries seen: %d\n", numeric_seen);
    printf("PROC_WALK-DONE\n");
    return 0;
}
