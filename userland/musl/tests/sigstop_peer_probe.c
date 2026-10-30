/*
 * sigstop_peer_probe.c -- timer-path stop delivery (03b).
 *
 * SIGSTOP_PROBE exercises the syscall-return delivery point: the
 * child raises SIGSTOP on itself and the return from its own kill
 * syscall is the delivery point.
 *
 * A peer SIGSTOP of a process RUNNING IN USER MODE never reaches
 * that point: the process is not making a syscall.  The timer's
 * dispatch is the only delivery point.  03b wires it.
 *
 * Shape:
 *
 *   child:
 *     install SIGCONT handler (on_cont -> _exit(0))
 *     install SIGSTOP handler?  NO -- SIGSTOP is uncatchable.
 *     write 'R' to pipe A       (about to busy-loop)
 *     for(;;) asm volatile("pause");   (never returns)
 *
 *   parent:
 *     read 'R' from pipe A      (child is spinning)
 *     nanosleep(100ms)          (parks; timer wakes it)
 *     kill(child, SIGSTOP)      (sets sig_pending bit 19)
 *     poll /proc/<child>/stat for 'T'   (bounded 30 x 10ms)
 *     kill(child, SIGCONT)      (SIGCONT-at-raise; 02d's path)
 *     read 'D' from pipe B (bounded 2s)
 *     waitpid; assert WIFEXITED && WEXITSTATUS == 0
 *
 * Pass = parent saw 'T' AND child exited 0.  SIGCONT resumed the
 * busy-looping child; the child's SIGCONT handler ran and exited.
 * If 03b is wrong, the child either never stops (T_seen=0, kill
 * SIGCONT arrives on a running process -> default-ignore -> child
 * keeps looping -> parent's wait4 blocks forever), or stops but
 * does not resume.
 *
 * The parent's SIGCONT path is 02d's SIGCONT-at-raise: it requeues
 * the STOPPED child and the timer dispatch resumes it.  The child's
 * SIGCONT handler runs when the resumed child delivers SIGCONT at
 * its next syscall-return point -- but the child is busy-looping
 * with NO syscalls!  So the SIGCONT handler runs on the *next tick*
 * the child is preempted.  That is fine: after resume, the timer
 * preempts, sees pending SIGCONT, delivers it, the handler runs and
 * _exit(0)s.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

static int read_byte(int fd, char* out) {
    ssize_t n;
    do { n = read(fd, out, 1); } while (n < 0 && errno == EINTR);
    return (n == 1);
}
static int write_byte(int fd, char c) {
    ssize_t n;
    do { n = write(fd, &c, 1); } while (n < 0 && errno == EINTR);
    return (n == 1);
}
static char read_proc_state(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char* rp = strrchr(buf, ')');
    if (!rp || rp[1] != ' ') return 0;
    return rp[2];
}

static void on_cont(int sig) {
    (void)sig;
    _exit(0);   /* the child is resumed; success is the parent's wait4 */
}

int main(void) {
    int ready[2], done[2];
    if (pipe(ready) != 0) { perror("pipe ready"); return 2; }
    if (pipe(done)  != 0) { perror("pipe done");  return 2; }

    pid_t child = fork();
    if (child < 0) { perror("fork"); return 2; }

    if (child == 0) {
        close(ready[0]);
        close(done[0]);

        /* SIGCONT handler -- installed BEFORE the handshake.  It is
         * the "resume worked" signal: _exit(0) from inside it is
         * the child's clean exit. */
        struct sigaction sa;
        sa.sa_handler = on_cont;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(SIGCONT, &sa, NULL) != 0) {
            perror("child sigaction SIGCONT");
            _exit(2);
        }
        /* SIGSTOP is uncatchable.  No sigaction for it. */

        if (!write_byte(ready[1], 'R')) _exit(3);
        close(ready[1]);

        /* Busy-loop in USER MODE: no syscalls, so the syscall-return
         * delivery point never fires.  Only the timer's dispatch
         * can observe a peer's SIGSTOP. */
        for (;;) __asm__ volatile("pause");
    }

    close(ready[1]);
    close(done[1]);

    char c;
    if (!read_byte(ready[0], &c) || c != 'R') {
        printf("parent: child did not signal ready\n");
        return 2;
    }
    close(ready[0]);
    printf("parent: child is busy-looping; sleeping 100ms\n");
    fflush(stdout);

    struct timespec ts100 = { .tv_sec = 0, .tv_nsec = 100000000L };
    struct timespec ts10  = { .tv_sec = 0, .tv_nsec =  10000000L };
    struct timespec rem;
    nanosleep(&ts100, &rem);

    if (kill(child, SIGSTOP) != 0) perror("kill(SIGSTOP)");

    int t_seen = 0;
    for (int i = 0; i < 30 && !t_seen; i++) {
        char sc = read_proc_state(child);
        if (sc == 'T') { t_seen = 1; break; }
        nanosleep(&ts10, &rem);
    }
    printf("parent: T_seen=%d\n", t_seen);
    fflush(stdout);

    if (kill(child, SIGCONT) != 0) perror("kill(SIGCONT)");

    /* Give the child a bounded window to resume and exit.  It has no
     * syscalls after resume, so the SIGCONT handler runs on the next
     * timer preemption; that is within one tick (10ms) plus the
     * _exit path.  Bound at 200 x 10ms = 2s. */
    int flags = fcntl(done[0], F_GETFL, 0);
    if (flags >= 0) fcntl(done[0], F_SETFL, flags | O_NONBLOCK);

    int st = 0, got_exit = 0;
    for (int i = 0; i < 200 && !got_exit; i++) {
        pid_t r = waitpid(child, &st, WNOHANG);
        if (r == child) { got_exit = 1; break; }
        if (r < 0 && errno != 0) break;
        nanosleep(&ts10, &rem);
    }
    close(done[0]);

    if (!got_exit) {
        printf("SIGSTOP-PEER-PROBE: FAIL (child did not exit within 2s; "
               "T_seen=%d)\n", t_seen);
        kill(child, SIGKILL);
        return 1;
    }
    printf("parent: status=0x%x WIFEXITED=%d WEXITSTATUS=%d "
           "WIFSIGNALED=%d WTERMSIG=%d\n",
           st, WIFEXITED(st),
           WIFEXITED(st) ? WEXITSTATUS(st) : -1,
           WIFSIGNALED(st), WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    fflush(stdout);

    if (WIFSIGNALED(st)) {
        printf("SIGSTOP-PEER-PROBE: FAIL (child killed by signal %d)\n",
               WTERMSIG(st));
        return 1;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("SIGSTOP-PEER-PROBE: FAIL (child did not exit 0)\n");
        return 1;
    }
    if (!t_seen) {
        printf("SIGSTOP-PEER-PROBE: FAIL (child exited but was never "
               "observed as T -- 03b's timer-path stop did not fire)\n");
        return 1;
    }
    printf("SIGSTOP-PEER-PROBE: PASS (T_seen=1; peer SIGSTOP stopped "
           "the busy-looping child; SIGCONT resumed and it exited 0)\n");
    return 0;
}
