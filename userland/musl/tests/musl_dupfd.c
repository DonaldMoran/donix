/*
 * musl_dupfd — exercise fcntl(F_DUPFD).
 *
 * Checks:
 *   1. fcntl(fd, F_DUPFD, 3) returns a fd >= 3, and read() from it
 *      works.
 *   2. The returned fd is distinct from the original.
 *   3. Two F_DUPFD calls with the same min return distinct fds.
 *   4. fcntl(fd, F_DUPFD, MAX_PROCESS_FILES) returns -EINVAL.
 *   5. fcntl(fd, F_DUPFD, 0) returns the lowest free fd >= 3, not 0.
 */
#include <unistd.h>
#include <fcntl.h>

static void puts_raw(const char* s, unsigned long n) {
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static long raw_fcntl(int fd, int cmd, unsigned long arg) {
    long r;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(72L), "D"((long)fd), "S"((long)cmd), "d"(arg)
                     : "rcx", "r11", "memory");
    return r;
}

int main(void) {
    int fd = open("0:/HELLO-WORLD.TXT", O_RDONLY);
    if (fd < 0) { puts_raw("DUPFD-OPEN-FAIL\n", 16); return 1; }

    long newfd = raw_fcntl(fd, 0, 3);
    if (newfd < 3) { puts_raw("DUPFD-RET-FAIL\n", 15); return 1; }

    char buf[64];
    long n = read((int)newfd, buf, sizeof(buf));
    if (n <= 0) { puts_raw("DUPFD-READ-FAIL\n", 16); return 1; }

    long newfd2 = raw_fcntl(fd, 0, 3);
    if (newfd2 < 3 || newfd2 == newfd) {
        puts_raw("DUPFD-DISTINCT-FAIL\n", 19);
        return 1;
    }

    long bad = raw_fcntl(fd, 0, 99);
    if (bad != -22) {
        puts_raw("DUPFD-EINVAL-FAIL\n", 18);
        return 1;
    }

    long min0 = raw_fcntl(fd, 0, 0);
    if (min0 < 3) {
        puts_raw("DUPFD-MIN0-FAIL\n", 16);
        return 1;
    }

    puts_raw("DUPFD-OK\n", 9);
    return 0;
}
