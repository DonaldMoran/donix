#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>

/* ============================================================
 * Syscall numbers — Linux x86_64 ABI
 *
 * Numbers 0..~450 are reserved for the Linux x86_64 syscall
 * table.  Every case in syscall_dispatch that shares a number
 * with a Linux syscall musl uses is at that Linux number.
 *
 * This file is the source of truth for the kernel's syscall
 * numbering.  When adding or changing an entry, verify the
 * number against the canonical Linux x86_64 table,
 * arch/x86/entry/syscalls/syscall_64.tbl.  Do NOT guess a
 * number from the name of the syscall that appears to be
 * missing: the "Unknown syscall: N" diagnostic names the
 * number the caller used, which may be a different syscall
 * entirely.  (See docs/gotchas.md: mkdir was at 7, which is
 * poll; setsid was at 107, which is geteuid.)
 *
 * Numbers 500+ are donix-private.  They are for syscalls that
 * exist only in donix and have no Linux equivalent.  As of A5
 * step 5, the only remaining 500+ number is SYS_REBOOT (503).
 * musl never calls it; it is reached only via the newlib
 * userland's reboot() wrapper, which is deleted at A5 step 7,
 * leaving raw syscall 503 as the only way in.
 * ============================================================ */

/* --- Linux x86_64 numbers, implemented --- */
#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_STAT            4
#define SYS_FSTAT           5
#define SYS_LSTAT           6
#define SYS_POLL            7
#define SYS_MMAP            9
#define SYS_MPROTECT        10
#define SYS_MUNMAP          11
#define SYS_BRK             12
#define SYS_RT_SIGACTION    13
#define SYS_RT_SIGPROCMASK  14
#define SYS_IOCTL           16
#define SYS_WRITEV          20
#define SYS_ACCESS          21
#define SYS_DUP2            33
#define SYS_GETPID          39
#define SYS_FORK            57
#define SYS_EXECVE          59
#define SYS_EXIT            60
#define SYS_WAIT4           61
#define SYS_FCNTL           72
#define SYS_GETCWD          79
#define SYS_MKDIR           83
#define SYS_UNLINK          87
#define SYS_GETPPID         110
#define SYS_SETSID          112
#define SYS_ARCH_PRCTL      158
#define SYS_GETDENTS64      217
#define SYS_SET_TID_ADDRESS 218
#define SYS_EXIT_GROUP      231
#define SYS_NEWFSTATAT      262
#define SYS_FACCESSAT       269
#define SYS_SET_ROBUST_LIST 273
#define SYS_GETRANDOM       318
#define SYS_RSEQ            334

/* --- donix-private numbers (500+) --- */
#define SYS_REBOOT      503

/*
 * SYS_EXECVE (59) — Linux execve.
 *
 * arg0: const char* path        (user pointer, NUL-terminated)
 * arg1: char* const argv[]      (user pointer to array of user string
 *                                pointers; NULL if argc == 0)
 * arg2: char* const envp[]      (ignored for now)
 * returns: only on failure, as -errno
 */
#define EXEC_MAX_ARGC 16
#define EXEC_MAX_ARG_LEN 256

/*
 * SYS_WAIT4 (61) — wait for a child to exit.
 *
 * arg0: long pid   (child pid, or (long)-1 to wait for any child)
 * arg1: int* status  (user pointer to receive the child's exit status;
 *                     may be NULL)
 * arg2: int options  (bit 0 set = WNOHANG: return 0 if no child has
 *                     exited yet)
 * returns: the reaped child's pid, 0 if WNOHANG and no child exited,
 *          -1 on error
 */
#define WNOHANG 1

/* --- kernel-side handlers (called by the dispatcher) --- */
long sys_write(int fd, const void* buf, size_t count);
void sys_exit(int status);
long sys_read(int fd, void* buf, size_t count);
long sys_mprotect(void* addr, size_t len, int prot);
void* sys_brk(void* addr);

long sys_open(const char* path, int flags);
long sys_close(int fd);
long sys_dup2(int oldfd, int newfd);
long sys_unlink(const char* path);
long sys_fstat(int fd, void* user_stat);
long sys_stat(const char* user_path, void* user_stat);
long sys_wait4(long pid, int* user_status, int options);
long sys_setsid(void);
long sys_getppid(void);
long sys_getcwd(char* buf, unsigned long size);

/*
 * sys_poll — minimal poll(2) for syscall 7.
 *
 * Signatures mirror musl's <poll.h>:
 *     struct pollfd { int fd; short events; short revents; };
 *     typedef unsigned long nfds_t;
 *     int poll(struct pollfd *, nfds_t, int);
 *
 * The handler takes the user pointer and count through the
 * dispatcher, so the C type here is deliberately a mirror
 * struct rather than musl's; see the definition in
 * user_syscall.c.
 */
struct kernel_pollfd;
/*
 * sys_poll — minimal poll(2) for syscall 7.
 *
 * See the definition in user_syscall.c for the struct pollfd
 * layout and the semantics.  The parameter is void* here because
 * the pollfd struct is private to user_syscall.c; the dispatcher
 * passes arg0 through unchanged, and sys_poll casts internally.
 */
long sys_poll(void* user_fds, unsigned long nfds, int timeout);

#endif
