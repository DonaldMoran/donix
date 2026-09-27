#include <unistd.h>
#include <stdint.h>

static void puthex64(uint64_t v) {
    char b[20]; int n = 0;
    b[n++]='0'; b[n++]='x';
    for (int i=15;i>=0;i--){unsigned d=(v>>(i*4))&0xf; b[n++]= d<10?('0'+d):('a'+d-10);}
    b[n++]='\n';
    write(1, b, n);
}

static long brk_sys(long addr) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(12L), "D"(addr) : "rcx","r11","memory");
    return ret;
}

int main(void) {
    long cur = brk_sys(0);
    write(1, "start=", 6); puthex64((uint64_t)cur);

    /* try 64 KB */
    long want1 = cur + 0x10000;
    long got1 = brk_sys(want1);
    write(1, "64K want=", 9); puthex64((uint64_t)want1);
    write(1, "64K got =", 9); puthex64((uint64_t)got1);

    /* try 1 MB more */
    long want2 = got1 + 0x100000;
    long got2 = brk_sys(want2);
    write(1, "1M want =", 9); puthex64((uint64_t)want2);
    write(1, "1M got  =", 9); puthex64((uint64_t)got2);

    return 0;
}
