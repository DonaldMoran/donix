#include "include/user_syscall.h"
#include <stdint.h>
#include "include/serial.h"
#include "include/vmm.h"
#include "include/keyboard.h"
#include "include/syscall.h"
#include "include/vga.h"
#include "include/process.h"
#include "include/pmm.h"
#include "include/scheduler.h"
#include "include/user_msr.h"
#include "include/elf.h"
#include "include/heap.h"
#include "include/user_space.h"
#include "ff.h"

/* Defined below, in the execve helpers section. */
static int exec_resolve_bare_name(const char* in, char* out, size_t out_cap);

/* Defined in kmain.c — reboots the machine via keyboard controller + ACPI reset port. */
extern void handle_reboot_sequence(void);

extern uint64_t g_syscall_stack_top;

#define WRITE_CHUNK 256
static char g_write_bounce[WRITE_CHUNK];

/* Maximum path length accepted from user space. */
#define USER_PATH_MAX 300

/* Maximum number of program headers we accept in an ELF. */
#define EXEC_MAX_PHDRS 16

/* Common Linux x86_64 errno values used by the file syscalls.
 *
 * musl's __syscall_ret converts any return value in the range
 * -4095..-1 to -errno; anything outside that range, including a
 * plain -1, is passed through unchanged and then the caller's
 * errno stays whatever it was, which is a stale value that
 * produces misleading error messages.  So a syscall that means
 * to report "file not found" MUST return -ENOENT, not -1.
 */
#define EPERM_   1
#define ENOENT_  2
#define EIO_     5
#define EBADF_   9
#define EAGAIN_  11
#define ENOMEM_  12
#define EFAULT_  14
#define ENOEXEC  8
#define ECHILD   10
#define EINVAL_  22
#define EPIPE_   32
#define ERANGE_  34
#define ENAMETOOLONG_ 36

#define DEBUG_GETCWD 0
/* ============================================================
 * DEBUG INSTRUMENTATION
 *
 * Set DEBUG_FIL to 1 to log every FIL field at every step of
 * open/write/close/read.  Set to 0 for production.
 * ============================================================ */
#define DEBUG_FIL 0

/* Set DEBUG_STAT_TRACE to 1 to log every sys_stat call.  Used to
 * find out exactly which paths busybox ash's PATH probe stats.
 * Set to 0 for production. */
#define DEBUG_STAT_TRACE 0

/* Set DEBUG_WAIT_TRACE to 1 to log every sys_wait4 call and its
 * return value, and every sys_exit.  Useful when debugging why a
 * shell is not seeing its children.  Set to 0 for production. */
#define DEBUG_WAIT_TRACE 0

// ============================================================
// FILE TABLE SLOT HEADER
// ============================================================
//
// The per-process file_table[] holds file_slot_t*, not raw FIL*.
// Each slot says whether its obj is a FIL or a DIR, and carries a
// refcount so that sys_dup2 can share a slot between two fds
// without either close freeing the other's memory.

#define FILE_KIND_FILE 1
#define FILE_KIND_DIR  2
/*
 * A console slot.  fds 0, 1, and 2 start life holding one of these
 * (see process.c's process_create) so that open(2) returns fd 3 for
 * a process that has not closed anything, matching Linux, where
 * stdio fds are always occupied at startup.
 *
 * A console slot carries no obj -- slot->obj is NULL and stays NULL.
 * sys_read/sys_write recognize it by kind and take their existing
 * keyboard/screen path; put_file_slot frees it without calling
 * f_close.  Nothing else in this file treats FILE_KIND_CONSOLE as a
 * real open file.
 */
#define FILE_KIND_CONSOLE 3

/*
 * A pipe end.  slot->obj points at a shared pipe_t (see below).
 * TWO file_slot_t objects -- the read end and the write end --
 * point at the SAME pipe_t.  The end flag lives on the slot, not
 * on the pipe object, because the object is shared and the two
 * ends differ only in which operation they permit.
 *
 * The pipe_t itself carries a refcount, separate from the slot
 * refcount.  slot->refcount counts fd references to one end (a
 * dup2 of the read end bumps the slot refcount, not the pipe
 * refcount); pipe->refcount counts live ENDS (open read end +
 * open write end = 2), and the pipe_t is freed when the last end
 * closes.  Keeping the two refcounts distinct is what makes
 * "close the write end, reader sees EOF" work while a dup'd read
 * end is still open.
 */
#define FILE_KIND_PIPE 4

/* Pipe end flags.  Stored in pipe_slot_t.end. */
#define PIPE_END_READ  1
#define PIPE_END_WRITE 2

/*
 * Pipe buffer capacity, in bytes.
 *
 * This is a FIELD on pipe_t (pipe->capacity), not a #define used
 * inline, so that a future session can grow the buffer the way
 * Linux does (4 KB -> 64 KB) by reallocating and copying the ring,
 * without touching sys_read/sys_write: those already consult
 * pipe->capacity instead of this literal.  Do not replace the
 * field with this macro at the use sites.
 *
 * 4096 is one page.  Linux starts at one page and grows toward
 * 64 KB; we start at one page and do not grow yet.  For the
 * current applet set (cat | head, echo | wc) the producer never
 * writes more than 4 KB before the consumer reads, so growth
 * would never trigger.  Raise PIPE_DEFAULT_CAPACITY if a test
 * ever shows otherwise.
 */
#define PIPE_DEFAULT_CAPACITY 4096

/*
 * The pipe object.  One per pipe(2) call, shared by the read-end
 * and write-end slots.  See the FILE_KIND_PIPE comment above for
 * the two-refcount scheme.
 *
 * Single-threaded kernel: no lock is needed.  The kernel runs with
 * interrupts disabled across the read/write paths that touch the
 * ring (Step 2 adds the cli/sti discipline; Step 1 is
 * non-blocking and never sleeps).  If a future change makes a
 * pipe operation preemptible, this struct needs a lock.
 */
typedef struct pipe_s {
    uint32_t capacity;      /* bytes; starts at PIPE_DEFAULT_CAPACITY */
    uint32_t used;          /* bytes currently in the ring            */
    uint32_t read_pos;      /* next byte to read                      */
    uint32_t write_pos;     /* next byte to write                     */
    uint8_t* buf;           /* kmalloc'd, capacity bytes              */
    uint32_t refcount;      /* live ends: 0, 1, or 2                  */

    /*
     * Open-end counts (Step 3).
     *
     * readers_open counts fds anywhere in the system that hold the
     * READ end of this pipe; writers_open counts fds holding the
     * WRITE end.  Each starts at 1 in sys_pipe and is decremented
     * when a slot holding that end is finally freed (in
     * put_file_slot's PIPE case, which is where a slot's last
     * reference dies).
     *
     * These are counts, not flags, because of dup2: after
     *
     *     pipe(fds);
     *     dup2(fds[1], 1);
     *
     * two fds hold the write end.  Closing one must not look
     * like "the writer closed" while the other still holds it.
     * The reader's EOF condition is writers_open == 0, which is
     * true only when every write-end fd is gone.  A per-slot
     * "closed" flag would be wrong here, and would pass a
     * simple test then fail the moment a shell dup2'd a pipe
     * onto stdout.
     *
     * The counts do NOT participate in freeing the pipe_t.  The
     * pipe object and its ring are freed by put_pipe_ref when
     * refcount (live ENDS, 0/1/2) reaches zero -- which happens
     * only after both ends have closed, at which point
     * readers_open and writers_open are already 0.
     */
    uint32_t readers_open;
    uint32_t writers_open;

    /*
     * Waiters (Step 2).  NULL when nobody is blocked on that end.
     *
     * At most one reader and one writer can be blocked at a time:
     * a second reader that finds reader_waiting already set would
     * have to block behind the first, and there is no wait queue
     * to hold it.  In practice this is not a limitation -- a pipe
     * has one read end and one write end, and the single reader /
     * single writer case is what shells use.  If a future test
     * needs multiple concurrent readers on one pipe, replace these
     * two pointers with a proper wait queue.
     *
     * These are DIRECTED-WAKE pointers.  The peer that makes
     * progress reads the appropriate one and wakes exactly that
     * process, instead of broadcasting to every blocked process.
     */
    struct pcb* reader_waiting;
    struct pcb* writer_waiting;
} pipe_t;

/* Set to 1 for one build to trace the fd lifecycle of ash's
 * redirection.  Turn back to 0 before committing. */
#define DEBUG_FD_TRACE 0

#if DEBUG_FD_TRACE
static const char* kind_name(uint32_t k) {
    switch (k) {
        case FILE_KIND_FILE:    return "FILE";
        case FILE_KIND_DIR:     return "DIR";
        case FILE_KIND_CONSOLE: return "CONSOLE";
        default:                return "?";
    }
}
#define FDTRACE(...) do { \
    serial_print("[fd] pid="); serial_print_dec((uint64_t)process_get_current()->pid); \
    serial_print(" "); __VA_ARGS__; serial_print("\n"); \
} while (0)
#else
#define FDTRACE(...) do {} while (0)
#endif

typedef struct file_slot_s {
    uint32_t kind;
    uint32_t refcount;
    /*
     * Pipe end flag.  Set only for FILE_KIND_PIPE slots, to
     * PIPE_END_READ or PIPE_END_WRITE.  0 for every other kind.
     *
     * The flag is on the SLOT, not on the shared pipe_t, because
     * each fd is exactly one end.  Two fds can hold the same end
     * (a dup2 of the write end), and in that case both slots
     * carry PIPE_END_WRITE -- which is why "the writer has
     * closed" is NOT a per-slot flag.  It is a count on the pipe
     * object (writers_open); see pipe_t.
     */
    uint32_t end;
    void    *obj;
} file_slot_t;

#if DEBUG_FIL
static void dump_fil(const char* tag, FIL* f) {
    if (!f) {
        serial_print(tag);
        serial_print(": FIL*=NULL\n");
        return;
    }
    serial_print(tag);
    serial_print(": FIL*=0x");       serial_print_hex((uint64_t)f);
    serial_print(" fs=0x");          serial_print_hex((uint64_t)f->obj.fs);
    serial_print(" sclust=");        serial_print_dec(f->obj.sclust);
    serial_print(" objsize=");       serial_print_dec(f->obj.objsize);
    serial_print(" fptr=");          serial_print_dec(f->fptr);
    serial_print(" clust=");         serial_print_dec(f->clust);
    serial_print(" sect=");          serial_print_dec(f->sect);
    serial_print(" dir_sect=");      serial_print_dec(f->dir_sect);
    serial_print(" flag=0x");        serial_print_hex((uint64_t)f->flag);
    serial_print(" err=");           serial_print_dec(f->err);
    serial_print("\n");
}
#endif

/*
 * Linux x86_64 struct stat, matching musl 1.2.5's layout exactly.
 *
 * Size 144 bytes on x86_64, field offsets verified by compiling
 * an offsetof()-based probe against the project-local musl headers
 * (third_party/musl-install/include/bits/stat.h).  Do NOT reorder,
 * resize, or add fields here without re-running that probe and
 * updating the offsets.  musl writes into this buffer directly from
 * userland; any mismatch means `ls` prints garbage or the caller
 * faults on an unaligned read.
 *
 *     offset  size  field
 *       0      8    st_dev
 *       8      8    st_ino
 *      16      8    st_nlink
 *      24      4    st_mode
 *      28      4    st_uid
 *      32      4    st_gid
 *      36      4    __pad0
 *      40      8    st_rdev
 *      48      8    st_size
 *      56      8    st_blksize
 *      64      8    st_blocks
 *      72     16    st_atim  (timespec: int64 tv_sec, int64 tv_nsec)
 *      88     16    st_mtim
 *     104     16    st_ctim
 *     120     24    __unused[3]
 *     144          total
 *
 * The natural C alignment of uint64_t / int64_t / uint32_t produces
 * exactly these offsets on x86_64 SysV, so the struct is written in
 * natural order with no explicit padding beyond __pad0.
 */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    struct {
        int64_t tv_sec;
        int64_t tv_nsec;
    } st_atim, st_mtim, st_ctim;
    int64_t  __unused[3];
} kernel_stat_t;

/* st_mode bits (POSIX).  Only the ones we need for FAT. */
#define KSTAT_IFMT   0170000
#define KSTAT_IFREG  0100000
#define KSTAT_IFDIR  0040000

/*
 * True if `p` names the FAT root directory.
 *
 * FatFs's f_stat() does not accept ".", "/", "0:", or "0:/" — it
 * returns FR_INVALID_NAME for all of them, because it treats the
 * path as a file name and the root has no file-name form.  musl
 * and busybox both hand these strings to stat(2):
 *
 *   - busybox's `ls` with no argument does stat(".").
 *   - busybox's `ls /`        does stat("/").
 *   - busybox's `ls 0:/`      does stat("0:/").
 *
 * All four forms denote the same thing on donix, so we synthesize
 * a directory stat for them instead of calling f_stat.
 */
static int path_is_root(const char* p) {
    if (!p || !*p) return 0;
    if ((p[0] == '.' || p[0] == '/') && p[1] == '\0') return 1;
    if (p[0] == '0' && p[1] == ':') {
        if (p[2] == '\0') return 1;
        if ((p[2] == '/' || p[2] == '\\') && p[3] == '\0') return 1;
    }
    return 0;
}

/*
 * Strip leading "./" and "/" components from a FatFs path, in place.
 *
 * FatFs accepts "0:/NAME", "0:NAME", and bare "NAME", but rejects
 * any leading "." or "/".  musl's lstat()/stat() and busybox's
 * `ls` hand us "./NAME" when the argument is a directory entry
 * under the current directory, e.g.:
 *
 *     ls: ./HELLO-WORLD.TXT: Operation not permitted
 *
 * This helper peels off the leading "./" (and repeated "./") and
 * a leading "/" if present, and leaves the rest alone.  It does
 * NOT rewrite bare names to "0:/NAME" — the existing code already
 * handles those, and we deliberately change one thing at a time.
 *
 * If the whole path was "." or "./" or "/" or a run of "./"
 * components, the stripped result is empty; we leave the string
 * as "." so path_is_root() still recognizes it as the root.
 */
