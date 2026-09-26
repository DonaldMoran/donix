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
 * Numbers 500+ are donix-private.  They are for syscalls that
 * exist only in donix and have no Linux equivalent: directory
 * streaming (opendir/readdir/closedir), the reboot hook, and
 * the newlib-only arch_set_fs.  musl never calls these; only
 * the newlib userland (arc2/syscalls.c) does.
 * ============================================================ */

/* --- Linux x86_64 numbers, implemented --- */
#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_STAT            4
#define SYS_FSTAT           5
#define SYS_MMAP            9
#define SYS_MPROTECT        10
#define SYS_MUNMAP          11
#define SYS_BRK             12
#define SYS_RT_SIGACTION    13
#define SYS_RT_SIGPROCMASK  14
#define SYS_IOCTL           16
#define SYS_WRITEV          20
#define SYS_GETPID          39
#define SYS_FORK            57
#define SYS_EXECVE          59
#define SYS_EXIT            60
#define SYS_WAIT4           61
#define SYS_UNLINK          87
#define SYS_ARCH_PRCTL      158
#define SYS_GETDENTS64      217
#define SYS_SET_TID_ADDRESS 218
#define SYS_EXIT_GROUP      231
#define SYS_NEWFSTATAT      262
#define SYS_SET_ROBUST_LIST 273
#define SYS_GETRANDOM       318
#define SYS_RSEQ            334

/* --- donix-private numbers (500+) --- */
#define SYS_OPENDIR     500
#define SYS_READDIR     501
#define SYS_CLOSEDIR    502
#define SYS_REBOOT      503
#define SYS_ARCH_SET_FS 504
#define SYS_DONIX_SBRK  505

/*
 * SYS_DONIX_SPAWN (507) — spawn semantics, donix-private.
 *
 * This is the old sys_execve behavior, moved off number 59 so that
 * 59 can become Linux execve (A2.12 step 2).  The newlib userland
 * (arc2/syscalls.c:spawn) now calls this number.  musl never calls
 * it; when musl's shell uses fork+execve it will hit number 59.
 *
 * Behavior: creates a NEW process, loads the named ELF into it,
 * lays argv on its stack, queues it, returns its pid.  The caller
 * is untouched.  This is NOT POSIX execve.
 *
 * arg0: const char* path (user pointer, NUL-terminated, "0:/NAME.EXT")
 * arg1: int argc         (0 if no arguments; capped at EXEC_MAX_ARGC)
 * arg2: char** argv      (user pointer to array of user string
 *                         pointers; NULL if argc == 0)
 * returns: pid (>0) on success, -1 on failure
 */
#define SYS_DONIX_SPAWN 507

/* ============================================================
 * Argument conventions for the donix-private syscalls
 *
 *   sys_opendir(path)          -> handle (>= 3), or -1
 *   sys_readdir(h, dirent)     -> 1 on entry, 0 on end, -1 on error
 *   sys_closedir(h)            -> 0 on success, -1 on error
 *
 * struct dons_dirent is defined in apps/include/donsdos.h on the
 * userland side, and mirrored in user_syscall.c so the kernel can
 * size the safe_copy_to_user.  Keep the two definitions in sync.
 * ============================================================ */

/*
 * SYS_EXECVE (59) — Linux execve.  NOT YET IMPLEMENTED.
 *
 * As of 20260924H, the dispatcher still routes 59 to sys_spawn as a
 * placeholder.  The next milestone (A2.12 step 2) flips 59 to a real
 * in-place execve that:
 *   - replaces the calling process's address space in place,
 *   - does not create a new process,
 *   - does not return on success.
 *
 * The spawn behavior that used to live at 59 is now at
 * SYS_DONIX_SPAWN (507); see the block above.
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
void* sys_sbrk(long inc);
void sys_arch_set_fs(void* base);

long sys_open(const char* path, int flags);
long sys_close(int fd);
long sys_unlink(const char* path);
long sys_fstat(int fd, void* user_stat);
long sys_wait4(long pid, int* user_status, int options);
long sys_opendir(const char* path);
long sys_readdir(int dirfd, void* user_dirent);
long sys_closedir(int dirfd);

long sys_spawn(const char* user_path, int argc, char** user_argv);
#endif
