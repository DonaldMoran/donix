#include <unistd.h>
#include <stdint.h>
#include <sys/uio.h>

static void puthex64(uint64_t v) {
    char b[32]; int n = 0;
    b[n++]='R'; b[n++]='1'; b[n++]='0'; b[n++]='-';
    b[n++]='A'; b[n++]='F'; b[n++]='T'; b[n++]='E'; b[n++]='R'; b[n++]='=';
    b[n++]='0'; b[n++]='x';
    for (int i = 15; i >= 0; i--) {
        unsigned d = (v >> (i * 4)) & 0xf;
        b[n++] = d < 10 ? ('0' + d) : ('a' + d - 10);
    }
    b[n++] = '\n';
    write(1, b, n);
}

int main(void) {
    struct iovec iov[2];
    iov[0].iov_base = (void*)"ABC";
    iov[0].iov_len  = 3;
    iov[1].iov_base = (void*)"DEF\n";
    iov[1].iov_len  = 4;

    long ret;
    register uint64_t r10_reg asm("r10") = 0xDEADBEEFCAFEBABEULL;

    __asm__ volatile (
        "syscall"
        : "=a"(ret), "+r"(r10_reg)
        : "a"(20L), "D"(1L), "S"(iov), "d"(2L)
        : "rcx", "r11", "memory"
    );

    puthex64(r10_reg);
    return (ret == 7) ? 0 : 1;
}
