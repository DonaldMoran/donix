#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 32
#define PROC_NAME_LEN 32
#define PROC_STACK_SIZE  16384   // 16KB: syscall entry + nested timer frame + sys_read blocking headroom
#define MAX_PROCESS_FILES 8
#define PROC_CWD_MAX 128

// Process states
typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_ZOMBIE,       // exited; waiting for parent to reap
    PROC_STATE_TERMINATED    // exited; parent never existed; reclaimed immediately
} proc_state_t;

/*
 * Block kinds.  A blocked process can be waiting on a keyboard byte
 * (BLOCK_KIND_NONE, the default) or on a specific child to exit
 * (BLOCK_KIND_WAITPID).  When the event arrives, the waker checks
 * block_kind before waking.
 */
#define BLOCK_KIND_NONE    0
#define BLOCK_KIND_WAITPID 1

// Process Control Block
typedef struct pcb {
    uint64_t pid;
    char name[PROC_NAME_LEN];
    proc_state_t state;
    
    // Page table
    uint64_t cr3;
    
    // Entry point
    uint64_t entry_point;
    
    // Stack fields
    uint64_t kernel_stack_phys;
    uint64_t kernel_stack_virt;
    uint64_t kernel_stack_top;

    int kernel_stack_slot;

    uint64_t user_stack_phys;
    uint64_t user_stack_virt;
    uint64_t user_stack_top;
    
    // ELF loading tracking - for cleanup
    uint64_t elf_base_virt;
    uint64_t elf_base_phys;
    uint64_t elf_num_pages;
    uint64_t* elf_page_list;  // Array of physical addresses allocated for ELF
    
    // ===== Context switching registers (for scheduler) =====
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rsp, rip;
    // =======================================================
    
    // For round-robin scheduling
    struct pcb* next;
    struct pcb* prev;
    uint64_t timeslice_ticks;
    uint64_t total_ticks;
    
    // For debugging
    uint64_t creation_time;
    uint64_t last_run_time;
    
    // Heap management (per-process)
    uint64_t brk_virt;  // Current break position for sbrk()
    
    uint64_t block_kind;

    /*
     * Parent / child tracking (v0.5.3).
     *
     * These fields are placed AFTER block_kind so that every offset
     * context_switch.asm reads (which stops at block_kind, 0x158)
     * remains unchanged.  Do not move them before block_kind.
     *
     * parent_pid == 0 means "no parent" — used by idle, the kernel
     * shell, and the user shell.  A process with parent_pid == 0
     * reclaims its own PCB on exit and does not become a zombie.
     *
     * A process with parent_pid != 0 becomes PROC_STATE_ZOMBIE on
     * exit; the parent reaps it via sys_waitpid.
     */
    uint64_t parent_pid;
    int      exit_status;    // set by sys_exit; read by sys_waitpid
    uint64_t wait_pid;       // pid the process is blocked waiting for; 0 if none, (uint64_t)-1 = "any"

    // Per-Process File Descriptor Tracking Array (Pointer maps to index)
    void* file_table[MAX_PROCESS_FILES];
    /*
     * FS base (MSR 0xC0000100), per-process.
     *
     * Set by arch_prctl(ARCH_SET_FS) at musl startup.  Restored on
     * every context switch, because the MSR is a CPU register: without
     * per-process save/restore, the second musl process to run clobbers
     * the first one's TLS base, and the first one's next %fs-relative
     * access (e.g. the errno load in musl's fork wrapper at 0x4009BD)
     * dereferences a stale or null pointer.
     *
     * Kernel-mode processes leave this at 0; they do not use %fs-
     * relative addressing.  Placed AFTER file_table so no offset that
     * context_switch.asm reads (which stops at block_kind, 0x158) moves.
     */
    uint64_t fs_base;
    /*
     * Per-process current working directory (chdir(2), syscall 80).
     *
     * Stored as a NUL-terminated absolute path.  Zero-initialized by
     * process_initialize_pcb's memset, so cwd[0] == '\0' means "never
     * called chdir"; sys_getcwd treats that as "/".  sys_chdir writes
     * a real path here.
     *
     * PLACED AT THE END of pcb_t, after fs_base, so no offset that
     * context_switch.asm reads (which stops at block_kind, 0x158)
     * moves.  The _Static_assert block in process.c pins everything
     * through block_kind; nothing after it is asserted, so this is
     * safe to append.
     *
     * SCOPE: this field is written by sys_chdir and read by
     * sys_getcwd ONLY.  The path-resolution syscalls (sys_open,
     * sys_stat, sys_access, sys_execve) do NOT yet resolve relative
     * paths against it -- they still treat every path as
     * root-relative.  Threading cwd through them is a follow-up;
     * see docs/open-issues.md.
     */
    char cwd[PROC_CWD_MAX];
} pcb_t;

