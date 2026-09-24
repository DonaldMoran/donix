#include "include/process.h"
#include "include/vmm.h"
#include "include/pmm.h"
#include "include/serial.h"
#include "include/vga.h"
#include "include/scheduler.h"
#include "include/heap.h"
#include "include/user_space.h"
#include "include/elf.h"
#include <string.h>
#include <stddef.h>

#define DBG 0

static uint8_t kernel_stack_pool[MAX_PROCESSES][PROC_STACK_SIZE] __attribute__((aligned(16)));
static pcb_t* slot_owner[MAX_PROCESSES];
static pcb_t pcb_pool[MAX_PROCESSES];
static pcb_t* current_process = NULL;
static uint64_t next_pid = 1;
static uint64_t process_count = 0;

/* The kernel shell's PCB. Set once at boot on the 'k' branch.
 * Resumed by process_exit's fallback after a kernel diagnostic exits. */
static pcb_t* g_kernel_shell_pcb = NULL;

static pcb_t* get_free_pcb(void);
static void process_initialize_pcb(pcb_t* pcb);

#define KERNEL_BASE 0xFFFFFFFF80000000ULL

static int kernel_stack_slot_alloc(pcb_t* pcb) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (slot_owner[i] == NULL) {
            slot_owner[i] = pcb;
            return i;
        }
    }
    return KERNEL_STACK_SLOT_NONE;
}

static void kernel_stack_slot_free(pcb_t* pcb) {
    if (!pcb) return;
    int slot = pcb->kernel_stack_slot;
    if (slot < 0 || slot >= MAX_PROCESSES) return;
    if (slot_owner[slot] == pcb) {
        slot_owner[slot] = NULL;
    }
    pcb->kernel_stack_slot = KERNEL_STACK_SLOT_NONE;
    pcb->kernel_stack_phys = 0;
    pcb->kernel_stack_virt = 0;
    pcb->kernel_stack_top  = 0;
}

pcb_t* process_get_kernel_shell(void) {
    return g_kernel_shell_pcb;
}

void process_set_kernel_shell(pcb_t* shell) {
    g_kernel_shell_pcb = shell;
}

void kernel_idle_loop(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void process_init(void) {
    memset(pcb_pool, 0, sizeof(pcb_pool));
    for (int i = 0; i < MAX_PROCESSES; i++) {
        slot_owner[i] = NULL;
    }

    pcb_t* idle = process_create("idle", (uint64_t)kernel_idle_loop, 0);
    if (idle) {
        idle->state = PROC_STATE_READY;
        current_process = idle;
        scheduler_set_current(idle);

        extern void tss_set_syscall_stack(uint64_t stack);
        tss_set_syscall_stack(idle->kernel_stack_top);

        extern void tss_set_kernel_stack(uint64_t stack);
        tss_set_kernel_stack(idle->kernel_stack_top);

        scheduler_ready_queue_remove(idle);
    }

    serial_lock();
    serial_print("PROCESS: init OK, ");
    serial_print_dec(process_count);
    serial_print(" process(es)\n");
    serial_unlock();
}

static pcb_t* get_free_pcb(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pcb_pool[i].state == PROC_STATE_UNUSED) {
            return &pcb_pool[i];
        }
    }
    serial_print("PROCESS: No free PCB slots!\n");
    return NULL;
}

static void process_initialize_pcb(pcb_t* pcb) {
    memset(pcb, 0, sizeof(pcb_t));
    pcb->pid = next_pid++;
    pcb->state = PROC_STATE_UNUSED;
    pcb->elf_page_list = NULL;
    pcb->elf_num_pages = 0;
    pcb->kernel_stack_slot = KERNEL_STACK_SLOT_NONE;
    pcb->parent_pid = 0;
    pcb->exit_status = 0;
    pcb->wait_pid = 0;
    process_count++;
}

