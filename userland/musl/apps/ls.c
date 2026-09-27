#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char path[128];
    if (argc >= 2 && argv[1][0] != '\0') {
        const char* tok = argv[1];
        int has_prefix = 0;
        for (const char* p = tok; *p; p++) {
            if (p[0] == ':' && p[1] == '/') { has_prefix = 1; break; }
        }
        if (has_prefix) {
            int i = 0;
            while (tok[i] && i < (int)sizeof(path) - 1) {
                path[i] = tok[i];
                i++;
            }
            path[i] = 0;
        } else {
            int i = 0;
            path[i++] = '0';
            path[i++] = ':';
            path[i++] = '/';
            for (const char* p = tok; *p && i < (int)sizeof(path) - 1; p++) {
                path[i++] = *p;
            }
            path[i] = 0;
        }
    } else {
        strcpy(path, "0:/");
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
        /* Build the full path for stat().  The directory path ends
         * with '/' (unless the user passed one without it), so a
         * plain concatenation works. */
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
            /* stat failed — fall back to d_type from dirent.  d_type
             * is DT_DIR for directories, DT_REG for files, and is
             * set correctly by sys_getdents64 from the FAT
             * attribute.  Size stays 0 in that case. */
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
