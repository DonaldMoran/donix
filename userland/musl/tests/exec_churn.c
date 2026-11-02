/*
 * exec_churn.c -- fork / execve / wait4, many times.
 *
 * The bug this targets: pmm_alloc_page's zone scan starts at a
 * cursor and moves in one direction.  A page freed after the
 * cursor passed it is found only if the free's rewind moves the
 * cursor back to it; a page that was free the whole time on the
 * far side of the cursor is never found by a linear scan.  That
 * made a fresh boot after adding userland ELFs fail to allocate a
 * page-table page, and the process that needed it faulted on its
 * first fetch.
 *
 * Two things free pages at run time: munmap, and process exit.
 * mmap_stress exercises the first.  This test exercises the
 * second, and it exercises elf_load_into_process on every round:
 * each child execs churn_helper, which is a full ELF load with
 * the huge-page split, and then exits, which frees that load's
 * pages and its page tables.  Repeated, that moves the allocator
 * cursor and frees pages behind it, which is the state the scan
 * has to handle.
 *
 * The child's exit STATUS is checked.  execve should never return
 * in the child; if it does, or if the child exits nonzero, the
 * ELF load or the entry went wrong -- and "the ELF loaded but
 * faulted immediately" is exactly the failure this catches.  (A
 * parent cannot capture a child's stdout on donix, so status is
 * the channel.)
 *
 * Read-only on the disk.  Not a canary row; run by hand.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

#define ROUNDS 24

/* EXEC_CHURN-TIMING: per-round wall-clock ms, via clock_gettime. */
static long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
}

static int fails = 0;

int main(void) {
    printf("=== exec_churn ===\n");

    char* const argv[] = { (char*)"/usr/bin/CHURN_HELPER", NULL };
    char* const envp[] = { NULL };

    long long total_t0 = now_ms();
    long long round_ms[ROUNDS];       /* per round total */
    /* EXEC_CHURN-PHASES: per-round fork / wait / printf. */
    long long fork_ms[ROUNDS];
    long long wait_ms[ROUNDS];
    long long prnt_ms[ROUNDS];
    long long round_min = -1, round_max = -1, round_sum = 0;
    int round_n = 0;

    for (int i = 0; i < ROUNDS; i++) {
        long long rt0 = now_ms();
        errno = 0;
        pid_t pid = fork();
        long long rt_fork = now_ms();
        if (pid < 0) {
            printf("FAIL round %d: fork failed: %s\n", i, strerror(errno));
            fails++;
            break;
        }

        if (pid == 0) {
            /* Child.  execve must not return. */
            execve("/usr/bin/CHURN_HELPER", argv, envp);
            /* If we get here, execve failed. */
            printf("FAIL round %d: child execve failed: %s\n",
                   i, strerror(errno));
            _exit(1);
        }

        /* Parent. */
        int status = 0;
        errno = 0;
        long long rt_wait0 = now_ms();
        pid_t w = wait4(pid, &status, 0, NULL);
        long long rt_wait1 = now_ms();
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

        long long rt1 = now_ms();
        long long rm = (rt0 < 0 || rt1 < 0) ? -1 : (rt1 - rt0);
        long long fk = (rt0 >= 0 && rt_fork >= 0) ? (rt_fork - rt0) : -1;
        long long wt = (rt_wait0 >= 0 && rt_wait1 >= 0) ? (rt_wait1 - rt_wait0) : -1;
        fork_ms[round_n] = fk;
        wait_ms[round_n] = wt;
        round_ms[round_n++] = rm;
        if (rm >= 0) {
            if (round_min < 0 || rm < round_min) round_min = rm;
            if (rm > round_max) round_max = rm;
            round_sum += rm;
        }
        /* EXEC_CHURN-PRNT: measure one printf per round. */
        long long pt0 = now_ms();
        if (rm >= 0) {
            printf("round %2d: total %3lld  fork %3lld  wait %3lld  prnt %3lld  ms\n",
                   i, rm, fk, wt, (long long)0);
        }
        long long pt1 = now_ms();
        prnt_ms[round_n - 1] = (pt0 >= 0 && pt1 >= 0) ? (pt1 - pt0) : -1;
        /* Reporting line: its own cost equals prnt_ms above. */
        printf("round %2d: total %3lld  fork %3lld  wait %3lld  prnt %3lld  ms\n",
               i, rm, fk, wt, prnt_ms[round_n - 1]);
    }

    long long total_t1 = now_ms();

    if (total_t0 >= 0 && total_t1 >= 0) {
        long long total = total_t1 - total_t0;
        long long mean = (round_n > 0 && round_min >= 0) ? (round_sum / round_n) : -1;
        printf("total: %lld ms over %d rounds (mean %lld ms, min %lld ms, max %lld ms)\n",
               total, round_n, mean, round_min, round_max);
    } else {
        printf("total: unmeasurable (clock_gettime failed)\n");
    }

    if (fails == 0) {
        printf("ok   %d rounds of fork/execve/wait4, all children exit 0\n",
               ROUNDS);
        printf("EXEC_CHURN-ALL-PASS\n");
        return 0;
    }
    printf("EXEC_CHURN: %d failed\n", fails);
    return 1;
}