pcb_t* process_create(const char* name, uint64_t entry_point, uint64_t flags) {
    (void)flags;

    pcb_t* pcb = get_free_pcb();
    if (!pcb) {
        serial_lock();
        serial_print("PROCESS: Failed to allocate PCB for ");
        if (name) serial_print(name);
        else serial_print("unnamed");
        serial_print("\n");
        serial_unlock();
        return NULL;
    }

    process_initialize_pcb(pcb);

    if (name) {
        strncpy(pcb->name, name, PROC_NAME_LEN - 1);
        pcb->name[PROC_NAME_LEN - 1] = '\0';
    } else {
        pcb->name[0] = 'p'; pcb->name[1] = 'r'; pcb->name[2] = 'o'; pcb->name[3] = 'c'; pcb->name[4] = '_';
        uint64_t pid = pcb->pid;
        int pos = 5; char temp[16]; int len = 0;
        if (pid == 0) temp[len++] = '0';
        else {
            while (pid > 0) {
                temp[len++] = '0' + (pid % 10);
                pid /= 10;
            }
        }
        for (int i = 0; i < len; i++) {
            pcb->name[pos + i] = temp[len - 1 - i];
        }
        pcb->name[pos + len] = '\0';
    }

    pcb->entry_point = entry_point;
    pcb->state = PROC_STATE_READY;

    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    pcb->cr3 = vmm_clone_page_table(current_cr3);

    if (entry_point != 0 && entry_point < KERNEL_BASE) {
        #define USER_STACK_PAGES 16
        #define USER_STACK_SIZE (USER_STACK_PAGES * 4096)

        uint64_t stack_top_anchor = 0x8000100000ULL;
        uint64_t stack_bottom_anchor = stack_top_anchor - USER_STACK_SIZE;

        pcb->user_stack_virt = stack_bottom_anchor;

        for (int i = 0; i < USER_STACK_PAGES; i++) {
            uint64_t phys = pmm_alloc_page_for_elf();
            if (!phys) {
                serial_print("PROCESS: Failed to allocate user stack page\n");
                return NULL;
            }

            uint64_t virt = stack_bottom_anchor + (i * 4096);
            uint64_t map_flags = PT_PRESENT | PT_WRITE | PT_USER;
            map_flags &= ~(0x80ULL | 0x40ULL | 0x200ULL | 0x800ULL);

            vmm_map_page_in_cr3(pcb->cr3, virt, phys, map_flags);

            void* hhdm = (void*)(HHDM_START + phys);
            for (uint64_t j = 0; j < 4096 / 8; j++) {
                ((uint64_t*)hhdm)[j] = 0;
            }

            elf_add_page_to_pcb(pcb, phys);

            if (i == (USER_STACK_PAGES - 1)) {
                pcb->user_stack_phys = phys;
            }
        }

        pcb->user_stack_top = stack_top_anchor - 32;
        pcb->user_stack_top &= ~0xFULL;
    } else {
        pcb->user_stack_phys = 0;
        pcb->user_stack_virt = 0;
        pcb->user_stack_top = 0;
    }

    int slot = kernel_stack_slot_alloc(pcb);
    if (slot == KERNEL_STACK_SLOT_NONE) {
        serial_print("PROCESS: kernel stack pool exhausted\n");
        pcb->state = PROC_STATE_UNUSED;
        pcb->pid = 0;
        process_count--;
        return NULL;
    }
    pcb->kernel_stack_slot = slot;
    pcb->kernel_stack_virt = (uint64_t)&kernel_stack_pool[slot];
    pcb->kernel_stack_phys = vmm_get_phys(pcb->kernel_stack_virt);
    pcb->kernel_stack_top  = pcb->kernel_stack_virt + PROC_STACK_SIZE;

    ensure_hhdm_mapped(pcb->kernel_stack_phys);
    if (pcb->user_stack_phys) {
        ensure_hhdm_mapped(pcb->user_stack_phys);
    }

    uint64_t* stack_ptr = (uint64_t*)pcb->kernel_stack_top;
    int is_user = (entry_point != 0 && entry_point < KERNEL_BASE);

    uint64_t frame_ss    = is_user ? 0x2BULL : 0x20ULL;
    uint64_t frame_rsp   = is_user ? pcb->user_stack_top : pcb->kernel_stack_top;
    uint64_t frame_flags = 0x202ULL;
    uint64_t frame_cs    = is_user ? 0x33ULL : 0x18ULL;
    uint64_t frame_rip   = entry_point;

    stack_ptr--; *stack_ptr = frame_ss;
    stack_ptr--; *stack_ptr = frame_rsp;
    stack_ptr--; *stack_ptr = frame_flags;
    stack_ptr--; *stack_ptr = frame_cs;
    stack_ptr--; *stack_ptr = frame_rip;

    stack_ptr--; *stack_ptr = 0; // rax
    stack_ptr--; *stack_ptr = 0; // rbx
    stack_ptr--; *stack_ptr = 0; // rcx
    stack_ptr--; *stack_ptr = 0; // rdx
    stack_ptr--; *stack_ptr = 0; // rsi
    stack_ptr--; *stack_ptr = 0; // rdi
    stack_ptr--; *stack_ptr = 0; // rbp
    stack_ptr--; *stack_ptr = 0; // r8
    stack_ptr--; *stack_ptr = 0; // r9
    stack_ptr--; *stack_ptr = 0; // r10
    stack_ptr--; *stack_ptr = 0; // r11
    stack_ptr--; *stack_ptr = 0; // r12
    stack_ptr--; *stack_ptr = 0; // r13
    stack_ptr--; *stack_ptr = 0; // r14
    stack_ptr--; *stack_ptr = 0; // r15

    pcb->rsp = (uint64_t)stack_ptr;
    pcb->rip = entry_point;

    pcb->next = NULL; pcb->prev = NULL;
    pcb->timeslice_ticks = 0; pcb->total_ticks = 0;

    /* Initialize process file descriptor slots to NULL */
    for (int i = 0; i < MAX_PROCESS_FILES; i++) {
        pcb->file_table[i] = NULL;
    }

    scheduler_ready_queue_add(pcb);
    return pcb;
}

