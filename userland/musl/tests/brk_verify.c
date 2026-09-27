#include <unistd.h>
#include <stdlib.h>

static void puthex(unsigned long v) {
    char buf[32];
    int n = 0;
    buf[n++] = 'p'; buf[n++] = '='; buf[n++] = '0'; buf[n++] = 'x';
    for (int i = 15; i >= 0; i--) {
        unsigned d = (v >> (i * 4)) & 0xf;
        buf[n++] = d < 10 ? ('0' + d) : ('a' + d - 10);
    }
    buf[n++] = '\n';
    write(1, buf, n);
}

int main(void) {
    char *p = malloc(4096);
    if (!p) {
        write(2, "malloc-null\n", 12);
        return 1;
    }
    puthex((unsigned long)p);

    for (int i = 0; i < 4096; i++) p[i] = (char)(i & 0xff);

    for (int i = 0; i < 4096; i++) {
        if (p[i] != (char)(i & 0xff)) {
            write(1, "VERIFY-FAIL\n", 12);
            return 2;
        }
    }
    write(1, "VERIFY-OK\n", 10);
    return 0;
}
