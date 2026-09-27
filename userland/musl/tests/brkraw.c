#include <unistd.h>
#include <stdint.h>

static void puthex64(uint64_t v) {
    char b[20]; int n = 0;
    b[n++]='0'; b[n++]='x';
    for (int i=15;i>=0;i--){unsigned d=(v>>(i*4))&0xf; b[n++]= d<10?('0'+d):('a'+d-10);}
    b[n++]='\n';
    write(1, b, n);
}

int main(void) {
    /* read fsbase — if musl's init worked, this is somewhere in the TLS region */
    uint64_t fs;
    __asm__ volatile ("rdfsbase %0" : "=r"(fs));
    write(1, "FS=", 3); puthex64(fs);

    /* ask for current break via the raw syscall, bypassing libc wrappers */
    long cur;
    __asm__ volatile ("syscall" : "=a"(cur) : "a"(12L), "D"(0L) : "rcx","r11","memory");
    write(1, "BRK0=", 5); puthex64((uint64_t)cur);

    /* try a small extension */
    long want = cur + 4096;
    long got;
    __asm__ volatile ("syscall" : "=a"(got) : "a"(12L), "D"(want) : "rcx","r11","memory");
    write(1, "BRKN=", 5); puthex64((uint64_t)got);
    write(1, "WANT=", 5); puthex64((uint64_t)want);

    return 0;
}