/*
 * Build the child's resume frame for fork(2).
 *
 * See the declaration in include/process.h for the layout.  Short
 * version: the child resumes via iretq at the parent's user RIP and
 * RSP with %rax = 0, as if the parent's fork() syscall had returned
 * 0 in the child.
 *
 * The parent's kernel stack top is the current %rsp of the caller
 * (which is running on the parent's kernel stack inside the
 * syscall).  We read the parent's pushed registers from there.
 *
 * Do NOT read parent->kernel_stack_top and assume the frame is at
 * the top.  The syscall entry path pushes 16 slots and then calls
 * syscall_dispatch, which pushes more.  By the time this function
 * runs, the parent's %rsp is somewhere below kernel_stack_top.
 * We use kernel_stack_top as the anchor only because the syscall
 * entry frame was built by pushes starting from that address, and
 * the values we need are at fixed offsets from it.
 *
 * Frame layout, offsets from parent->kernel_stack_top, as built by
 * user_syscall_entry.asm (first push = offset -8):
 *
 *   -8   rbx
 *   -16  rbp
 *   -24  r12
 *   -32  r13
 *   -40  r14
 *   -48  r15
 *   -56  user RIP      (from CPU rcx at syscall)
 *   -64  user RFLAGS   (from CPU r11 at syscall)
 *   -72  user RSP      (saved before stack switch)
 *   -80  r8            (saved before arg shuffle)
 *   -88  r9            (saved before arg shuffle)
 *   -96  rdi           (saved before arg shuffle)
 *   -104 rsi           (saved before arg shuffle)
 *   -112 rdx           (saved before arg shuffle)
 *   -120 r10           (saved before arg shuffle)
 *   -128 arg5          (pushed for the C call, discarded at return)
 *
 * The parent's return path pops only through -120 (rdi/rsi/rdx/r10
 * are skipped with add rsp, 32) and does not restore them.  The
 * only consumer of those four slots is this function: a fork child
 * needs the parent's pre-syscall values for all GPRs except %rax
 * (0), %rcx (user RIP) and %r11 (user RFLAGS), the way Linux's
 * syscall ABI promises.
 */
