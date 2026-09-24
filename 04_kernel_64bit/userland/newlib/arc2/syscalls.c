#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "donsdos.h"

/* Linux x86_64 numbers — match include/syscall.h in the kernel tree. */
#define SYS_READ      0
#define SYS_WRITE     1
#define SYS_OPEN      2
#define SYS_CLOSE     3
#define SYS_EXIT      60
#define SYS_UNLINK    87
#define SYS_SPAWN     507
#define SYS_WAITPID   61
#define SYS_BRK       12

/* donix-private numbers (500-range) — no Linux equivalent. */
#define SYS_OPENDIR   500
#define SYS_READDIR   501
#define SYS_CLOSEDIR  502
#define SYS_REBOOT    503
#define SYS_DONIX_SBRK 505

static inline int64_t syscall3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

void _init(void) {}
void _fini(void) {}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

void exit(int status) {
    syscall3(SYS_EXIT, (uint64_t)status, 0, 0);
    while (1) {
        __asm__ volatile("hlt");
    }
}

/*
 * sbrk — increment-based, donix-private syscall 505.
 *
 * Syscall 12 (SYS_BRK) is Linux ABI: it takes an absolute address
 * and returns the new break.  newlib's sbrk() is the traditional
 * Unix contract: it takes an increment and returns the OLD break.
 * The two are not compatible, so the kernel exposes the increment
 * form at 505.
 *
 * The kernel's sys_sbrk(505) implements the whole operation: it
 * remembers the current break per process, adds the increment,
 * maps pages as needed, and returns the OLD break.  So this
 * function does not need to cache or compute anything itself —
 * it just forwards, and returns whatever the kernel returned.
 */
void *sbrk(ptrdiff_t incr) {
    int64_t ret = syscall3(SYS_DONIX_SBRK, (uint64_t)incr, 0, 0);
    if (ret == -1) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)ret;
}

int open(const char *path, int flags, int mode) {
    return (int)syscall3(SYS_OPEN, (uint64_t)path, (uint64_t)flags, (uint64_t)mode);
}

int close(int fd) {
    return (int)syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
}

int unlink(const char *path) {
    return (int)syscall3(SYS_UNLINK, (uint64_t)path, 0, 0);
}

/*
 * spawn(path, argc, argv) — see apps/include/donsdos.h.
 *
 * Passes argc in the second syscall argument (rsi) and argv in the
 * third (rdx).  The kernel reads them as arg1 and arg2.
 *
 * Number is 507 (SYS_DONIX_SPAWN), the donix-private spawn number.
 * Number 59 is reserved for Linux execve and does NOT have spawn
 * semantics — do not point this at 59.
 */
int spawn(const char *path, int argc, char **argv) {
    return (int)syscall3(SYS_SPAWN,
                         (uint64_t)path,
                         (uint64_t)(int64_t)argc,
                         (uint64_t)argv);
}

int waitpid(int pid, int *status, int options) {
    return (int)syscall3(SYS_WAITPID,
                         (uint64_t)(int64_t)pid,
                         (uint64_t)status,
                         (uint64_t)options);
}

int opendir(const char *path) {
    return (int)syscall3(SYS_OPENDIR, (uint64_t)path, 0, 0);
}

int readdir(int dirfd, struct dons_dirent *out) {
    return (int)syscall3(SYS_READDIR, (uint64_t)dirfd, (uint64_t)out, 0);
}

int closedir(int dirfd) {
    return (int)syscall3(SYS_CLOSEDIR, (uint64_t)dirfd, 0, 0);
}

ssize_t _write(int fd, const void *buf, size_t count) { return write(fd, buf, count); }
ssize_t _read(int fd, void *buf, size_t count) { return read(fd, buf, count); }
void _exit(int status) { exit(status); }
void *_sbrk(ptrdiff_t incr) { return sbrk(incr); }

void sys_reboot(void) {
    fflush(NULL);
    syscall3(SYS_REBOOT, 0, 0, 0);
}

int fstat(int fd, struct stat *st) {
    if (fd == 1 || fd == 2) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    return -1;
}

int isatty(int fd) { if (fd == 1 || fd == 2) return 1; return 0; }
off_t lseek(int fd, off_t offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
int kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int getpid(void) { return 1; }

int _close(int fd) { return close(fd); }
int _fstat(int fd, struct stat *st) { return fstat(fd, st); }
int _isatty(int fd) { return isatty(fd); }
off_t _lseek(int fd, off_t offset, int whence) { return lseek(fd, offset, whence); }
int _open(const char *path, int flags, int mode) { return open(path, flags, mode); }
int _kill(int pid, int sig) { return kill(pid, sig); }
int _getpid(void) { return getpid(); }
