#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>

/* ============================================================
 * Syscall numbers — Linux x86_64 ABI
 *
 * Numbers 0..~472 (plus 512..547 for x32) are reserved for the
 * Linux x86_64 syscall table.  Every case in syscall_dispatch
 * that shares a number with a Linux syscall musl uses is at
 * that Linux number.
 *
 * This file is the source of truth for the kernel's syscall
 * numbering.  When adding or changing an entry, verify the
 * number against the canonical Linux x86_64 table,
 * arch/x86/entry/syscalls/syscall_64.tbl:
 *
 *     https://github.com/torvalds/linux/blob/master/arch/x86/entry/syscalls/syscall_64.tbl
 *
 * Do NOT guess a number from the name of the syscall that
 * appears to be missing: the "Unknown syscall: N" diagnostic
 * names the number the caller used, which may be a different
 * syscall entirely.  Getting a number wrong is not a
 * "wrong handler" bug -- it is a SILENT SHADOWING bug.  The
 * handler sits at a number no correct caller uses (dead code)
 * AND occupies the number of another syscall that now cannot
 * be reached.  This has happened twice:
 *
 *   - mkdir was at 7.  Linux 7 is poll(2).  Fixed session 27
 *     (tag 20260928-05): mkdir is 83.
 *   - setsid was at 107.  Linux 107 is geteuid(2).  Fixed
 *     session 27: setsid is 112.
 *
 * Both were guessed from an "Unknown syscall: N" log line
 * rather than read from the table above.  Do not do that.
 *
 * KNOWN INCOMPLETE (revisit): sys_prctl (157) currently accepts
 * PR_SET_NAME and returns 0 without storing the name.  donix has
 * no separate "comm" field; pcb->name is the exec name.  A real
 * implementation would add a per-process comm field, set it here,
 * and expose it (e.g. in process_dump_all).  Deferred; see
 * docs/open-issues.md.
 *
 * ------------------------------------------------------------
 * CANONICAL NUMBERS — implemented syscalls
 * ------------------------------------------------------------
 *
 *   0  read              sys_read
 *   1  write             sys_write
 *   2  open              sys_open
 *   3  close             sys_close
 *   4  stat              sys_stat          (Linux entry: sys_newstat)
 *   5  fstat             sys_fstat         (Linux entry: sys_newfstat)
 *   6  lstat             sys_lstat         (Linux entry: sys_newlstat)
 *   7  poll              sys_poll
 *   9  mmap              sys_mmap
 *  10  mprotect          sys_mprotect      (stub)
 *  11  munmap            sys_munmap        (stub)
 *  12  brk               sys_brk
 *  13  rt_sigaction      sys_rt_sigaction  (stub)
 *  14  rt_sigprocmask    sys_rt_sigprocmask(stub)
 *  16  ioctl             sys_ioctl
 *  20  writev            sys_writev
 *  21  access            sys_access
 *  33  dup2              sys_dup2
 *  39  getpid            sys_getpid
 *  57  fork              sys_fork
 *  59  execve            sys_execve
 *  60  exit              sys_exit
 *  61  wait4             sys_wait4
 *  63  uname             sys_uname
 *  72  fcntl             sys_fcntl
 *  77  ftruncate         sys_ftruncate     (session 31)
 *  79  getcwd            sys_getcwd
 *  80  chdir             sys_chdir
 *  83  mkdir             sys_mkdir
 *  84  rmdir             sys_rmdir
 *  87  unlink            sys_unlink        (dispatched; applet off)
 * 107  geteuid           sys_geteuid       (returns fixed uid 1000)
 * 110  getppid           sys_getppid
 * 112  setsid            sys_setsid
 * 157  prctl             sys_prctl         (PR_SET_NAME only; see note)
 * 158  arch_prctl        sys_arch_prctl
 * 217  getdents64        sys_getdents64
 * 218  set_tid_address   sys_set_tid_address
 * 231  exit_group        sys_exit_group -> sys_exit
 * 235  utimes            sys_utimes        (session 31; no-op stub)
 * 261  futimesat         sys_futimesat     (session 31; no-op stub)
 * 269  faccessat         sys_faccessat
 * 273  set_robust_list   sys_set_robust_list
 * 280  utimensat         sys_utimensat     (session 31; no-op stub)
 * 318  getrandom         sys_getrandom     (stub, -ENOSYS)
 * 334  rseq              sys_rseq          (stub, -ENOSYS)
 *
 * ------------------------------------------------------------
 * CANONICAL NUMBERS — known gaps (not yet implemented)
 * ------------------------------------------------------------
 *
 *  262 newfstatat        number reserved; no dispatch case.
 *                        musl routes fstatat through stat/lstat
 *                        on x86_64 for the common case, so it is
 *                        not hit yet.  A caller passing
 *                        AT_FDCWD plus flags would reach it.
 *
 * ------------------------------------------------------------
 * DONIX-PRIVATE NUMBERS (500+)
 * ------------------------------------------------------------
 *
 * These are NOT Linux numbers.  They must never collide with the
 * canonical table above.  Linux uses 0..472 (common/64) and
 * 512..547 (x32); donix does not support x32, so 500..511 is
 * currently safe, but a new private number above 547 would be
 * safer still.
 *
 * 503  reboot            donix-private.  musl never calls it; it
 *                        is reached only via raw syscall(503).
 *
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
#define SYS_UNAME           63
#define SYS_FCNTL           72
#define SYS_FTRUNCATE       77
#define SYS_GETCWD          79
#define SYS_CHDIR           80
#define SYS_MKDIR           83
#define SYS_RMDIR           84
#define SYS_UNLINK          87
#define SYS_GETEUID         107
#define SYS_GETPPID         110
#define SYS_SETSID          112
#define SYS_PRCTL           157
#define SYS_ARCH_PRCTL      158
#define SYS_GETDENTS64      217
#define SYS_SET_TID_ADDRESS 218
#define SYS_EXIT_GROUP      231
#define SYS_UTIMES          235
#define SYS_FUTIMESAT       261
#define SYS_NEWFSTATAT      262   /* number reserved; no dispatch case yet */
#define SYS_FACCESSAT       269
#define SYS_SET_ROBUST_LIST 273
#define SYS_UTIMENSAT       280
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
long sys_munmap(void* addr, size_t length);
void* sys_brk(void* addr);

long sys_open(const char* path, int flags);
long sys_close(int fd);
long sys_dup2(int oldfd, int newfd);
long sys_unlink(const char* path);
long sys_rmdir(const char* path);
long sys_ftruncate(int fd, long length);
long sys_fstat(int fd, void* user_stat);
long sys_stat(const char* user_path, void* user_stat);
long sys_wait4(long pid, int* user_status, int options);
long sys_setsid(void);
long sys_geteuid(void);
long sys_getppid(void);
long sys_uname(void* user_buf);
long sys_getcwd(char* buf, unsigned long size);
long sys_chdir(const char* path);
long sys_utimes(const char* path, const void* times);
long sys_futimesat(int dirfd, const char* path, const void* times);
long sys_utimensat(int dirfd, const char* path, const void* times,
                   int flags);
long sys_prctl(int option, unsigned long arg2, unsigned long arg3,
               unsigned long arg4, unsigned long arg5);

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