void process_fork_copy_frame(pcb_t* child, pcb_t* parent) {
    if (!child || !parent) return;

    uint64_t ptop = parent->kernel_stack_top;

    /* Registers the parent's syscall-return path preserves, plus the
       four extra GPRs saved at entry for the child's benefit. */
    uint64_t p_rbx   = *(uint64_t*)(ptop - 8);
    uint64_t p_rbp   = *(uint64_t*)(ptop - 16);
    uint64_t p_r12   = *(uint64_t*)(ptop - 24);
    uint64_t p_r13   = *(uint64_t*)(ptop - 32);
    uint64_t p_r14   = *(uint64_t*)(ptop - 40);
    uint64_t p_r15   = *(uint64_t*)(ptop - 48);
    uint64_t p_rip   = *(uint64_t*)(ptop - 56);
    uint64_t p_flags = *(uint64_t*)(ptop - 64);
    uint64_t p_rsp   = *(uint64_t*)(ptop - 72);
    uint64_t p_r8    = *(uint64_t*)(ptop - 80);
    uint64_t p_r9    = *(uint64_t*)(ptop - 88);
    uint64_t p_rdi   = *(uint64_t*)(ptop - 96);
    uint64_t p_rsi   = *(uint64_t*)(ptop - 104);
    uint64_t p_rdx   = *(uint64_t*)(ptop - 112);
    uint64_t p_r10   = *(uint64_t*)(ptop - 120);

    uint64_t frame_base = child->kernel_stack_top - 20 * 8;
    uint64_t* f = (uint64_t*)frame_base;

    f[0x00 / 8] = p_r15;
    f[0x08 / 8] = p_r14;
    f[0x10 / 8] = p_r13;
    f[0x18 / 8] = p_r12;
    f[0x20 / 8] = p_flags;   /* r11: user RFLAGS (from r11 at syscall entry) */
    f[0x28 / 8] = p_r10;     /* r10: parent's pre-syscall value */
    f[0x30 / 8] = p_r9;      /* r9:  preserved by entry/exit */
    f[0x38 / 8] = p_r8;      /* r8:  preserved by entry/exit */
    f[0x40 / 8] = p_rbp;
    f[0x48 / 8] = p_rdi;     /* rdi: parent's pre-syscall value */
    f[0x50 / 8] = p_rsi;     /* rsi: parent's pre-syscall value */
    f[0x58 / 8] = p_rdx;     /* rdx: parent's pre-syscall value */
    f[0x60 / 8] = p_rip;     /* rcx slot: user RIP */
    f[0x68 / 8] = p_rbx;
    f[0x70 / 8] = 0;         /* rax: fork() returns 0 in the child */
    f[0x78 / 8] = p_rip;     /* RIP */
    f[0x80 / 8] = 0x33;      /* CS: user code segment */
    f[0x88 / 8] = p_flags;   /* RFLAGS */
    f[0x90 / 8] = p_rsp;     /* RSP: parent's user stack pointer */
    f[0x98 / 8] = 0x2B;      /* SS: user data segment */

    child->rsp = frame_base;
    child->rip = p_rip;
}

pcb_t* process_get_current(void) {
    return current_process;
}

void process_set_current(pcb_t* proc) {
    current_process = proc;
}

pcb_t* process_find_by_pid(uint64_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pcb_pool[i].pid == pid && pcb_pool[i].state != PROC_STATE_UNUSED) {
            return &pcb_pool[i];
        }
    }
    return NULL;
}

/*
 * ============================================================
 * DIAGNOSTIC: dump every BLOCKED process.
 * Added 2026-09-24 evening to chase the intermittent
 * dead-keyboard bug after musl_fork.
 *
 * Safe to call from IRQ context on a single CPU: serial_lock is
 * cli-based (or a nesting counter), so re-entering it from IRQ1
 * when the main line is not printing is fine.
 * ============================================================
 */
void process_debug_dump_blocked(const char* tag) {
    serial_lock();
    serial_print("[");
    serial_print(tag);
    serial_print("] BLOCKED dump:\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t* p = &pcb_pool[i];
        if (p->state == PROC_STATE_UNUSED) continue;
        if (p->state != PROC_STATE_BLOCKED) continue;

        serial_print("  pid=");
        serial_print_dec(p->pid);
        serial_print(" name=");
        serial_print(p->name);
        serial_print(" kind=");
        serial_print_dec(p->block_kind);
        serial_print(" wait_pid=");
        serial_print_dec(p->wait_pid);
        serial_print(" onq=");
        serial_print_dec(scheduler_ready_queue_contains(p) ? 1 : 0);
        serial_print(" kstack_top=0x");
        serial_print_hex(p->kernel_stack_top);
        serial_print("\n");
    }
    serial_print("[/");
    serial_print(tag);
    serial_print("]\n");
    serial_unlock();
}

