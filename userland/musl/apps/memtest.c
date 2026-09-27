#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("[memtest] musl malloc/free via mmap\n");

    size_t N = 4096;
    unsigned char *buf = (unsigned char *)malloc(N);
    if (!buf) {
        printf("[memtest] FAIL: malloc(%lu) returned NULL\n",
               (unsigned long)N);
        return 1;
    }
    printf("  malloc(%lu) = %p\n", (unsigned long)N, (void *)buf);

    for (size_t i = 0; i < N; i++) {
        buf[i] = (unsigned char)(i & 0xFF);
    }
    printf("  wrote %lu bytes\n", (unsigned long)N);

    for (size_t i = 0; i < N; i++) {
        if (buf[i] != (unsigned char)(i & 0xFF)) {
            printf("  MISMATCH at %lu: got 0x%02x, expected 0x%02x\n",
                   (unsigned long)i, buf[i],
                   (unsigned char)(i & 0xFF));
            printf("[memtest] FAIL: readback mismatch\n");
            free(buf);
            return 1;
        }
    }
    printf("  readback OK\n");

    free(buf);
    printf("  free() returned\n");

    size_t M = 8192;
    unsigned char *buf2 = (unsigned char *)malloc(M);
    if (!buf2) {
        printf("  second malloc(%lu) returned NULL\n",
               (unsigned long)M);
        printf("[memtest] FAIL: second malloc\n");
        return 1;
    }
    printf("  second malloc(%lu) = %p\n",
           (unsigned long)M, (void *)buf2);
    for (size_t i = 0; i < M; i++) {
        buf2[i] = 0xAA;
    }
    printf("  wrote %lu bytes to second buffer\n",
           (unsigned long)M);
    free(buf2);

    printf("[memtest] PASS\n");
    return 0;
}