#define KERNEL_STACK_SLOT_NONE (-1)

// Function prototypes
void process_init(void);
pcb_t* process_create(const char* name, uint64_t entry_point, uint64_t flags);
pcb_t* process_get_current(void);
void process_set_current(pcb_t* proc);
pcb_t* process_find_by_pid(uint64_t pid);
void process_dump_all(void);
void process_test_clone(void);
void process_start(pcb_t* process);
void process_destroy(pcb_t* process);
void process_cleanup_elf_pages(pcb_t* pcb);
/*
 * Build the child's resume frame for fork(2).
 *
 * The parent called fork() and is currently in a syscall.  Its
 * callee-saved registers are on the parent's kernel stack, pushed by
 * user_syscall_entry.asm in this order (relative to kernel_stack_top):
 *
 *   [top -  8] = rbx
 *   [top - 16] = rbp
 *   [top - 24] = r12
 *   [top - 32] = r13
 *   [top - 40] = r14
 *   [top - 48] = r15
 *   [top - 56] = user RIP   (from RCX at syscall entry)
 *   [top - 64] = user RFLAGS (from R11)
 *   [top - 72] = user RSP   (from g_user_rsp_save)
 *
 * The child's frame is built at child->kernel_stack_top - 0xA0, in
 * the same 20-slot iretq-resumable layout that process_create builds
 * and that context_switch.asm restores.  The child's %rax is 0, so
 * fork() returns 0 in the child.
 *
 * Only the callee-saved registers are copied from the parent's
 * frame.  The caller-saved registers (rax, rcx, rdx, rsi, rdi, r8,
 * r9, r10, r11) were either clobbered by the syscall argument
 * shuffling or are the caller's responsibility to save.  This is a
 * deliberate first-cut limitation: it differs from Linux, which
 * preserves all registers except %rax.  If a future test fails
 * because a register has an unexpected value after fork, this is
 * the first place to look.
 */
void process_fork_copy_frame(pcb_t* child, pcb_t* parent);
void process_reclaim(pcb_t* pcb);
void process_exit(void) __attribute__((noreturn));
void kernel_idle_loop(void);
void process_wake_all_blocked(void);

/*
 * Wake the parent of `child` if it is blocked in waitpid on this
 * child (or on any child, which is wait_pid == (uint64_t)-1).
 * Called from process_exit before the exiting process becomes a
 * zombie.  Returns the parent's PCB if it was woken, or NULL.
 */
pcb_t* process_wake_parent_if_waiting(pcb_t* child);

/*
 * Kernel shell accessors.
 *
 * The kernel shell is a real kernel-mode process created at boot (on
 * the 'k' branch of the boot prompt). Its PCB is stored here so that
 * process_exit's fallback can resume it after a kernel diagnostic
 * process exits. Before the shell is created, the pointer is NULL.
 *
 * The shell is marked BLOCKED when it yields to another process (the
 * user shell), so it is not picked up by the scheduler until someone
 * explicitly wakes it.
 */
pcb_t* process_get_kernel_shell(void);
void   process_set_kernel_shell(pcb_t* shell);

#endif
