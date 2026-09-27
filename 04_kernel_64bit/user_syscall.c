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

/* Defined in kmain.c — reboots the machine via keyboard controller + ACPI reset port. */
extern void handle_reboot_sequence(void);

extern uint64_t g_syscall_stack_top;

#define WRITE_CHUNK 256
static char g_write_bounce[WRITE_CHUNK];

/* Maximum path length accepted from user space. */
#define USER_PATH_MAX 300

/* Maximum number of program headers we accept in an ELF. */
#define EXEC_MAX_PHDRS 16

/* ============================================================
 * DEBUG INSTRUMENTATION
 *
 * Set DEBUG_FIL to 1 to log every FIL field at every step of
 * open/write/close/read.  Set to 0 for production.
 * ============================================================ */
#define DEBUG_FIL 0

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

typedef struct file_slot_s {
    uint32_t kind;
    uint32_t refcount;
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
static void put_file_slot(file_slot_t* slot) {
    if (!slot) return;
    if (slot->refcount > 1) {
        slot->refcount--;
        return;
    }
    if (slot->kind == FILE_KIND_FILE) {
        f_close((FIL*)slot->obj);
    } else if (slot->kind == FILE_KIND_DIR) {
        f_closedir((DIR*)slot->obj);
    }
    kfree(slot->obj);
    kfree(slot);
}

static void close_all_files(pcb_t* proc) {
    if (!proc) return;
    for (int i = 3; i < MAX_PROCESS_FILES; i++) {
        file_slot_t* slot = (file_slot_t*)proc->file_table[i];
        if (!slot) continue;
        put_file_slot(slot);
        proc->file_table[i] = NULL;
    }
}

static int alloc_file_slot(file_slot_t** out_slot) {
    pcb_t* self = process_get_current();
    if (!self) return -1;

    int fd = -1;
    for (int i = 3; i < MAX_PROCESS_FILES; i++) {
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
#define EINVAL_  22
#define EAGAIN_  11
#define EBADF_   9

long sys_fcntl(int fd, int cmd, unsigned long arg) {
    file_slot_t* slot = get_file_slot(fd, 0);
    if (!slot) return -(long)EBADF_;

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
// FILE SYSCALLS
// ============================================================
long sys_open(const char* path, int flags) {
    pcb_t* self = process_get_current();
    if (!self || !path) return -1;

    char local_path[USER_PATH_MAX];
    if (copy_user_string(local_path, sizeof(local_path), path) != 0) return -1;
    strip_dot_prefix(local_path);

    file_slot_t* slot = NULL;
    int fd = alloc_file_slot(&slot);
    if (fd == -1) return -1;

    BYTE mode = 0;
    switch (flags & 0x3) {
        case 0:  mode |= FA_READ;             break;
        case 1:  mode |= FA_WRITE;            break;
        case 2:  mode |= FA_READ | FA_WRITE;  break;
        default: kfree(slot); self->file_table[fd] = NULL; return -1;
    }

    if (flags & 0x0400) {
        mode |= FA_CREATE_ALWAYS;
    } else if (flags & 0x0200) {
        if (flags & 0x0800) mode |= FA_CREATE_NEW;
        else                mode |= FA_OPEN_ALWAYS;
    } else {
        mode |= FA_OPEN_EXISTING;
    }

    if (flags & 0x0008) mode |= FA_OPEN_APPEND;

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
            return -1;
        }
        const char* dir_path = is_root ? "0:/" : local_path;
        FRESULT dr = f_opendir(dir_obj, dir_path);
        if (dr == FR_OK) {
            slot->kind = FILE_KIND_DIR;
            slot->obj  = dir_obj;
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
        return -1;
    }

    FIL* file_obj = (FIL*)kmalloc(sizeof(FIL));
    if (!file_obj) {
        kfree(slot);
        self->file_table[fd] = NULL;
        return -1;
    }

    FRESULT r = f_open(file_obj, local_path, mode);
    if (r == FR_OK) {
        slot->kind = FILE_KIND_FILE;
        slot->obj  = file_obj;
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
                return fd;
            }
            kfree(dir_obj);
        }
    }

    serial_print("sys_open: f_open FAIL path=");
    serial_print(local_path);
    serial_print(" r=");
    serial_print_dec(r);
    serial_print("\n");
    kfree(file_obj);
    kfree(slot);
    self->file_table[fd] = NULL;
    return -1;
}

long sys_close(int fd) {
    file_slot_t* slot = get_file_slot(fd, 0);
    if (!slot) return -1;

    pcb_t* self = process_get_current();
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
    if (!self) return -(long)9;   /* -EBADF */

    if (oldfd < 0 || oldfd >= MAX_PROCESS_FILES) return -(long)9;
    if (newfd < 0 || newfd >= MAX_PROCESS_FILES) return -(long)9;

    file_slot_t* old_slot = get_file_slot(oldfd, 0);
    if (!old_slot) return -(long)9;   /* -EBADF */

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
    return (long)newfd;
}

long sys_unlink(const char* path) {
    pcb_t* self = process_get_current();
    if (!self || !path) return -1;

    char local_path[USER_PATH_MAX];
    if (copy_user_string(local_path, sizeof(local_path), path) != 0) return -1;
    strip_dot_prefix(local_path);

    FRESULT r = f_unlink(local_path);
    if (r != FR_OK) {
        serial_print("sys_unlink: f_unlink FAIL path=");
        serial_print(local_path);
        serial_print(" r="); serial_print_dec(r);
        serial_print("\n");
        return -1;
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
    if (!user_stat) return -1;

    file_slot_t* slot = get_file_slot(fd, 0);
    if (!slot) return -1;

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
        return -1;
    }

    fill_kstat_from_filinfo(&st, &fno);

    if (safe_copy_to_user(user_stat, &st, sizeof(st)) != 0) {
        return -1;
    }
    return 0;
}

/*
 * Linux x86_64 stat(2) — syscall 4.
 *
 * Path-based sibling of sys_fstat.  musl's stat(path, st) routes
 * through fstatat(AT_FDCWD, path, st, 0) → fstatat_kstat →
 * __syscall(SYS_stat, path, &kst).  SYS_stat is 4.
 *
 * The path is passed straight through to FatFs's f_stat, which
 * accepts the same "0:/NAME" form as f_open, after strip_dot_prefix
 * has removed any leading "./" that musl's lstat()/stat() would
 * otherwise hand us.
 *
 * Returns 0 on success, -1 on failure.  Like sys_fstat, proper
 * negative errno is deferred until a caller actually inspects it.
 */
long sys_stat(const char* user_path, void* user_stat) {
    if (!user_path || !user_stat) return -1;

    char path[USER_PATH_MAX];
    if (copy_user_string(path, sizeof(path), user_path) != 0) {
        return -1;
    }
    strip_dot_prefix(path);

    /* ".", "/", "0:", "0:/" — synthesize a root-directory stat.
     * f_stat would return FR_INVALID_NAME for all of them. */
    if (path_is_root(path)) {
        kernel_stat_t st;
        fill_kstat_as_root(&st);
        if (safe_copy_to_user(user_stat, &st, sizeof(st)) != 0) {
            return -1;
        }
        return 0;
    }

    FILINFO fno;
    FRESULT r = f_stat(path, &fno);
    if (r != FR_OK) {
        return -1;
    }

    kernel_stat_t st;
    fill_kstat_from_filinfo(&st, &fno);

    if (safe_copy_to_user(user_stat, &st, sizeof(st)) != 0) {
        return -1;
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
        return -1;
    }

    /* ---- 1. Copy the path string. ---- */
    char path[USER_PATH_MAX];
    if (copy_user_string(path, sizeof(path), user_path) != 0) {
        serial_print("sys_execve: bad path pointer\n");
        __asm__ volatile("sti");
        return -1;
    }

    /* ---- 2. Open and read the whole ELF file. ---- */
    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK) {
        serial_print("sys_execve: f_open(");
        serial_print(path);
        serial_print(") -> ");
        serial_print_dec(fr);
        serial_print("\n");
        __asm__ volatile("sti");
        return -1;
    }

    Elf64_Ehdr ehdr;
    UINT got = 0;
    fr = f_read(&file, &ehdr, sizeof(ehdr), &got);
    if (fr != FR_OK || got != sizeof(ehdr)) {
        serial_print("sys_execve: short read of ELF header\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -1;
    }

    if (ehdr.e_ident[0] != ELF_MAGIC0 || ehdr.e_ident[1] != ELF_MAGIC1 ||
        ehdr.e_ident[2] != ELF_MAGIC2 || ehdr.e_ident[3] != ELF_MAGIC3) {
        serial_print("sys_execve: not an ELF file\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -1;
    }
    if (ehdr.e_ident[4] != 2) { serial_print("sys_execve: not ELFCLASS64\n");     f_close(&file); __asm__ volatile("sti"); return -1; }
    if (ehdr.e_ident[5] != 1) { serial_print("sys_execve: not little-endian\n");  f_close(&file); __asm__ volatile("sti"); return -1; }
    if (ehdr.e_type != 2)     { serial_print("sys_execve: not ET_EXEC\n");        f_close(&file); __asm__ volatile("sti"); return -1; }
    if (ehdr.e_machine != 62) { serial_print("sys_execve: not x86-64\n");         f_close(&file); __asm__ volatile("sti"); return -1; }
    if (ehdr.e_phentsize != sizeof(Elf64_Phdr) ||
        ehdr.e_phnum == 0 || ehdr.e_phnum > EXEC_MAX_PHDRS) {
        serial_print("sys_execve: bad program header table\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -1;
    }

    FSIZE_t file_size = f_size(&file);
    if (file_size == 0 || file_size > 4ULL * 1024 * 1024) {
        serial_print("sys_execve: file size out of range\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -1;
    }
    uint8_t* elf_buf = (uint8_t*)kmalloc((size_t)file_size);
    if (!elf_buf) {
        serial_print("sys_execve: kmalloc failed for ");
        serial_print_dec((uint64_t)file_size);
        serial_print(" bytes\n");
        f_close(&file);
        __asm__ volatile("sti");
        return -1;
    }
    fr = f_lseek(&file, 0);
    if (fr != FR_OK) {
        serial_print("sys_execve: rewind failed\n");
        kfree(elf_buf); f_close(&file);
        __asm__ volatile("sti");
        return -1;
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
            return -1;
        }
        if (br == 0) break;
        total += br;
    }
    f_close(&file);
    if (total != file_size) {
        serial_print("sys_execve: short read of file body\n");
        kfree(elf_buf);
        __asm__ volatile("sti");
        return -1;
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
                return -1;
            }
            if (user_str_va == 0) break;

            if (copy_user_string(argv_scratch[argc], EXEC_MAX_ARG_LEN,
                                 (const char*)user_str_va) != 0) {
                serial_print("sys_execve: bad argv[");
                serial_print_dec(argc);
                serial_print("] string\n");
                kfree(elf_buf);
                __asm__ volatile("sti");
                return -1;
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
    /*
     * No scratch CR3.  Everything targets self->cr3.  From this point
     * on, failures cannot be cleanly recovered (the caller's old
     * pages are gone), so any failure exits the process.  See the
     * function comment for the rationale.
     */
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

        /*
         * Layout on the argv region, low to high:
         *
         *   argv_region_bottom + 0               argv[0]
         *   argv_region_bottom + 8               argv[1]
         *   ...
         *   argv_region_bottom + 8*argc          argv NULL terminator
         *   argv_region_bottom + 8*(argc+1)      envp NULL terminator
         *   argv_region_bottom + 8*(argc+2)      first argument string
         *
         * array_bytes = 8*(argc+1) covers the argv slots plus the
         * argv NULL terminator.  The envp NULL terminator needs one
         * MORE slot, at argv_region_bottom + array_bytes.  Strings
         * must therefore start at argv_region_bottom + array_bytes
         * + 8, not at argv_region_bottom + array_bytes.
         *
         * The previous code put strings_start at
         * argv_region_bottom + array_bytes, the same address as the
         * envp NULL write, so the envp NULL zeroed the first 8
         * bytes of argv[0]'s string.  When argv[0] was long (e.g.
         * "0:/cat.elf") the collateral damage stopped short of
         * argv[1]'s string and nothing visible broke.  When argv[0]
         * was short (e.g. "cat", "echo"), argv[1]'s string began
         * inside the 8-byte zeroing window and was clobbered — the
         * child saw an empty argv[1] and silently did nothing.
         */
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

        /* SysV initial stack: [rsp]=argc, [rsp+8]=argv[0..n-1],
           then argv NULL, then envp NULL.  We put argc in the 8 bytes
           immediately below the argv array. */
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

    /* ---- 7. Rewrite the syscall-entry frame. ----
     *
     * user_syscall_entry.asm pushed the frame with the first push at
     * kernel_stack_top - 8.  Offsets we care about:
     *   -56 = user RIP   (loaded into %rcx, used by sysret)
     *   -72 = user RSP   (loaded into %r10, moved to %rsp before sysret)
     *
     * ktop[-7] = -56, ktop[-9] = -72.  RFLAGS at -64 is left alone:
     * the new program starts with the same user RFLAGS the old one
     * had, which is 0x202 for musl.
     */
    uint64_t* ktop = (uint64_t*)self->kernel_stack_top;
    ktop[-7] = entry;      /* -56: user RIP  */
    ktop[-9] = rsp_init;   /* -72: user RSP  */

    /*
     * Also pass argc/argv in %rdi/%rsi.
     *
     * musl's _start reads the SysV stack layout above and ignores
     * %rdi/%rsi on entry.  donix's newlib crt0.S (arc2/crt0.S) reads
     * %rdi/%rsi and ignores the stack layout:
     *
     *     mov [rip + argc_saved], rdi
     *     mov [rip + argv_saved], rsi
     *
     * sys_spawn (507) already passes argc/argv in %rdi/%rsi via
     * frame[9]/frame[10] of the child's initial resume frame.
     * sys_execve was missing it, so a newlib binary launched via
     * execve ran with %rdi/%rsi holding the execve call's own
     * arguments — pointers into the old, torn-down address space.
     * cat.elf read argv[1] from there, got NULL, and tried to open
     * "0:/(null)".
     *
     * Setting both is harmless for musl (it ignores the registers)
     * and makes execve work for either kind of binary, which matters
     * because the newlib userland is still the regression canary and
     * is launched from musl_sh via execve.
     *
     * Frame slots per user_syscall_entry.asm's push order:
     *   -96 = %rdi, -104 = %rsi.
     */
    ktop[-12] = (uint64_t)argc;       /* -96: rdi */
    ktop[-13] = argv_array_base;      /* -104: rsi */

    /* ---- 8. Update PCB fields. ---- */
    self->entry_point = entry;
    self->rip         = entry;
    self->exit_status = 0;
    self->wait_pid    = 0;
    self->block_kind  = BLOCK_KIND_NONE;
    self->state       = PROC_STATE_RUNNING;

    /* Manual name copy — no strncpy, we don't want zero-padding. */
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
    if (!self) return -1;

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
                    return -1;
                }
            }
            return reaped;
        }

        if (!live) {
            return -1;
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
    if (!self) return -1;

    if (fd == 1 || fd == 2) {
        size_t remaining = count;
        const uint8_t* user_ptr = (const uint8_t*)buf;
        while (remaining > 0) {
            size_t chunk = remaining > WRITE_CHUNK ? WRITE_CHUNK : remaining;
            if (safe_copy_from_user(g_write_bounce, user_ptr, chunk) != 0) {
                return -1;
            }
            for (size_t i = 0; i < chunk; i++) {
                char c = g_write_bounce[i];
                serial_putc(c); vga_putc(c);
            }
            user_ptr += chunk; remaining -= chunk;
        }
        return (long)count;
    }

    file_slot_t* slot = get_file_slot(fd, FILE_KIND_FILE);
    if (slot) {
        FIL* file_obj = (FIL*)slot->obj;
        char* bounce = (char*)kmalloc(512);
        if (!bounce) return -1;

        size_t total_written = 0;
        while (total_written < count) {
            size_t chunk = (count - total_written) > 512 ? 512 : (count - total_written);
            if (safe_copy_from_user(bounce, (const uint8_t*)buf + total_written, chunk) != 0) {
                kfree(bounce);
                return (total_written > 0) ? (long)total_written : -1;
            }
            UINT written;
            if (f_write(file_obj, bounce, chunk, &written) != FR_OK) {
                kfree(bounce);
                return (total_written > 0) ? (long)total_written : -1;
            }
            total_written += written;
            if (written < chunk) break;
        }
        kfree(bounce);
        return (long)total_written;
    }

    return -1;
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
    if (!user_iov || iovcnt <= 0) return 0;
    if (iovcnt > WRITEV_MAX_IOVS) return -22;  /* -EINVAL */

    struct iovec local[WRITEV_MAX_IOVS];
    size_t bytes = (size_t)iovcnt * sizeof(struct iovec);
    if (safe_copy_from_user(local, user_iov, bytes) != 0) {
        return -14;  /* -EFAULT */
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

long sys_read(int fd, void* buf, size_t count) {
    if (!buf || count == 0) return 0;
    pcb_t* self = process_get_current();
    if (!self) return 0;

    if (fd == 0) {
        char c; size_t bytes_read = 0; uint8_t* dest_ptr = (uint8_t*)buf;
        while (bytes_read < count) {
            __asm__ volatile("cli");
            if (kbd_buffer_get(&c)) {
                __asm__ volatile("sti");
                if (safe_copy_to_user(dest_ptr + bytes_read, &c, 1) == 0) bytes_read++;
                else return -1;
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

    file_slot_t* slot = get_file_slot(fd, FILE_KIND_FILE);
    if (slot) {
        FIL* file_obj = (FIL*)slot->obj;
        char* bounce = (char*)kmalloc(512);
        if (!bounce) return -1;

        size_t total_read = 0;
        while (total_read < count) {
            size_t chunk = (count - total_read) > 512 ? 512 : (count - total_read);
            UINT read_bytes;
            if (f_read(file_obj, bounce, chunk, &read_bytes) != FR_OK) {
                kfree(bounce); return -1;
            }
            if (read_bytes == 0) break;
            if (safe_copy_to_user((uint8_t*)buf + total_read, bounce, read_bytes) != 0) {
                kfree(bounce); return -1;
            }
            total_read += read_bytes;
            if (read_bytes < chunk) break;
        }
        kfree(bounce);
        return (long)total_read;
    }

    return 0;
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
 * Linux x86_64 setsid(2) — syscall 107.
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
 */
long sys_setsid(void) {
    pcb_t* current = process_get_current();
    if (!current) return -(long)1;
    return (long)current->pid;
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
 * Linux x86_64 ioctl(2) — minimal stub.
 *
 * musl's __stdout_write calls ioctl(1, TCGETS, &tio) to decide
 * whether stdout is a terminal.  If the ioctl fails, musl treats
 * stdout as a regular file and uses fully-buffered stdio, flushing
 * on exit.  That is exactly the behavior we want for donix's serial
 * console, which is not a POSIX tty.
 *
 * Return -ENOTTY (errno 25) for all requests.  The Linux syscall ABI
 * expects negative errno in the return register.
 *
 * If a later test genuinely needs a working TCGETS (e.g. busybox's
 * `tput` or `stty`), implement a proper termios response then.
 */
#define ENOTTY 25
long sys_ioctl(int fd, unsigned long request, void* argp) {
    (void)fd; (void)request; (void)argp;
    return -(long)ENOTTY;
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

    if (length == 0) return -(long)22;  /* -EINVAL */

    if ((flags & MAP_ANONYMOUS) == 0 || fd != -1) {
        return -(long)ENOMEM;
    }

    pcb_t* self = process_get_current();
    if (!self) return -(long)ENOMEM;

    uint64_t rounded = ((uint64_t)length + 0xFFF) & ~0xFFFULL;

    uint64_t base;
    if (flags & MAP_FIXED) {
        base = (uint64_t)addr & ~0xFFFULL;
    } else {
        base = mmap_find_free_slot(self, rounded);
        if (base == 0) return -(long)ENOMEM;
    }

    for (uint64_t v = base; v < base + rounded; v += 0x1000) {
        uint64_t phys = pmm_alloc_page_for_elf();
        if (!phys) {
            return -(long)ENOMEM;
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
#define ENOSYS 38
long sys_getrandom(void* buf, size_t buflen, unsigned int flags) {
    (void)buf; (void)buflen; (void)flags;
    return -(long)ENOSYS;
}

long sys_rseq(void* rseq, uint32_t rseq_len, int flags, uint32_t sig) {
    (void)rseq; (void)rseq_len; (void)flags; (void)sig;
    return -(long)ENOSYS;
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
    if (!dirp) return -(long)14;      /* -EFAULT */
    if (count == 0) return -(long)22; /* -EINVAL */

    file_slot_t* slot = get_file_slot(fd, FILE_KIND_DIR);
    if (!slot) return -(long)9;       /* -EBADF */

    FILINFO fno;
    FRESULT r = f_readdir((DIR*)slot->obj, &fno);
    if (r != FR_OK) return -(long)5;    /* -EIO */
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
        return -(long)22; /* -EINVAL */
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
        return -(long)14;  /* -EFAULT */
    }
    return (long)reclen;
}

/*
 * Linux x86_64 fork(2) — syscall 57.
 *
 * First cut.  Creates a child that resumes at the parent's user RIP
 * with %rax = 0.  The child inherits a clone of the parent's page
 * table (vmm_clone_page_table), but the leaf physical pages are
 * shared at first; we then eagerly copy the user stack so the
 * parent's later stack writes do not clobber the child's view.
 *
 * Known limitations:
 *
 *   1. ELF segment pages (.text, .data, .bss) are still shared with
 *      the parent after fork.  Writes to .data or .bss in either
 *      process are visible to the other.  This is not a problem for
 *      the immediate use case (fork + execve, where the child
 *      replaces its address space before writing anything), but it
 *      is not correct fork semantics.  Real copy-on-write comes
 *      later.
 *
 *   2. Only the callee-saved registers are preserved in the child.
 *      See process_fork_copy_frame's declaration for the details.
 *
 *   3. The child does not inherit the parent's brk.  It gets the
 *      parent's brk_virt, but the child's ELF pages aren't duplicated
 *      so writes to the brk region in either process alias.  Again,
 *      not a problem for fork + execve.
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
        return -1;
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
        return -1;
    }
    scheduler_ready_queue_remove(child);

    /*
     * Eager user-stack copy.
     *
     * vmm_clone_page_table shares leaf physical pages, so the child's
     * user-stack PTEs currently point at the parent's stack pages.
     * Walk the range, allocate fresh pages, copy the contents, and
     * remap the child's PTEs.
     */
    if (parent->user_stack_virt && parent->user_stack_top) {
        for (uint64_t virt = parent->user_stack_virt;
             virt < parent->user_stack_top;
             virt += 4096) {

            uint64_t parent_phys = vmm_get_phys_from_cr3(parent->cr3, virt);
            if (!parent_phys) continue;

            uint64_t new_phys = pmm_alloc_page_for_elf();
            if (!new_phys) {
                serial_print("sys_fork: out of memory for stack page\n");
                process_destroy(child);
                __asm__ volatile("sti");
                return -1;
            }

            const uint8_t* src = (const uint8_t*)(HHDM_START + parent_phys);
            uint8_t* dst = (uint8_t*)(HHDM_START + new_phys);
            for (uint64_t i = 0; i < 4096; i++) dst[i] = src[i];

            uint64_t map_flags = PT_PRESENT | PT_WRITE | PT_USER;
            vmm_map_page_in_cr3(child->cr3, virt, new_phys, map_flags);
            elf_add_page_to_pcb(child, new_phys);
        }
    }
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
     * Build the child's iretq-resume frame from the parent's current
     * syscall-entry frame.  This sets the child's %rax to 0, which is
     * how fork() signals "you are the child" to user code.
     */
    process_fork_copy_frame(child, parent);

    /*
     * Child inherits the parent's heap break so its brk region starts
     * where the parent's was.  The pages themselves are not copied
     * (see the limitation note above), but brk_virt must match so the
     * child's subsequent brk calls land in a plausible range.
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
    return -1;
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

        /* --- Linux x86_64 numbers --- */
        case SYS_READ:            return (uint64_t)sys_read((int)arg0, (void*)arg1, (size_t)arg2);
        case SYS_WRITE:           return (uint64_t)sys_write((int)arg0, (const void*)arg1, (size_t)arg2);
        case SYS_OPEN:            return (uint64_t)sys_open((const char*)arg0, (int)arg1);
        case SYS_CLOSE:           return (uint64_t)sys_close((int)arg0);
        case SYS_FCNTL:           return (uint64_t)sys_fcntl((int)arg0, (int)arg1, (unsigned long)arg2);
        case SYS_STAT:            return (uint64_t)sys_stat((const char*)arg0, (void*)arg1);
        case SYS_LSTAT:           return (uint64_t)sys_lstat((const char*)arg0, (void*)arg1);
        case SYS_FSTAT:           return (uint64_t)sys_fstat((int)arg0, (void*)arg1);
        case SYS_MMAP:            return (uint64_t)sys_mmap((void*)arg0, (size_t)arg1, (int)arg2, (int)arg3, (int)arg4, (long)arg5);
        case SYS_MPROTECT:        return (uint64_t)sys_mprotect((void*)arg0, (size_t)arg1, (int)arg2);
        case SYS_MUNMAP:          return (uint64_t)sys_munmap((void*)arg0, (size_t)arg1);
        case SYS_BRK:             return (uint64_t)sys_brk((void*)arg0);
        case SYS_RT_SIGACTION:    return (uint64_t)sys_rt_sigaction((int)arg0, (const void*)arg1, (void*)arg2, (size_t)arg3);
        case SYS_RT_SIGPROCMASK:  return (uint64_t)sys_rt_sigprocmask((int)arg0, (const void*)arg1, (void*)arg2, (size_t)arg3);
        case SYS_IOCTL:           return (uint64_t)sys_ioctl((int)arg0, (unsigned long)arg1, (void*)arg2);
        case SYS_WRITEV:          return (uint64_t)sys_writev((int)arg0, (const struct iovec*)arg1, (int)arg2);
        case SYS_DUP2:            return (uint64_t)sys_dup2((int)arg0, (int)arg1);
        case SYS_GETPID:          return (uint64_t)sys_getpid();
        case SYS_GETPPID:         return (uint64_t)sys_getppid();
        case SYS_SETSID:          return (uint64_t)sys_setsid();
        case SYS_FORK:            return (uint64_t)sys_fork();
        case SYS_EXECVE:          return (uint64_t)sys_execve((const char*)arg0, (char**)arg1, (char**)arg2);
        case SYS_EXIT:            sys_exit((int)arg0); return 0;
        case SYS_WAIT4:           return (uint64_t)sys_wait4((long)arg0, (int*)arg1, (int)arg2);
        case SYS_UNLINK:          return (uint64_t)sys_unlink((const char*)arg0);
        case SYS_ARCH_PRCTL:      return (uint64_t)sys_arch_prctl((int)arg0, (void*)arg1);
        case SYS_GETDENTS64:      return (uint64_t)sys_getdents64((int)arg0, (void*)arg1, (size_t)arg2);
        case SYS_SET_TID_ADDRESS: return (uint64_t)sys_set_tid_address((int*)arg0);
        case SYS_EXIT_GROUP:      sys_exit((int)arg0); return 0;
        case SYS_SET_ROBUST_LIST: return (uint64_t)sys_set_robust_list((void*)arg0, (size_t)arg1);
        case SYS_GETRANDOM:       return (uint64_t)sys_getrandom((void*)arg0, (size_t)arg1, (unsigned int)arg2);
        case SYS_RSEQ:            return (uint64_t)sys_rseq((void*)arg0, (uint32_t)arg1, (int)arg2, (uint32_t)arg3);

        /* --- donix-private numbers (500+) --- */
        case SYS_REBOOT:          kernel_do_reboot(); return 0;

        default:
            serial_print("Unknown syscall: ");
            serial_print_dec(num); serial_print("\n");
            return -1;
    }
}