static void strip_dot_prefix(char* p) {
    if (!p || !*p) return;

    char* src = p;
    while (*src) {
        if (src[0] == '.' && src[1] == '/') { src += 2; continue; }
        if (src[0] == '/')                  { src += 1; continue; }
        break;
    }

    /* Entire path was "." components — treat as root. */
    if (*src == '\0') {
        p[0] = '.';
        p[1] = '\0';
        return;
    }

    /* src >= p always, so an in-place move is safe. */
    char* dst = p;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

/*
 * Resolve a possibly-relative path against the process's cwd.
 *
 * The kernel's path syscalls (sys_open, sys_stat, sys_access) all
 * need this before handing a path to FatFs, because FatFs has no
 * notion of a process cwd -- it resolves every path against the
 * FAT root.  Unix semantics require relative paths to resolve
 * against pcb->cwd instead, and that is what this does.
 *
 * Rules, applied in order:
 *
 *   - An empty path is copied through unchanged (the caller's
 *     error handling deals with it).
 *   - A path that already has a FatFs drive (contains ':') is
 *     copied through unchanged -- it is already absolute.
 *   - A path starting with '/' is an absolute Unix path.  It is
 *     copied through unchanged; the caller (strip_dot_prefix)
 *     peels the leading '/' before calling FatFs.
 *   - "." resolves to cwd itself.
 *   - ".." resolves to the parent of cwd (cwd with its last
 *     component removed; "/" is its own parent).
 *   - "../rest" resolves to (parent of cwd) + "/rest".
 *   - "./rest" resolves to cwd + "/rest".
 *   - Anything else is relative: cwd + "/" + path.
 *
 * The default cwd is "/" (from process_initialize_pcb's memset
 * leaving cwd[0] == '\0', which this treats as "/").  So with a
 * never-chdir'd process:
 *
 *     "."      -> "/"
 *     ".."     -> "/"
 *     "foo"    -> "/foo"
 *     "./foo"  -> "/foo"
 *     "../foo" -> "/foo"
 *     "/foo"   -> "/foo"     (absolute, unchanged)
 *     "0:/foo" -> "0:/foo"   (FatFs form, unchanged)
 *
 * which is exactly what the code did before this helper existed.
 * Only a non-root cwd changes anything.
 *
 * This does NOT collapse interior ".." components.  "a/../b" is
 * passed through as a relative path and prefixed with cwd; the
 * kernel does not canonicalize it.  A shell generating paths will
 * not produce interior "..", so this covers the real cases.  Full
 * canonicalization is a follow-up.
 *
 * Returns 0 on success, -1 if the result would overflow `cap`.
 */
static int resolve_against_cwd(pcb_t* self, const char* path,
                               char* out, size_t cap) {
    /* cwd, or "/" if the process has never chdir'd. */
    const char* cwd = "/";
    if (self && self->cwd[0] != '\0') {
        cwd = self->cwd;
    }

    /* Empty path: pass through; caller handles the error. */
    if (path[0] == '\0') {
        if (cap < 1) return -1;
        out[0] = '\0';
        return 0;
    }

    /* Already FatFs-absolute (has a drive prefix): pass through. */
    for (const char* p = path; *p; p++) {
        if (*p == ':') {
            size_t i = 0;
            while (path[i] && i + 1 < cap) { out[i] = path[i]; i++; }
            if (path[i] != '\0') return -1;
            out[i] = '\0';
            return 0;
        }
    }

    /* Absolute Unix path: pass through unchanged (caller strips
     * the leading '/' before calling FatFs). */
    if (path[0] == '/') {
        size_t i = 0;
        while (path[i] && i + 1 < cap) { out[i] = path[i]; i++; }
        if (path[i] != '\0') return -1;
        out[i] = '\0';
        return 0;
    }

    /* "." alone: the cwd. */
    if (path[0] == '.' && path[1] == '\0') {
        size_t i = 0;
        while (cwd[i] && i + 1 < cap) { out[i] = cwd[i]; i++; }
        if (cwd[i] != '\0') return -1;
        out[i] = '\0';
        return 0;
    }

    /* ".." or "../...": start from the parent of cwd. */
    if (path[0] == '.' && path[1] == '.' &&
        (path[2] == '\0' || path[2] == '/')) {

        /* Compute parent of cwd.  "/" is its own parent. */
        size_t clen = 0;
        while (cwd[clen]) clen++;

        size_t parent_len = clen;
        if (clen > 1) {
            size_t j = clen;
            while (j > 1 && cwd[j - 1] != '/') j--;
            if (j <= 1) parent_len = 1;
            else        parent_len = j - 1;
        } else {
            parent_len = 1;   /* cwd was "/" */
        }

        size_t o = 0;
        for (size_t i = 0; i < parent_len; i++) {
            if (o + 1 >= cap) return -1;
            out[o++] = cwd[i];
        }
        if (o == 0) {
            if (cap < 2) return -1;
            out[o++] = '/';
        }

        const char* rem = path + 2;   /* "" or "/..." */
        if (rem[0] == '/') {
            if (out[o - 1] != '/') {
                if (o + 1 >= cap) return -1;
                out[o++] = '/';
            }
            for (size_t i = 1; rem[i]; i++) {
                if (o + 1 >= cap) return -1;
                out[o++] = rem[i];
            }
        }
        out[o] = '\0';
        return 0;
    }

    /* "./..." -- resolve as cwd + the part after "./". */
    if (path[0] == '.' && path[1] == '/') {
        size_t o = 0;
        size_t clen = 0;
        while (cwd[clen]) clen++;
        for (size_t i = 0; i < clen; i++) {
            if (o + 1 >= cap) return -1;
            out[o++] = cwd[i];
        }
        if (o == 0) {
            if (cap < 2) return -1;
            out[o++] = '/';
        }
        if (out[o - 1] != '/') {
            if (o + 1 >= cap) return -1;
            out[o++] = '/';
        }
        for (size_t i = 2; path[i]; i++) {
            if (o + 1 >= cap) return -1;
            out[o++] = path[i];
        }
        out[o] = '\0';
        return 0;
    }

    /* Ordinary relative path: cwd + "/" + path. */
    {
        size_t o = 0;
        size_t clen = 0;
        while (cwd[clen]) clen++;
        for (size_t i = 0; i < clen; i++) {
            if (o + 1 >= cap) return -1;
            out[o++] = cwd[i];
        }
        if (o == 0) {
            if (cap < 2) return -1;
            out[o++] = '/';
        }
        if (out[o - 1] != '/') {
            if (o + 1 >= cap) return -1;
            out[o++] = '/';
        }
        for (size_t i = 0; path[i]; i++) {
            if (o + 1 >= cap) return -1;
            out[o++] = path[i];
        }
        out[o] = '\0';
        return 0;
    }
}

/*
 * Map a FatFs FRESULT to a Linux -errno suitable for return
 * from a syscall.
 *
 * Called only on failure (r != FR_OK).  The mapping is
 * approximate: FatFs does not distinguish as finely as POSIX,
 * but the two that matter here are FR_NO_FILE / FR_NO_PATH
 * (-> ENOENT) and FR_INVALID_NAME (also a "does not exist in
 * this form" signal for our purposes, but arguably EINVAL).
 *
 * FR_INVALID_NAME is mapped to ENOENT because busybox ash probes
 * PATH candidates like "/usr/local/sbin/ls" with stat() and
 * expects a "not found" answer (ENOENT) for anything it will
 * not be able to exec.  Reporting EINVAL there makes ash print
 * "Invalid argument" instead of "not found", which is just as
 * misleading as the old "Operation not permitted".
 */
static long fatfs_errno(FRESULT r) {
    switch (r) {
        case FR_OK:            return 0;
        case FR_NO_FILE:       return -(long)ENOENT_;
        case FR_NO_PATH:       return -(long)ENOENT_;
        case FR_INVALID_NAME:  return -(long)ENOENT_;
        case FR_DENIED:        return -(long)EPERM_;
        case FR_EXIST:         return -(long)EPERM_;
        case FR_INVALID_OBJECT:return -(long)EBADF_;
        case FR_WRITE_PROTECTED:return -(long)EPERM_;
        case FR_INVALID_DRIVE: return -(long)ENOENT_;
        case FR_NOT_READY:     return -(long)EIO_;
        case FR_DISK_ERR:      return -(long)EIO_;
        case FR_INT_ERR:       return -(long)EIO_;
        case FR_NOT_ENABLED:   return -(long)EIO_;
        case FR_NO_FILESYSTEM: return -(long)EIO_;
        case FR_TIMEOUT:       return -(long)EIO_;
        case FR_LOCKED:        return -(long)EIO_;
        case FR_NOT_ENOUGH_CORE:return -(long)EIO_;
        case FR_TOO_MANY_OPEN_FILES:return -(long)EIO_;
        case FR_MKFS_ABORTED:  return -(long)EIO_;
        default:               return -(long)EIO_;
    }
}

/* Fill a kernel_stat_t describing the FAT root as a directory. */
static void fill_kstat_as_root(kernel_stat_t* st) {
    for (size_t i = 0; i < sizeof(*st); i++) ((uint8_t*)st)[i] = 0;
    st->st_nlink   = 1;
    st->st_blksize = 512;
    st->st_mode    = KSTAT_IFDIR | 0755;
    st->st_size    = 0;
    st->st_blocks  = 0;
}

// ============================================================
// SAFE COPY OPERATIONS
// ============================================================
static int safe_copy_from_user(void* kernel_dest, const void* user_src, size_t count) {
    pcb_t* current = process_get_current();
    if (!current) return -1;

    uint64_t base = (uint64_t)user_src;
    uint8_t* dst = (uint8_t*)kernel_dest;
    size_t copied = 0;
    while (copied < count) {
        uint64_t cur = base + copied;
        uint64_t phys_with_offset = vmm_get_phys_from_cr3(current->cr3, cur);
        if (!phys_with_offset) return -1;

        const uint8_t* src = (const uint8_t*)(HHDM_START + phys_with_offset);
        size_t chunk = count - copied;
        uint64_t page_off = cur & 0xFFF;
        size_t page_rem = 4096 - page_off;
        if (chunk > page_rem) chunk = page_rem;

        for (size_t i = 0; i < chunk; i++) dst[copied + i] = src[i];
        copied += chunk;
    }
    return 0;
}

static int safe_copy_to_user(void* user_dest, const void* kernel_src, size_t count) {
    pcb_t* current = process_get_current();
    if (!current) return -1;

    uint64_t base = (uint64_t)user_dest;
    const uint8_t* src = (const uint8_t*)kernel_src;
    size_t copied = 0;
    while (copied < count) {
        uint64_t cur = base + copied;
        uint64_t phys_with_offset = vmm_get_phys_from_cr3(current->cr3, cur);
        if (!phys_with_offset) return -1;

        uint8_t* dst = (uint8_t*)(HHDM_START + phys_with_offset);
        size_t chunk = count - copied;
        uint64_t page_off = cur & 0xFFF;
        size_t page_rem = 4096 - page_off;
        if (chunk > page_rem) chunk = page_rem;

        for (size_t i = 0; i < chunk; i++) dst[i] = src[copied + i];
        copied += chunk;
    }
    return 0;
}

/*
 * Like safe_copy_to_user, but resolves the destination against an
 * explicit cr3 rather than the current process's.  Used by
 * sys_execve to write the new argv region into the process's own
 * (new) address space before the syscall-return path resumes the
 * new program.
 *
 * Does NOT switch cr3.  vmm_get_phys_from_cr3 walks the target page
 * tables via the HHDM, and HHDM_START + phys is mapped in every
 * address space (it's in the shared kernel higher half), so we can
 * write to the child's physical pages while the current process's
 * cr3 is still active.
 */
static int safe_copy_to_user_cr3(uint64_t cr3, void* user_dest,
                                 const void* kernel_src, size_t count) {
    uint64_t base = (uint64_t)user_dest;
    const uint8_t* src = (const uint8_t*)kernel_src;
    size_t copied = 0;
    while (copied < count) {
        uint64_t cur = base + copied;
        uint64_t phys_with_offset = vmm_get_phys_from_cr3(cr3, cur);
        if (!phys_with_offset) return -1;

        uint8_t* dst = (uint8_t*)(HHDM_START + phys_with_offset);
        size_t chunk = count - copied;
        uint64_t page_off = cur & 0xFFF;
        size_t page_rem = 4096 - page_off;
        if (chunk > page_rem) chunk = page_rem;

        for (size_t i = 0; i < chunk; i++) dst[i] = src[copied + i];
        copied += chunk;
    }
    return 0;
}

static int copy_user_string(char* dst, size_t dst_cap, const char* user_src) {
    if (dst_cap == 0) return -1;
    size_t i = 0;
    while (i < dst_cap - 1) {
        char c;
        if (safe_copy_from_user(&c, user_src + i, 1) != 0) return -1;
        dst[i++] = c;
        if (c == '\0') return 0;
    }
    dst[dst_cap - 1] = '\0';
    return 0;
}

// ============================================================
// FILE TABLE HELPERS
// ============================================================

/*
 * Release one reference to a file_slot_t.
 *
 * Decrements the refcount; frees the slot's obj and the slot itself
 * only when the count reaches zero.  This is what makes sys_dup2's
 * aliasing safe: after dup2, two fds share one slot, and closing
 * either fd must not free memory the other fd still points at.
 *
 * Callers that just want to detach an fd without freeing the slot
 * (e.g. sys_close, which sets file_table[fd] = NULL separately)
 * use this and then clear their own table entry.
 */
/*
 * Drop one reference to the pipe OBJECT (not the slot).
 *
 * The pipe_t is shared by the read-end and write-end slots.  Each
 * end holds one reference; the pipe_t is freed, and its ring
 * buffer with it, when the last end goes away.  A slot that merely
 * aliases an existing end (dup2) does NOT take a pipe reference --
 * slot->refcount counts fd references to one end, pipe->refcount
 * counts ends.
 *
 * Called only from put_file_slot, when a PIPE-kind slot is about
 * to be freed (its own slot refcount reached zero).
 */
static void put_pipe_ref(pipe_t* pipe) {
    if (!pipe) return;
    if (pipe->refcount > 1) {
        pipe->refcount--;
        return;
    }
    if (pipe->buf) kfree(pipe->buf);
    kfree(pipe);
}

/*
 * Wake one pipe waiter, if it is still there and still waiting.
 *
 * `waiter` is pipe->reader_waiting or pipe->writer_waiting.  `kind`
 * is the block_kind that waiter must currently have for the wake
 * to be valid -- BLOCK_KIND_PIPE_READ for a reader,
 * BLOCK_KIND_PIPE_WRITE for a writer.
 *
 * The checks defend against a STALE pointer:
 *
 *   - waiter == NULL: nobody was waiting when the peer recorded
 *     the pointer, or the peer already woke and cleared it.
 *
 *   - waiter->state != BLOCKED: the process was woken by something
 *     else in the window between the peer reading the pointer and
 *     this call, or it exited and its PCB slot was reused.  A
 *     reused slot could in principle be a *different* process now
 *     blocked on a *different* pipe, so the block_kind check below
 *     is what actually makes the reuse safe.
 *
 *   - waiter->block_kind != kind: the process is blocked, but on
 *     something else (a different pipe end, waitpid, keyboard).
 *     Waking it would be wrong -- it is not the process that put
 *     itself in this pipe's waiter slot.  This is the check that
 *     makes a recycled PCB slot harmless.
 *
 * On a valid wake: mark READY, clear block_kind, and re-add to the
 * ready queue.  scheduler_ready_queue_add is idempotent, so a
 * redundant add is harmless; the state flip is the load-bearing
 * part.
 *
 * Does NOT touch the pipe's waiter field.  The caller clears it,
 * inside its own cli region, so the pointer and the state stay
 * consistent.
 */
static void pipe_wake_waiter(struct pcb* waiter, uint64_t kind) {
    if (!waiter) return;
    if (waiter->state != PROC_STATE_BLOCKED) return;
    if (waiter->block_kind != kind) return;

    waiter->state      = PROC_STATE_READY;
    waiter->block_kind = BLOCK_KIND_NONE;
    scheduler_ready_queue_add(waiter);
}

static void put_file_slot(file_slot_t* slot) {
    if (!slot) return;
    if (slot->refcount > 1) {
        slot->refcount--;
        return;
    }
    if (slot->kind == FILE_KIND_FILE) {
        f_close((FIL*)slot->obj);
        kfree(slot->obj);
    } else if (slot->kind == FILE_KIND_DIR) {
        f_closedir((DIR*)slot->obj);
        kfree(slot->obj);
    } else if (slot->kind == FILE_KIND_CONSOLE) {
        /* obj is NULL; nothing to close or free. */
    } else if (slot->kind == FILE_KIND_PIPE) {
        /*
         * This is the last reference to this END (the slot refcount
         * just hit zero).  Decrement the pipe's open-end count for
         * whichever end this slot was.
         *
         * Order matters: decrement the count BEFORE calling
         * put_pipe_ref, because put_pipe_ref may free the pipe
         * object once the last END closes, and after that the
         * counts are gone.
         *
         * WAKE ON ZERO.  If the decrement took the count to zero,
         * the end is gone systemwide and any peer blocked on the
         * other end has a terminal condition to observe: a reader
         * with no writer left sees EOF, a writer with no reader
         * left sees EPIPE.  This function wakes that peer directly.
         *
         * sys_close also wakes, and it runs BEFORE this function on
         * the close path, so on that path the peer is already
         * marked READY and the wake here is a no-op (the waiter
         * pointer was cleared by sys_close).  The wake here is
         * what covers the OTHER path: process exit.  When a process
         * exits, close_all_files calls put_file_slot directly, with
         * no sys_close involved, and without this wake a reader
         * blocked on the exiting writer's pipe would sit in hlt
         * until the next keyboard IRQ woke it via
         * process_wake_all_blocked.  On a headless system, or in a
         * pipeline where no key is pressed, that is a hang.
         *
         * Context: put_file_slot is reached from sys_close (process
         * context, interrupts on) and from process_exit (interrupts
         * off, but process_wake_parent_if_waiting already calls
         * scheduler_ready_queue_add in that same context, so
         * touching the ready queue here is consistent with what
         * process_exit already does).
         *
         * The wake is only attempted when the count REACHES zero.
         * A non-final close leaves the count > 0; the peer (if any)
         * is woken by sys_close, re-checks, sees the count still
         * positive, and re-blocks.  Doing it here only on zero
         * keeps this function from waking spuriously on every
         * intermediate close in a dup'd chain.
         */
        pipe_t* pipe = (pipe_t*)slot->obj;
        if (slot->end == PIPE_END_READ) {
            if (pipe->readers_open > 0) pipe->readers_open--;
            if (pipe->readers_open == 0 && pipe->writer_waiting) {
                pipe_wake_waiter(pipe->writer_waiting,
                                 BLOCK_KIND_PIPE_WRITE);
                pipe->writer_waiting = NULL;
            }
        } else if (slot->end == PIPE_END_WRITE) {
            if (pipe->writers_open > 0) pipe->writers_open--;
            if (pipe->writers_open == 0 && pipe->reader_waiting) {
                pipe_wake_waiter(pipe->reader_waiting,
                                 BLOCK_KIND_PIPE_READ);
                pipe->reader_waiting = NULL;
            }
        }
        put_pipe_ref(pipe);
    }
    kfree(slot);
}

/*
 * Allocate a console sentinel slot for fd 0, 1, or 2.
 *
 * The slot has no obj; sys_read/sys_write dispatch on kind, not on
 * obj, for console fds.  Returns NULL on kmalloc failure, in which
 * case the caller leaves the fd NULL and it behaves the way it did
 * before sentinels existed.
 */
static file_slot_t* alloc_console_slot(void) {
    file_slot_t* slot = (file_slot_t*)kmalloc(sizeof(file_slot_t));
    if (!slot) return NULL;
    slot->kind     = FILE_KIND_CONSOLE;
    slot->refcount = 1;
    slot->obj      = NULL;
    return slot;
}

/*
 * Install console sentinels in fds 0, 1, and 2 of a new process.
 *
 * See include/user_syscall.h for the contract.  Called from
 * process_create after the file_table[] zeroing loop.  Idempotent
 * in the sense that it overwrites whatever is in 0/1/2, but it is
 * only ever called on a freshly zeroed table.
 */
void user_syscall_init_console_fds(struct pcb* pcb) {
    if (!pcb) return;
    for (int fd = 0; fd <= 2; fd++) {
        /*
         * On failure, alloc_console_slot returns NULL and the fd
         * stays NULL.  NULL fd 0/1/2 already takes the keyboard/
         * screen path in sys_read/sys_write, so this degrades to
         * the pre-sentinel behavior rather than breaking.
         */
        ((pcb_t*)pcb)->file_table[fd] = alloc_console_slot();
    }
}

static void close_all_files(pcb_t* proc) {
    if (!proc) return;
    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        file_slot_t* slot = (file_slot_t*)proc->file_table[i];
        if (!slot) continue;
        put_file_slot(slot);
        proc->file_table[i] = NULL;
    }
}

/*
* open(2) must return the LOWEST free fd, including 0, 1, and
* 2 when they are free.  This is Linux/POSIX semantics and
* real programs depend on it.  busybox `uniq FILE` does:
*
*     close(STDIN_FILENO);              // frees fd 0
*     xopen(input_filename, O_RDONLY);  // expects fd 0 back
*
* and then reads from stdin (fd 0).  Before this change the
* search started at fd 3, so the open landed on fd 3, stdin
* stayed pointed at the now-closed fd 0, read(0, ...) fell
* through to the keyboard path in sys_read, and the applet
* blocked forever waiting for a keystroke that never came.
* That was the `uniq` "hang".
*
* fds 0/1/2 are not permanently reserved, but a fresh process
* starts with all three HELD by console sentinels (see
* user_syscall_init_console_fds, called from process_create).
* So the first open() of a fresh process returns fd 3 -- the
* sentinels are occupied -- and fd 0/1/2 only become available
* again after the program explicitly closes them, exactly as
* on Linux.  Without the sentinels a fresh process would get
* fd 0 back from its first open(), which breaks shell
* redirection bookkeeping (ash saves and restores stdio fds).
*
* This does NOT relax the fd<3 guard in get_file_slot().
* That guard remains the default for the file syscalls.  Two
* callers opt out explicitly, because a redirected stdio fd
* is a real open file there:
*
*   - sys_dup2 uses get_file_slot_any so `dup2(file_fd, 0/1/2)`
*     and its restore twin `dup2(saved, 0/1/2)` work.
*   - sys_fcntl uses get_file_slot_any for F_DUPFD and
*     F_DUPFD_CLOEXEC only, so a shell can save stdio.  Its
*     other subcommands still call get_file_slot and refuse
*     fd < 3.  The new fd from F_DUPFD still lands on fd >= 3.
*/
static int alloc_file_slot(file_slot_t** out_slot) {
    pcb_t* self = process_get_current();
    if (!self) return -1;
    int fd = -1;
    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        if (self->file_table[i] == NULL) { fd = i; break; }
    }
    if (fd == -1) return -1;
    file_slot_t* slot = (file_slot_t*)kmalloc(sizeof(file_slot_t));
    if (!slot) return -1;
    slot->kind     = 0;
    slot->refcount = 1;
    slot->obj      = NULL;
    self->file_table[fd] = slot;
    *out_slot = slot;
    return fd;
}

static file_slot_t* get_file_slot(int fd, uint32_t kind) {
    pcb_t* self = process_get_current();
    if (!self || fd < 3 || fd >= MAX_PROCESS_FILES) return NULL;
    file_slot_t* slot = (file_slot_t*)self->file_table[fd];
    if (!slot) return NULL;
    if (kind != 0 && slot->kind != kind) return NULL;
    return slot;
}

/*
 * Like get_file_slot, but accepts fds 0, 1, and 2.
 *
 * The general get_file_slot() refuses fds < 3.  That guard is the
 * default because most file syscalls must not treat stdin/stdout/
 * stderr as ordinary open files.  But redirection -- busybox ash's
 * `<`, `>`, `2>` -- works by opening a file and calling dup2() to
 * install it as fd 0, 1, or 2.  After that, those low fds hold
 * real files, and the syscalls involved in the redirect dance must
 * see them:
 *
 *   - sys_read and sys_write, so read(0, ...) and write(1, ...)
 *     consult file_table[0] / file_table[1] instead of falling
 *     through to the keyboard/screen.
 *   - sys_close, so a redirect-created file's refcount reaches
 *     zero and FatFs commits the directory entry.
 *   - sys_dup2, so dup2(file_fd, 1) and the restore dup2(saved, 1)
 *     work -- without this a second redirect in the same shell
 *     failed with EBADF once an earlier redirect had freed a low fd.
 *   - sys_fcntl, for F_DUPFD and F_DUPFD_CLOEXEC only, so a shell
 *     can save stdio before redirecting it.
 *
 * Everything else -- fstat, getdents64, lseek, ftruncate, and the
 * other fcntl subcommands -- keeps using get_file_slot and refuses
 * fd < 3.
 *
 * Returns NULL if the fd is out of range, or holds no slot.
 * Does not check slot->kind -- the caller does.
 */
static file_slot_t* get_file_slot_any(int fd) {
    pcb_t* self = process_get_current();
    if (!self || fd < 0 || fd >= MAX_PROCESS_FILES) return NULL;
    return (file_slot_t*)self->file_table[fd];
}


/*
 * Linux x86_64 fcntl(2) — syscall 72.
 *
 * musl's opendir() calls
 *     fcntl(fd, F_SETFD, FD_CLOEXEC)
 * after opening the directory, and treats a failure as fatal: it
 * closes the fd, frees the DIR, and returns NULL.  busybox's `ls`
 * then unwinds through its own error path and, in the build we
 * have, hits a musl a_crash() (an `hlt` in user mode -> #GP).
 *
 * We don't implement fd flags.  Return 0 for the query/set-flags
 * requests musl actually issues, so opendir succeeds.  Everything
 * else returns -EINVAL.
 *
 * F_DUPFD is implemented; F_DUPFD_CLOEXEC aliases it, with the
 * close-on-exec bit silently dropped (donix does not track it).
 */
#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4
#define F_DUPFD_CLOEXEC 1030

long sys_fcntl(int fd, int cmd, unsigned long arg) {
    file_slot_t* slot;
    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
        slot = get_file_slot_any(fd);
    } else {
        slot = get_file_slot(fd, 0);
    }
    if (!slot) {
        FDTRACE({ serial_print("fcntl fd="); serial_print_dec((uint64_t)fd);
                  serial_print(" cmd="); serial_print_dec((uint64_t)cmd);
                  serial_print(" -> EBADF"); });
        return -(long)EBADF_;
    }

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC: {
            /*
             * Linux fcntl(F_DUPFD, min): return the lowest free fd
             * >= min, aliased to the same open file description.
             *
             * donix reserves fds 0, 1, 2 for stdin/stdout/stderr, and
             * get_file_slot() refuses them (fd < 3 returns NULL).  So
             * an F_DUPFD with min = 0, 1, or 2 must still land on an
             * fd >= 3; otherwise the caller gets a "valid" return
             * value it cannot subsequently read or write through.
             * musl's dup(fd) wrapper is fcntl(fd, F_DUPFD, 0), so this
             * clamp is on the hot path for busybox sh redirection.
             *
             * The new fd shares the file_slot_t with `fd` and takes a
             * reference, exactly like sys_dup2.  The refcount
             * machinery is what makes the shared slot safe.
             *
             * F_DUPFD_CLOEXEC additionally sets FD_CLOEXEC on the new
             * fd.  donix does not track FD_CLOEXEC (all fds survive
             * execve), so the flag is silently dropped — the same
             * behavior as the F_SETFD case below.
             */
            int min = (int)arg;
            if (min < 0 || min >= MAX_PROCESS_FILES) {
                return -(long)EINVAL_;
            }
            pcb_t* self = process_get_current();
            if (!self) return -(long)EBADF_;

            int start = min;
            if (start < 3) start = 3;

            int newfd = -1;
            for (int i = start; i < MAX_PROCESS_FILES; i++) {
                if (self->file_table[i] == NULL) { newfd = i; break; }
            }
            if (newfd == -1) return -(long)EAGAIN_;

            slot->refcount++;
            self->file_table[newfd] = slot;
            FDTRACE({ serial_print("fcntl DUPFD fd="); serial_print_dec((uint64_t)fd);
                      serial_print(" min="); serial_print_dec((uint64_t)min);
                      serial_print(" -> "); serial_print_dec((uint64_t)newfd); });
            return (long)newfd;
        }
        case F_GETFD:  return 0;              /* no FD_CLOEXEC set */
        case F_SETFD:  return 0;              /* ignore the flag   */
        case F_GETFL:  return 0;              /* O_RDONLY          */
        case F_SETFL:  return 0;              /* ignore            */
        default:       return -(long)EINVAL_;
    }
}

// ============================================================
// PIPE (Linux syscall 22) -- Step 1: object, sys_pipe, non-blocking I/O
//
// This step creates pipes and moves bytes through them.  It does
// NOT block: read on an empty pipe and write on a full pipe return
// -EAGAIN.  Blocking, EOF, and EPIPE are Step 2/3.
//
// The object and the refcount scheme are described at the
// FILE_KIND_PIPE and pipe_t definitions near the top of this file.
// ============================================================

/* 24 is EMFILE (too many open files) on Linux x86_64. */
#ifndef EMFILE_
#define EMFILE_ 24
#endif

/*
 * Allocate one pipe end as a file_slot_t pointing at `pipe`.
 *
 * On success, stores the slot in *out_slot and returns its fd.  On
 * failure (no free fd, or kmalloc failed) returns -1 and stores
 * NULL.  Does not increment pipe->refcount; the caller does that
 * once per end, after both ends have allocated, so a failure on
 * the second end does not leave the pipe's count inflated.
 */
static int pipe_alloc_end(pipe_t* pipe, uint32_t end, file_slot_t** out_slot) {
    file_slot_t* slot = NULL;
    int fd = alloc_file_slot(&slot);
    if (fd == -1) {
        *out_slot = NULL;
        return -1;
    }
    slot->kind = FILE_KIND_PIPE;
    slot->obj  = pipe;
    slot->end  = end;    /* PIPE_END_READ or PIPE_END_WRITE */
    /* slot->refcount is already 1 from alloc_file_slot. */
    *out_slot = slot;
    return fd;
}

/*
 * Linux x86_64 pipe(2) -- syscall 22.
 *
 * ABI:
 *   arg0  int[2]  user pointer to a two-int array
 *   returns  0 on success, -errno on failure.
 *
 * On success, pipefd[0] is the read end and pipefd[1] is the write
 * end.  Both are low fds: pipe() allocates the lowest free fds,
 * which after a fresh process's console sentinels means fd 3 and
 * fd 4.
 *
 * Allocation is atomic from the caller's point of view: if the
 * second end cannot be allocated (table full, kmalloc failed), the
 * first end is rolled back and the pipe_t is freed, and the
 * function returns -EMFILE.  A half-created pipe must not leak, and
 * a caller that got back a read end with no write end could never
 * see EOF.
 *
 * EMFILE (24) is the Linux errno for "process file table full".
 */
long sys_pipe(int* user_pipefd) {
    if (!user_pipefd) return -(long)EFAULT_;

    pcb_t* self = process_get_current();
    if (!self) return -(long)EMFILE_;

    /* 1. The shared object. */
    pipe_t* pipe = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!pipe) return -(long)ENOMEM_;

    pipe->buf = (uint8_t*)kmalloc(PIPE_DEFAULT_CAPACITY);
    if (!pipe->buf) {
        kfree(pipe);
        return -(long)ENOMEM_;
    }
    pipe->capacity     = PIPE_DEFAULT_CAPACITY;
    pipe->used         = 0;
    pipe->read_pos     = 0;
    pipe->write_pos    = 0;
    pipe->refcount     = 0;   /* incremented once per end, below */
    pipe->readers_open = 0;   /* set to 1 when the read end allocates */
    pipe->writers_open = 0;   /* set to 1 when the write end allocates */
    pipe->reader_waiting = NULL;
    pipe->writer_waiting = NULL;

    /* 2. The two ends. */
    file_slot_t* rslot = NULL;
    file_slot_t* wslot = NULL;

    int rfd = pipe_alloc_end(pipe, PIPE_END_READ, &rslot);
    if (rfd == -1) {
        kfree(pipe->buf);
        kfree(pipe);
        return -(long)EMFILE_;
    }
    /* The read end exists: record its end flag on the slot and
     * bump the open-reader count.  These two must agree, or EOF
     * will never fire (or fire early). */
    rslot->end = PIPE_END_READ;
    pipe->readers_open = 1;

    int wfd = pipe_alloc_end(pipe, PIPE_END_WRITE, &wslot);
    if (wfd == -1) {
        /* Roll back the read end.  Mirror what put_file_slot's
         * PIPE case does, since the slot is being discarded
         * without going through it. */
        self->file_table[rfd] = NULL;
        pipe->readers_open = 0;
        kfree(rslot);
        kfree(pipe->buf);
        kfree(pipe);
        return -(long)EMFILE_;
    }
    wslot->end = PIPE_END_WRITE;
    pipe->writers_open = 1;

    /* 3. Both ends exist: the pipe now has two live references. */
    pipe->refcount = 2;

    /* 4. Write the fds back to the user.  This is a 2-int write,
     * 8 bytes.  If it faults, unwind everything we built. */
    int fds[2];
    fds[0] = rfd;
    fds[1] = wfd;
    if (safe_copy_to_user(user_pipefd, fds, sizeof(fds)) != 0) {
        self->file_table[rfd] = NULL;
        self->file_table[wfd] = NULL;
        kfree(rslot);
        kfree(wslot);
        kfree(pipe->buf);
        kfree(pipe);
        return -(long)EFAULT_;
    }

    FDTRACE({ serial_print("pipe  rfd="); serial_print_dec((uint64_t)rfd);
              serial_print(" wfd="); serial_print_dec((uint64_t)wfd); });
    return 0;
}

