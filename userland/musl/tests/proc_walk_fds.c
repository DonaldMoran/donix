/*
 * proc_walk_fds.c -- walk /proc with low fds occupied.
 *
 * proc_walk proves the walk succeeds in a fresh process (pid 4):
 * opendir/readdir list the numeric pids, /proc/<n>/stat parses
 * (the consumer's own sscanf gives n=11), status and cmdline open.
 *
 * But ps and pstree -- two consumers of the same libbb procps_scan
 * -- print nothing, and they run INSIDE the busybox process (the
 * shell), whose fd table is not empty.  xopendir() allocates the
 * lowest free fd; if the walk behaves differently when fds 3..N
 * are already taken, proc_walk (a fresh process) would not see it.
 *
 * This test occupies several fds first -- the way the shell has
 * them open -- and then runs the SAME walk proc_walk runs.  If it
 * fails where proc_walk succeeded, the cause is fd state and the
 * trace names it.
 *
 * Report-and-pass, like proc_walk: exit 0, print what each step
 * returns.  Read-only, fast.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

static int read_all(int fd, char* buf, size_t cap) {
    ssize_t n = read(fd, buf, cap - 1);
    if (n < 0) { buf[0] = '\0'; return -1; }
    buf[n] = '\0';
    return (int)n;
}

int main(void) {
    printf("=== proc_walk_fds ===\n");

    /* Occupy a few fds first, the way the shell has some open.
     * /dev/null is the safe choice: opening it does not block,
     * and it is the one device we know works.  Fall back to
     * /proc/self/stat (also known to open) if /dev/null fails. */
    int held[6];
    int nheld = 0;
    for (int i = 0; i < 6; i++) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) fd = open("/proc/self/stat", O_RDONLY);
        if (fd < 0) break;
        held[nheld++] = fd;
    }
    printf("held %d fds, first=%d last=%d\n",
           nheld, nheld ? held[0] : -1, nheld ? held[nheld-1] : -1);

    DIR* d = opendir("/proc");
    if (!d) {
        printf("opendir(/proc) FAILED after holding fds: %s\n",
               strerror(errno));
        printf(">>> this reproduces the ps/pstree failure\n");
        printf("PROC_WALK_FDS-DONE\n");
        return 0;
    }
    printf("opendir(/proc) ok (dir fd is above the held ones)\n");

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

        snprintf(path, sizeof(path), "/proc/%s/stat", nm);
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("  stat: open FAILED: %s\n", strerror(errno));
        } else {
            int n = read_all(fd, buf, sizeof(buf));
            close(fd);
            if (n <= 0) {
                printf("  stat: read returned %d\n", n);
            } else {
                char* cp = strrchr(buf, ')');
                if (!cp) {
                    printf("  stat: %d bytes, no ')'\n", n);
                } else {
                    *cp = '\0';
                    cp += 2;
                    char state[4] = {0};
                    unsigned ppid = 0, pgid = 0, sid = 0;
                    int tty = 0, tasknice = 0;
                    unsigned long utime = 0, stime = 0;
                    unsigned long start_time = 0, vsz = 0, rss = 0;
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
                    printf("  stat: %d bytes, sscanf n=%d\n", n, conv);
                    if (conv < 11)
                        printf("  >>> procps_scan would SKIP this entry\n");
                }
            }
        }

        snprintf(path, sizeof(path), "/proc/%s/status", nm);
        fd = open(path, O_RDONLY);
        if (fd < 0) printf("  status: open FAILED: %s\n", strerror(errno));
        else { read_all(fd, buf, sizeof(buf)); close(fd);
               printf("  status: open ok\n"); }

        snprintf(path, sizeof(path), "/proc/%s/cmdline", nm);
        fd = open(path, O_RDONLY);
        if (fd < 0) printf("  cmdline: open FAILED: %s\n", strerror(errno));
        else { int n = read_all(fd, buf, sizeof(buf)); close(fd);
               printf("  cmdline: open ok, %d bytes\n", n); }
    }
    closedir(d);

    for (int i = 0; i < nheld; i++) close(held[i]);
    printf("numeric entries seen: %d\n", numeric_seen);
    printf("PROC_WALK_FDS-DONE\n");
    return 0;
}
