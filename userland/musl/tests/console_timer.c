#include <stdio.h>
#include <time.h>
#include <unistd.h>

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

int main(void) {
    /* Force line buffering so each printf does one write. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("=== console_timer ===\n");
    for (int i = 0; i < 20; i++) {
        long long a = now_ns();
        printf("line %02d: the quick brown fox jumps over the lazy dog\n", i);
        long long b = now_ns();
        printf("  printf returned in %lld us\n", (b - a) / 1000);
    }
    /* Now time a raw write() with a pre-formatted buffer. */
    char buf[128];
    for (int i = 0; i < 20; i++) {
        int n = snprintf(buf, sizeof buf, "raw %02d: the quick brown fox jumps over the lazy dog\n", i);
        long long a = now_ns();
        write(1, buf, n);
        long long b = now_ns();
        printf("  write returned in %lld us\n", (b - a) / 1000);
    }
    printf("CONSOLE_TIMER-DONE\n");
    return 0;
}
