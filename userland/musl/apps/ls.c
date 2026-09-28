#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    /*
     * The path to list, as a string the KERNEL will resolve.
     *
     * Do NOT prepend "0:/" here.  The kernel's path syscalls now
     * resolve relative paths against the process's cwd, so passing
     * the user's argument through unchanged is both correct and
     * necessary:
     *
     *   ls            -> "."          -> kernel resolves to cwd
     *   ls foo        -> "foo"        -> cwd/foo
     *   ls /bin       -> "/bin"       -> absolute, kernel resolves
     *   ls 0:/foo     -> "0:/foo"     -> FatFs path, passed through
     *
     * The old code hardcoded "0:/" for the no-argument case and
     * prepended "0:/" for the argument case, which forced every
     * lookup to the FAT root and ignored `cd` entirely.
     */
    char path[128];
    if (argc >= 2 && argv[1][0] != '\0') {
        int i = 0;
        while (argv[1][i] && i < (int)sizeof(path) - 1) {
            path[i] = argv[1][i];
            i++;
        }
        path[i] = 0;
    } else {
        strcpy(path, ".");
    }

    /*
     * If an explicit argument was given, stat it first.  A regular
     * file has no directory entries to list; print the file and
     * exit.  Only fall through to opendir() when the argument is a
     * directory (or no argument was given, in which case path is
     * ".", the current directory).
     */
    if (argc >= 2 && argv[1][0] != '\0') {
        struct stat st;
        if (stat(path, &st) != 0) {
            printf("ls: %s: not found\n", argv[1]);
            return 1;
        }
        if (!S_ISDIR(st.st_mode)) {
            printf("FILE   %s  (%ld bytes)\n", argv[1], (long)st.st_size);
            return 0;
        }
        /* Fall through: it is a directory, list its entries. */
    }

    DIR* d = opendir(path);
    if (!d) {
        printf("[ls] FAIL: opendir(\"%s\") failed\n", path);
        return 1;
    }

    long files = 0;
    long dirs  = 0;
    struct dirent* e;

    while ((e = readdir(d)) != 0) {
        /*
         * Build the full path for stat().  With path being a
         * relative or absolute string the kernel understands, a
         * plain concatenation with '/' works: "." -> "./name",
         * "/bin" -> "/bin/name", "0:/foo" -> "0:/foo/name".
         */
        char entry_path[256];
        size_t plen = strlen(path);
        if (plen > 0 && path[plen - 1] != '/') {
            if (plen + 1 + strlen(e->d_name) + 1 > sizeof(entry_path)) continue;
            memcpy(entry_path, path, plen);
            entry_path[plen] = '/';
            strcpy(entry_path + plen + 1, e->d_name);
        } else {
            if (plen + strlen(e->d_name) + 1 > sizeof(entry_path)) continue;
            strcpy(entry_path, path);
            strcpy(entry_path + plen, e->d_name);
        }

        struct stat st;
        int sr = stat(entry_path, &st);

        int is_dir = 0;
        long size  = 0;
        if (sr == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size   = (long)st.st_size;
        } else {
            is_dir = (e->d_type == DT_DIR);
        }

        if (is_dir) {
            printf("<DIR>  %s\n", e->d_name);
            dirs++;
        } else {
            printf("FILE   %s  (%ld bytes)\n", e->d_name, size);
            files++;
        }
    }

    closedir(d);

    printf("\n%ld file(s), %ld directory(ies)\n", files, dirs);
    return 0;
}