/*
 * Wake every process blocked on input (BLOCKED).
 *
 * The kernel shell is deliberately excluded. Its BLOCKED state means
 * "suspended pending a diagnostic's exit", not "waiting for a key".
 * Waking it from irq1 causes it to resume at the same time as the
 * user shell, which is how the earlier interleaved-banner bug
 * happened. The shell is resumed only by process_exit's fallback.
 *
 * Processes blocked in waitpid are also skipped: they are waiting
 * for a child, not for a key.  The child's process_exit wakes them.
 */
void process_wake_all_blocked(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (&pcb_pool[i] == g_kernel_shell_pcb) continue;
        if (pcb_pool[i].state != PROC_STATE_BLOCKED) continue;
        if (pcb_pool[i].block_kind != BLOCK_KIND_NONE) continue;

        /*
         * Skip if already on the ready queue.
         *
         * sys_read's blocking loop sets state=BLOCKED and hlt's,
         * and a timer tick can run this function before the hlt
         * returns.  Without this check, the process is added to
         * the queue on every tick while it is still inside the
         * hlt, corrupting the queue (prev/next overwritten, self-
         * links).  The symptom was a fork child being added behind
         * the shell in the ready queue, so the child never ran
         * before the parent's zombie wake resumed the shell.
         *
         * scheduler_ready_queue_add is also idempotent now, but
         * checking here keeps the state transitions clear and
         * avoids the redundant call.
         */
        /* --- DIAGNOSTIC (added 2026-09-24 evening) --- */
        {
            int was_onq = scheduler_ready_queue_contains(&pcb_pool[i]) ? 1 : 0;
            serial_lock();
            serial_print("WAKE: pid=");
            serial_print_dec(pcb_pool[i].pid);
            serial_print(" name=");
            serial_print(pcb_pool[i].name);
            serial_print(" kind=");
            serial_print_dec(pcb_pool[i].block_kind);
            serial_print(" onq=");
            serial_print_dec(was_onq);
            serial_print("\n");
            serial_unlock();
            if (was_onq) {
                pcb_pool[i].state = PROC_STATE_READY;
                continue;
            }
        }
        /* --- end DIAGNOSTIC --- */

        pcb_pool[i].state = PROC_STATE_READY;
        scheduler_ready_queue_add(&pcb_pool[i]);
    }
}

/*
 * Wake the parent of `child` if it is blocked in waitpid on this
 * child, or on any child (wait_pid == (uint64_t)-1).
 *
 * Called from process_exit after the child has been marked ZOMBIE
 * and removed from the ready queue, and before the scheduler picks
 * the next process.  The parent must already be in
 * PROC_STATE_BLOCKED with block_kind == BLOCK_KIND_WAITPID.
 *
 * Returns the parent PCB if it was woken (and re-added to the ready
 * queue), or NULL if the parent was not blocked on this child.
 */
pcb_t* process_wake_parent_if_waiting(pcb_t* child) {
    if (!child || child->parent_pid == 0) return NULL;

    pcb_t* parent = process_find_by_pid(child->parent_pid);
    if (!parent) return NULL;
    if (parent->state != PROC_STATE_BLOCKED) return NULL;
    if (parent->block_kind != BLOCK_KIND_WAITPID) return NULL;

    /* Match: parent->wait_pid == child->pid, or == (uint64_t)-1
       (wait for any child). */
    if (parent->wait_pid != (uint64_t)-1 &&
        parent->wait_pid != child->pid) {
        return NULL;
    }

    parent->state = PROC_STATE_READY;
    parent->block_kind = BLOCK_KIND_NONE;
    parent->wait_pid = 0;
    scheduler_ready_queue_add(parent);
    return parent;
}

