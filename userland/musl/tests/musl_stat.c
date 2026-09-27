#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>

int main(void) {
    /* ---- fstat via open() ---- */
    int fd = open("0:/HELLO-WORLD.TXT", O_RDONLY);
    if (fd < 0) { printf("STAT-OPEN-FAIL\n"); return 1; }

    struct stat st;
    long fr = fstat(fd, &st);
    close(fd);

    printf("STAT-FR %ld\n", fr);
    printf("STAT-SIZE %ld\n", (long)st.st_size);
    printf("STAT-MODE 0x%lx\n", (unsigned long)st.st_mode);

    if (fr != 0) {
        printf("STAT-FR-FAIL\n");
        return 1;
    }
    if (st.st_size != 180) {
        printf("STAT-SIZE-FAIL\n");
        return 1;
    }
    if ((st.st_mode & 0170000) != 0100000) {   /* S_IFREG */
        printf("STAT-MODE-FAIL\n");
        return 1;
    }
    printf("STAT-OK\n");

    /* ---- stat via path ---- */
    struct stat st2;
    long sr = stat("0:/HELLO-WORLD.TXT", &st2);

    printf("STAT2-SR %ld\n", sr);
    printf("STAT2-SIZE %ld\n", (long)st2.st_size);
    printf("STAT2-MODE 0x%lx\n", (unsigned long)st2.st_mode);

    if (sr != 0) {
        printf("STAT2-FR-FAIL\n");
        return 1;
    }
    if (st2.st_size != 180) {
        printf("STAT2-SIZE-FAIL\n");
        return 1;
    }
    if ((st2.st_mode & 0170000) != 0100000) {
        printf("STAT2-MODE-FAIL\n");
        return 1;
    }
    printf("STAT2-OK\n");
    return 0;
}
