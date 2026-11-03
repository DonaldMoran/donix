/*
 * exec_churn.c -- fork / execve / wait4, many times.
 *
 * (original header comment unchanged -- kept for context)
 *
 * TIMING NOTE.  clock_gettime(CLOCK_MONOTONIC) here has 1 ms
 * granularity, so per-phase ms are often 0.  To see the real
 * distribution, we also record per-round nanoseconds and the
 * maximum single-round total.  The "prnt" phase is measured
 * around the *reporting* printf only, and reported once.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

#define ROUNDS 24

static long long now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

static int fails = 0;

int main(void) {
    printf("=== exec_churn ===\n");

    char* const argv[] = { (char*)"/usr/bin/CHURN_HELPER", NULL };
    char* const envp[] = { NULL };

    long long total_t0 = now_ns();
    long long round_ns[ROUNDS];
    long long fork_ns[ROUNDS];
    long long wait_ns[ROUNDS];
    long long prnt_ns[ROUNDS];
    long long round_min = -1, round_max = -1, round_sum = 0;
    int round_n = 0;

    for (int i = 0; i < ROUNDS; i++) {
        long long rt0 = now_ns();
        errno = 0;
        pid_t pid = fork();
        long long rt_fork = now_ns();
        if (pid < 0) {
            printf("FAIL round %d: fork failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }

        if (pid == 0) {
            execve("/usr/bin/CHURN_HELPER", argv, envp);
            printf("FAIL round %d: child execve failed: %s\n",
                   i, strerror(errno));
            _exit(1);
        }

        int status = 0;
        errno = 0;
        long long rt_wait0 = now_ns();
        pid_t w = wait4(pid, &status, 0, NULL);
        long long rt_wait1 = now_ns();
        if (w < 0) {
            printf("FAIL round %d: wait4 failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("FAIL round %d: child status=0x%x (exited=%d code=%d)\n",
                   i, status, WIFEXITED(status),
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            fails++;
            break;
        }

        long long rt1 = now_ns();
        long long rn = (rt0 >= 0 && rt1 >= 0) ? (rt1 - rt0) : -1;
        long long fk = (rt0 >= 0 && rt_fork >= 0) ? (rt_fork - rt0) : -1;
        long long wt = (rt_wait0 >= 0 && rt_wait1 >= 0) ? (rt_wait1 - rt_wait0) : -1;
        fork_ns[round_n] = fk;
        wait_ns[round_n] = wt;
        round_ns[round_n] = rn;
        round_n++;

        if (rn >= 0) {
            if (round_min < 0 || rn < round_min) round_min = rn;
            if (rn > round_max) round_max = rn;
            round_sum += rn;
        }

        long long pt0 = now_ns();
        printf("round %2d: total %6lld us  fork %6lld  wait %6lld\n",
               i, rn / 1000, fk / 1000, wt / 1000);
        long long pt1 = now_ns();
        prnt_ns[round_n - 1] = (pt0 >= 0 && pt1 >= 0) ? (pt1 - pt0) : -1;
    }

    long long total_t1 = now_ns();

    if (total_t0 >= 0 && total_t1 >= 0) {
        long long total = total_t1 - total_t0;
        long long mean = (round_n > 0 && round_min >= 0) ? (round_sum / round_n) : -1;
        printf("total: %lld us over %d rounds (mean %lld us, min %lld us, max %lld us)\n",
               total / 1000, round_n, mean / 1000, round_min / 1000, round_max / 1000);
        /* Per-round total in ns is the real signal: the bimodality is
         * visible here without the 1 ms truncation. */
        printf("per-round total (us):");
        for (int i = 0; i < round_n; i++) {
            printf(" %lld", round_ns[i] / 1000);
        }
        printf("\n");
        printf("per-round wait  (us):");
        for (int i = 0; i < round_n; i++) {
            printf(" %lld", wait_ns[i] / 1000);
        }
        printf("\n");
        printf("per-round prnt  (us):");
        for (int i = 0; i < round_n; i++) {
            printf(" %lld", prnt_ns[i] / 1000);
        }
        printf("\n");
    } else {
        printf("total: unmeasurable (clock_gettime failed)\n");
    }

    if (fails == 0) {
        printf("ok   %d rounds of fork/execve/wait4, all children exit 0\n", ROUNDS);
        printf("EXEC_CHURN-ALL-PASS\n");
        return 0;
    }
    printf("EXEC_CHURN: %d failed\n", fails);
    return 1;
}
