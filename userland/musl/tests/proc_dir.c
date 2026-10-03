/*
 * proc_dir.c -- /proc is a directory.
 *
 * Checks the directory shape only.  ps does not work yet: it needs
 * /proc/<pid>/stat and /proc/<pid>/status, which are later
 * commits.  This test checks:
 *
 *   - opendir("/proc") succeeds
 *   - readdir returns exactly one entry, "self"
 *   - a second readdir returns NULL (end)
 *   - closedir succeeds
 *   - stat("/proc") reports a directory
 *   - opendir("/proc/self") succeeds and readdir returns NULL
 *
 * Read-only, fast.  Not a canary row.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

static int fails = 0;

int main(void) {
    errno = 0;
    DIR* d = opendir("/proc");
    if (!d) {
        printf("FAIL 1: opendir(/proc) failed: %s\n", strerror(errno));
        return 1;
    }
    printf("ok 1: opendir(/proc) succeeds\n");

    errno = 0;
    struct dirent* e = readdir(d);
    if (!e) {
        printf("FAIL 2: readdir(/proc) returned NULL, want \"self\"\n");
        fails++;
    } else if (strcmp(e->d_name, "self") != 0) {
        printf("FAIL 2: readdir(/proc) returned \"%s\", want \"self\"\n",
               e->d_name);
        fails++;
    } else {
        printf("ok 2: readdir(/proc) returns \"self\"\n");
    }

    errno = 0;
    e = readdir(d);
    if (e) {
        printf("FAIL 3: readdir(/proc) returned \"%s\" after self\n",
               e->d_name);
        fails++;
    } else {
        printf("ok 3: readdir(/proc) ends after self\n");
    }

    errno = 0;
    if (closedir(d) != 0) {
        printf("FAIL 4: closedir(/proc) failed: %s\n", strerror(errno));
        fails++;
    } else {
        printf("ok 4: closedir(/proc) succeeds\n");
    }

    errno = 0;
    struct stat st;
    if (stat("/proc", &st) != 0) {
        printf("FAIL 5: stat(/proc) failed: %s\n", strerror(errno));
        fails++;
    } else if (!S_ISDIR(st.st_mode)) {
        printf("FAIL 5: stat(/proc) mode 0%o is not a directory\n",
               (unsigned)st.st_mode);
        fails++;
    } else {
        printf("ok 5: stat(/proc) reports a directory\n");
    }

    errno = 0;
    DIR* ds = opendir("/proc/self");
    if (!ds) {
        printf("FAIL 6: opendir(/proc/self) failed: %s\n", strerror(errno));
        fails++;
    } else {
        errno = 0;
        struct dirent* es = readdir(ds);
        if (es) {
            printf("FAIL 6: readdir(/proc/self) returned \"%s\", want NULL\n",
                   es->d_name);
            fails++;
        } else {
            printf("ok 6: readdir(/proc/self) is empty\n");
        }
        closedir(ds);
    }

    if (fails == 0) {
        printf("PROC_DIR-ALL-PASS\n");
        return 0;
    }
    printf("PROC_DIR: %d failed\n", fails);
    return 1;
}
