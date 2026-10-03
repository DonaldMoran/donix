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

#define ROUNDS 24

static int fails = 0;

int main(void) {
    printf("=== exec_churn ===\n");

    char* const argv[] = { (char*)"/usr/bin/CHURN_HELPER", NULL };
    char* const envp[] = { NULL };

    for (int i = 0; i < ROUNDS; i++) {
        errno = 0;
        pid_t pid = fork();
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
        pid_t w = wait4(pid, &status, 0, NULL);
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
