/*
 * proc_dir.c -- /proc is a directory, and lists live pids.
 *
 * Checks the /proc directory shape only.  ps does not work yet:
 * it reads /proc/<pid>/stat and /proc/<pid>/status after the
 * directory walk, and those files are later commits.
 *
 *   1. opendir("/proc") succeeds
 *   2. readdir returns an entry named "self"
 *   3. a second readdir eventually returns NULL
 *   4. closedir succeeds
 *   5. stat("/proc") reports a directory
 *   6. opendir("/proc/self") succeeds and readdir returns NULL
 *   7. /proc lists at least one numeric pid
 *   8. /proc lists this process's own pid
 *
 * Read-only, fast.  Not a canary row.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static int fails = 0;

int main(void) {
    /* 1. opendir("/proc") succeeds. */
    errno = 0;
    DIR* d = opendir("/proc");
    if (!d) {
        printf("FAIL 1: opendir(/proc) failed: %s\n", strerror(errno));
        return 1;
    }
    printf("ok 1: opendir(/proc) succeeds\n");

    /* 2. readdir returns an entry named "self".
     *    The listing is "self" first, then pids, so "self" is
     *    the first entry; but scan until it is found or the
     *    directory ends, so a future reordering does not break
     *    the check. */
    errno = 0;
    struct dirent* e;
    int saw_self = 0;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, "self") == 0) { saw_self = 1; break; }
    }
    if (saw_self) {
        printf("ok 2: readdir(/proc) returns \"self\"\n");
    } else {
        printf("FAIL 2: readdir(/proc) has no \"self\" entry\n");
        fails++;
    }

    /* 3. reading the directory to the end returns NULL.
     *    (The loop above already reached an entry or the end;
     *    rewind and drain to be explicit.) */
    rewinddir(d);
    errno = 0;
    while ((e = readdir(d)) != NULL) { /* drain */ }
    printf("ok 3: readdir(/proc) ends at NULL\n");

    /* 4. closedir succeeds. */
    errno = 0;
    if (closedir(d) != 0) {
        printf("FAIL 4: closedir(/proc) failed: %s\n", strerror(errno));
        fails++;
    } else {
        printf("ok 4: closedir(/proc) succeeds\n");
    }

    /* 5. stat("/proc") reports a directory. */
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

    /* 6. opendir("/proc/self") succeeds and readdir returns NULL. */
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

    /* 7. /proc lists at least one numeric pid. */
    errno = 0;
    DIR* dp = opendir("/proc");
    if (!dp) {
        printf("FAIL 7: opendir(/proc) failed: %s\n", strerror(errno));
        fails++;
    } else {
        int saw_numeric = 0;
        struct dirent* ep;
        while ((ep = readdir(dp)) != NULL) {
            const char* s = ep->d_name;
            if (s[0] >= '1' && s[0] <= '9') {
                int alldig = 1;
                for (const char* q = s; *q; q++)
                    if (*q < '0' || *q > '9') { alldig = 0; break; }
                if (alldig) { saw_numeric = 1; break; }
            }
        }
        closedir(dp);
        if (saw_numeric) {
            printf("ok 7: readdir(/proc) lists a numeric pid\n");
        } else {
            printf("FAIL 7: readdir(/proc) has no numeric entry\n");
            fails++;
        }
    }

    /* 8. the current process's own pid is listed. */
    errno = 0;
    DIR* dm = opendir("/proc");
    if (!dm) {
        printf("FAIL 8: opendir(/proc) failed: %s\n", strerror(errno));
        fails++;
    } else {
        char me[24];
        snprintf(me, sizeof(me), "%d", (int)getpid());
        int found_me = 0;
        struct dirent* em;
        while ((em = readdir(dm)) != NULL) {
            if (strcmp(em->d_name, me) == 0) { found_me = 1; break; }
        }
        closedir(dm);
        if (found_me) {
            printf("ok 8: readdir(/proc) lists this process (pid %s)\n", me);
        } else {
            printf("FAIL 8: readdir(/proc) does not list this pid (%s)\n", me);
            fails++;
        }
    }

    if (fails == 0) {
        printf("PROC_DIR-ALL-PASS\n");
        return 0;
    }
    printf("PROC_DIR: %d failed\n", fails);
    return 1;
}
