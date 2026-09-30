#include <unistd.h>
#include <fcntl.h>

/*
 * Stream fd to stdout until EOF.
 *
 * Returns 0 on success, non-zero on error.  Both read and write
 * are looped: read(2) may return short, and write(2) may write
 * fewer bytes than asked.  A read error or a write error stops
 * this fd's stream (the caller decides whether to continue with
 * the next file).
 */
static int cat_fd(int fd, char* buf, unsigned long bufsize) {
    for (;;) {
        int r = read(fd, buf, bufsize);
        if (r < 0) {
            write(2, "cat: read error\n", 16);
            return 1;
        }
        if (r == 0) break;

        int written = 0;
        while (written < r) {
            int w = write(1, buf + written, r - written);
            if (w <= 0) {
                write(2, "cat: write error\n", 17);
                return 1;
            }
            written += w;
        }
    }
    return 0;
}

/*
 * Write a small message to stderr: a fixed prefix, a caller-
 * supplied string, then a newline.  Used for per-file error
 * messages so the failing file's name is visible when cat is
 * looping over several arguments.
 */
static void err_msg(const char* prefix, const char* what) {
    unsigned long n;

    n = 0; while (prefix[n]) n++;
    write(2, prefix, n);

    if (what) {
        n = 0; while (what[n]) n++;
        write(2, what, n);
    }

    write(2, "\n", 1);
}

int main(int argc, char **argv) {
    /*
     * 4096 matches the busybox copy buffer size (FEATURE_COPYBUF_KB=4)
     * and is a better match for the kernel's read/write granularity
     * than the old 512.
     */
    char buf[4096];

    /*
     * No arguments: read stdin, like busybox cat.  This is the path
     * that makes `cat < file` work now that musl_sh parses
     * redirection.  fd 0 is the redirected file (or the console,
     * if nothing was redirected).
     */
    if (argc < 2) {
        return cat_fd(0, buf, sizeof(buf));
    }

    /*
     * One or more arguments.  Each is a filename, except "-", which
     * means stdin.  Files are concatenated in order; a failure on
     * one file is reported and cat moves on to the next, returning
     * non-zero at the end.  This matches busybox cat.
     *
     * Paths are passed through unchanged: the kernel's open(2)
     * resolves relative paths against the process's cwd, so
     *
     *   cat foo        -> "foo"        -> cwd/foo
     *   cat /hello.txt -> "/hello.txt" -> absolute
     *   cat 0:/foo     -> "0:/foo"     -> FatFs path, passed through
     */
    int status = 0;

    for (int i = 1; i < argc; i++) {
        const char* path = argv[i];

        if (path[0] == '-' && path[1] == 0) {
            /* "-" means stdin. */
            if (cat_fd(0, buf, sizeof(buf)) != 0) {
                status = 1;
            }
            continue;
        }

        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            err_msg("cat: ", path);
            status = 1;
            continue;
        }

        if (cat_fd(fd, buf, sizeof(buf)) != 0) {
            status = 1;
        }
        close(fd);
    }

    return status;
}
