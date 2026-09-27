#include <unistd.h>

static long raw_syscall(long nr, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

int main(void) {
    raw_syscall(1, 1, (long)"A\n", 2);

    long pid = raw_syscall(57, 0, 0, 0);

    if (pid == 0) {
        raw_syscall(1, 1, (long)"C\n", 2);
        raw_syscall(60, 0, 0, 0);   /* SYS_exit */
    } else {
        raw_syscall(1, 1, (long)"P\n", 2);
    }
    return 0;
}
