/*
 * realpath_probe: reproduce busybox realpath's sequence on /proc
 * paths and print each call's result, so the failing syscall is
 * measured rather than guessed.  The kernel side of /proc/1 is
 * correct for every call the shell can make; realpath still
 * reports ENOENT, so print each step.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>

static void show_stat(const char *what, const char *p, int rc, int err,
                      const struct stat *st)
{
    if (rc != 0) {
        printf("%-22s %-20s ERR rc=%d errno=%d (%s)\n", what, p, rc, err, strerror(err));
        return;
    }
    printf("%-22s %-20s ok  dev=%llu ino=%llu mode=0%o\n", what, p,
           (unsigned long long)st->st_dev,
           (unsigned long long)st->st_ino,
           (unsigned)st->st_mode);
}

int main(void)
{
    printf("=== realpath_probe ===\n");

    const char *paths[] = { "/proc", "/proc/1", "/proc/1/status", "/proc/1/" };
    for (int i = 0; i < 4; i++) {
        struct stat st; errno = 0;
        int rc = lstat(paths[i], &st);
        show_stat("lstat", paths[i], rc, errno, &st);
    }

    /* opendir /proc, readdir, look for "1" */
    errno = 0;
    DIR *d = opendir("/proc");
    if (!d) { printf("opendir(/proc)        ERR errno=%d (%s)\n", errno, strerror(errno)); }
    else {
        printf("opendir(/proc)        ok\n");
        struct dirent *de; int saw1 = 0; errno = 0;
        while ((de = readdir(d)) != NULL) if (strcmp(de->d_name, "1") == 0) { saw1 = 1; break; }
        printf("readdir saw \"1\"       %s\n", saw1 ? "yes" : "no");
        int dfd = dirfd(d);
        /* the descent realpath does */
        errno = 0;
        int fd = openat(dfd, "1", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        printf("openat(dfd,\"1\",ODIR|ONOFOLLOW)  %s\n",
               fd < 0 ? strerror(errno) : "ok");
        if (fd >= 0) {
            struct stat st; errno = 0;
            int rc = fstat(fd, &st);
            show_stat("fstat(fd)", "(/proc/1)", rc, errno, &st);
            close(fd);
        }
        closedir(d);
    }

    /* absolute open, the way ls does it */
    errno = 0;
    int fd2 = open("/proc/1", O_RDONLY | O_DIRECTORY);
    printf("open(/proc/1,ODIR)    %s\n", fd2 < 0 ? strerror(errno) : "ok");
    if (fd2 >= 0) close(fd2);

    /* readlink on the child and on /proc/self */
    { char buf[256]; errno = 0; ssize_t n = readlink("/proc/1", buf, sizeof buf);
      printf("readlink(/proc/1)     rc=%ld errno=%d (%s)\n", (long)n, errno, strerror(errno)); }
    { char buf[256]; errno = 0; ssize_t n = readlink("/proc/self", buf, sizeof buf);
      printf("readlink(/proc/self)  rc=%ld errno=%d (%s)\n", (long)n, errno, strerror(errno)); }

    /* realpath itself, the thing that fails */
    const char *rp[] = { "/proc", "/proc/", "/proc/1", "/proc/1/", "/proc/1/status", "/proc/self/status" };
    for (int i = 0; i < 6; i++) {
        char out[PATH_MAX]; errno = 0;
        char *r = realpath(rp[i], out);
        if (r) printf("realpath(%s) -> %s\n", rp[i], out);
        else   printf("realpath(%s) ERR errno=%d (%s)\n", rp[i], errno, strerror(errno));
    }
    return 0;
}