void process_dump_all(void) {
    serial_lock();
    serial_print("\n=== PROCESS LIST ===\n");
    serial_print("PID  Name                State    Entry     Kernel Stack\n");
    serial_print("---  -------------------  -------  ----------  ----------\n");
    serial_unlock();

    vga_print("=== PROCESS LIST ===\n");
    vga_print("PID  Name                State    Entry       Kernel Stack\n");
    vga_print("---  -------------------  -------  ----------  ----------\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t* p = &pcb_pool[i];
        if (p->state == PROC_STATE_UNUSED) continue;

        const char* state_str;
        if (p->state == PROC_STATE_READY && p->pid != 1 && p->prev == NULL && p->next == NULL) {
            state_str = "DETACHED";
        } else {
            switch (p->state) {
                case PROC_STATE_READY: state_str = "READY"; break;
                case PROC_STATE_RUNNING: state_str = "RUNNING"; break;
                case PROC_STATE_BLOCKED: state_str = "BLOCKED"; break;
                case PROC_STATE_ZOMBIE: state_str = "ZOMBIE"; break;
                case PROC_STATE_TERMINATED: state_str = "TERMINATED"; break;
                default: state_str = "UNKNOWN"; break;
            }
        }

        serial_lock();
        serial_print_dec(p->pid); serial_print("  ");
        serial_print(p->name);
        int len = strlen(p->name);
        for (int j = len; j < 18; j++) serial_print(" ");
        serial_print("  ");
        serial_print(state_str); serial_print("  ");
        serial_print("0x"); serial_print_hex(p->entry_point); serial_print("  ");
        serial_print("0x"); serial_print_hex(p->kernel_stack_top);
        serial_print("\n");
        serial_unlock();

        vga_print_dec_cur(p->pid); vga_print("  ");
        vga_print(p->name);
        for (int j = len; j < 18; j++) vga_print(" ");
        vga_print("  ");
        vga_print(state_str);
        int state_len = strlen(state_str);
        for (int j = state_len; j < 8; j++) vga_print(" ");
        vga_print(" 0x"); vga_print_hex_cur(p->entry_point);
        vga_print(" 0x"); vga_print_hex_cur(p->kernel_stack_top);
        vga_print("\n");
    }

    serial_lock();
    serial_print("=== END PROCESS LIST ===\n\n");
    serial_unlock();

    vga_print("=== END PROCESS LIST ===\n");
}

void process_test_clone(void) {
    vga_print("\n=== Virtual Memory Manager Page Table Clone Test ===\n");
    serial_print("\n=== Virtual Memory Manager Page Table Clone Test ===\n");

    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uint64_t clean_src_cr3 = current_cr3 & ~0xFFFULL;

    vga_print("  1. Source Paging Table Root (CR3) : 0x"); vga_print_hex_cur(clean_src_cr3); vga_print("\n");
    serial_print("  1. Source Paging Table Root (CR3) : 0x"); serial_print_hex(clean_src_cr3); serial_print("\n");

    uint64_t new_cr3 = vmm_clone_page_table(current_cr3);
    uint64_t clean_new_cr3 = new_cr3 & ~0xFFFULL;

    if (new_cr3 == 0) {
        vga_print("  [ERR] Page Table Cloning routine FAILED to allocate frames!\n");
        serial_print("  [ERR] Page Table Cloning routine FAILED to allocate frames!\n");
        return;
    }

    vga_print("  2. Cloned Paging Table Root (CR3) : 0x"); vga_print_hex_cur(clean_new_cr3); vga_print("\n");
    serial_print("  2. Cloned Paging Table Root (CR3) : 0x"); serial_print_hex(clean_new_cr3); serial_print("\n");

    uint64_t* new_pml4 = (uint64_t*)ensure_hhdm_mapped(clean_new_cr3);
    uint64_t recursive_entry = new_pml4[RECURSIVE_PML4_INDEX];

    vga_print("  3. Probing Cloned PML4 Entry: 0x"); vga_print_hex_cur(recursive_entry); vga_print("\n");
    serial_print("  3. Probing Cloned PML4 Entry: 0x"); serial_print_hex(recursive_entry); serial_print("\n");

    if (recursive_entry & PT_PRESENT) {
        uint64_t recursive_phys = recursive_entry & ~0xFFFULL;
        if (recursive_phys == clean_new_cr3) {
            vga_print("  Status: SUCCESS! Recursive mapping links back to the new root perfectly.\n");
            serial_print("  Status: SUCCESS! Recursive mapping links back to the new root perfectly.\n");
        } else {
            vga_print("  Status: FAILED! Recursive entry points to a stale memory root.\n");
            serial_print("  Status: FAILED! Recursive entry points to a stale memory root.\n");
        }
    } else {
        vga_print("  Status: FAILED! Recursive index entry flag is marked NOT PRESENT.\n");
        serial_print("  Status: FAILED! Recursive index entry flag is marked NOT PRESENT.\n");
    }

    extern void pmm_free_page(uint64_t phys_addr);
    pmm_free_page(clean_new_cr3);

    vga_print("  4. Testing sandbox page tables released back to PMM cleanly.\n");
    serial_print("  4. Testing sandbox page tables released back to PMM cleanly.\n");
}