// ============================================================
// FILE SYSCALLS
// ============================================================
long sys_open(const char* path, int flags) {
    pcb_t* self = process_get_current();
    if (!self || !path) return -(long)EFAULT_;

    char local_path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];
    if (copy_user_string(local_path, sizeof(local_path), path) != 0) {
        return -(long)EFAULT_;
    }
    if (resolve_against_cwd(self, local_path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(local_path) - 1) {
            local_path[i] = resolved[i];
            i++;
        }
        local_path[i] = '\0';
    }
    strip_dot_prefix(local_path);

    file_slot_t* slot = NULL;
    int fd = alloc_file_slot(&slot);
    if (fd == -1) return -(long)EIO_;

    BYTE mode = 0;
    switch (flags & 0x3) {
        case 0:  mode |= FA_READ;             break;   /* O_RDONLY */
        case 1:  mode |= FA_WRITE;            break;   /* O_WRONLY */
        case 2:  mode |= FA_READ | FA_WRITE;  break;   /* O_RDWR   */
        default: /* O_ACCMODE == 3 is invalid */
            kfree(slot);
            self->file_table[fd] = NULL;
            return -(long)EINVAL_;
    }

    /*
     * Translate Linux open(2) flags to FatFs open mode.
     *
     * Linux x86_64 flag bits (include/uapi/asm-generic/fcntl.h):
     *
     *   0x0001   O_WRONLY
     *   0x0002   O_RDWR
     *   0x0040   O_CREAT
     *   0x0080   O_EXCL
     *   0x0200   O_TRUNC
     *   0x0400   O_APPEND
     *   0x8000   O_LARGEFILE   (ignored here; we are already 64-bit)
     *   0x10000  O_DIRECTORY   (handled further below)
     *
     * FatFs mode bits (ff.h):
     *
     *   0x01  FA_READ
     *   0x02  FA_WRITE
     *   0x04  FA_CREATE_NEW
     *   0x08  FA_CREATE_ALWAYS
     *   0x10  FA_OPEN_ALWAYS
     *   0x30  FA_OPEN_APPEND
     *
     * The previous translation in this file tested the wrong bits
     * and as a result never set any creation flag for the common
     * open(O_RDWR|O_CREAT) that touch(1), vi's :wq path, cp(1),
     * and every other file-creating program issues.  FatFs then
     * tried to open an existing file, found none, and returned
     * FR_NO_FILE.  See the trace:
     *
     *   sys_open: f_open FAIL path=don.txt flags=0x8042 mode=0x03 r=4
     *
     * flags=0x8042 is O_RDWR|O_CREAT|O_LARGEFILE; mode=0x03 is just
     * FA_READ|FA_WRITE, with no create bit.  The create request was
     * being silently dropped because the check was `flags & 0x0200`
     * (which is O_TRUNC) rather than `flags & 0x0040` (which is
     * O_CREAT).
     *
     * FatFs expresses the creation choice as a single mode value,
     * not as independent bits, so the order of precedence matters.
     */
    int o_creat  = (flags & 0x0040) != 0;   /* O_CREAT  */
    int o_excl   = (flags & 0x0080) != 0;   /* O_EXCL   */
    int o_trunc  = (flags & 0x0200) != 0;   /* O_TRUNC  */
    int o_append = (flags & 0x0400) != 0;   /* O_APPEND */

    if (o_creat && o_excl) {
        /* O_CREAT|O_EXCL: fail if the file already exists.
         * FA_CREATE_NEW does exactly that: create only if
         * missing, return FR_EXIST otherwise.  This is the
         * correct mapping for mkstemp(3) and for callers that
         * need a guarantee the file did not previously exist. */
        mode |= FA_CREATE_NEW;
    } else if (o_creat && o_trunc) {
        /* O_CREAT|O_TRUNC: create if missing, truncate if
         * present.  FA_CREATE_ALWAYS does both.  This is what
         * `vi :wq`, `cp`, and `touch` on a missing file
         * actually issue. */
        mode |= FA_CREATE_ALWAYS;
    } else if (o_creat) {
        /* O_CREAT alone: create if missing, leave existing
         * content untouched.  FA_OPEN_ALWAYS is exactly this. */
        mode |= FA_OPEN_ALWAYS;
    } else if (o_trunc) {
        /* O_TRUNC without O_CREAT: POSIX calls this undefined;
         * Linux truncates an existing file and fails if it does
         * not exist.  FatFs has no exact equivalent.  We use
         * FA_CREATE_ALWAYS, which truncates existing files and
         * (unlike Linux) also creates missing ones.  Nothing in
         * busybox or musl relies on the "fail if missing" part
         * of the Linux semantics; a caller that does would need
         * an explicit stat first. */
        mode |= FA_CREATE_ALWAYS;
    } else {
        /* No creation or truncation flag: open existing only. */
        mode |= FA_OPEN_EXISTING;
    }

    if (o_append) {
        mode |= FA_OPEN_APPEND;
    }

    /*
     * Linux open(2) is also used to open directories — musl's
     * opendir() calls open(path, O_RDONLY|O_DIRECTORY) and then
     * readdir(), which uses getdents64(2).
     *
     * FatFs's f_open is not usable for this in two cases:
     *
     *   1. The path is a root alias (".", "/", "0:", "0:/").
     *      f_open returns FR_INVALID_NAME for "." and friends,
     *      and for "0:/" it can return FR_OK with a FIL that is
     *      not usable because the root is not a file.
     *
     *   2. The caller explicitly asked for a directory with
     *      O_DIRECTORY, and the path is a real directory.  f_open
     *      returns FR_INVALID_NAME or FR_NO_FILE, which the old
     *      fallback then recovered from by calling f_opendir — but
     *      it did so only after trying f_open first, and the "0:/"
     *      case above bypassed the fallback entirely because
     *      f_open returned FR_OK.
     *
     * The fix is to route every directory request to f_opendir up
     * front, and to normalize root aliases to "0:/" (the only form
     * f_opendir reliably accepts for the root).
     *
     * O_DIRECTORY is 0x10000 on Linux x86_64.
     */
    #define O_DIRECTORY 0x10000
    int wants_dir = (flags & O_DIRECTORY) != 0;
    int is_root   = path_is_root(local_path);

    if (wants_dir || is_root) {
        DIR* dir_obj = (DIR*)kmalloc(sizeof(DIR));
        if (!dir_obj) {
            kfree(slot);
            self->file_table[fd] = NULL;
            return -(long)EIO_;
        }
        const char* dir_path = is_root ? "0:/" : local_path;
        FRESULT dr = f_opendir(dir_obj, dir_path);
        if (dr == FR_OK) {
            slot->kind = FILE_KIND_DIR;
            slot->obj  = dir_obj;
            FDTRACE({ serial_print("open  "); serial_print(dir_path);
                      serial_print(" -> fd="); serial_print_dec((uint64_t)fd);
                      serial_print(" kind=DIR"); });
            return fd;
        }
        kfree(dir_obj);
        serial_print("sys_open: f_opendir FAIL path=");
        serial_print(dir_path);
        serial_print(" dr=");
        serial_print_dec(dr);
        serial_print("\n");
        kfree(slot);
        self->file_table[fd] = NULL;
        return fatfs_errno(dr);
    }

    FIL* file_obj = (FIL*)kmalloc(sizeof(FIL));
    if (!file_obj) {
        kfree(slot);
        self->file_table[fd] = NULL;
        return -(long)EIO_;
    }

    FRESULT r = f_open(file_obj, local_path, mode);
    if (r == FR_OK) {
        slot->kind = FILE_KIND_FILE;
        slot->obj  = file_obj;    
        FDTRACE({ serial_print("open  "); serial_print(local_path);
                  serial_print(" -> fd="); serial_print_dec((uint64_t)fd);
                  serial_print(" kind=FILE"); });
        return fd;
    }

    /*
     * f_open failed and the caller did not ask for a directory.
     * Last-resort fallback: the path might be a directory the
     * caller opened without O_DIRECTORY.  Try f_opendir; if it
     * works, hand back a directory slot.  FR_INVALID_NAME (6) and
     * FR_NO_FILE (4) are what FatFs returns when f_open is asked
     * to open a directory with file semantics.
     */
    if (r == FR_INVALID_NAME || r == FR_NO_FILE) {
        DIR* dir_obj = (DIR*)kmalloc(sizeof(DIR));
        if (dir_obj) {
            FRESULT dr = f_opendir(dir_obj, local_path);
            if (dr == FR_OK) {
                kfree(file_obj);
                slot->kind = FILE_KIND_DIR;
                slot->obj  = dir_obj;
                FDTRACE({ serial_print("open  "); serial_print(local_path);
                          serial_print(" -> fd="); serial_print_dec((uint64_t)fd);
                          serial_print(" kind=DIR (fallback)"); });
                return fd;
            }
            kfree(dir_obj);
        }
    }

    serial_print("sys_open: f_open FAIL path=");
    serial_print(local_path);
    serial_print(" flags=0x");
    serial_print_hex((uint64_t)flags);
    serial_print(" mode=0x");
    serial_print_hex((uint64_t)mode);
    serial_print(" r=");
    serial_print_dec(r);
    serial_print("\n");

    kfree(file_obj);
    kfree(slot);
    self->file_table[fd] = NULL;
    FDTRACE({ serial_print("open  FAIL "); serial_print(local_path);
              serial_print(" r="); serial_print_dec(r); });
    return fatfs_errno(r);
}

long sys_close(int fd) {
    file_slot_t* slot = get_file_slot_any(fd);

    if (!slot) {
        FDTRACE({ serial_print("close fd="); serial_print_dec((uint64_t)fd);
                  serial_print(" -> EBADF"); });
        return -(long)EBADF_;
    }

    pcb_t* self = process_get_current();
    FDTRACE({ serial_print("close fd="); serial_print_dec((uint64_t)fd);
              serial_print(" kind="); serial_print(kind_name(slot->kind));
              serial_print(" -> 0"); });

    /*
     * If this is a pipe end, a peer may be blocked on the other
     * end and needs to wake up so it can see the state change.
     *
     * STEP 2 SEMANTICS: we wake the peer, but we do NOT yet
     * deliver EOF or EPIPE.  A blocked reader whose writer just
     * closed will loop, find the pipe still empty, and re-block
     * (because Step 2 has no "no writer left" case).  A blocked
     * writer whose reader just closed will do the same.  That is
     * a known Step-2 gap; Step 3 adds the closed-end flags and
     * turns these wakes into EOF / EPIPE.
     *
     * Why wake at all, if the peer just re-blocks?  Because
     * without the wake the peer is never scheduled to re-check,
     * and Step 3 will not be able to fix the behavior without
     * also adding this wake.  We are putting the wake in now so
     * Step 3 is a smaller change.  The wake is harmless today:
     * the peer re-checks, re-blocks, and the only cost is one
     * scheduler pass.
     *
     * A pipe_t is freed when the LAST end closes.  If this is the
     * first end to close, put_file_slot's refcount sees 2 -> 1
     * and keeps the object alive, so reading pipe->reader_waiting
     * / writer_waiting below is safe.  If this is the last end,
     * the peer cannot be blocked on a pipe neither end of which
     * is open -- but to be safe, the wake happens BEFORE
     * put_file_slot frees the object.
     */
    if (slot->kind == FILE_KIND_PIPE) {
        pipe_t* pipe = (pipe_t*)slot->obj;

        /*
         * Step 3: selective wake.  Closing a WRITE end matters to
         * a blocked READER (it may now see EOF); closing a READ
         * end matters to a blocked WRITER (it may now see EPIPE).
         * Wake only the peer that the close affects.
         *
         * This does not need to know whether this is the LAST
         * write-end fd.  If it is not -- another fd still holds
         * the write end -- the woken reader loops, sees
         * writers_open > 0, and re-blocks.  One spurious wake,
         * correct outcome.  If it IS the last one, the reader
         * sees writers_open == 0 and returns 0 (EOF).  Either
         * way the reader's own loop decides; the wake just
         * ensures it gets scheduled to decide.
         *
         * IMPORTANT ORDER: this runs BEFORE put_file_slot below,
         * so pipe->readers_open / writers_open have not yet been
         * decremented and the peer's re-check (which happens
         * after we return and the scheduler runs it) sees the
         * post-close counts.  Do not move the wake after the
         * put_file_slot call.
         */
        if (slot->end == PIPE_END_WRITE) {
            if (pipe->reader_waiting) {
                pipe_wake_waiter(pipe->reader_waiting,
                                 BLOCK_KIND_PIPE_READ);
                pipe->reader_waiting = NULL;
            }
        } else if (slot->end == PIPE_END_READ) {
            if (pipe->writer_waiting) {
                pipe_wake_waiter(pipe->writer_waiting,
                                 BLOCK_KIND_PIPE_WRITE);
                pipe->writer_waiting = NULL;
            }
        }
    }

    self->file_table[fd] = NULL;
    put_file_slot(slot);
    return 0;
}

/*
 * Linux x86_64 dup2(2) — syscall 33.
 *
 * Duplicate oldfd onto newfd.  If newfd is already open it is
 * closed first; if oldfd == newfd the call is a no-op that returns
 * newfd.  Returns newfd on success, -EBADF if oldfd is not open.
 *
 * The file_slot_t is SHARED, not copied: after dup2, both fds
 * point at the same slot, and the slot's refcount is incremented.
 * Closing either fd drops one reference; the slot's obj and the
 * slot itself are freed only when the count reaches zero.  This
 * is what put_file_slot does, and it is what makes
 *
 *     open(); dup2(fd, 7); exit();
 *
 * safe: exit() closes both fds, the first close drops refcount to
 * 1, the second frees the slot once.
 */
long sys_dup2(int oldfd, int newfd) {
    pcb_t* self = process_get_current();
    if (!self) return -(long)EBADF_;

    if (oldfd < 0 || oldfd >= MAX_PROCESS_FILES) return -(long)EBADF_;
    if (newfd < 0 || newfd >= MAX_PROCESS_FILES) return -(long)EBADF_;

    file_slot_t* old_slot = get_file_slot_any(oldfd);
    if (!old_slot) {
        FDTRACE({ serial_print("dup2  old="); serial_print_dec((uint64_t)oldfd);
                  serial_print(" new="); serial_print_dec((uint64_t)newfd);
                  serial_print(" -> EBADF"); });
        return -(long)EBADF_;
    }

    /* dup2(fd, fd) is a no-op returning fd, not an error. */
    if (oldfd == newfd) return (long)newfd;

    /*
     * If newfd was already open, detach it and drop its reference.
     * put_file_slot frees only when the refcount reaches zero, so
     * if newfd was aliased to another live fd, the slot survives.
     */
    file_slot_t* new_slot = (file_slot_t*)self->file_table[newfd];
    if (new_slot) {
        self->file_table[newfd] = NULL;
        put_file_slot(new_slot);
    }

    /* Share the slot and take a new reference. */
    old_slot->refcount++;
    self->file_table[newfd] = old_slot;

    FDTRACE({ serial_print("dup2  old="); serial_print_dec((uint64_t)oldfd);
              serial_print(" new="); serial_print_dec((uint64_t)newfd);
              serial_print(" kind="); serial_print(kind_name(old_slot->kind));
              serial_print(" -> "); serial_print_dec((uint64_t)newfd); });
    return (long)newfd;
}

long sys_unlink(const char* path) {
    pcb_t* self = process_get_current();
    if (!self || !path) return -(long)EFAULT_;

    char local_path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];
    if (copy_user_string(local_path, sizeof(local_path), path) != 0) {
        return -(long)EFAULT_;
    }
    if (resolve_against_cwd(self, local_path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(local_path) - 1) {
            local_path[i] = resolved[i];
            i++;
        }
        local_path[i] = '\0';
    }
    strip_dot_prefix(local_path);

    FRESULT r = f_unlink(local_path);
    if (r != FR_OK) {
        serial_print("sys_unlink: f_unlink FAIL path=");
        serial_print(local_path);
        serial_print(" r="); serial_print_dec(r);
        serial_print("\n");
        return fatfs_errno(r);
    }
    return 0;
}

/*
 * Linux x86_64 rmdir(2) — syscall 84.
 *
 * Remove an empty directory.  FatFs's f_unlink handles both files
 * and empty directories; for a non-empty directory it returns
 * FR_DENIED, which fatfs_errno maps to -EPERM.  Linux returns
 * -ENOTEMPTY (39) for that case.  busybox's rmdir reports the
 * failure either way; if a caller ever needs the exact errno,
 * special-case FR_DENIED in a dedicated check.
 *
 * Path resolution matches sys_unlink: copy the user string, resolve
 * against the cwd (so `rmdir x` in a non-root cwd removes that
 * directory, not a root one of the same name), strip the leading
 * "./" or "/", then call f_unlink.
 */
long sys_rmdir(const char* path) {
    pcb_t* self = process_get_current();
    if (!self || !path) return -(long)EFAULT_;

    char local_path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];
    if (copy_user_string(local_path, sizeof(local_path), path) != 0) {
        return -(long)EFAULT_;
    }
    if (resolve_against_cwd(self, local_path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(local_path) - 1) {
            local_path[i] = resolved[i];
            i++;
        }
        local_path[i] = '\0';
    }
    strip_dot_prefix(local_path);

    FRESULT r = f_unlink(local_path);
    if (r != FR_OK) {
        serial_print("sys_rmdir: f_unlink FAIL path=");
        serial_print(local_path);
        serial_print(" r="); serial_print_dec(r);
        serial_print("\n");
        return fatfs_errno(r);
    }
    return 0;
}

/*
 * Linux x86_64 rename(2) — syscall 82.
 *
 * Rename (move) a file or directory.  This is the first
 * two-path syscall in donix: both oldpath and newpath must be
 * resolved against the process cwd, then stripped of any leading
 * "./" or "/", exactly as the one-path syscalls do.  The same
 * pattern will be needed for link(2) and symlink(2) when they
 * land.
 *
 * ABI:
 *   arg0  const char*  oldpath
 *   arg1  const char*  newpath
 *   returns  0 on success, -errno on failure.
 *
 * FatFs semantics (see ff.h f_rename):
 *   - f_rename does NOT replace an existing destination.  Linux
 *     rename(2) DOES replace.  If newpath exists, f_rename
 *     returns FR_EXIST, which fatfs_errno maps to -EPERM.  So
 *     `mv a b` where b already exists fails rather than
 *     overwriting.  This is a deliberate first cut: safer, and
 *     nothing in the current applet set relies on the replace
 *     behavior.  A future caller that needs Linux semantics can
 *     unlink the destination first, then rename -- but that loses
 *     the atomicity of rename, so it is a decision, not a fix.
 *
 *   - On FAT16, f_rename only works within a directory.  Renaming
 *     `dir/a` to `dir/b` works; `dir/a` to `other/b` fails.
 *     Linux rename(2) allows cross-directory moves.  For donix's
 *     flat FAT root this covers `mv a b`; a cross-directory move
 *     is a known limitation, tracked in docs/open-issues.md.
 *
 * Errors: -EFAULT for a bad user pointer, -ENAMETOOLONG if a
 * resolved path overflows, otherwise the FatFs errno mapping
 * (FR_NO_FILE -> -ENOENT, FR_EXIST -> -EPERM, etc.).
 */
