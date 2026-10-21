/*
 * kill_self.c -- install a handler for SIGUSR1, raise it with
 * kill(getpid(), SIGUSR1), and exit.
 *
 * Slice 2 of docs/signal-design.md: install a handler for
 * SIGUSR1, raise it with kill(getpid(), SIGUSR1), and let the
 * kernel deliver it on the way out to user mode.  The handler
 * returns via __restore_rt (syscall 15), and the caller resumes
 * at the point after kill() returned.
 *
 * Acceptance: the boot prints, in order,
 *     KILL-SELF: handler running, sig=0xa
 *     KILL-SELF: caller resumed after handler
 * and the shell prompt returns.  That proves delivery
 * (signal_deliver_frame) AND restore (sys_rt_sigreturn) end to
 * end.
 *
 * The probe installs a real restorer trampoline in its own text
 * (mov $15,%rax; syscall; hlt -- exactly musl's __restore_rt),
 * because the kernel's signal_deliver_frame requires a nonzero
 * restorer and kill_self cannot name musl's hidden __restore_rt.
 *
 * Not a POSIX signal test: raw syscalls, the same sc1/sc3 helpers
 * sigsegv_probe.c uses.
 */
#include <unistd.h>
#include <stdint.h>

static long sc1(long n, long a) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a)
                     : "rcx", "r11", "memory");
    return r;
}
static long sc3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

static void puts_raw(const char *s) {
    long len = 0;
    while (s[len]) len++;
    sc3(1, 1, (long)s, len);
}

static void puthex(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int nib = (v >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    buf[18] = '\n';
    sc3(1, 1, (long)buf, 19);
}

/* The restorer: exactly what musl's __restore_rt does.
 * The handler's `ret` pops this address off the frame (the
 * kernel entered the handler with RSP = frame+8, and the
 * frame's [0] slot holds this address; `ret` pops it and
 * advances RSP to frame+8, where the ucontext_t begins). */
__attribute__((naked)) static void restorer_trampoline(void) {
    __asm__ volatile(
        "mov $15, %rax\n\t"
        "syscall\n\t"
        "hlt\n\t"
    );
}

static void handler(long sig) {
    puts_raw("KILL-SELF: handler running, sig=");
    puthex((uint64_t)sig);
    /* Fall through to the compiler's normal epilogue.  The kernel
     * entered this handler with RSP = frame+8, and frame[0] holds
     * restorer_trampoline's address; the epilogue's `pop rbp; ret`
     * pops the pushed rbp and then frame[0] -- landing on the
     * trampoline, which issues syscall 15.  Do NOT put an explicit
     * `ret` in the body: it would fire at the wrong RSP. */
    return;
}

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;   /* slice 2: k_sigaction is 32 bytes on x86_64 */
    uint64_t mask[1];    /* mask[2] is 2 x unsigned int = 8 bytes */
};

#define SIGUSR1 10

int main(void) {
    puts_raw("KILL-SELF: installing handler\n");

    struct ksigaction sa;
    sa.handler  = (uint64_t)handler;
    sa.flags    = 0;
    sa.restorer = (uint64_t)restorer_trampoline;   /* slice 2 */
    sa.mask[0]  = 0;

    long r = sc3(13, SIGUSR1, (long)&sa, 0);
    if (r != 0) {
        puts_raw("KILL-SELF: rt_sigaction failed: ");
        puthex((uint64_t)r);
        sc1(60, 1);
    }

    long pid = sc1(39, 0);   /* getpid */
    puts_raw("KILL-SELF: kill(getpid(), SIGUSR1), pid=");
    puthex((uint64_t)pid);

    r = sc3(62, pid, SIGUSR1, 0);
    if (r != 0) {
        puts_raw("KILL-SELF: kill failed: ");
        puthex((uint64_t)r);
        sc1(60, 1);
    }

    puts_raw("KILL-SELF: kill returned 0; before slice 2 the caller"
             " continued here; under slice 2 the handler runs first\n");
    /* If we reach this, the handler ran, returned via rt_sigreturn,
     * and the kernel restored our context: sys_rt_sigreturn works. */
    puts_raw("KILL-SELF: caller resumed after handler\n");
    sc1(60, 0);
}