void process_start(pcb_t* process) {
    if (!process) return;
    scheduler_switch_to(process);
}

void process_cleanup_elf_pages(pcb_t* pcb) {
    if (!pcb || !pcb->elf_page_list) return;
    for (uint64_t i = 0; i < pcb->elf_num_pages; i++) {
        uint64_t phys = pcb->elf_page_list[i];
        if (phys) pmm_free_page(phys);
    }
    kfree(pcb->elf_page_list);
    pcb->elf_page_list = NULL;
    pcb->elf_num_pages = 0;
}

void process_reclaim(pcb_t* pcb) {
    if (!pcb) return;
    if (pcb->state == PROC_STATE_UNUSED) return;

    process_cleanup_elf_pages(pcb);
    pcb->user_stack_phys = 0;
    kernel_stack_slot_free(pcb);

    pcb->state = PROC_STATE_UNUSED;
    pcb->pid = 0;
    process_count--;
}

void process_destroy(pcb_t* pcb) {
    if (!pcb || pcb->state == PROC_STATE_UNUSED) return;
    process_cleanup_elf_pages(pcb);
    pcb->user_stack_phys = 0;
    kernel_stack_slot_free(pcb);
    pcb->state = PROC_STATE_UNUSED;
    pcb->pid = 0;
    process_count--;
    scheduler_ready_queue_remove(pcb);
    if (current_process == pcb) current_process = NULL;
    serial_print("PROCESS: Process destroyed\n");
}

/* ABI LOCK COMPLIANCE VERIFICATION ASSERTS */
_Static_assert(offsetof(pcb_t, cr3)              == 0x030, "context_switch.asm: cr3 offset");
_Static_assert(offsetof(pcb_t, entry_point)      == 0x038, "context_switch.asm: entry_point offset");
_Static_assert(offsetof(pcb_t, user_stack_top)   == 0x070, "context_switch.asm: user_stack_top offset");
_Static_assert(offsetof(pcb_t, r15)              == 0x098, "context_switch.asm: r15 offset");
_Static_assert(offsetof(pcb_t, r14)              == 0x0A0, "context_switch.asm: r14 offset");
_Static_assert(offsetof(pcb_t, r13)              == 0x0A8, "context_switch.asm: r13 offset");
_Static_assert(offsetof(pcb_t, r12)              == 0x0B0, "context_switch.asm: r12 offset");
_Static_assert(offsetof(pcb_t, r11)              == 0x0B8, "context_switch.asm: r11 offset");
_Static_assert(offsetof(pcb_t, r10)              == 0x0C0, "context_switch.asm: r10 offset");
_Static_assert(offsetof(pcb_t, r9)               == 0x0C8, "context_switch.asm: r9 offset");
_Static_assert(offsetof(pcb_t, r8)               == 0x0D0, "context_switch.asm: r8 offset");
_Static_assert(offsetof(pcb_t, rbp)              == 0x0D8, "context_switch.asm: rbp offset");
_Static_assert(offsetof(pcb_t, rdi)              == 0x0E0, "context_switch.asm: rdi offset");
_Static_assert(offsetof(pcb_t, rsi)              == 0x0E8, "context_switch.asm: rsi offset");
_Static_assert(offsetof(pcb_t, rdx)              == 0x0F0, "context_switch.asm: rdx offset");
_Static_assert(offsetof(pcb_t, rcx)              == 0x0F8, "context_switch.asm: rcx offset");
_Static_assert(offsetof(pcb_t, rbx)              == 0x100, "context_switch.asm: rbx offset");
_Static_assert(offsetof(pcb_t, rax)              == 0x108, "context_switch.asm: rax offset");
_Static_assert(offsetof(pcb_t, rsp)              == 0x110, "context_switch.asm: rsp offset");
_Static_assert(offsetof(pcb_t, rip)              == 0x118, "context_switch.asm: rip offset");
_Static_assert(offsetof(pcb_t, block_kind)       == 0x158, "context_switch.asm: block_kind offset");
