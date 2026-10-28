/*
 * sigspin.c -- slice 3 acceptance: a signal delivered on the tick.
 *
 * The child installs a SIGUSR1 handler, then enters a loop that makes
 * NO syscalls.  The parent sleeps 200 ms, kills the child with
 * SIGUSR1, sleeps another 500 ms, kills with SIGKILL (the child never
 * exits on its own), and wait4s.  Because the child is inside a
 * no-syscall loop when the signal is raised, the ONLY delivery path
 * that can fire is the tick's -- signal_deliver_frame_iretq, slice 3.
 * Slice 2's syscall-return path is unreachable: the child has no
 * syscall in flight.
 *
 * The observable is a three-step sequence:
 *
 *     SPIN: loop start          (child, before the loop)
 *     SPIN: got SIGUSR1         (handler, on the tick)
 *     SPIN: HANDLER-RAN i=<n>   (loop, first iteration after return)
 *
 * If got-SIGUSR1 prints and HANDLER-RAN does not, the handler ran but
 * sys_rt_sigreturn did not resume the loop -- the frame rewrite is
 * wrong.  If neither prints, the tick never delivered.
 *
 * Raw syscalls, the same sc1/sc3 helpers kill_self.c uses.
 */
#include <unistd.h>
#include <stdint.h>

static long sc1(long n, long a) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c),
                       "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}

static void puts_raw(const char *s) {
    long len = 0;
    while (s[len]) len++;
    sc3(1, 1, (long)s, len);
}
static void putdec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i--] = '\n';
    if (v == 0) { buf[i--] = '0'; }
    else { while (v) { buf[i--] = (char)('0' + (v % 10)); v /= 10; } }
    sc3(1, 1, (long)&buf[i + 1], (long)(20 - i));
}

/* The restorer: exactly what musl's __restore_rt does.  The kernel
 * enters the handler with RSP = frame (NOT frame+8); the handler's
 * epilogue `pop rbp; ret` pops the pushed rbp, then frame[0], which
 * is this address. */
__attribute__((naked)) static void restorer_trampoline(void) {
    __asm__ volatile(
        "mov $15, %rax\n\t"
        "syscall\n\t"
        "hlt\n\t"
    );
}

static volatile int handler_flag = 0;

static void handler(long sig) {
    (void)sig;
    handler_flag = 1;
    puts_raw("SPIN: got SIGUSR1\n");
    /* Normal return: epilogue pops rbp then frame[0] (restorer),
     * which issues syscall 15 and resumes the interrupted loop. */
}

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask[1];
};

#define SIGUSR1 10
#define SIGKILL 9

static void child_loop(void) {
    struct ksigaction sa;
    sa.handler  = (uint64_t)handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer_trampoline;
    sa.mask[0]  = 0;
    long r = sc3(13, SIGUSR1, (long)&sa, 0);
    if (r != 0) {
        puts_raw("SPIN: rt_sigaction failed\n");
        sc1(60, 1);
    }
    puts_raw("SPIN: loop start\n");

    /* NO SYSCALLS in this loop until handler_flag is seen.  That is
     * what makes the signal reachable only on the tick. */
    volatile uint64_t i = 0;
    for (;;) {
        i++;
        if (handler_flag) {
            handler_flag = 0;
            puts_raw("SPIN: HANDLER-RAN i=");
            putdec(i);
            /* The loop resumed after the handler returned, which
             * is the observable slice 3 needs.  Exit cleanly:
             * SIGKILL is not a forced kill on this kernel (its
             * default action is slice 5), so the parent cannot
             * end us with kill(2).  exit(0) is a syscall made
             * AFTER the signal was delivered, so it does not
             * create a slice-2 delivery window for the signal
             * under test. */
            sc1(60, 0);
        }
    }
}

int main(void) {
    puts_raw("SPIN: fork\n");
    long child = sc1(57, 0);   /* fork */
    if (child == 0) {
        child_loop();          /* does not return */
    }
    if (child < 0) {
        puts_raw("SPIN: fork failed\n");
        sc1(60, 1);
    }

    /* ~200 ms: 20 ticks at 100 Hz.  The child is in the no-syscall
     * loop, so the kill lands mid-loop. */
    struct { int64_t tv_sec; int64_t tv_nsec; } ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = 200000000LL;
    sc3(35, (long)&ts, 0, 0);

    puts_raw("SPIN: parent kill child\n");
    long kr = sc3(62, child, SIGUSR1, 0);
    if (kr != 0) {
        puts_raw("SPIN: kill failed\n");
        sc1(60, 1);
    }

    /* ~500 ms: ample time for the tick to deliver, the handler to
     * run, sys_rt_sigreturn to resume the loop, and the loop to
     * print HANDLER-RAN. */
    ts.tv_sec  = 0;
    ts.tv_nsec = 500000000LL;
    sc3(35, (long)&ts, 0, 0);

    /* No SIGKILL: on this kernel it sets a pending bit, it does
     * not terminate (default actions are slice 5).  The child
     * exits itself after printing HANDLER-RAN; wait4 reaps it. */
    int status = 0;
    long wp = sc6(61, child, (long)&status, 0, 0, 0, 0);   /* wait4 */
    puts_raw("SPIN: parent reaped, status=");
    putdec((uint64_t)(wp < 0 ? (uint64_t)wp : (uint64_t)status));

    sc1(60, 0);
}