long sys_rename(const char* user_oldpath, const char* user_newpath) {
    pcb_t* self = process_get_current();
    if (!self || !user_oldpath || !user_newpath) return -(long)EFAULT_;

    char old_path[USER_PATH_MAX];
    char new_path[USER_PATH_MAX];
    char old_resolved[USER_PATH_MAX];
    char new_resolved[USER_PATH_MAX];

    /* Copy both paths out of user space. */
    if (copy_user_string(old_path, sizeof(old_path), user_oldpath) != 0) {
        return -(long)EFAULT_;
    }
    if (copy_user_string(new_path, sizeof(new_path), user_newpath) != 0) {
        return -(long)EFAULT_;
    }

    /* Resolve both against the process cwd. */
    if (resolve_against_cwd(self, old_path, old_resolved,
                            sizeof(old_resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    if (resolve_against_cwd(self, new_path, new_resolved,
                            sizeof(new_resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }

    /* Copy the resolved forms back into the working buffers. */
    {
        size_t i = 0;
        while (old_resolved[i] && i < sizeof(old_path) - 1) {
            old_path[i] = old_resolved[i];
            i++;
        }
        old_path[i] = '\0';
    }
    {
        size_t i = 0;
        while (new_resolved[i] && i < sizeof(new_path) - 1) {
            new_path[i] = new_resolved[i];
            i++;
        }
        new_path[i] = '\0';
    }

    /* Strip leading "./" and "/" that FatFs rejects. */
    strip_dot_prefix(old_path);
    strip_dot_prefix(new_path);

    FRESULT r = f_rename(old_path, new_path);
    if (r != FR_OK) {
        serial_print("sys_rename: f_rename FAIL old=");
        serial_print(old_path);
        serial_print(" new=");
        serial_print(new_path);
        serial_print(" r="); serial_print_dec(r);
        serial_print("\n");
        return fatfs_errno(r);
    }
    return 0;
}

/*
 * Linux x86_64 mkdir(2) — syscall 83.
 *
 * FatFs has no notion of UNIX permissions, so the mode argument is
 * ignored.  Path normalization is identical to sys_unlink's:
 * copy_user_string pulls the path out of user space, then
 * strip_dot_prefix peels any leading "./" or "/" so FatFs sees a
 * form it accepts.
 *
 * An empty path returns -ENOENT before reaching FatFs.  Linux
 * returns -ENOENT from mkdir("") too, so this matches the Linux
 * ABI.  The early return is defensive: it keeps an empty-path
 * call out of the FatFs diagnostic path rather than letting
 * f_mkdir("") produce a confusing error.
 *
 * HISTORY: this function was originally registered at syscall 7
 * (tag 20260927-18), on the theory that the per-keystroke
 * "Unknown syscall: 7" noise from busybox ash's line editor was
 * an mkdir("") probe.  That was a misidentification.  On Linux
 * x86_64, 7 is poll(2) and mkdir is 83; ash's line editor was
 * polling stdin for readability once per keystroke, not calling
 * mkdir.  Implementing mkdir at 7 silenced the noise, and because
 * the old number was never reached by any correct caller, the
 * handler was effectively dead code that shadowed poll.  The
 * number is now 83, matching the Linux ABI, so stock binaries
 * (busybox) reach it.  The missing poll(2) is tracked in
 * docs/open-issues.md and is the reason the keystroke noise
 * returns until poll is implemented.
 *
 * FatFs returns FR_EXIST when a directory or file of the same name
 * already exists; fatfs_errno maps that to -EPERM.  Linux would
 * return -EEXIST (17).  busybox does not distinguish the two for
 * its purposes, so the mapping is left alone for now.  If a caller
 * ever needs -EEXIST, change fatfs_errno's FR_EXIST case to
 * -(long)17 at that time.
 */
long sys_mkdir(const char* path, int mode) {
    (void)mode;

    pcb_t* self = process_get_current();
    if (!self || !path) return -(long)EFAULT_;

    char local_path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];
    if (copy_user_string(local_path, sizeof(local_path), path) != 0) {
        return -(long)EFAULT_;
    }
    if (resolve_against_cwd(self, local_path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(local_path) - 1) {
            local_path[i] = resolved[i];
            i++;
        }
        local_path[i] = '\0';
    }
    strip_dot_prefix(local_path);

    if (local_path[0] == '\0') {
        /* An empty path is not a valid path on any Unix.  Linux
         * returns -ENOENT from mkdir("") too.  The early return
         * keeps an empty-path call out of the FatFs diagnostic
         * path. */
        return -(long)ENOENT_;
    }

    FRESULT r = f_mkdir(local_path);
    if (r != FR_OK) {
        return fatfs_errno(r);
    }
    return 0;
}

/*
 * Fill a kernel_stat_t from a FatFs FILINFO.
 *
 * Shared by sys_fstat (which derives the FILINFO from an open FIL)
 * and sys_stat (which derives it from a path via f_stat).  FatFs
 * reports the FAT attribute byte in `fattrib`; the AM_DIR bit
 * distinguishes directories from files.  Nothing else in the
 * FILINFO is used for the layout musl reads.
 *
 * Sizes are reported from `fsize` for files and left 0 for
 * directories (FatFs does not carry a directory size).  st_blksize
 * is set to 512 because that is the block size of the FAT volume
 * produced by the image build; st_blocks is rounded up
 * accordingly.  Neither value is read by the A4 test suite; they
 * are set for informational correctness.
 */
static void fill_kstat_from_filinfo(kernel_stat_t* st, const FILINFO* fno) {
    for (size_t i = 0; i < sizeof(*st); i++) ((uint8_t*)st)[i] = 0;

    st->st_nlink   = 1;
    st->st_blksize = 512;

    if (fno->fattrib & AM_DIR) {
        st->st_mode   = KSTAT_IFDIR | 0755;
        st->st_size   = 0;
        st->st_blocks = 0;
    } else {
        st->st_mode   = KSTAT_IFREG | 0644;
        st->st_size   = (int64_t)fno->fsize;
        st->st_blocks = (st->st_size + 511) / 512;
    }
}

long sys_fstat(int fd, void* user_stat) {
    if (!user_stat) return -(long)EFAULT_;

    file_slot_t* slot = get_file_slot(fd, 0);
    if (!slot) return -(long)EBADF_;

    kernel_stat_t st;
    FILINFO fno;
    for (size_t i = 0; i < sizeof(fno); i++) ((uint8_t*)&fno)[i] = 0;

    if (slot->kind == FILE_KIND_FILE) {
        FIL* f = (FIL*)slot->obj;
        /*
         * FatFs does not expose an f_stat-on-an-open-FIL.  The
         * information we need is available directly from the FIL:
         * f_size() for the byte size, and the file is by definition
         * not a directory.  Fill the FILINFO fields we use and let
         * fill_kstat_from_filinfo do the rest.
         */
        fno.fsize   = (FSIZE_t)f_size(f);
        fno.fattrib = 0;   /* not AM_DIR */
    } else if (slot->kind == FILE_KIND_DIR) {
        fno.fattrib = AM_DIR;
        fno.fsize   = 0;
    } else {
        return -(long)EBADF_;
    }

    fill_kstat_from_filinfo(&st, &fno);

    if (safe_copy_to_user(user_stat, &st, sizeof(st)) != 0) {
        return -(long)EFAULT_;
    }
    return 0;
}

/*
 * Shared f_stat-with-bare-name-retry helper.
 *
 * Both sys_stat and sys_access need the same thing: stat the path
 * as given; if that fails with FR_INVALID_NAME / FR_NO_FILE /
 * FR_NO_PATH and the path has no ':' (i.e. is not already in FatFs
 * drive form), resolve the base name to "0:/NAME.ELF" and retry.
 *
 * FR_NO_PATH is included because busybox ash's PATH candidates are
 * "sbin/ls", "usr/bin/ls", etc. -- paths with a directory component
 * that does not exist on the flat FAT root.  FatFs returns
 * FR_NO_PATH for those, and without this the retry never fires and
 * `sh: ls: not found` is the result.  See the session-22 notes.
 *
 * On success, *out_fno holds the FILINFO and the function returns
 * FR_OK.  On failure, returns the last FRESULT.
 */
static FRESULT f_stat_with_retry(const char* path, FILINFO* out_fno) {
    FRESULT r = f_stat(path, out_fno);

    if (r == FR_INVALID_NAME || r == FR_NO_FILE || r == FR_NO_PATH) {
        int has_drive = 0;
        for (const char* p = path; *p; p++) {
            if (*p == ':') { has_drive = 1; break; }
        }

        char resolved[USER_PATH_MAX];
        if (!has_drive &&
            exec_resolve_bare_name(path, resolved, sizeof(resolved)) == 0) {
#if DEBUG_STAT_TRACE
            serial_print("f_stat retry: '");
            serial_print(path);
            serial_print("' -> '");
            serial_print(resolved);
            serial_print("' = ");
            serial_print_dec((uint64_t)r);
#endif
            FRESULT r2 = f_stat(resolved, out_fno);
#if DEBUG_STAT_TRACE
            serial_print("/");
            serial_print_dec((uint64_t)r2);
            serial_print("\n");
#endif
            if (r2 == FR_OK) {
                r = FR_OK;
            }
        }
    }
    return r;
}

/*
 * Linux x86_64 stat(2) — syscall 4.
 *
 * Path-based sibling of sys_fstat.  musl's stat(path, st) routes
 * through fstatat(AT_FDCWD, path, st, 0) → fstatat_kstat →
 * __syscall(SYS_stat, path, &kst).  SYS_stat is 4.
 *
 * The path is resolved against the process cwd first (see
 * resolve_against_cwd), then strip_dot_prefix removes any leading
 * "./" or "/", then it is handed to FatFs's f_stat.
 *
 * Failure return is a proper negative errno (via fatfs_errno) so
 * that callers like busybox ash's PATH search get a sensible
 * message ("No such file or directory") instead of the default
 * "Operation not permitted" that musl assigns to a bare -1.
 */
long sys_stat(const char* user_path, void* user_stat) {
    if (!user_path || !user_stat) return -(long)EFAULT_;

    char path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];
    if (copy_user_string(path, sizeof(path), user_path) != 0) {
        return -(long)EFAULT_;
    }
    if (resolve_against_cwd(process_get_current(), path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(path) - 1) {
            path[i] = resolved[i];
            i++;
        }
        path[i] = '\0';
    }
    strip_dot_prefix(path);

#if DEBUG_STAT_TRACE
    serial_print("sys_stat: '");
    serial_print(path);
    serial_print("'\n");
#endif

    /* ".", "/", "0:", "0:/" — synthesize a root-directory stat.
     * f_stat would return FR_INVALID_NAME for all of them. */
    if (path_is_root(path)) {
        kernel_stat_t st;
        fill_kstat_as_root(&st);
        if (safe_copy_to_user(user_stat, &st, sizeof(st)) != 0) {
            return -(long)EFAULT_;
        }
        return 0;
    }

    FILINFO fno;
    FRESULT r = f_stat_with_retry(path, &fno);
    if (r != FR_OK) {
        return fatfs_errno(r);
    }

    kernel_stat_t st;
    fill_kstat_from_filinfo(&st, &fno);

    if (safe_copy_to_user(user_stat, &st, sizeof(st)) != 0) {
        return -(long)EFAULT_;
    }
    return 0;
}

/*
 * Linux x86_64 lstat(2) — syscall 6.
 *
 * FAT has no symlinks, so lstat and stat are identical.  musl's
 * lstat() routes to fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW)
 * which, on x86_64, dispatches to __syscall(SYS_lstat, path, &kst).
 * Without this, busybox falls back and prints the "Unknown syscall:
 * 6" line we saw during musl_readdir.
 */
long sys_lstat(const char* user_path, void* user_stat) {
    return sys_stat(user_path, user_stat);
}

/*
 * Linux x86_64 access(2) — syscall 21.
 *
 * Returns 0 if the path exists and the requested permission bits
 * are satisfiable, -errno otherwise.  donix does not track UNIX
 * permissions (FAT has none), so we only check existence: F_OK
 * (0), R_OK (4), W_OK (2), X_OK (1) all reduce to "does this
 * path resolve to something on the FAT".
 *
 * The path is resolved against the process cwd first, then checked
 * with the same f_stat-with-retry used by sys_stat.
 *
 * Why this exists: busybox's find_execable() (libbb/find_execable.c)
 * calls access(path, X_OK) for each PATH candidate before deciding
 * whether to execve it.  On donix there is no sys_access, so the
 * probe hit "Unknown syscall: 21", returned -ENOSYS, and ash
 * concluded the command did not exist — before ever reaching
 * sys_stat or sys_execve.  That is why `sh: ls: not found`
 * appeared even though stat("ls") would now succeed.
 */
long sys_access(const char* user_path, int mode) {
    (void)mode;   /* permissions are not tracked; existence is all */

    if (!user_path) return -(long)EFAULT_;

    char path[USER_PATH_MAX];
    char resolved[USER_PATH_MAX];
    if (copy_user_string(path, sizeof(path), user_path) != 0) {
        return -(long)EFAULT_;
    }
    if (resolve_against_cwd(process_get_current(), path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(path) - 1) {
            path[i] = resolved[i];
            i++;
        }
        path[i] = '\0';
    }
    strip_dot_prefix(path);

#if DEBUG_STAT_TRACE
    serial_print("sys_access: '");
    serial_print(path);
    serial_print("'\n");
#endif

    /* Root always "exists" as a directory. */
    if (path_is_root(path)) {
        return 0;
    }

    FILINFO fno;
    FRESULT r = f_stat_with_retry(path, &fno);
    if (r != FR_OK) {
        return fatfs_errno(r);
    }
    return 0;
}

/*
 * Linux x86_64 utimensat(2) — syscall 280.
 *
 * touch(1) and other tools use this to set file timestamps.
 * donix does not persist timestamps; FAT stores modification
 * time in a coarse 2-second-resolution field that we do not
 * currently write.  The correct minimal implementation is to
 * verify the path exists and return 0.
 *
 * Reporting success without storing is what makes `touch`
 * usable now; reporting -ENOSYS makes busybox fall through its
 * whole fallback chain (utimensat -> utimes -> futimesat) and
 * print "Function not implemented".
 *
 * AT_FDCWD is -100; dirfd is ignored.  The flags argument may
 * carry AT_SYMLINK_NOFOLLOW, which is meaningless on FAT (no
 * symlinks).  Both are accepted and ignored.
 */
long sys_utimensat(int dirfd, const char* path, const void* times, int flags) {
    (void)times; (void)flags; (void)dirfd;

    /* NULL path with a valid dirfd is the futimens(fd) form,
     * which we do not support. */
    if (!path) return -(long)EINVAL_;

    char local[USER_PATH_MAX];
    if (copy_user_string(local, sizeof(local), path) != 0) {
        return -(long)EFAULT_;
    }
    strip_dot_prefix(local);

    /* Root always "exists". */
    if (path_is_root(local)) return 0;

    FILINFO fno;
    FRESULT r = f_stat_with_retry(local, &fno);
    if (r != FR_OK) return fatfs_errno(r);
    return 0;
}

/*
 * Linux x86_64 utimes(2) — syscall 235.
 * Legacy timeval form.  Same no-op semantics as utimensat.
 */
long sys_utimes(const char* path, const void* times) {
    return sys_utimensat(-100, path, times, 0);
}

/*
 * Linux x86_64 futimesat(2) — syscall 261.
 * Older glibc form.  Same no-op semantics.
 */
long sys_futimesat(int dirfd, const char* path, const void* times) {
    return sys_utimensat(dirfd, path, times, 0);
}

/*
 * Linux x86_64 faccessat(2) — syscall 269.
 *
 * musl's faccessat() on x86_64 with AT_EACCESS unset does NOT go
 * straight to syscall 269; it calls access() (21).  But busybox
 * and glibc-built code may call faccessat directly, and glibc's
 * faccessat wrapper is __NR_faccessat (269).  Implement it as a
 * direct alias of sys_access; the dirfd / flags arguments are
 * ignored, which is correct for the only case we can support
 * (AT_FDCWD, no flags).
 */
long sys_faccessat(int dirfd, const char* user_path, int mode, int flags) {
    (void)dirfd; (void)flags;
    return sys_access(user_path, mode);
}

// ============================================================
// execve helpers (used only by sys_execve below)
// ============================================================

/*
 * Free and unmap every user page the process owns.
 *
 * The pages are tracked in pcb->elf_page_list, populated by
 * elf_add_page_to_pcb from elf_load_into_process, the user-stack
 * allocator, sys_brk, and sys_mmap.  Every page in the
 * list is a user page currently mapped in pcb->cr3.
 *
 * Order matters: unmap before free, so no live PTE points at a
 * freed physical page.  We resolve VA->PA by walking the regions
 * the process could have used and matching against the list; the
 * list stores physical addresses only.
 *
 * The regions, in VA order:
 *   - ELF image:   [0x400000,      0x600000)      2 MB
 *   - user stack:  [0x8000000000,  0x8000100000)  64 KB
 *   - brk heap:    [0x8000200000,  0x8000300000)  1 MB
 *   - mmap region: [0x8010000000,  0x8010400000)  4 MB
 *
 * Scanning ~1900 pages total is fast (sub-millisecond on KVM,
 * a few ms on TCG).  Do NOT widen these ranges back to the whole
 * user half — an earlier version scanned [0x400000, 0x8020000000)
 * and was so slow under QEMU's -d in_asm,cpu flag that it looked
 * like a hang.
 *
 * If a future test maps a VA outside these ranges, extend the
 * table with a new row.
 */
static void exec_free_and_unmap_user_pages(pcb_t* pcb) {
    if (!pcb || !pcb->elf_page_list) return;

    static const struct { uint64_t start, end; } regions[] = {
        { 0x0000000000400000ULL, 0x0000000000600000ULL },  /* ELF image   */
        { 0x0000008000000000ULL, 0x0000008000100000ULL },  /* user stack  */
        { 0x0000008000200000ULL, 0x0000008000300000ULL },  /* brk heap    */
        { 0x0000008010000000ULL, 0x0000008010400000ULL },  /* mmap region */
    };
    const size_t n_regions = sizeof(regions) / sizeof(regions[0]);

    for (size_t r = 0; r < n_regions; r++) {
        for (uint64_t va = regions[r].start;
             va < regions[r].end;
             va += 0x1000) {

            uint64_t phys = vmm_get_phys_from_cr3(pcb->cr3, va);
            if (!phys) continue;
            phys &= ~0xFFFULL;

            int owned = 0;
            for (uint64_t i = 0; i < pcb->elf_num_pages; i++) {
                if (pcb->elf_page_list[i] == phys) { owned = 1; break; }
            }
            if (!owned) continue;

            vmm_unmap_page_in_cr3(pcb->cr3, va);
        }
    }

    for (uint64_t i = 0; i < pcb->elf_num_pages; i++) {
        uint64_t phys = pcb->elf_page_list[i];
        if (phys) pmm_free_page(phys);
    }
    kfree(pcb->elf_page_list);
    pcb->elf_page_list = NULL;
    pcb->elf_num_pages = 0;
}

/*
 * Allocate and map a fresh user stack in pcb->cr3.
 *
 * Mirrors the USER_STACK_PAGES block in process_create exactly, so
 * the new program's stack looks identical to a freshly-created
 * process's.  Returns the new user_stack_top, or 0 on failure.  On
 * failure, any pages already allocated are tracked in elf_page_list
 * and will be freed by the caller's cleanup path.
 */
static uint64_t exec_alloc_user_stack(pcb_t* pcb) {
    #define EXEC_USER_STACK_PAGES 16
    #define EXEC_USER_STACK_SIZE (EXEC_USER_STACK_PAGES * 4096)

    uint64_t stack_top_anchor    = 0x8000100000ULL;
    uint64_t stack_bottom_anchor = stack_top_anchor - EXEC_USER_STACK_SIZE;

    pcb->user_stack_virt = stack_bottom_anchor;

    for (int i = 0; i < EXEC_USER_STACK_PAGES; i++) {
        uint64_t phys = pmm_alloc_page_for_elf();
        if (!phys) return 0;

        uint64_t virt = stack_bottom_anchor + (i * 4096);
        uint64_t map_flags = PT_PRESENT | PT_WRITE | PT_USER;
        map_flags &= ~(0x80ULL | 0x40ULL | 0x200ULL | 0x800ULL);

        vmm_map_page_in_cr3(pcb->cr3, virt, phys, map_flags);

        void* hhdm = (void*)(HHDM_START + phys);
        for (uint64_t j = 0; j < 4096 / 8; j++) {
            ((uint64_t*)hhdm)[j] = 0;
        }

        elf_add_page_to_pcb(pcb, phys);

        if (i == (EXEC_USER_STACK_PAGES - 1)) {
            pcb->user_stack_phys = phys;
        }
    }

    uint64_t top = stack_top_anchor - 32;
    top &= ~0xFULL;
    pcb->user_stack_top = top;
    return top;
}

/*
 * Resolve a bare command name to a root-level path.
 *
 * This is sub-attempt (c1) of sys_execve's three-attempt open:
 * turn a bare name like "ls" into "0:/LS.ELF" — uppercased, with
 * ".ELF" appended, at the FAT root.  This is what makes ash's
 * execve("ls", ...) find the donix-native root binary, and what
 * makes `donix> hello` work from musl_sh without the shell doing
 * any rewriting of its own.
 *
 * Root is tried before /bin (see exec_resolve_bin_name) so a
 * donix-native binary shadows a same-named entry in /bin.
 *
 * The rule:
 *   - Take the base name: the substring after the last '/', or
 *     the whole string if there is no '/'.
 *   - If the base already ends in ".ELF" (case-insensitive), do
 *     not append it again.
 *   - Uppercase the base to match the FAT layout.
 *   - Prepend "0:/".
 *
 * Returns 0 on success, -1 if the resolved path would overflow
 * `out_cap` or if `in` has no base name (was "/" or "").
 */
static int exec_resolve_bare_name(const char* in, char* out, size_t out_cap) {
    /* Pick the base name: the substring after the last '/', or the
     * whole string if there is no '/'. */
    const char* base = in;
    for (const char* p = in; *p; p++) {
        if (*p == '/') base = p + 1;
    }

    if (*base == '\0') return -1;   /* path was just "/" or "" */

    /* If the base already ends in ".ELF" (case-insensitive), don't
     * append it again. */
    size_t blen = 0;
    while (base[blen]) blen++;
    int have_suffix = 0;
    if (blen >= 4) {
        char c0 = base[blen - 4];
        char c1 = base[blen - 3];
        char c2 = base[blen - 2];
        char c3 = base[blen - 1];
        if ((c0 == '.' && (c1 == 'E' || c1 == 'e') &&
             (c2 == 'L' || c2 == 'l') && (c3 == 'F' || c3 == 'f'))) {
            have_suffix = 1;
        }
    }

    size_t need = 3 /* "0:/" */ + blen + (have_suffix ? 0 : 4) + 1;
    if (need > out_cap) return -1;

    out[0] = '0';
    out[1] = ':';
    out[2] = '/';
    size_t o = 3;
    for (size_t i = 0; i < blen; i++) {
        char c = base[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[o++] = c;
    }
    if (!have_suffix) {
        out[o++] = '.';
        out[o++] = 'E';
        out[o++] = 'L';
        out[o++] = 'F';
    }
    out[o] = '\0';
    return 0;
}

/*
 * Resolve a bare command name to a path under /bin.
 *
 * `in` is a bare name like "busybox" (no '/').  Produces:
 *
 *   with_suffix == 0:  "0:/BIN/NAME"
 *   with_suffix == 1:  "0:/BIN/NAME.ELF"
 *
 * NAME is uppercased, matching the convention exec_resolve_bare_name
 * uses.  FatFs is case-insensitive on lookup, so the uppercase form
 * finds both "busybox" and "BUSYBOX.ELF" on disk.
 *
 * Returns 0 on success, -1 on overflow or if `in` is not a bare name.
 */
static int exec_resolve_bin_name(const char* in, char* out,
                                 size_t out_cap, int with_suffix) {
    /* Callers pass bare names only; reject anything with a slash so
     * a mistake here does not silently produce a doubled path. */
    for (const char* p = in; *p; p++) {
        if (*p == '/') return -1;
    }
    if (*in == '\0') return -1;

    size_t blen = 0;
    while (in[blen]) blen++;

    /* "0:/BIN/" is 7 chars, plus name, plus optional ".ELF", plus NUL. */
    size_t need = 7 + blen + (with_suffix ? 4 : 0) + 1;
    if (need > out_cap) return -1;

    out[0] = '0';
    out[1] = ':';
    out[2] = '/';
    out[3] = 'B';
    out[4] = 'I';
    out[5] = 'N';
    out[6] = '/';

    size_t o = 7;
    for (size_t i = 0; i < blen; i++) {
        char c = in[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[o++] = c;
    }
    if (with_suffix) {
        out[o++] = '.';
        out[o++] = 'E';
        out[o++] = 'L';
        out[o++] = 'F';
    }
    out[o] = '\0';
    return 0;
}

// ============================================================
// SYS_EXECVE (59) — Linux execve, in-place
//
// Replaces the calling process's user address space with a new ELF,
// without creating a new process.  On success this function returns
// 0 to the *new* program's entry point via the syscall return path,
// not to the caller of execve.
//
// Reuses the ELF validation, file reading, and argv-layout logic
// from sys_spawn.  The differences:
//   - operates on `self`, not a freshly-created child;
//   - reads argv from the *old* address space before tearing it
//     down;
//   - frees and unmaps the old user pages before loading the new
//     ELF (the teardown-then-load approach, not copy-then-swap);
//   - rewrites the syscall-entry frame at self->kernel_stack_top so
//     the return path resumes at the new entry with the new stack.
//
// SIMPLIFICATION: if the load fails after the old address space has
// been torn down, the process is exited via sys_exit(-1).  The
// Linux contract is to return -errno with the caller intact, but
// implementing that requires a scratch address space and a CR3
// swap, which was tried earlier and produced a cascade of page-table
// sharing bugs.  Given the failure paths after teardown are
// essentially unreachable for a validated ELF (file already in
// memory, allocators behave like every other syscall), we accept
// the shortcut.  If a specific failure needs to be user-recoverable
// later, add copy-then-swap as a follow-up.
//
// Runs with interrupts disabled.  A timer tick in the middle would
// let the scheduler pick a process whose address space is
// half-destroyed.
// ============================================================
long sys_execve(const char* user_path, char** user_argv, char** user_envp) {
    (void)user_envp;

    __asm__ volatile("cli");

    pcb_t* self = process_get_current();
    if (!self || !user_path) {
        __asm__ volatile("sti");
        return -(long)EFAULT_;
    }

    /* ---- 1. Copy the path string. ---- */
    char path[USER_PATH_MAX];
    if (copy_user_string(path, sizeof(path), user_path) != 0) {
        serial_print("sys_execve: bad path pointer\n");
        __asm__ volatile("sti");
        return -(long)EFAULT_;
    }
    /*
     * Normalize the path for FatFs: strip any leading "./" or "/"
     * so a path like "./test.sh" becomes "test.sh".  FatFs rejects
     * a leading "." or "/" with FR_INVALID_NAME, so without this
     * the first f_open below fails before any retry can run, and
     * `./script.sh` never reaches the interpreter fallback.
     *
     * The caller's original `path` is kept for the proc_name
     * extraction in section 4.
     */
    char exec_path[USER_PATH_MAX];
    {
        size_t i = 0;
        while (path[i] && i < sizeof(exec_path) - 1) {
            exec_path[i] = path[i];
            i++;
        }
        exec_path[i] = '\0';
        strip_dot_prefix(exec_path);
    }
    
    /* ---- 2. Open and read the whole ELF file. ----
     *
     * Three attempts, in order:
     *
     *   (a) the path exactly as the caller supplied it;
     *   (b) if the path starts with '/', "0:" + path, preserving
     *       case and suffix -- the Unix-style absolute path form;
     *   (c) if the path has no ':' at all, the bare-name form.
     *       Attempt (c) itself has three sub-attempts: root
     *       "0:/NAME.ELF", then "/bin/NAME", then "/bin/NAME.ELF".
     *
     * (b) is what makes `/bin/busybox sh` work from the custom
     * musl shell: the kernel was previously handing "/bin/busybox"
     * straight to FatFs, which rejects any path with a leading
     * slash.  The kernel is the layer that should translate a
     * Unix-style path to the FatFs form, not the caller.
     *
     * (c1) is what makes ash's bare `execve("ls", ...)` resolve to
     * the donix-native root binary.  (c2) and (c3) are what make
     * bare `busybox` resolve to /bin/busybox now that the busybox
     * binary no longer sits at the FAT root.  Root is tried first
     * so donix-native binaries shadow same-named /bin entries.
     */
    /*
     * VFS SHIM.  The three attempts below stand in for a virtual
     * filesystem layer that donix does not have yet.  On real
     * Unix, execve hands the path to the VFS and the VFS resolves
     * it; there is no guessing and no retry.  Here, FatFs has no
     * notion of '/', no root directory in the POSIX sense, and no
     * way to walk a multi-component path, so the kernel does the
     * translation inline.
     *
     * When a VFS lands, DELETE this whole block and make execve
     * call the VFS resolver once.  Do not add a fourth attempt;
     * add the VFS instead.  Candidates that must then be removed:
     * the "0:" + path prepend, exec_resolve_bare_name, and
     * exec_resolve_bin_name.
     */
    FIL file;
    FRESULT fr = f_open(&file, exec_path, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK) {
        int has_drive = 0;
        for (const char* p = exec_path; *p; p++) {
            if (*p == ':') { has_drive = 1; break; }
        }

        /*
         * Attempt (b): Unix-style absolute path -> "0:" + path.
         *
         * Preserves case and any suffix: the caller named an
         * exact path, so we honor it as written and only add the
         * drive prefix FatFs requires.  No uppercasing, no
         * ".ELF" appended.
         */
        if (!has_drive && exec_path[0] == '/') {
            char resolved[USER_PATH_MAX];
            size_t plen = 0;
            while (exec_path[plen]) plen++;
            if (plen + 3 <= sizeof(resolved)) {   /* "0:" + path + NUL */
                resolved[0] = '0';
                resolved[1] = ':';
                for (size_t i = 0; i <= plen; i++) {
                    resolved[2 + i] = exec_path[i];
                }
                FRESULT fr2 = f_open(&file, resolved,
                                     FA_READ | FA_OPEN_EXISTING);
                if (fr2 == FR_OK) {
                    fr = FR_OK;
                }
            }
            /* if too long, skip (b) and fall through to (c) */
        }

        /*
         * Attempt (c): bare name.  Three sub-attempts, in order:
         *
         *   (c1) "0:/NAME.ELF"     -- root, uppercased, .ELF appended.
         *                             Donix-native binaries win here.
         *   (c2) "0:/BIN/NAME"     -- /bin, uppercased, no suffix.
         *                             This is where busybox lives now.
         *   (c3) "0:/BIN/NAME.ELF" -- /bin, uppercased, .ELF appended.
         *                             Covers a future /bin/NAME.ELF.
         */
        if (fr != FR_OK && !has_drive) {
            char resolved[USER_PATH_MAX];

            /* (c1) root, uppercased, .ELF appended. */
            if (exec_resolve_bare_name(exec_path, resolved,
                                       sizeof(resolved)) == 0) {
                FRESULT fr2 = f_open(&file, resolved,
                                     FA_READ | FA_OPEN_EXISTING);
                if (fr2 == FR_OK) {
                    fr = FR_OK;
                }
            }

            /* (c2) /bin, uppercased, as-is (no .ELF). */
            if (fr != FR_OK &&
                exec_resolve_bin_name(exec_path, resolved,
                                      sizeof(resolved), 0) == 0) {
                FRESULT fr2 = f_open(&file, resolved,
                                     FA_READ | FA_OPEN_EXISTING);
                if (fr2 == FR_OK) {
                    fr = FR_OK;
                }
            }

            /* (c3) /bin, uppercased, .ELF appended. */
            if (fr != FR_OK &&
                exec_resolve_bin_name(exec_path, resolved,
                                      sizeof(resolved), 1) == 0) {
                FRESULT fr2 = f_open(&file, resolved,
                                     FA_READ | FA_OPEN_EXISTING);
                if (fr2 == FR_OK) {
                    fr = FR_OK;
                }
            }
        }
    }

    if (fr != FR_OK) {
        serial_print("sys_execve: f_open(");
        serial_print(path);
        serial_print(") -> ");
        serial_print_dec(fr);
        serial_print("\n");
        __asm__ volatile("sti");
        return fatfs_errno(fr);
    }

    Elf64_Ehdr ehdr;
    UINT got = 0;
    fr = f_read(&file, &ehdr, sizeof(ehdr), &got);
    if (fr != FR_OK) {
        serial_print("sys_execve: f_read header failed\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)EIO_;
    }

    /*
     * A short read here is NOT an I/O error.  The file may simply
     * be shorter than an ELF header -- a shell script, for example.
     * What matters is whether the bytes we did read start with the
     * ELF magic.  If they do, the file claims to be an ELF and we
     * then require the full 64-byte header.  If they do not, this
     * is not an ELF and the correct errno is ENOEXEC, which tells
     * the caller's shell to fall back to running the file through
     * an interpreter.
     *
     * Returning EIO for a short non-ELF file is what broke
     * `./script.sh`: busybox ash only falls back to `sh script.sh`
     * when execve returns ENOEXEC; on EIO it prints "I/O error"
     * and gives up.
     */
    if (got < 4 ||
        ehdr.e_ident[0] != ELF_MAGIC0 || ehdr.e_ident[1] != ELF_MAGIC1 ||
        ehdr.e_ident[2] != ELF_MAGIC2 || ehdr.e_ident[3] != ELF_MAGIC3) {
        serial_print("sys_execve: not an ELF file\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)ENOEXEC;
    }

    /* The magic matched: it claims to be an ELF, so require the
     * full header. */
    if (got != sizeof(ehdr)) {
        serial_print("sys_execve: short ELF header\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)ENOEXEC;
    }
    if (ehdr.e_ident[4] != 2) { serial_print("sys_execve: not ELFCLASS64\n");     f_close(&file); __asm__ volatile("sti"); return -(long)ENOEXEC; }
    if (ehdr.e_ident[5] != 1) { serial_print("sys_execve: not little-endian\n");  f_close(&file); __asm__ volatile("sti"); return -(long)ENOEXEC; }
    if (ehdr.e_type != 2)     { serial_print("sys_execve: not ET_EXEC\n");        f_close(&file); __asm__ volatile("sti"); return -(long)ENOEXEC; }
    if (ehdr.e_machine != 62) { serial_print("sys_execve: not x86-64\n");         f_close(&file); __asm__ volatile("sti"); return -(long)ENOEXEC; }
    if (ehdr.e_phentsize != sizeof(Elf64_Phdr) ||
        ehdr.e_phnum == 0 || ehdr.e_phnum > EXEC_MAX_PHDRS) {
        serial_print("sys_execve: bad program header table\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)ENOEXEC;
    }

    FSIZE_t file_size = f_size(&file);
    if (file_size == 0) {
        serial_print("sys_execve: empty file\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)ENOEXEC;
    }
    if (file_size > 4ULL * 1024 * 1024) {
        serial_print("sys_execve: file too large\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)EIO_;
    }
    uint8_t* elf_buf = (uint8_t*)kmalloc((size_t)file_size);
    if (!elf_buf) {
        serial_print("sys_execve: kmalloc failed for ");
        serial_print_dec((uint64_t)file_size);
        serial_print(" bytes\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -(long)ENOMEM_;
    }
    fr = f_lseek(&file, 0);
    if (fr != FR_OK) {
        serial_print("sys_execve: rewind failed\n");
        kfree(elf_buf); f_close(&file);
        __asm__ volatile("sti");
        return -(long)EIO_;
    }
    UINT total = 0;
    while (total < file_size) {
        UINT br = 0;
        UINT want = (UINT)(file_size - total);
        if (want > 4096) want = 4096;
        fr = f_read(&file, elf_buf + total, want, &br);
        if (fr != FR_OK) {
            serial_print("sys_execve: read failed at offset ");
            serial_print_dec(total);
            serial_print("\n");
            kfree(elf_buf); f_close(&file);
            __asm__ volatile("sti");
            return -(long)EIO_;
        }
        if (br == 0) break;
        total += br;
    }
    f_close(&file);
    if (total != file_size) {
        serial_print("sys_execve: short read of file body\n");
        kfree(elf_buf);
        __asm__ volatile("sti");
        return -(long)EIO_;
    }

    /* ---- 3. Snapshot argv from the OLD address space. ---- */
    int argc = 0;
    char argv_scratch[EXEC_MAX_ARGC][EXEC_MAX_ARG_LEN];

    if (user_argv) {
        for (argc = 0; argc < EXEC_MAX_ARGC; argc++) {
            uint64_t user_str_va = 0;
            if (safe_copy_from_user(&user_str_va,
                                    (const char**)user_argv + argc,
                                    sizeof(user_str_va)) != 0) {
                serial_print("sys_execve: bad argv[");
                serial_print_dec(argc);
                serial_print("] pointer\n");
                kfree(elf_buf);
                __asm__ volatile("sti");
                return -(long)EFAULT_;
            }
            if (user_str_va == 0) break;

            if (copy_user_string(argv_scratch[argc], EXEC_MAX_ARG_LEN,
                                 (const char*)user_str_va) != 0) {
                serial_print("sys_execve: bad argv[");
                serial_print_dec(argc);
                serial_print("] string\n");
                kfree(elf_buf);
                __asm__ volatile("sti");
                return -(long)EFAULT_;
            }
        }
    }

    /* ---- 4. Build the process name from the path. ---- */
    char proc_name[PROC_NAME_LEN];
    {
        const char* base = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/' || *p == ':') base = p + 1;
        }
        int i = 0;
        while (base[i] && i < PROC_NAME_LEN - 1) {
            proc_name[i] = base[i];
            i++;
        }
        proc_name[i] = '\0';
        if (i == 0) {
            const char* fallback = "exec";
            for (i = 0; fallback[i] && i < PROC_NAME_LEN - 1; i++)
                proc_name[i] = fallback[i];
            proc_name[i] = '\0';
        }
    }

    /* ---- 5. Tear down the old address space, then load the new one. ---- */
    exec_free_and_unmap_user_pages(self);
    self->user_stack_virt = 0;
    self->user_stack_phys = 0;
    self->user_stack_top  = 0;
    self->brk_virt        = 0;

    uint64_t entry = elf_load_into_process(self, elf_buf);
    kfree(elf_buf);
    if (entry == 0) {
        serial_print("sys_execve: elf_load_into_process failed\n");
        __asm__ volatile("sti");
        sys_exit(-1);
        return -1;  /* unreachable */
    }

    uint64_t new_user_stack_top = exec_alloc_user_stack(self);
    if (new_user_stack_top == 0) {
        serial_print("sys_execve: user stack alloc failed\n");
        __asm__ volatile("sti");
        sys_exit(-1);
        return -1;  /* unreachable */
    }

    /* ---- 6. Lay out argv on the new stack. ---- */
    uint64_t rsp_init = 0;
    uint64_t argv_array_base = 0;   /* for %rsi below; 0 when argc == 0 */

    if (argc > 0) {
        uint64_t argv_region_top    = new_user_stack_top;
        uint64_t argv_region_bottom = argv_region_top - 4096;

        size_t array_bytes = ((size_t)argc + 1) * sizeof(uint64_t);
        uint64_t array_base    = argv_region_bottom;
        uint64_t strings_start = argv_region_bottom + array_bytes + 8;

        uint64_t cursor = strings_start;
        uint64_t arg_vaddrs[EXEC_MAX_ARGC];

        for (int i = 0; i < argc; i++) {
            size_t slen = 0;
            while (slen < EXEC_MAX_ARG_LEN && argv_scratch[i][slen] != '\0') slen++;
            slen++;

            if (cursor + slen > argv_region_top) {
                serial_print("sys_execve: argv region overflow\n");
                __asm__ volatile("sti");
                sys_exit(-1);
                return -1;  /* unreachable */
            }
            uint64_t dst = cursor;

            if (safe_copy_to_user_cr3(self->cr3, (void*)dst,
                                      argv_scratch[i], slen) != 0) {
                serial_print("sys_execve: failed to write argv[");
                serial_print_dec(i);
                serial_print("]\n");
                __asm__ volatile("sti");
                sys_exit(-1);
                return -1;  /* unreachable */
            }
            arg_vaddrs[i] = dst;
            cursor += slen;
        }

        uint64_t array_data[EXEC_MAX_ARGC + 1];
        for (int i = 0; i < argc; i++) array_data[i] = arg_vaddrs[i];
        array_data[argc] = 0;

        if (safe_copy_to_user_cr3(self->cr3, (void*)array_base,
                                  array_data, array_bytes) != 0) {
            serial_print("sys_execve: failed to write argv array\n");
            __asm__ volatile("sti");
            sys_exit(-1);
            return -1;  /* unreachable */
        }

        argv_array_base = array_base;

        rsp_init = argv_region_bottom - 8;
        uint64_t argc_slot = (uint64_t)argc;
        if (safe_copy_to_user_cr3(self->cr3, (void*)rsp_init,
                                  &argc_slot, sizeof(argc_slot)) != 0) {
            serial_print("sys_execve: failed to write argc\n");
            __asm__ volatile("sti");
            sys_exit(-1);
            return -1;  /* unreachable */
        }

        uint64_t envp_null = 0;
        if (safe_copy_to_user_cr3(self->cr3,
                                  (void*)(argv_region_bottom + array_bytes),
                                  &envp_null, sizeof(envp_null)) != 0) {
            serial_print("sys_execve: failed to write envp terminator\n");
            __asm__ volatile("sti");
            sys_exit(-1);
            return -1;  /* unreachable */
        }
    } else {
        rsp_init = new_user_stack_top - 16;
        uint64_t zero = 0;
        if (safe_copy_to_user_cr3(self->cr3, (void*)rsp_init,
                                  &zero, sizeof(zero)) != 0) {
            serial_print("sys_execve: failed to write argc=0\n");
            __asm__ volatile("sti");
            sys_exit(-1);
            return -1;  /* unreachable */
        }
    }

    /* ---- 7. Rewrite the syscall-entry frame. ---- */
    uint64_t* ktop = (uint64_t*)self->kernel_stack_top;
    ktop[-7] = entry;      /* -56: user RIP  */
    ktop[-9] = rsp_init;   /* -72: user RSP  */
    ktop[-12] = (uint64_t)argc;       /* -96: rdi */
    ktop[-13] = argv_array_base;      /* -104: rsi */

    /* ---- 8. Update PCB fields. ---- */
    self->entry_point = entry;
    self->rip         = entry;
    self->exit_status = 0;
    self->wait_pid    = 0;
    self->block_kind  = BLOCK_KIND_NONE;
    self->state       = PROC_STATE_RUNNING;

    {
        int i = 0;
        while (proc_name[i] && i < PROC_NAME_LEN - 1) {
            self->name[i] = proc_name[i];
            i++;
        }
        self->name[i] = '\0';
    }

    keyboard_buffer_flush();

    serial_print("sys_execve: pid=");
    serial_print_dec(self->pid);
    serial_print(" entry=0x"); serial_print_hex(entry);
    serial_print(" argc=");    serial_print_dec((uint64_t)argc);
    serial_print(" rsp=0x");   serial_print_hex(rsp_init);
    serial_print(" (");        serial_print(proc_name);
    serial_print(")\n");

    __asm__ volatile("sti");
    return 0;
}

// ============================================================
// SYS_WAIT4 (61)
// ============================================================
long sys_wait4(long pid, int* user_status, int options) {
    pcb_t* self = process_get_current();
    if (!self) return -(long)ECHILD;

    uint64_t target = (pid <= 0) ? (uint64_t)-1 : (uint64_t)pid;

    for (;;) {
        pcb_t* zombie = NULL;
        pcb_t* live   = NULL;

        for (uint64_t child_pid = 1; child_pid < 1000; child_pid++) {
            pcb_t* c = process_find_by_pid(child_pid);
            if (!c) continue;
            if (c->parent_pid != self->pid) continue;
            if (target != (uint64_t)-1 && c->pid != target) continue;

            if (c->state == PROC_STATE_ZOMBIE) {
                zombie = c;
                break;
            }
            if (c->state != PROC_STATE_UNUSED) {
                live = c;
            }
        }

        if (zombie) {
            long reaped = (long)zombie->pid;
            int status = (zombie->exit_status & 0xff) << 8;
            process_reclaim(zombie);
            if (user_status) {
                if (safe_copy_to_user(user_status, &status, sizeof(status)) != 0) {
                    return -(long)EFAULT_;
                }
            }
            return reaped;
        }

        if (!live) {
            return -(long)ECHILD;
        }

        if (options & WNOHANG) {
            return 0;
        }

        self->state = PROC_STATE_BLOCKED;
        self->block_kind = BLOCK_KIND_WAITPID;
        self->wait_pid = target;

        /*
         * Late re-scan.  A child may have exited between the scan above
         * and this point: we were preemptible, and the child could have
         * run to completion on another CPU tick, become a zombie, and
         * tried to wake us — but failed, because our state was still
         * RUNNING when it looked.  Without this re-scan, we would block
         * on a child that has already exited and nobody would ever wake
         * us.
         *
         * If a zombie is now visible, un-block, restore state, and
         * loop; the outer for(;;) will reap it and return.
         */
        {
            pcb_t* late_zombie = NULL;
            for (uint64_t child_pid = 1; child_pid < 1000; child_pid++) {
                pcb_t* c = process_find_by_pid(child_pid);
                if (!c) continue;
                if (c->parent_pid != self->pid) continue;
                if (target != (uint64_t)-1 && c->pid != target) continue;
                if (c->state == PROC_STATE_ZOMBIE) { late_zombie = c; break; }
            }
            if (late_zombie) {
                self->state = PROC_STATE_RUNNING;
                self->block_kind = BLOCK_KIND_NONE;
                self->wait_pid = 0;
                continue;
            }
        }

        process_yield();
    }
}

// ============================================================
// IO
// ============================================================
long sys_write(int fd, const void* buf, size_t count) {
    if (!buf || count == 0) return 0;
    pcb_t* self = process_get_current();
    if (!self) return -(long)EBADF_;

    /*
     * fd 1 and 2 are the screen ONLY when they hold a console slot,
     * or nothing at all.  A redirected file (`echo hi > file` does
     * dup2(file_fd, 1)) or a pipe end (a shell's `cmd | other`
     * puts the write end of the pipe on the child's fd 1) is a
     * real fd and must be written through, not sent to the screen.
     *
     * Same reasoning as sys_read's fd-0 guard: the old test was
     * `kind != FILE_KIND_FILE`, which mis-routes a pipe.  Invert
     * it to "is this a console slot" -- that is what the keyboard/
     * screen path is for.
     *
     * Note this one reads file_table[fd] (via get_file_slot_any(fd)),
     * not file_table[1], because the guard applies to both fd 1 and
     * fd 2 and each needs its own slot.  The sys_read guard reads
     * file_table[0] specifically because it only ever guards fd 0.
     */
    file_slot_t* std_slot = get_file_slot_any(fd);

    if ((fd == 1 || fd == 2) &&
        (!std_slot || std_slot->kind == FILE_KIND_CONSOLE)) {
        size_t remaining = count;
        const uint8_t* user_ptr = (const uint8_t*)buf;
        while (remaining > 0) {
            size_t chunk = remaining > WRITE_CHUNK ? WRITE_CHUNK : remaining;
            if (safe_copy_from_user(g_write_bounce, user_ptr, chunk) != 0) {
                return -(long)EFAULT_;
            }
            for (size_t i = 0; i < chunk; i++) {
                char c = g_write_bounce[i];
                serial_putc(c); vga_putc(c);
            }
            user_ptr += chunk; remaining -= chunk;
        }
        return (long)count;
    }

    /*
     * Pipe write end.  Step 2: if the pipe is full, block until a
     * reader drains space, then retry.  Step 3 will add EPIPE when
     * the read end has closed; Step 2 does not distinguish "full
     * and a reader exists" from "full and the reader is gone."
     *
     * BLOCKING DISCIPLINE.  The loop below mirrors sys_read's fd-0
     * path and sys_poll's blocking path exactly:
     *
     *     cli
     *     if (space available) { sti; proceed; }
     *     record self as the pipe's writer_waiter
     *     mark BLOCKED
     *     sti; hlt
     *
     * The cli is what makes the check and the waiter-store atomic
     * with respect to a reader that might drain the pipe on another
     * tick.  Without it there is a missed-wakeup window: we check
     * "full", a reader drains the pipe and calls pipe_wake_waiter
     * on writer_waiting (still NULL), we store self and hlt, and
     * nobody wakes us -- the wake already happened.
     *
     * We do NOT assume the wake means space is available.  After
     * hlt we loop and re-check; the space may have been taken by
     * another writer (we are the only writer for this pipe in
     * practice, but the loop is correct either way).
     */
    file_slot_t* pslot = get_file_slot_any(fd);
    if (pslot && pslot->kind == FILE_KIND_PIPE) {
        pipe_t* pipe = (pipe_t*)pslot->obj;
        pcb_t* self = process_get_current();

        for (;;) {
            __asm__ volatile("cli");

            /*
             * Step 3: no reader left anywhere == EPIPE.  This
             * check comes BEFORE the space check, because the
             * correct answer to "write when nobody will ever
             * read" is EPIPE regardless of whether there happens
             * to be space in the ring.  Linux returns EPIPE from
             * write() when all read ends are closed, even if the
             * buffer is not full.
             *
             * The count, not a per-slot flag: a dup2'd read end
             * keeps writers alive until every read-end fd closes.
             *
             * On real Linux this also raises SIGPIPE.  donix's
             * signal path is a stub (sys_rt_sigaction returns 0
             * and does not install anything), so we deliver only
             * the -EPIPE errno.  A program that checks the
             * return value sees the right answer; a program that
             * relies on dying from SIGPIPE does not, and that is
             * a known limitation of the signal stubs, not of
             * this code.  Track it in open-issues.
             */
            if (pipe->readers_open == 0) {
                __asm__ volatile("sti");
                return -(long)EPIPE_;
            }

            uint32_t free_space = pipe->capacity - pipe->used;
            if (free_space > 0) {
                __asm__ volatile("sti");
                break;
            }

            /*
             * Full.  If there is no current process we cannot
             * block; return EAGAIN as Step 1 did.  This is a
             * defensive path -- sys_write always runs in process
             * context -- but it keeps a bug in the block path
             * from becoming a hang.
             */
            if (!self) {
                __asm__ volatile("sti");
                return -(long)EAGAIN_;
            }

            /*
             * Record the waiter and block, with interrupts still
             * off from the cli above.  Both the store and the
             * state change are inside the critical section so a
             * reader on another tick sees a consistent picture:
             * either we are BLOCKED and recorded, or we are not
             * yet either and will re-check.
             */
            pipe->writer_waiting = self;
            self->state      = PROC_STATE_BLOCKED;
            self->block_kind = BLOCK_KIND_PIPE_WRITE;

            __asm__ volatile("sti");
            __asm__ volatile("hlt");
            /* Woken by pipe_wake_waiter via a reader's drain.
             * Loop and re-check the free space. */
        }

        /* We hold the cli?  No -- the break above did sti.  From
         * here on we run with interrupts enabled, matching the
         * Step 1 path.  Copy as much as fits. */
        uint32_t free_space = pipe->capacity - pipe->used;
        size_t to_write = count;
        if (to_write > free_space) to_write = free_space;

        size_t done = 0;
        while (done < to_write) {
            size_t span = to_write - done;
            size_t to_end = pipe->capacity - pipe->write_pos;
            if (span > to_end) span = to_end;

            if (safe_copy_from_user(pipe->buf + pipe->write_pos,
                                    (const uint8_t*)buf + done,
                                    span) != 0) {
                pipe->used += (uint32_t)done;
                /* A reader may have been waiting while we copied. */
                if (pipe->reader_waiting) {
                    pipe_wake_waiter(pipe->reader_waiting,
                                     BLOCK_KIND_PIPE_READ);
                    pipe->reader_waiting = NULL;
                }
                return (done > 0) ? (long)done : -(long)EFAULT_;
            }
            pipe->write_pos = (pipe->write_pos + (uint32_t)span)
                              % pipe->capacity;
            done += span;
        }
        pipe->used += (uint32_t)done;

        /* Bytes are in the ring: wake a waiting reader, if any. */
        if (pipe->reader_waiting) {
            pipe_wake_waiter(pipe->reader_waiting, BLOCK_KIND_PIPE_READ);
            pipe->reader_waiting = NULL;
        }
        return (long)done;
    }

    file_slot_t* slot = get_file_slot_any(fd);
    if (slot && slot->kind == FILE_KIND_FILE) {
        FIL* file_obj = (FIL*)slot->obj;
        char* bounce = (char*)kmalloc(512);
        if (!bounce) return -(long)ENOMEM_;

        size_t total_written = 0;
        while (total_written < count) {
            size_t chunk = (count - total_written) > 512 ? 512 : (count - total_written);
            if (safe_copy_from_user(bounce, (const uint8_t*)buf + total_written, chunk) != 0) {
                kfree(bounce);
                return (total_written > 0) ? (long)total_written : -(long)EFAULT_;
            }
            UINT written;
            if (f_write(file_obj, bounce, chunk, &written) != FR_OK) {
                kfree(bounce);
                return (total_written > 0) ? (long)total_written : -(long)EIO_;
            }
            total_written += written;
            if (written < chunk) break;
        }
        kfree(bounce);
        return (long)total_written;
    }

    return -(long)EBADF_;
}

struct iovec {
    void*  iov_base;
    size_t iov_len;
};

/*
 * Linux x86_64 writev(2) — syscall 20.
 *
 * musl's stdio uses writev for buffered output.  This is a thin
 * loop over sys_write; partial writes are returned as a short
 * count (which is what write(2) semantics allow and what musl
 * expects).  Stops early on a short write, like the kernel.
 *
 * The iov array is a user pointer and must be copied through
 * safe_copy_from_user.  Dereferencing it directly reads whatever
 * happens to live at that kernel virtual address, which is not
 * the user's iov array.  This bug was latent because the
 * mis-read values sometimes happened to look like valid iov
 * entries.  printnum's stack layout made it visible.
 *
 * iovcnt is capped at WRITEV_MAX_IOVS to bound the kernel stack
 * usage of the local copy.
 */
#define WRITEV_MAX_IOVS 16
#define DEBUG_WRITEV 0

long sys_writev(int fd, const struct iovec* user_iov, int iovcnt) {
    if (!user_iov || iovcnt <= 0) return -(long)EINVAL_;
    if (iovcnt > WRITEV_MAX_IOVS) return -(long)EINVAL_;

    struct iovec local[WRITEV_MAX_IOVS];
    size_t bytes = (size_t)iovcnt * sizeof(struct iovec);
    if (safe_copy_from_user(local, user_iov, bytes) != 0) {
        return -(long)EFAULT_;
    }

#if DEBUG_WRITEV
    serial_print("[writev] fd=");
    serial_print_dec((uint64_t)fd);
    serial_print(" iovcnt=");
    serial_print_dec((uint64_t)iovcnt);
    for (int i = 0; i < iovcnt; i++) {
        serial_print(" iov[");
        serial_print_dec((uint64_t)i);
        serial_print("] base=0x");
        serial_print_hex((uint64_t)local[i].iov_base);
        serial_print(" len=");
        serial_print_dec((uint64_t)local[i].iov_len);
    }
    serial_print("\n");
#endif

    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (local[i].iov_len == 0) continue;
        long n = sys_write(fd, local[i].iov_base, local[i].iov_len);
        if (n < 0) return (total > 0) ? total : n;
        total += n;
        if ((size_t)n < local[i].iov_len) break;  /* short write — stop */
    }
    return total;
}

/*
 * Linux x86_64 readv(2) — syscall 19.
 *
 * Mirror of sys_writev (20).  Reads into each iovec in turn,
 * accumulating the total; stops early on a short read, exactly as
 * writev stops on a short write.  The same iov-copy discipline
 * applies: the iov array is a user pointer and must be copied
 * through safe_copy_from_user, not dereferenced directly.  The
 * same WRITEV_MAX_IOVS cap bounds the kernel-stack copy.
 *
 * WHY THIS EXISTS: busybox `od -c` calls readv to read the file it
 * is formatting.  Without syscall 19, `od` got -ENOSYS and logged
 * "Unknown syscall: 19" and failed.  `od` is off until this lands;
 * it is re-enabled in the next commit.
 *
 * `struct iovec` and WRITEV_MAX_IOVS are defined above, at the
 * writev section -- both syscalls share them.
 */
long sys_readv(int fd, const struct iovec* user_iov, int iovcnt) {
    if (!user_iov || iovcnt <= 0) return -(long)EINVAL_;
    if (iovcnt > WRITEV_MAX_IOVS) return -(long)EINVAL_;

    struct iovec local[WRITEV_MAX_IOVS];
    size_t bytes = (size_t)iovcnt * sizeof(struct iovec);
    if (safe_copy_from_user(local, user_iov, bytes) != 0) {
        return -(long)EFAULT_;
    }

    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (local[i].iov_len == 0) continue;
        long n = sys_read(fd, local[i].iov_base, local[i].iov_len);
        if (n < 0) return (total > 0) ? total : n;
        total += n;
        if ((size_t)n < local[i].iov_len) break;  /* short read — stop */
    }
    return total;
}

/*
 * Linux x86_64 ftruncate(2) — syscall 77.
 *
 * Confirmed against arch/x86/entry/syscalls/syscall_64.tbl:
 *   77  common  ftruncate  sys_ftruncate
 *
 * vi's save path calls this to set the file size exactly after
 * writing, because the underlying FAT layer may have padded the
 * final write to a sector boundary.  On the current build FatFs
 * already updates the directory entry to the exact fptr at
 * f_close, so the file size in `ls -l` was correct without this
 * — but the syscall fires on every :wq and logging "Unknown
 * syscall: 77" on every save is noise that hides real problems.
 *
 * FatFs's f_truncate() truncates the file to fp->fptr, so we
 * seek first, then truncate.  Both operations must succeed on a
 * FIL opened FA_WRITE.
 *
 * Linux clamps negative lengths to EINVAL.  We do the same.
 */
long sys_ftruncate(int fd, long length) {
    if (length < 0) return -(long)EINVAL_;

    file_slot_t* slot = get_file_slot(fd, FILE_KIND_FILE);
    if (!slot) return -(long)EBADF_;

    FIL* file_obj = (FIL*)slot->obj;

    FRESULT r = f_lseek(file_obj, (FSIZE_t)length);
    if (r != FR_OK) return fatfs_errno(r);

    r = f_truncate(file_obj);
    if (r != FR_OK) return fatfs_errno(r);

    return 0;
}

/*
 * Linux x86_64 lseek(2) — syscall 8.
 *
 * Repositions the read/write cursor on an open file.  Backed by
 * FatFs's f_lseek() on the FIL's fptr; f_tell() returns the new
 * position.  sys_ftruncate already uses f_lseek, so the mechanism
 * is proven.
 *
 * ABI:
 *   arg0  int    fd
 *   arg1  off_t  offset  (signed 64-bit)
 *   arg2  int    whence  SEEK_SET(0) / SEEK_CUR(1) / SEEK_END(2)
 *   returns  the resulting offset from the start of the file,
 *            or -errno.
 *
 * Linux permits seeking past EOF; FatFs does not reliably.  We
 * clamp the target to [0, file_size] and reject anything outside
 * with -EINVAL.  Nothing on donix seeks past EOF today; if a
 * caller appears that needs sparse writes, revisit this.
 *
 * WHY THIS EXISTS: busybox `head -n N` calls lseek(fd, 0,
 * SEEK_END) to size the file before reading.  Without syscall 8
 * it got -ENOSYS and logged "Unknown syscall: 8" (session 33,
 * head applet).  The output was still correct because busybox
 * fell back to sequential reading, but the noise hid real
 * problems and other applets (tail, cp's size checks) would
 * exercise lseek harder.
 *
 * Linux x86_64 syscall numbers: lseek is 8 (see
 * arch/x86/entry/syscalls/syscall_64.tbl).  It was previously
 * unimplemented -- not shadowed at another number, just absent.
 */
#define SEEK_SET_ 0
#define SEEK_CUR_ 1
#define SEEK_END_ 2

long sys_lseek(int fd, long offset, int whence) {
    file_slot_t* slot = get_file_slot(fd, FILE_KIND_FILE);
    if (!slot) return -(long)EBADF_;

    FIL* file_obj = (FIL*)slot->obj;

    FSIZE_t size = f_size(file_obj);
    FSIZE_t cur  = f_tell(file_obj);

    long target;
    switch (whence) {
        case SEEK_SET_:  target = offset;                    break;
        case SEEK_CUR_:  target = (long)cur + offset;        break;
        case SEEK_END_:  target = (long)size + offset;       break;
        default:         return -(long)EINVAL_;
    }

    /* Clamp to [0, size].  See the header comment: Linux allows
     * seeking past EOF, FatFs does not; reject rather than
     * silently truncate. */
    if (target < 0 || (FSIZE_t)target > size) {
        return -(long)EINVAL_;
    }

    FRESULT r = f_lseek(file_obj, (FSIZE_t)target);
    if (r != FR_OK) return fatfs_errno(r);

    return (long)f_tell(file_obj);
}

long sys_read(int fd, void* buf, size_t count) {
    if (!buf || count == 0) return 0;
    pcb_t* self = process_get_current();
    if (!self) return -(long)EBADF_;

    /*
     * fd 0 is the keyboard ONLY when it holds a console slot, or
     * nothing at all.  Anything else on fd 0 -- a redirected file
     * (busybox ash's `<` does dup2(file_fd, 0)) or a pipe end (a
     * shell's `cmd | other` puts the read end of the pipe on the
     * child's fd 0) -- is a real fd and must be read through, not
     * sent to the keyboard.
     *
     * The old guard said `kind != FILE_KIND_FILE`, which was
     * correct when a file was the only thing dup2 could put on
     * fd 0.  It is wrong for a pipe: a pipe slot has kind
     * FILE_KIND_PIPE, which is != FILE_KIND_FILE, so the guard
     * was true and a piped stdin fell through to the keyboard
     * path.  Inverting the test to "is this a console slot"
     * fixes that and is also what the sentinel design means: the
     * keyboard path exists exactly for the console sentinels
     * installed by user_syscall_init_console_fds.
     *
     * A NULL fd 0 is also treated as the keyboard: that is the
     * pre-sentinel behavior, and it is what a process that closed
     * fd 0 without reopening anything sees.
     */
    file_slot_t* fd0_slot = get_file_slot_any(0);

    if (fd == 0 &&
        (!fd0_slot || fd0_slot->kind == FILE_KIND_CONSOLE)) {
        char c; size_t bytes_read = 0; uint8_t* dest_ptr = (uint8_t*)buf;
        while (bytes_read < count) {
            __asm__ volatile("cli");
            if (kbd_buffer_get(&c)) {
                __asm__ volatile("sti");
                if (safe_copy_to_user(dest_ptr + bytes_read, &c, 1) == 0) bytes_read++;
                else return -(long)EFAULT_;
                /*
                 * Return as soon as at least one byte has been copied.
                 *
                 * POSIX read(2) on a terminal returns when at least
                 * one byte is available; it does not block until
                 * count bytes have been accumulated.  The previous
                 * behavior (loop until bytes_read == count) made
                 * musl's read(0, line, 255) wait for 255 keystrokes
                 * before returning, which is not how any Unix
                 * program expects stdin to behave.  The newlib
                 * shell never noticed because it reads 1 byte at a
                 * time (count == 1), so the loop exited on the
                 * first byte anyway.
                 *
                 * The byte is NOT echoed here.  Users of this path
                 * that want their input echoed (musl_sh, the kernel
                 * shell) do the echoing themselves.  busybox ash
                 * with FEATURE_EDITING=y also echoes itself via
                 * lineedit.c.  Echoing here as well would
                 * double-echo every keystroke for whichever of
                 * those programs was currently reading.
                 */
                break;
            }
            if (self->pid == 1) { __asm__ volatile("sti"); __asm__ volatile("hlt"); continue; }
            self->state = PROC_STATE_BLOCKED;
            self->block_kind = BLOCK_KIND_NONE;
            __asm__ volatile("sti"); __asm__ volatile("hlt");
        }
        return (long)bytes_read;
    }

    /*
     * Pipe read end.  Step 2: if the pipe is empty, block until a
     * writer puts bytes in, then retry.  Step 3 will distinguish
     * "empty and writer still open" (block) from "empty and writer
     * closed" (return 0, EOF); Step 2 blocks in both cases.
     *
     * The blocking discipline mirrors sys_write's pipe branch
     * above, and sys_read's own fd-0 path: cli, check, record
     * self, mark BLOCKED, sti+hlt, loop.  See the sys_write pipe
     * branch for the full comment on the missed-wakeup window.
     *
     * We do NOT assume the wake means data is available; we loop
     * and re-check, because the wake can be redundant.
     */
    file_slot_t* pslot = get_file_slot_any(fd);
    if (pslot && pslot->kind == FILE_KIND_PIPE) {
        pipe_t* pipe = (pipe_t*)pslot->obj;
        pcb_t* self = process_get_current();

        for (;;) {
            __asm__ volatile("cli");
            if (pipe->used > 0) {
                __asm__ volatile("sti");
                break;
            }

            /*
             * Step 3: empty AND no writer left anywhere == EOF.
             * This is the only place read() returns 0 for a pipe.
             * A reader with a writer still open falls through to
             * block, exactly as Step 2 did.
             *
             * The check is on writers_open (a count), not on any
             * slot flag, so a dup2'd write end keeps the reader
             * blocking until EVERY write-end fd is closed.
             */
            if (pipe->writers_open == 0) {
                __asm__ volatile("sti");
                return 0;
            }

            if (!self) {
                __asm__ volatile("sti");
                return -(long)EAGAIN_;
            }

            pipe->reader_waiting = self;
            self->state      = PROC_STATE_BLOCKED;
            self->block_kind = BLOCK_KIND_PIPE_READ;

            __asm__ volatile("sti");
            __asm__ volatile("hlt");
        }

        /* Copy what is available, then wake a waiting writer. */
        size_t to_read = count;
        if (to_read > pipe->used) to_read = pipe->used;

        size_t done = 0;
        while (done < to_read) {
            size_t span = to_read - done;
            size_t to_end = pipe->capacity - pipe->read_pos;
            if (span > to_end) span = to_end;

            if (safe_copy_to_user((uint8_t*)buf + done,
                                  pipe->buf + pipe->read_pos,
                                  span) != 0) {
                pipe->used -= (uint32_t)done;
                if (pipe->writer_waiting) {
                    pipe_wake_waiter(pipe->writer_waiting,
                                     BLOCK_KIND_PIPE_WRITE);
                    pipe->writer_waiting = NULL;
                }
                return (done > 0) ? (long)done : -(long)EFAULT_;
            }
            pipe->read_pos = (pipe->read_pos + (uint32_t)span)
                             % pipe->capacity;
            done += span;
        }
        pipe->used -= (uint32_t)done;

        /* Space freed: wake a waiting writer, if any. */
        if (pipe->writer_waiting) {
            pipe_wake_waiter(pipe->writer_waiting, BLOCK_KIND_PIPE_WRITE);
            pipe->writer_waiting = NULL;
        }
        return (long)done;
    }

    file_slot_t* slot = get_file_slot_any(fd);
    if (slot && slot->kind == FILE_KIND_FILE) {
        FIL* file_obj = (FIL*)slot->obj;
        char* bounce = (char*)kmalloc(512);
        if (!bounce) return -(long)ENOMEM_;

        size_t total_read = 0;
        while (total_read < count) {
            size_t chunk = (count - total_read) > 512 ? 512 : (count - total_read);
            UINT read_bytes;
            if (f_read(file_obj, bounce, chunk, &read_bytes) != FR_OK) {
                kfree(bounce); return -(long)EIO_;
            }
            if (read_bytes == 0) break;
            if (safe_copy_to_user((uint8_t*)buf + total_read, bounce, read_bytes) != 0) {
                kfree(bounce); return -(long)EFAULT_;
            }
            total_read += read_bytes;
            if (read_bytes < chunk) break;
        }
        kfree(bounce);
        return (long)total_read;
    }

    return -(long)EBADF_;
}

/*
 * Linux x86_64 brk(2) — absolute-address ABI.
 *
 * ABI notes:
 *   - brk(0)            returns the current break, unchanged.
 *   - brk(addr)         sets the break to `addr`.
 *   - Grows or shrinks; if it cannot move the break, returns the
 *     OLD (unchanged) break.  Never returns -1.  Callers detect
 *     failure by comparing the return value to the requested address.
 *   - Refuses to move the break below heap_base.
 *
 * The shrink path is a no-op (returns old_brk without touching
 * brk_virt).  Nothing in musl or busybox needs shrinking yet; add it
 * when a specific test requires it.
 */
void* sys_brk(void* addr) {
    pcb_t* current = process_get_current();
    if (!current) return (void*)-1;

    static uint64_t heap_base = 0;
    if (heap_base == 0) heap_base = 0x8000200000ULL;

    if (current->brk_virt == 0) {
        current->brk_virt = heap_base;
    }
    uint64_t old_brk = current->brk_virt;

    /* brk(0): query only, no change. */
    if (addr == (void*)0) {
        return (void*)old_brk;
    }

    uint64_t new_brk = (uint64_t)addr;

    /* Refuse to move the break below the heap base. */
    if (new_brk < heap_base) {
        return (void*)old_brk;
    }

    if (new_brk > old_brk) {
        uint64_t old_page = (old_brk + 0xFFF) & ~0xFFFULL;
        uint64_t new_page = (new_brk + 0xFFF) & ~0xFFFULL;

        for (uint64_t virt = old_page; virt < new_page; virt += 4096) {
            uint64_t phys = pmm_alloc_page_for_elf();
            if (!phys) {
                /*
                 * Partial growth.  We have mapped [old_page, virt) but
                 * cannot finish.  Return the OLD break: the Linux ABI
                 * says the caller sees the unchanged break and treats
                 * that as failure.  The partially-mapped pages stay
                 * mapped and are still tracked in elf_page_list; they
                 * will be reclaimed when the process exits.
                 */
                return (void*)old_brk;
            }

            void* hhdm = (void*)(HHDM_START + phys);
            for (uint64_t j = 0; j < 4096 / 8; j++) ((uint64_t*)hhdm)[j] = 0ULL;

            uint64_t map_flags = PT_PRESENT | PT_WRITE | PT_USER;
            vmm_map_page_in_cr3(current->cr3, virt, phys, map_flags);
            elf_add_page_to_pcb(current, phys);
        }
    } else if (new_brk < old_brk) {
        /* Shrink: not implemented.  Leave the break where it is. */
        return (void*)old_brk;
    }

    current->brk_virt = new_brk;
    return (void*)new_brk;
}

long sys_getpid(void) {
    pcb_t* current = process_get_current();
    if (!current) return 1;
    return (long)current->pid;
}

/*
 * Linux x86_64 getppid(2) — syscall 110.
 *
 * Returns the calling process's parent pid.  pid 0 in pcb_t means
 * "no parent" (idle, kernel shell, user shell); Linux returns 1
 * for the ultimate ancestor, so map 0 to 1 here.  busybox ash
 * calls this to populate $PPID; the exact value does not matter
 * for correctness, but it must be a plausible pid.
 */
long sys_getppid(void) {
    pcb_t* current = process_get_current();
    if (!current) return 1;
    if (current->parent_pid == 0) return 1;
    return (long)current->parent_pid;
}

/*
 * Linux x86_64 prctl(2) — syscall 157.
 *
 * Multiplexer.  busybox calls it once per invocation with
 * PR_SET_NAME (15) to set the process's name (comm).  We have
 * nowhere separate to store it: pcb->name is the exec name, used
 * by process_dump_all and the sys_execve trace line, and
 * overwriting it with busybox's comm value ("busybox", or the
 * applet name) would make our own diagnostics less useful, not
 * more.
 *
 * So PR_SET_NAME is ACCEPTED AND DROPPED: return 0, do not store.
 * That satisfies busybox, which ignores the return value.  All
 * other options return -EINVAL, matching Linux's behavior for an
 * unknown or unsupported option.
 *
 * REVISIT: this is deliberately minimal.  A Unix-shaped
 * implementation would give pcb_t a separate `comm` field (Linux
 * keeps comm and the exec path distinct), set it here, and show
 * it in process_dump_all alongside the exec name.  That is
 * deferred -- see docs/open-issues.md.  The point of this
 * function today is to stop busybox's per-invocation
 * "Unknown syscall: 157" noise, not to model prctl.
 *
 * PR_SET_NAME = 15.  EINVAL_ = 22.
 */
long sys_prctl(int option, unsigned long arg2, unsigned long arg3,
               unsigned long arg4, unsigned long arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    if (option == 15) {   /* PR_SET_NAME: accept and drop */
        return 0;
    }
    return -(long)EINVAL_;
}

/*
 * Linux x86_64 setsid(2) — syscall 112.
 *
 * Creates a new session.  donix has no notion of sessions or
 * process groups; the pragmatic implementation is a no-op that
 * returns the calling process's pid (the new session id in
 * Linux's model).  busybox ash calls this at startup to detach
 * from the controlling terminal and does not depend on any
 * session semantics beyond a successful return.
 *
 * Linux would return -EPERM if the caller is already a process
 * group leader; that case does not arise here.
 *
 * HISTORY: this function was originally registered at syscall 107
 * (tag 20260927-07), because busybox ash's startup logged
 * "Unknown syscall: 107" and 107 was assumed to be setsid.  It is
 * not: on Linux x86_64, 107 is geteuid(2) and setsid is 112.  The
 * handler at 107 was therefore reached by ash's geteuid() call,
 * which received the caller's pid where it expected a uid.  The
 * real setsid(2) call from musl (112) went unhandled.  The number
 * is now 112, matching the Linux ABI.  The 107 gap was closed in
 * session 30, when sys_geteuid was implemented (see below).
 */
long sys_setsid(void) {
    pcb_t* current = process_get_current();
    if (!current) return -(long)EPERM_;
    return (long)current->pid;
}

/*
 * Linux x86_64 geteuid(2) — syscall 107.
 *
 * donix has no users; return a fixed uid.  1000 matches the
 * typical Fedora user and is what musl and busybox expect to see
 * as a plausible non-root uid.  Nothing on donix checks the
 * value; it exists only to stop the once-per-ash-startup
 * "Unknown syscall: 107" diagnostic.
 *
 * HISTORY: session 24 mistakenly implemented setsid at 107,
 * which meant ash's geteuid() call received the caller's pid
 * where it expected a uid.  Session 27 (tag 20260928-05) moved
 * setsid to 112 and exposed the real 107 gap.  This is the
 * closure of that gap.
 */
long sys_geteuid(void) {
    return 1000;
}

/*
 * Linux x86_64 uname(2) — syscall 63.
 *
 * Fills a struct utsname with fixed fields.  Every field is
 * _UTSNAME_LENGTH (65) bytes, NUL-terminated, in this order:
 *
 *   sysname    "Linux"       -- what busybox checks to pick the
 *                               Linux code path over BSD or other
 *   nodename   "donix"       -- hostname; anything reasonable
 *   release    "6.0.0"       -- kernel version; digits and dots
 *                               only, so version parsers do not
 *                               choke
 *   version    "#1 donix"    -- free-form build string
 *   machine    "x86_64"      -- architecture; checkers branch on
 *                               this to pick word size
 *   domainname "(none)"      -- NIS domain; everyone prints this
 *
 * The exact values are less important than the shape: sysname
 * must be "Linux" so glibc/musl/busybox pick their Linux
 * behavior, and machine must be "x86_64" so anything doing
 * architecture-specific work gets the right answer.  The
 * remaining fields are informational.
 *
 * Without this syscall, `busybox uname` prints an error.  It is
 * NOT required for shell script execution -- the session-33
 * script fixes are independent of this.
 */
struct donix_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static void set_utsname_field(char* dst, const char* src) {
    int i = 0;
    while (src[i] && i < 64) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

long sys_uname(void* user_buf) {
    if (!user_buf) return -(long)EFAULT_;

    struct donix_utsname u;
    /* Zero every byte so the fields beyond the NUL we write are
     * deterministic.  Some callers memcmp the whole struct. */
    for (size_t i = 0; i < sizeof(u); i++) ((uint8_t*)&u)[i] = 0;

    set_utsname_field(u.sysname,    "Linux");
    set_utsname_field(u.nodename,   "donix");
    set_utsname_field(u.release,    "6.0.0");
    set_utsname_field(u.version,    "#1 donix");
    set_utsname_field(u.machine,    "x86_64");
    set_utsname_field(u.domainname, "(none)");

    if (safe_copy_to_user(user_buf, &u, sizeof(u)) != 0) {
        return -(long)EFAULT_;
    }
    return 0;
}

/*
 * Linux x86_64 getcwd(2) — syscall 79.
 *
 * Return the current working directory: the path stored by
 * sys_chdir (pcb->cwd), or "/" for a process that has never
 * called chdir (cwd[0] == '\0', thanks to the zero-init in
 * process_initialize_pcb).
 *
 * Linux ABI: getcwd(buf, size) copies the NUL-terminated path into
 * buf and returns buf (a pointer, which for our int64 return
 * convention is the buf address).  If the path does not fit in
 * size bytes, return -ERANGE.  If buf is NULL, Linux returns a
 * freshly malloc'd buffer for the GNU extension; donix does not
 * support that, so -EINVAL.
 *
 * 34 is ERANGE on Linux x86_64.  22 is EINVAL.
 */
long sys_getcwd(char* buf, unsigned long size) {
    if (!buf) return -(long)EINVAL_;

    pcb_t* self = process_get_current();

    const char* path = "/";
    if (self && self->cwd[0] != '\0') {
        path = self->cwd;
    }

    size_t len = 0;
    while (path[len]) len++;
#if DEBUG_GETCWD
    serial_print("sys_getcwd: buf=0x");
    serial_print_hex((uint64_t)buf);
    serial_print(" size=");
    serial_print_dec((uint64_t)size);
    serial_print(" cwd='");
    if (self) serial_print(self->cwd); else serial_print("(no self)");
    serial_print("' path='");
    serial_print(path);
    serial_print("' len=");
    serial_print_dec((uint64_t)len);
    serial_print(" ret=0x");
    serial_print_hex((uint64_t)buf);
    serial_print("\n");
#endif
    if (size < len + 1) return -(long)ERANGE_;

    if (safe_copy_to_user(buf, path, len + 1) != 0) {
        return -(long)EFAULT_;
    }
    return (long)(uint64_t)buf;
}

/*
 * Linux x86_64 chdir(2) — syscall 80.
 *
 * Change the calling process's current working directory.
 *
 * This stores an absolute path in pcb->cwd after validating that it
 * exists and is a directory, and sys_getcwd returns the stored value.
 * The relative-path story is split across two layers:
 *
 *   - This function only *stores* the cwd.  It does not itself
 *     rewrite the path relative to anything; the caller's path is
 *     normalized to absolute Unix form and saved.
 *
 *   - The path-taking syscalls that must honor the cwd call
 *     resolve_against_cwd BEFORE strip_dot_prefix: sys_open,
 *     sys_stat, and sys_access all do.  That is what makes
 *     `cd /bin; ls busybox` look at `0:/BIN/BUSYBOX` instead of
 *     `0:/BUSYBOX`.
 *
 *   - sys_unlink and sys_mkdir do NOT call resolve_against_cwd yet;
 *     they only strip the leading "./" or "/".  So a remove or mkdir
 *     in a non-root cwd resolves against the FAT root instead of the
 *     cwd.  This is a known gap, tracked in docs/open-issues.md, and
 *     is part of the v0.6.4 basics work.
 *
 * Validation uses f_stat_with_retry, so bare names and leading-slash
 * paths resolve the same way sys_stat resolves them.  The path must
 * resolve to a directory (AM_DIR); a file path returns -ENOTDIR.  If
 * validation fails, cwd is NOT changed.
 *
 * The stored form is unsimplified, matching Linux: `cd /bin/../bin`
 * keeps that exact form, and `pwd` prints it back verbatim.  Root
 * aliases (".", "/", "0:/") are normalized to "/" so getcwd reports
 * a consistent form.
 *
 * 20 is ENOTDIR on Linux x86_64.
 */
#ifndef ENOTDIR_
#define ENOTDIR_ 20
#endif

long sys_chdir(const char* user_path) {
    pcb_t* self = process_get_current();
    if (!self || !user_path) return -(long)EFAULT_;

    char path[USER_PATH_MAX];
    if (copy_user_string(path, sizeof(path), user_path) != 0) {
        return -(long)EFAULT_;
    }

    /*
     * Resolve the path against the current cwd before anything else.
     *
     * This is what makes `cd ..` and `cd .` work from the donix
     * shell.  musl_sh's cd builtin passes the raw argument straight
     * to chdir -- unlike ash, which resolves `..` against its own
     * $PWD first -- so without this step the kernel would hand ".."
     * to FatFs, which has no `..` directory entry, and chdir would
     * fail with ENOENT.
     *
     * resolve_against_cwd produces an ABSOLUTE Unix path:
     *
     *     "."      -> cwd
     *     ".."     -> parent of cwd
     *     "../x"   -> parent of cwd + "/x"
     *     "./x"    -> cwd + "/x"
     *     "x"      -> cwd + "/x"
     *     "/x"     -> "/x"          (already absolute, unchanged)
     *     "0:/x"   -> "0:/x"        (FatFs form, unchanged)
     *
     * The result is absolute, so it can be stored as the new cwd
     * directly -- no separate "Unix form" step is needed.  It may
     * still carry a leading '/' or "./" that FatFs rejects, so a
     * copy is stripped for validation.
     */
    char resolved[USER_PATH_MAX];
    if (resolve_against_cwd(self, path, resolved,
                            sizeof(resolved)) != 0) {
        return -(long)ENAMETOOLONG_;
    }

    /*
     * FAT form: strip the leading '/' and "./" components that
     * FatFs rejects.  Applied to a copy so the resolved absolute
     * form survives for the cwd store below.
     */
    char fat_path[USER_PATH_MAX];
    {
        size_t i = 0;
        while (resolved[i] && i < sizeof(fat_path) - 1) {
            fat_path[i] = resolved[i];
            i++;
        }
        fat_path[i] = '\0';
    }
    strip_dot_prefix(fat_path);

    /* Validate: the path must exist and be a directory. */
    FILINFO fno;
    if (!path_is_root(fat_path)) {
        FRESULT r = f_stat_with_retry(fat_path, &fno);
        if (r != FR_OK) {
            return fatfs_errno(r);
        }
        if (!(fno.fattrib & AM_DIR)) {
            return -(long)ENOTDIR_;
        }
    }

    /*
     * Store the cwd in ABSOLUTE Unix form.
     *
     * Root aliases (".", "/", "0:/") normalize to "/".  Everything
     * else comes from `resolved`, which is already absolute: a
     * relative input has been resolved against the old cwd by
     * resolve_against_cwd above, so there is no need to prefix '/'.
     *
     * musl's getcwd() validates that the result starts with '/',
     * and rejects a relative cwd; resolve_against_cwd guarantees
     * that leading '/', which is why the store can copy it directly.
     */
    if (path_is_root(fat_path)) {
        self->cwd[0] = '/';
        self->cwd[1] = '\0';
    } else {
        size_t o = 0;
        for (size_t i = 0; resolved[i] && o < sizeof(self->cwd) - 1; i++) {
            self->cwd[o++] = resolved[i];
        }
        self->cwd[o] = '\0';
    }

#if DEBUG_CHDIR
    {
        size_t l = 0;
        while (self->cwd[l]) l++;
        serial_print("sys_chdir: stored cwd='");
        serial_print(self->cwd);
        serial_print("' len=");
        serial_print_dec((uint64_t)l);
        serial_print("\n");
    }
#endif

    return 0;
}

/*
 * Linux x86_64 poll(2) — syscall 7.
 *
 * busybox ash's line editor (FEATURE_EDITING=y) calls poll() once
 * per readline iteration to wait for stdin readability.  Before
 * this handler existed, every keystroke logged "Unknown syscall:
 * 7" -- 7 is poll on Linux x86_64, not mkdir (see
 * docs/gotchas.md, session 27).
 *
 * Semantics implemented here:
 *   - fd 0 with timeout < 0 (block forever): block the process
 *     until a byte is buffered, then report POLLIN.
 *   - fd 0 with timeout >= 0: report POLLIN if a byte is
 *     buffered, else report 0 (timeout).  Does not actually
 *     sleep for `timeout` milliseconds; see "Timeout handling"
 *     below.
 *   - other fds: revents = POLLNVAL.  We cannot wait on files,
 *     directories, or pipes, and reporting POLLIN there would be
 *     a lie the reader could not back with a non-blocking read.
 *   - events (the caller's interest mask) is ignored.  ash asks
 *     for POLLIN; a caller asking for POLLOUT on fd 0 would
 *     still get POLLIN when data is available, which is
 *     over-eager but harmless -- the caller then reads.
 *
 * Returns the number of fds with nonzero revents, or -errno.
 *
 * WHY THIS BLOCKS.  The first version of this handler returned 0
 * unconditionally when nothing was ready.  That was wrong for
 * the timeout == -1 case, and the failure was immediate and
 * total: ash's line editor interprets a 0 return from
 * poll(fds, 1, -1) as end-of-input and exits the shell.  On real
 * Linux that combination is unreachable -- a poll with an
 * infinite timeout never returns 0 -- so ash has no code path
 * for it.  A handler that returns 0 there is technically within
 * the letter of the poll(2) contract ("return 0 on timeout") but
 * not within its spirit, and ash falls off a cliff.  The correct
 * implementation blocks.  A poll with timeout == -1 on a
 * readable-event fd MUST NOT return until either the fd is
 * readable or a signal interrupts it.
 *
 * TIMEOUT HANDLING.  For timeout >= 0 this handler still does
 * not actually wait.  A real implementation would arm a
 * deadline (g_ticks is available; PIT frequency is 500 Hz so
 * 1 tick == 2 ms) and loop on hlt until the deadline or
 * readability.  That is more machinery than ash needs -- ash
 * passes timeout == -1 -- and adding it now would mean writing
 * and testing a timeout path no current caller exercises.  The
 * non-blocking behavior for timeout >= 0 matches Linux in the
 * "data ready" and "would-block past deadline" cases; it
 * differs only in returning early rather than sleeping.  If a
 * future caller relies on a real timeout, add it then, with a
 * test that exercises it.
 *
 * INTERRUPT DISCIPLINE.  The blocking loop below mirrors
 * sys_read's fd-0 path exactly:
 *
 *     cli
 *     if (data) { sti; consume; break; }
 *     mark BLOCKED
 *     sti; hlt
 *
 * The cli is what makes the check and the state transition
 * atomic with respect to irq1_handler.  Without it there is a
 * missed-wakeup window: has_data() returns 0, irq1 fires and
 * puts a byte and calls process_wake_all_blocked (which sees
 * our state is still RUNNING and does nothing), and then we set
 * state = BLOCKED and hlt -- and nobody will wake us, because
 * the wake already happened.  sys_read got this right; the
 * blocking poll must too.
 *
 * The pid == 1 case is special: process 1 (the kernel shell,
 * or whatever is running on the idle/kernel stack) must not
 * BLOCK, because nothing would schedule it back in -- it is
 * not on the ready queue in the usual way.  It does a bare
 * sti; hlt; loop instead, which is what sys_read does.  In
 * practice busybox ash is pid > 1, so this branch is for
 * safety, not for the current code path.
 *
 * Linux x86_64 struct pollfd (musl's <poll.h> and the kernel's
 * uapi/asm-generic/poll.h agree):
 *
 *     offset 0: int   fd
 *     offset 4: short events
 *     offset 6: short revents
 *
 * 8 bytes total, no padding.  Verified against
 * third_party/musl-install/include/poll.h.  Do not change this
 * layout without re-checking that header: busybox compares
 * revents against the POLL* constants its own musl compiled in,
 * and a mismatch here would make every revents test silently
 * false.
 */
#define POLLIN_   0x001
#define POLLNVAL_ 0x020

/* 4 is EINTR on Linux x86_64.  Not currently defined elsewhere
 * in this file; keep it local to this function's section. */
#ifndef EINTR_
#define EINTR_ 4
#endif

typedef struct {
    int   fd;
    short events;
    short revents;
} kernel_pollfd_t;

/* Set POLL_TRACE to 1 for one build to log what ash actually
 * passes, then back to 0.  The trace from the first run
 * confirmed nfds == 1, timeout == (unsigned)-1, fd == 0,
 * events == POLLIN.  It is now off by default. */
#define POLL_TRACE 0

/* Maximum number of pollfds accepted in one call.  ash passes 1.
 * The bound exists so a bad user pointer cannot make us loop
 * indefinitely; sixteen is generous and still bounded. */
#define POLL_MAX_NFDS 16

long sys_poll(void* user_fds_arg, unsigned long nfds, int timeout) {
    kernel_pollfd_t* user_fds = (kernel_pollfd_t*)user_fds_arg;

#if POLL_TRACE
    serial_print("poll: a0=0x"); serial_print_hex((uint64_t)user_fds);
    serial_print(" nfds=");      serial_print_dec((uint64_t)nfds);
    serial_print(" timeout=");   serial_print_dec((uint64_t)(int64_t)timeout);
#endif

    if (nfds == 0) {
#if POLL_TRACE
        serial_print("\n");
#endif
        return 0;
    }
    if (!user_fds) {
#if POLL_TRACE
        serial_print(" -> EFAULT (null fds)\n");
#endif
        return -(long)EFAULT_;
    }
    if (nfds > POLL_MAX_NFDS) {
#if POLL_TRACE
        serial_print(" -> EINVAL (nfds too large)\n");
#endif
        return -(long)EINVAL_;
    }

    long ready = 0;
    for (unsigned long i = 0; i < nfds; i++) {
        kernel_pollfd_t pfd;
        if (safe_copy_from_user(&pfd, user_fds + i, sizeof(pfd)) != 0) {
#if POLL_TRACE
            serial_print(" -> EFAULT (copy_in)\n");
#endif
            return -(long)EFAULT_;
        }

#if POLL_TRACE
        if (i == 0) {
            serial_print(" fd0=");  serial_print_dec((uint64_t)(int64_t)pfd.fd);
            serial_print(" ev0=0x"); serial_print_hex((uint64_t)(uint16_t)pfd.events);
        }
#endif

        short revents = 0;

        if (pfd.fd == 0) {
            /*
             * Blocking path: timeout < 0 means "wait forever",
             * and on Linux that is a real wait.  Loop on hlt
             * until kbd_buffer_has_data() is true.  The cli
             * around the test is what closes the missed-wakeup
             * window (see the header comment).
             *
             * If the caller passed timeout >= 0 we skip this
             * block entirely and fall through to the
             * non-blocking check below: report POLLIN if a
             * byte is buffered, else report 0.  See "Timeout
             * handling" in the header comment for why we do
             * not actually sleep for the timeout duration.
             */
            if (timeout < 0) {
                pcb_t* self = process_get_current();
                for (;;) {
                    __asm__ volatile("cli");
                    if (kbd_buffer_has_data()) {
                        __asm__ volatile("sti");
                        break;
                    }
                    if (!self) {
                        /* No current process -- cannot block.
                         * Drop through; the non-blocking check
                         * below reports 0.  This is a defensive
                         * path and should not be reached. */
                        __asm__ volatile("sti");
                        break;
                    }
                    if (self->pid == 1) {
                        /* Kernel/idle shell: no one would
                         * schedule us back in, so do a bare
                         * hlt and retry, exactly as sys_read
                         * does on fd 0. */
                        __asm__ volatile("sti");
                        __asm__ volatile("hlt");
                        continue;
                    }
                    self->state = PROC_STATE_BLOCKED;
                    self->block_kind = BLOCK_KIND_NONE;
                    __asm__ volatile("sti");
                    __asm__ volatile("hlt");
                    /* Woken by irq1_handler ->
                     * process_wake_all_blocked, which sets
                     * state = READY and re-adds us to the
                     * ready queue.  Loop and re-check. */
                }
            }

            if (kbd_buffer_has_data()) {
                revents |= POLLIN_;
                ready++;
            }
        } else {
            revents = POLLNVAL_;
            ready++;
        }

        pfd.revents = revents;
        if (safe_copy_to_user(user_fds + i, &pfd, sizeof(pfd)) != 0) {
#if POLL_TRACE
            serial_print(" -> EFAULT (copy_out)\n");
#endif
            return -(long)EFAULT_;
        }
    }

#if POLL_TRACE
    serial_print(" -> ready=");
    serial_print_dec((uint64_t)ready);
    serial_print("\n");
#endif

    return ready;
}

/*
 * Linux x86_64 set_tid_address(2).
 *
 * musl's __libc_start_main calls this during init and uses the
 * return value as the caller's TID (in musl's single-threaded
 * model, that's the same as the pid).  The kernel's job is just
 * to return a stable, positive number.
 *
 * The tidptr argument is the address the kernel is supposed to
 * clear when the thread exits (clear_child_tid).  We don't track
 * that yet — musl only uses the return value in __libc_start_main,
 * and busybox doesn't rely on clear_child_tid for correctness in
 * our single-threaded model.  Add the pointer tracking later if a
 * specific test requires it.
 */
long sys_set_tid_address(int* tidptr) {
    (void)tidptr;
    pcb_t* current = process_get_current();
    if (!current) return 1;
    return (long)current->pid;
}

/*
 * Linux x86_64 ioctl(2) — syscall 16.
 *
 * Previously returned -ENOTTY for every request.  That is fine for
 * musl's __stdout_write probe (stdout is not a tty; buffered stdio
 * is what donix wants), but it is fatal for busybox ash: ash treats
 * a failing ioctl(0, TCGETS, ...) as "stdin is not a tty" and, in
 * that mode, neither prints a prompt nor echoes typed input.  The
 * shell still runs commands — it is usable blind — but it is not
 * interactive.
 *
 * This implementation returns a plausible struct termios for
 * TCGETS on fd 0 (stdin), 1 (stdout), and 2 (stderr), and accepts
 * TCSETS / TCSETSW / TCSETSF as no-ops.  It also answers
 * TIOCGWINSZ with a 24x80 window, because busybox ash probes the
 * window size -- not TCGETS -- to decide whether stdin is an
 * interactive terminal.  Everything else still returns -ENOTTY.
 *
 * Echo of typed input is NOT done by the kernel.  busybox ash,
 * built with FEATURE_EDITING=y, does its own echo and line
 * editing via lineedit.c; musl_sh and the kernel shell echo typed
 * input themselves.  The kernel hands the raw bytes to the reader
 * and lets the reader decide what to display.
 *
 * The struct termios layout is musl's, from bits/termios.h on
 * x86_64.  49 bytes, no trailing padding:
 *
 *     offset  size  field
 *       0      4    c_iflag   (tcflag_t = unsigned int)
 *       4      4    c_oflag
 *       8      4    c_cflag
 *      12      4    c_lflag
 *      16      1    c_line
 *      17     32    c_cc[32]
 *      49          total
 *
 * The flags we report (ICANON | ECHO | ISIG | IEXTEN, etc.) are
 * the standard cooked-mode set; ash only reads the flags, not the
 * control characters, but c_cc[VEOF]/[VERASE]/[VINTR] are filled
 * in anyway so a future caller that does read them gets sane
 * values.
 *
 * Linux x86_64 ioctl request codes:
 *     TCGETS     = 0x5401
 *     TCSETS     = 0x5402
 *     TCSETSW    = 0x5403
 *     TCSETSF    = 0x5404
 *     TIOCGWINSZ = 0x5413
 *     TIOCSWINSZ = 0x5414
 */

#define ENOTTY 25

/* ioctl request codes (Linux x86_64). */
#define TCGETS_  0x5401
#define TCSETS_  0x5402
#define TCSETSW_ 0x5403
#define TCSETSF_ 0x5404
#define TIOCGWINSZ_ 0x5413
#define TIOCSWINSZ_ 0x5414

/* termios flag bits (musl bits/termios.h, x86_64). */
#define T_ICRNL  0x0100   /* c_iflag: map CR to NL on input       */
#define T_OPOST  0x0001   /* c_oflag: enable output processing    */
#define T_ONLCR  0x0004   /* c_oflag: map NL to CR-NL on output   */
#define T_CS8    0x0030   /* c_cflag: 8 bits per byte             */
#define T_CREAD  0x0080   /* c_cflag: enable receiver             */
#define T_CLOCAL 0x0800   /* c_cflag: ignore modem control lines  */
#define T_B38400 0x000F   /* c_cflag: baud rate B38400            */
#define T_ISIG   0x0001   /* c_lflag: enable signals (INTR, etc.) */
#define T_ICANON 0x0002   /* c_lflag: canonical (line) mode       */
#define T_ECHO   0x0008   /* c_lflag: echo input characters       */
#define T_ECHOE  0x0010   /* c_lflag: echo erase as BS-SP-BS      */
#define T_ECHOK  0x0020   /* c_lflag: echo NL after kill char     */
#define T_IEXTEN 0x8000   /* c_lflag: enable implementation-defined input */

/* Control-character indices (musl bits/termios.h). */
#define T_VEOF   0
#define T_VINTR  3
#define T_VERASE 0x7F

/*
 * A plausible cooked-mode termios.  Field-by-field initialization is
 * used instead of a struct literal so the layout is explicit at the
 * point of definition; the wire format is what matters, not the C
 * type, since we memcpy the bytes into user space.
 *
 * Total size is 49 bytes (4+4+4+4+1+32), matching musl's struct on
 * x86_64.  No compiler padding is inserted because every field
 * except c_line and c_cc is naturally aligned, and c_cc is a byte
 * array.
 */
typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[32];
} kernel_termios_t;

static void fill_kernel_termios(kernel_termios_t* tio) {
    for (size_t i = 0; i < sizeof(*tio); i++) ((uint8_t*)tio)[i] = 0;

    tio->c_iflag = T_ICRNL;
    tio->c_oflag = T_OPOST | T_ONLCR;
    tio->c_cflag = T_B38400 | T_CS8 | T_CREAD | T_CLOCAL;
    tio->c_lflag = T_ISIG | T_ICANON | T_ECHO | T_ECHOE | T_ECHOK | T_IEXTEN;
    tio->c_line  = 0;

    tio->c_cc[T_VEOF]   = 0x04;   /* ^D */
    tio->c_cc[T_VINTR]  = 0x03;   /* ^C */
    tio->c_cc[T_VERASE] = 0x7F;   /* DEL */
}

/*
 * A plausible window size.  busybox ash probes this (not TCGETS)
 * to decide whether stdin is an interactive terminal: it calls
 * ioctl(0, TIOCGWINSZ, &ws) and, if that succeeds with non-zero
 * rows and columns, prints a prompt and echoes typed input.  If
 * it fails, ash runs in non-interactive mode -- it still reads
 * and executes commands, but silently, with no prompt and no
 * echo.
 *
 * The struct is Linux x86_64 struct winsize: four unsigned
 * shorts, 8 bytes total, no padding.  24x80 is the classic
 * VT100 default; the pixel fields are zero, which is what Linux
 * returns for a text console and what ash expects.
 */
typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} kernel_winsize_t;

static void fill_kernel_winsize(kernel_winsize_t* ws) {
    ws->ws_row    = 24;
    ws->ws_col    = 80;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
}

long sys_ioctl(int fd, unsigned long request, void* argp) {
    switch (request) {
        case TCGETS_: {
            /*
             * Only fds 0/1/2 are terminals; anything else is a file
             * or directory and gets -ENOTTY, which is the truth.
             * argp must be a valid user pointer of at least
             * sizeof(kernel_termios_t) bytes.
             */
            if (fd != 0 && fd != 1 && fd != 2) {
                return -(long)ENOTTY;
            }
            if (!argp) {
                return -(long)EFAULT_;
            }
            kernel_termios_t tio;
            fill_kernel_termios(&tio);
            if (safe_copy_to_user(argp, &tio, sizeof(tio)) != 0) {
                return -(long)EFAULT_;
            }
            return 0;
        }
        case TIOCGWINSZ_: {
            /*
             * fd 0 is what ash probes, but answering on 0/1/2 is
             * harmless and matches the TCGETS handling above.
             */
            if (fd != 0 && fd != 1 && fd != 2) {
                return -(long)ENOTTY;
            }
            if (!argp) {
                return -(long)EFAULT_;
            }
            kernel_winsize_t ws;
            fill_kernel_winsize(&ws);
            if (safe_copy_to_user(argp, &ws, sizeof(ws)) != 0) {
                return -(long)EFAULT_;
            }
            return 0;
        }
        case TCSETS_:
        case TCSETSW_:
        case TCSETSF_:
        case TIOCSWINSZ_:
            /*
             * Accept and ignore.  donix's console has no settable
             * line discipline or window size; pretending the write
             * succeeded is what ash expects and costs nothing.
             */
            if (fd != 0 && fd != 1 && fd != 2) {
                return -(long)ENOTTY;
            }
            return 0;
        default:
            return -(long)ENOTTY;
    }
}

#define MMAP_BASE 0x8010000000ULL
#define MMAP_END  0x8010400000ULL   /* 4 MB window, per exec_free_and_unmap_user_pages */

/* Find `rounded` bytes of free VA space in the mmap window.
 * Returns 0 on failure. */
static uint64_t mmap_find_free_slot(pcb_t* self, uint64_t rounded) {
    if (rounded == 0 || rounded > (MMAP_END - MMAP_BASE)) return 0;

    for (uint64_t candidate = MMAP_BASE;
         candidate + rounded <= MMAP_END;
         candidate += 0x1000) {

        int all_free = 1;
        for (uint64_t off = 0; off < rounded; off += 0x1000) {
            if (vmm_get_phys_from_cr3(self->cr3, candidate + off) != 0) {
                all_free = 0;
                break;
            }
        }
        if (all_free) return candidate;
    }
    return 0;
}

/*
 * Linux x86_64 mmap(2) — minimal anonymous implementation.
 *
 * Handles:
 *   - MAP_PRIVATE | MAP_ANONYMOUS, addr = NULL  (musl malloc)
 *   - MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, addr = <page-aligned>
 *     (musl malloc's guard-page request after brk)
 *
 * Ignores: fd, offset, PROT_* bits (always maps PT_PRESENT|PT_WRITE|
 * PT_USER).  Returns -ENOMEM for anything else.
 *
 * MMAP_BASE is 0x8010000000.
 */
#define ENOMEM 12
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

long sys_mmap(void* addr, size_t length, int prot, int flags,
              int fd, long offset) {
    (void)prot;
    (void)offset;

    if (length == 0) return -(long)EINVAL_;

    if ((flags & MAP_ANONYMOUS) == 0 || fd != -1) {
        return -(long)ENOMEM_;
    }

    pcb_t* self = process_get_current();
    if (!self) return -(long)ENOMEM_;

    uint64_t rounded = ((uint64_t)length + 0xFFF) & ~0xFFFULL;

    uint64_t base;
    if (flags & MAP_FIXED) {
        base = (uint64_t)addr & ~0xFFFULL;
    } else {
        base = mmap_find_free_slot(self, rounded);
        if (base == 0) return -(long)ENOMEM_;
    }

    for (uint64_t v = base; v < base + rounded; v += 0x1000) {
        uint64_t phys = pmm_alloc_page_for_elf();
        if (!phys) {
            return -(long)ENOMEM_;
        }

        void* hhdm = (void*)(HHDM_START + phys);
        for (uint64_t j = 0; j < 4096 / 8; j++) ((uint64_t*)hhdm)[j] = 0ULL;

        uint64_t map_flags = PT_PRESENT | PT_WRITE | PT_USER;
        vmm_map_page_in_cr3(self->cr3, v, phys, map_flags);
        elf_add_page_to_pcb(self, phys);
    }

    return (long)base;
}

/*
 * Linux x86_64 munmap(2) — stub.  Return 0 (nothing to unmap yet).
 */
long sys_munmap(void* addr, size_t length) {
    (void)addr; (void)length;
    return 0;
}

/*
 * Linux x86_64 mprotect(2) — stub.
 *
 * musl calls this after mmap to set page permissions.  We don't
 * implement real permission changes yet: every page is mapped
 * PT_PRESENT|PT_WRITE|PT_USER by sys_mmap and sys_brk, and the
 * caller already has read/write access.  Returning 0 tells musl
 * the operation succeeded.
 *
 * If a later test requires real protection (e.g. a guard page that
 * actually faults on access), implement the page-table walk here
 * and update the PTE bits.
 */
long sys_mprotect(void* addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0;
}

/*
 * Linux x86_64 rt_sigaction / rt_sigprocmask — stubs.
 *
 * Return 0 (success).  musl's __libc_start_main installs a few
 * signal handlers during init; the kernel doesn't do anything with
 * them, but musl doesn't check the result in a way that matters.
 * If musl later reads back sigaction to inspect SA_RESTORER, this
 * will need a real table.
 */
long sys_rt_sigaction(int signum, const void* act, void* oldact,
                      size_t sigsetsize) {
    (void)signum; (void)act; (void)oldact; (void)sigsetsize;
    return 0;
}

long sys_rt_sigprocmask(int how, const void* set, void* oldset,
                        size_t sigsetsize) {
    (void)how; (void)set; (void)oldset; (void)sigsetsize;
    return 0;
}

/*
 * Linux x86_64 set_robust_list — stub.  Return 0.
 * musl calls this once at thread start; single-threaded programs
 * never actually have anything to put in the list.
 */
long sys_set_robust_list(void* head, size_t len) {
    (void)head; (void)len;
    return 0;
}

/*
 * Linux x86_64 getrandom / rseq — stubs.  Return -ENOSYS.
 * musl doesn't require either for a static single-threaded binary;
 * it falls back to /dev/urandom or to a fixed seed.  If a program
 * needs real randomness later, implement getrandom against the PIT
 * or RDRAND.
 */
#define ENOSYS_ 38
long sys_getrandom(void* buf, size_t buflen, unsigned int flags) {
    (void)buf; (void)buflen; (void)flags;
    return -(long)ENOSYS_;
}

long sys_rseq(void* rseq, uint32_t rseq_len, int flags, uint32_t sig) {
    (void)rseq; (void)rseq_len; (void)flags; (void)sig;
    return -(long)ENOSYS_;
}

/*
 * Linux x86_64 getdents64(2) — syscall 217.
 *
 * Emits exactly one struct linux_dirent64 record per call:
 *
 *     struct linux_dirent64 {
 *         uint64_t d_ino;       // inode number — 0 (FAT has none)
 *         int64_t  d_off;       // offset to next entry — 0 (unused)
 *         uint16_t d_reclen;    // record length, 8-byte aligned
 *         uint8_t  d_type;      // DT_REG / DT_DIR / DT_UNKNOWN
 *         char     d_name[];    // NUL-terminated, no padding
 *     };
 *
 * The header is 19 bytes.  d_name is included in d_reclen, and
 * d_reclen is rounded up to an 8-byte boundary as Linux does, so
 * a caller iterating records can do `dirp += d_reclen` and stay
 * aligned.
 *
 * One record per call keeps the implementation obviously correct:
 * FatFs's f_readdir() advances its cursor irreversibly, so an
 * attempt to pack multiple records into one call would consume
 * an entry it couldn't fit and lose it.  musl's readdir() passes
 * a buffer big enough for one record, so this is sufficient.
 *
 * Wraps the donix-private opendir/readdir/closedir (500/501/502)
 * machinery: the fd is a file_slot_t of kind FILE_KIND_DIR, and
 * f_readdir() supplies the entry.  Returns the record length on
 * success, 0 at end of directory, or a negative errno.
 */
#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4

#define GETDENTS64_HDR 19
#define GETDENTS64_MAXREC (GETDENTS64_HDR + 256)

long sys_getdents64(int fd, void* dirp, size_t count) {
    if (!dirp) return -(long)EFAULT_;
    if (count == 0) return -(long)EINVAL_;

    file_slot_t* slot = get_file_slot(fd, FILE_KIND_DIR);
    if (!slot) return -(long)EBADF_;

    FILINFO fno;
    FRESULT r = f_readdir((DIR*)slot->obj, &fno);
    if (r != FR_OK) return -(long)EIO_;
    if (fno.fname[0] == '\0') return 0; /* end of directory */

    size_t nlen = 0;
    while (nlen < 255 && fno.fname[nlen] != '\0') nlen++;
    size_t reclen = GETDENTS64_HDR + nlen + 1;
    reclen = (reclen + 7) & ~(size_t)7;

    if (reclen > count) {
        /*
         * Caller's buffer cannot hold one record.  The entry was
         * already consumed from FatFs; there is no seek-back.
         * musl always passes a buffer large enough, so this is
         * unreachable in practice.
         */
        return -(long)EINVAL_;
    }

    uint8_t entbuf[GETDENTS64_MAXREC];
    uint8_t* p = entbuf;
    *(uint64_t*)(p + 0)  = 0;                /* d_ino    */
    *(int64_t*)(p + 8)   = 0;                /* d_off    */
    *(uint16_t*)(p + 16) = (uint16_t)reclen; /* d_reclen */
    p[18] = (fno.fattrib & AM_DIR) ? DT_DIR : DT_REG;
    for (size_t i = 0; i < nlen; i++) p[GETDENTS64_HDR + i] = fno.fname[i];
    p[GETDENTS64_HDR + nlen] = '\0';
    for (size_t i = GETDENTS64_HDR + nlen + 1; i < reclen; i++) p[i] = 0;

    if (safe_copy_to_user(dirp, entbuf, reclen) != 0) {
        return -(long)EFAULT_;
    }
    return (long)reclen;
}

/*
 * Linux x86_64 fork(2) — syscall 57.
 *
 * Creates a child that resumes at the parent's user RIP with %rax = 0.
 * The child inherits a clone of the parent's page table
 * (vmm_clone_page_table), but the leaf physical pages are shared at
 * first; we then eagerly copy every writable region the process owns
 * so the parent's later writes do not clobber the child's view (and
 * vice versa).
 *
 * The regions copied, in VA order:
 *   - ELF image:   [0x400000,      0x600000)      2 MB
 *   - user stack:  [0x8000000000,  0x8000100000)  64 KB
 *   - brk heap:    [0x8000200000,  0x8000300000)  1 MB
 *   - mmap region: [0x8010000000,  0x8010400000)  4 MB
 *
 * The ELF image copy is what makes busybox ash's pre-execve window
 * safe: without it, the child's writes to busybox's globals in .data
 * landed in the page the parent was still using, corrupting the
 * parent's FILE/job list and causing a #PF at CR2=0x20 in stdio
 * teardown.  See session-23 notes.
 *
 * This is not real copy-on-write: every page in every region is
 * copied unconditionally, including read-only pages like .text and
 * .rodata.  A production fix would mark shared PTEs read-only and
 * only copy on write.  Until then, each fork copies ~6 MB.
 *
 * Runs with interrupts disabled across the whole operation.  A timer
 * tick mid-clone could schedule another process whose allocations
 * race with ours.
 */
long sys_fork(void) {
    __asm__ volatile("cli");

    pcb_t* parent = process_get_current(); 
    
    if (!parent) {
        __asm__ volatile("sti");
        return -(long)EAGAIN_;
    }

    /*
     * process_create clones the parent's current CR3 and allocates a
     * fresh kernel stack for the child.  It also builds an initial
     * frame at the child's kernel_stack_top, but we discard that and
     * build the real fork frame below.
     */
    pcb_t* child = process_create(parent->name, parent->entry_point, 0);
    if (!child) {
        __asm__ volatile("sti");
        return -(long)EAGAIN_;
    }
    scheduler_ready_queue_remove(child);

    /*
     * Eager copy helper.  Walks [start, end), and for each present
     * page, allocates a fresh physical page, copies the contents,
     * remaps the child's PTE, and tracks the new page in the child's
     * elf_page_list.
     *
     * Factored out so the four region copies below (ELF, stack, brk,
     * mmap) share one implementation.  On allocation failure it
     * destroys the child and returns -1; the caller propagates the
     * error.
     */
    #define EAGER_COPY_REGION(start_, end_)                              \
        do {                                                             \
            for (uint64_t _virt = (start_); _virt < (end_); _virt += 4096) { \
                uint64_t _parent_phys =                                  \
                    vmm_get_phys_from_cr3(parent->cr3, _virt);           \
                if (!_parent_phys) continue;                             \
                                                                         \
                uint64_t _new_phys = pmm_alloc_page_for_elf();           \
                if (!_new_phys) {                                        \
                    serial_print("sys_fork: out of memory for page\n");  \
                    process_destroy(child);                              \
                    __asm__ volatile("sti");                             \
                    return -(long)ENOMEM_;                               \
                }                                                        \
                                                                         \
                const uint8_t* _src =                                    \
                    (const uint8_t*)(HHDM_START + _parent_phys);         \
                uint8_t* _dst =                                          \
                    (uint8_t*)(HHDM_START + _new_phys);                  \
                for (uint64_t _i = 0; _i < 4096; _i++) _dst[_i] = _src[_i]; \
                                                                         \
                uint64_t _map_flags = PT_PRESENT | PT_WRITE | PT_USER;   \
                vmm_map_page_in_cr3(child->cr3, _virt, _new_phys,        \
                                    _map_flags);                         \
                elf_add_page_to_pcb(child, _new_phys);                   \
            }                                                            \
        } while (0)

    /* ELF image: .text, .rodata, .data, .bss. */
    EAGER_COPY_REGION(0x0000000000400000ULL, 0x0000000000600000ULL);

    /* User stack. */
    if (parent->user_stack_virt && parent->user_stack_top) {
        EAGER_COPY_REGION(parent->user_stack_virt, parent->user_stack_top);
    }

    /* brk heap: only the pages the parent actually touched. */
    if (parent->brk_virt != 0) {
        EAGER_COPY_REGION(0x0000008000200000ULL, 0x0000008000300000ULL);
    }

    /* mmap window: same, only the pages that are present. */
    EAGER_COPY_REGION(0x0000008010000000ULL, 0x0000008010400000ULL);

    #undef EAGER_COPY_REGION

    /*
     * Child inherits the parent's FS base.
     *
     * musl's TLS pointer lives in MSR_FS_BASE and is read on the
     * very first instruction after _Fork returns in the child
     * (__post_Fork calls __get_tp, which is `mov %fs:0, %rdx`).
     * Without this, the child runs with MSR_FS_BASE = 0 and
     * __get_tp faults at CR2 = 0.
     *
     * See the fs_base comment in process.h.
     */
    child->fs_base = parent->fs_base;


    /*
     * Child inherits the parent's cwd.
     *
     * The cwd is a per-process property, so fork must propagate it,
     * exactly like fs_base and brk_virt.  Without this, a forked
     * child starts with cwd[0] == '\0' (the fresh-PCB default from
     * process_initialize_pcb's memset), which sys_stat and
     * sys_open treat as "/".  busybox applets that fork -- `ls`,
     * `cat`, `echo` when run as separate processes -- would then
     * see the root as their working directory regardless of what
     * `cd` set in the parent.
     *
     * This is what made `cd /bin; ls` list the root: the shell did
     * `cd` in its own process (setting pcb->cwd = "/bin"), then
     * forked a child for `ls`, and the child's empty cwd resolved
     * "." to "/".
     *
     * cwd already survives execve untouched (sys_execve does not
     * clear it), so this one propagation point covers both fork
     * and the fork+exec path.
     */
    {
        size_t _i = 0;
        while (parent->cwd[_i] && _i < sizeof(child->cwd) - 1) {
            child->cwd[_i] = parent->cwd[_i];
            _i++;
        }
        child->cwd[_i] = '\0';
    }

    /*
     * Child inherits the parent's open fds.
     *
     * process_create zeroed the child's file_table[], but fork
     * must give the child the same open fds as the parent.  This
     * is what makes `cmd < file` work: the shell opens the file,
     * dup2()s it onto fd 0, then forks and execs the command.  If
     * the child starts with an empty table, fd 0 is NULL in the
     * child, its read(0) falls through to the keyboard, and the
     * command blocks forever.  Same for `>`: fd 1 must carry the
     * parent's redirected file into the child.
     *
     * Each slot is SHARED, not copied: the child's fd and the
     * parent's fd point at the same file_slot_t, and the refcount
     * is incremented for the new reference -- exactly as dup2
     * shares a slot.  Closing either fd drops one reference; the
     * slot is freed only when the last reference goes away.
     *
     * fds 0, 1, 2 are included: the parent may have dup2'd a file
     * onto them, and the child must see the same redirection.
     */
    for (int _fd = 0; _fd < MAX_PROCESS_FILES; _fd++) {
        file_slot_t* _slot = (file_slot_t*)parent->file_table[_fd];
        if (!_slot) continue;
        _slot->refcount++;
        child->file_table[_fd] = _slot;
    }

    /*
     * Build the child's iretq-resume frame from the parent's current
     * syscall-entry frame.  This sets the child's %rax to 0, which is
     * how fork() signals "you are the child" to user code.
     */
    process_fork_copy_frame(child, parent);

    /*
     * Child inherits the parent's heap break so its brk region starts
     * where the parent's was.  The pages themselves are now copied
     * (see the eager-copy block above), so writes to the brk region
     * in either process no longer alias.
     */
    child->brk_virt = parent->brk_virt;

    child->parent_pid = parent->pid;
    child->state = PROC_STATE_READY;
    scheduler_ready_queue_add(child);

    __asm__ volatile("sti");
    return (long)child->pid;
}

void sys_exit(int status) {
    pcb_t* self = process_get_current();
    if (self) {
        self->exit_status = status;
    }
    close_all_files(self);
    process_exit();
}

/*
 * Linux x86_64 arch_prctl(2).
 *
 * musl's __init_tls calls arch_prctl(ARCH_SET_FS, tp) before
 * __libc_start_main, so a musl binary will hit this before main().
 *
 * Only ARCH_SET_FS (0x1002) is implemented in A2 item 1.  Other
 * codes return -1 for now; -EINVAL and the remaining subcodes
 * (ARCH_GET_FS, ARCH_SET_GS, ARCH_GET_GS) come later if a specific
 * musl or busybox path needs them.
 */

#define ARCH_SET_FS 0x1002

long sys_arch_prctl(int code, void* addr) {
    if (code == ARCH_SET_FS) {
        wrmsr(0xC0000100, (uint64_t)addr);
        pcb_t* self = process_get_current();
        if (self) self->fs_base = (uint64_t)addr;
        return 0;
    }
    return -(long)EINVAL_;
}


static void kernel_do_reboot(void) {
    serial_print("[REBOOT] closing file handles before reset\n");
    close_all_files(process_get_current());
    handle_reboot_sequence();
}

uint64_t syscall_dispatch(uint64_t num,
                          uint64_t arg0, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;

    switch (num) {

        /* --- Linux x86_64 numbers, strictly ascending --- */
        case SYS_READ:            return (uint64_t)sys_read((int)arg0, (void*)arg1, (size_t)arg2);
        case SYS_WRITE:           return (uint64_t)sys_write((int)arg0, (const void*)arg1, (size_t)arg2);
        case SYS_OPEN:            return (uint64_t)sys_open((const char*)arg0, (int)arg1);
        case SYS_CLOSE:           return (uint64_t)sys_close((int)arg0);
        case SYS_STAT:            return (uint64_t)sys_stat((const char*)arg0, (void*)arg1);
        case SYS_FSTAT:           return (uint64_t)sys_fstat((int)arg0, (void*)arg1);
        case SYS_LSTAT:           return (uint64_t)sys_lstat((const char*)arg0, (void*)arg1);
        case SYS_POLL:            return (uint64_t)sys_poll((void*)arg0, (unsigned long)arg1, (int)arg2);
        case SYS_LSEEK:           return (uint64_t)sys_lseek((int)arg0, (long)arg1, (int)arg2);
        case SYS_MMAP:            return (uint64_t)sys_mmap((void*)arg0, (size_t)arg1, (int)arg2, (int)arg3, (int)arg4, (long)arg5);
        case SYS_MPROTECT:        return (uint64_t)sys_mprotect((void*)arg0, (size_t)arg1, (int)arg2);
        case SYS_MUNMAP:          return (uint64_t)sys_munmap((void*)arg0, (size_t)arg1);
        case SYS_BRK:             return (uint64_t)sys_brk((void*)arg0);
        case SYS_RT_SIGACTION:    return (uint64_t)sys_rt_sigaction((int)arg0, (const void*)arg1, (void*)arg2, (size_t)arg3);
        case SYS_RT_SIGPROCMASK:  return (uint64_t)sys_rt_sigprocmask((int)arg0, (const void*)arg1, (void*)arg2, (size_t)arg3);
        case SYS_IOCTL:           return (uint64_t)sys_ioctl((int)arg0, (unsigned long)arg1, (void*)arg2);
        case SYS_READV:           return (uint64_t)sys_readv((int)arg0, (const struct iovec*)arg1, (int)arg2);
        case SYS_WRITEV:          return (uint64_t)sys_writev((int)arg0, (const struct iovec*)arg1, (int)arg2);
        case SYS_ACCESS:          return (uint64_t)sys_access((const char*)arg0, (int)arg1);
        case SYS_PIPE:            return (uint64_t)sys_pipe((int*)arg0);
        case SYS_DUP2:            return (uint64_t)sys_dup2((int)arg0, (int)arg1);
        case SYS_GETPID:          return (uint64_t)sys_getpid();
        case SYS_FORK:            return (uint64_t)sys_fork();
        case SYS_EXECVE:          return (uint64_t)sys_execve((const char*)arg0, (char**)arg1, (char**)arg2);
        case SYS_EXIT:            sys_exit((int)arg0); return 0;
        case SYS_WAIT4:           return (uint64_t)sys_wait4((long)arg0, (int*)arg1, (int)arg2);
        case SYS_UNAME:           return (uint64_t)sys_uname((void*)arg0);
        case SYS_FCNTL:           return (uint64_t)sys_fcntl((int)arg0, (int)arg1, (unsigned long)arg2);
        case SYS_FTRUNCATE:       return (uint64_t)sys_ftruncate((int)arg0, (long)arg1);
        case SYS_GETCWD:          return (uint64_t)sys_getcwd((char*)arg0, (unsigned long)arg1);
        case SYS_CHDIR:           return (uint64_t)sys_chdir((const char*)arg0);
        case SYS_RENAME:          return (uint64_t)sys_rename((const char*)arg0, (const char*)arg1);
        case SYS_MKDIR:           return (uint64_t)sys_mkdir((const char*)arg0, (int)arg1);
        case SYS_RMDIR:           return (uint64_t)sys_rmdir((const char*)arg0);
        case SYS_UNLINK:          return (uint64_t)sys_unlink((const char*)arg0);
        case SYS_GETEUID:         return (uint64_t)sys_geteuid();
        case SYS_GETPPID:         return (uint64_t)sys_getppid();
        case SYS_SETSID:          return (uint64_t)sys_setsid();
        case SYS_PRCTL:           return (uint64_t)sys_prctl((int)arg0, (unsigned long)arg1, 0, 0, 0);
        case SYS_ARCH_PRCTL:      return (uint64_t)sys_arch_prctl((int)arg0, (void*)arg1);
        case SYS_GETDENTS64:      return (uint64_t)sys_getdents64((int)arg0, (void*)arg1, (size_t)arg2);
        case SYS_SET_TID_ADDRESS: return (uint64_t)sys_set_tid_address((int*)arg0);
        case SYS_EXIT_GROUP:      sys_exit((int)arg0); return 0;
        case SYS_UTIMES:          return (uint64_t)sys_utimes((const char*)arg0, (const void*)arg1);
        case SYS_FUTIMESAT:       return (uint64_t)sys_futimesat((int)arg0, (const char*)arg1, (const void*)arg2);
        case SYS_FACCESSAT:       return (uint64_t)sys_faccessat((int)arg0, (const char*)arg1, (int)arg2, (int)arg3);
        case SYS_SET_ROBUST_LIST: return (uint64_t)sys_set_robust_list((void*)arg0, (size_t)arg1);
        case SYS_UTIMENSAT:       return (uint64_t)sys_utimensat((int)arg0, (const char*)arg1, (const void*)arg2, (int)arg3);
        case SYS_GETRANDOM:       return (uint64_t)sys_getrandom((void*)arg0, (size_t)arg1, (unsigned int)arg2);
        case SYS_RSEQ:            return (uint64_t)sys_rseq((void*)arg0, (uint32_t)arg1, (int)arg2, (uint32_t)arg3);

        /* --- donix-private numbers (500+) --- */
        case SYS_REBOOT:          kernel_do_reboot(); return 0;

        default:
            serial_print("Unknown syscall: ");
            serial_print_dec(num); serial_print("\n");
            return -(long)ENOSYS_;
    }
}
