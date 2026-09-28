#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        write(2, "usage: cat FILE\n", 16);
        return 1;
    }

    /*
     * Pass the filename through unchanged.  The kernel's open(2)
     * resolves relative paths against the process's cwd, so:
     *
     *   cat foo        -> "foo"        -> cwd/foo
     *   cat /hello.txt -> "/hello.txt" -> absolute
     *   cat 0:/foo     -> "0:/foo"     -> FatFs path, passed through
     *
     * The old code prepended "0:/", forcing every open to the FAT
     * root and ignoring `cd`.
     */
    const char* path = argv[1];

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
