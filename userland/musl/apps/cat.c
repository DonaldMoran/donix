#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        write(2, "usage: cat FILE\n", 16);
        return 1;
    }

    char path[64];
    {
        int i = 0;
        path[i++] = '0';
        path[i++] = ':';
        path[i++] = '/';
        for (const char* p = argv[1]; *p; p++) {
            if (i >= (int)sizeof(path) - 1) {
                write(2, "cat: path too long\n", 19);
                return 1;
            }
            path[i++] = *p;
        }
        path[i] = 0;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        write(2, "cat: cannot open\n", 17);
        return 2;
    }

    char buf[512];
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            close(fd);
            write(2, "cat: read error\n", 16);
            return 3;
        }
        if (r == 0) break;
        int written = 0;
        while (written < r) {
            int w = write(1, buf + written, r - written);
            if (w <= 0) {
                close(fd);
                return 3;
            }
            written += w;
        }
    }

    close(fd);
    return 0;
}
