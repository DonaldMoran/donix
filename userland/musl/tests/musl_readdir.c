#include <dirent.h>
#include <unistd.h>

static void puts_raw(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(s), "d"(n)
                     : "rcx", "r11", "memory");
}

static void put_dec(long v) {
    char b[24]; int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t > 0) b[n++] = tmp[--t];
    b[n++] = '\n';
    __asm__ volatile("syscall"
                     :
                     : "a"(1L), "D"(1L), "S"(b), "d"((long)n)
                     : "rcx", "r11", "memory");
}

int main(void) {
    DIR* d = opendir("0:/");
    if (!d) { puts_raw("OPENDIR-FAIL\n"); return 1; }

    long count = 0;
    struct dirent* e;
    while ((e = readdir(d)) != 0) {
        puts_raw(e->d_name);
        puts_raw("\n");
        count++;
    }
    closedir(d);

    puts_raw("READDIR-DONE count=");
    put_dec(count);
    return 0;
}
