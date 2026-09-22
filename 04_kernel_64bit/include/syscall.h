#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>

#define SYS_WRITE 1
#define SYS_EXIT  60
#define SYS_READ  0

/*
 * Directory syscalls (v0.6.x, Stage 3).
 *
 *   sys_opendir(path)          -> handle (>= 3), or -1
 *   sys_readdir(h, dirent)     -> 1 on entry, 0 on end, -1 on error
 *   sys_closedir(h)            -> 0 on success, -1 on error
 *
 * struct dons_dirent is defined in apps/include/donsdos.h on the
 * userland side, and mirrored in user_syscall.c so the kernel can
 * size the safe_copy_to_user.  Keep the two definitions in sync.
 */
#define SYS_OPENDIR  12
#define SYS_READDIR  13
#define SYS_CLOSEDIR 14

#define SYS_BRK   10
#define SYS_ARCH_SET_FS 11

#define SYS_PROCLIST    20
#define SYS_REBOOT      25

#define SYS_OPEN   2
#define SYS_CLOSE  3
#define SYS_UNLINK 7

/*
 * SYS_EXEC (8) — spawn a new process from an ELF on the FAT volume.
 *
 * Spawn semantics, not execve.  Creates a new process, loads the
 * named ELF into it, places argv on the child's user stack, queues
 * it, and returns its pid.  The calling process is untouched and
 * continues running.
 *
 * arg0: const char* path (user pointer, NUL-terminated, "0:/NAME.EXT")
 * arg1: int argc         (0 if no arguments; capped at EXEC_MAX_ARGC)
 * arg2: char** argv      (user pointer to array of user string
 *                         pointers; NULL if argc == 0)
 * returns: pid (>0) on success, -1 on failure
 *
 * v0.6.x (Stage 4) extended this from 1 argument to 3.  Old callers
 * that passed only a path now pass argc=0, argv=NULL.
 */
#define SYS_EXEC 8
#define EXEC_MAX_ARGC 16
#define EXEC_MAX_ARG_LEN 256

/*
 * SYS_WAITPID (9) — wait for a child to exit.
 *
 * arg0: long pid   (child pid, or (long)-1 to wait for any child)
 * arg1: int* status  (user pointer to receive the child's exit status;
 *                     may be NULL)
 * arg2: int options  (bit 0 set = WNOHANG: return 0 if no child has
 *                     exited yet)
 * returns: the reaped child's pid, 0 if WNOHANG and no child exited,
 *          -1 on error
 */
#define SYS_WAITPID 9
#define WNOHANG 1

long sys_write(int fd, const void* buf, size_t count);
void sys_exit(int status);
long sys_read(int fd, void* buf, size_t count);
void* sys_brk(long inc);
void sys_arch_set_fs(void* base);

/*
 * Kernel-side handlers.  Called by the dispatcher.
 */
long sys_open(const char* path, int flags);
long sys_close(int fd);
long sys_unlink(const char* path);
long sys_exec(const char* user_path, int argc, char** user_argv);
long sys_waitpid(long pid, int* user_status, int options);
long sys_opendir(const char* path);
long sys_readdir(int dirfd, void* user_dirent);
long sys_closedir(int dirfd);

#endif
