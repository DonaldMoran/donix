#include <stdint.h>
#include "include/vga.h"
#include "include/keyboard.h"
#include "include/interrupts.h"
#include "include/serial.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/user_msr.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20

#define PIT_CH0      0x40
#define PIT_CMD      0x43
#define PIT_MODE     0x36

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

/* Boot fingerprint: dump the raw interrupt frame for the first few
   ticks so we can sanity-check the initial timer interceptions. */
// #define TIMER_BOOT_TRACE_TICKS 3
#define TIMER_BOOT_TRACE_TICKS 0

/*
 * Exception frame layout, as seen by C after the stub has pushed all GPRs:
 *
 *   offset 0x00: r15      }
 *   offset 0x08: r14      }
 *   offset 0x10: r13      }
 *   offset 0x18: r12      }
 *   offset 0x20: r11      }
 *   offset 0x28: r10      }  15 GPRs pushed by PUSH_ALL_GPRS
 *   offset 0x30: r9       }  (120 bytes total)
 *   offset 0x38: r8       }
 *   offset 0x40: rbp      }
 *   offset 0x48: rdi      }
 *   offset 0x50: rsi      }
 *   offset 0x58: rdx      }
 *   offset 0x60: rcx      }
 *   offset 0x68: rbx      }
 *   offset 0x70: rax      }
 *   offset 0x78: error_code   <- pushed by CPU (for #GP, #PF, #DF)
 *   offset 0x80: rip          <- pushed by CPU
 *   offset 0x88: cs           <- pushed by CPU
 *   offset 0x90: rflags       <- pushed by CPU
 *   offset 0x98: rsp          <- pushed by CPU
 *   offset 0xA0: ss           <- pushed by CPU
 *
 * The old handlers read raw[0..4] as if the frame began at error_code,
 * which meant they printed r15/r14/r13/r11 instead. That is why the #GP
 * dump showed "RIP=0x0 CS=0x0 RSP=0x7FFF" regardless of the real fault.
 * The fix is to index past the GPR save area.
 */
#define EXC_OFF_ERROR_CODE 15
#define EXC_OFF_RIP        16
#define EXC_OFF_CS         17
#define EXC_OFF_RFLAGS     18
#define EXC_OFF_RSP        19
#define EXC_OFF_SS         20

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) preempt_frame_t;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void pic_remap(void) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    outb(PIC1_DATA, 0x20);   /* master vector offset 32 */
    outb(PIC2_DATA, 0x28);   /* slave vector offset 40 */

    outb(PIC1_DATA, 0x04);   /* slave on IRQ2 */
    outb(PIC2_DATA, 0x02);   /* cascade identity */

    outb(PIC1_DATA, 0x01);   /* 8086 mode */
    outb(PIC2_DATA, 0x01);

    /*
     * Do NOT restore the BIOS mask values. The BIOS leaves IRQ14
     * unmasked because it uses the ATA controller during boot. If we
     * restore that mask, the first ATA command we send (IDENTIFY) makes
     * the controller assert IRQ14, the PIC forwards it as vector 46, and
     * the CPU takes a #GP because we have no gate at vector 46.
     *
     * Start from a known-good mask instead:
     *   master: unmask IRQ0 (PIT) and IRQ1 (keyboard) only
     *   slave:  mask everything (IRQ8-IRQ15, including IRQ14/15)
     */
    (void)a1;
    (void)a2;
    outb(PIC1_DATA, 0xFC);   /* 1111 1100 */
    outb(PIC2_DATA, 0xFF);   /* 1111 1111 */
}

volatile uint64_t g_ticks = 0;
static int g_shift = 0;
static int g_caps  = 0;

/*
 * Expected-fault protocol for the kernel self-test.  See interrupts.h
 * for the contract.  -1 means "no fault expected"; any other value is
 * the vector the self-test has deliberately triggered.  0 is a valid
 * vector (#DE) so it cannot be the sentinel.
 */
volatile int g_expect_fault   = -1;
volatile int g_fault_observed = -1;

/*
 * Terminate the current process from an expected fault.
 *
 * process_exit() is declared noreturn in process.h.  It disables
 * interrupts, removes the current process from the ready queue, marks
 * it TERMINATED, calls process_reclaim() to free its ELF pages and
 * kernel stack slot, and switches back to the kernel shell (when the
 * exiting process is a kernel-mode diagnostic — which is the case for
 * every faulttest child spawned by the self-test).  That is exactly
 * the behavior the self-test needs: the child that faulted is
 * destroyed, and the shell resumes at the point after the
 * scheduler_switch_to() that spawned it.
 *
 * The kernel shell reads g_fault_observed when it resumes and uses it
 * to decide pass/fail.  The reset of g_expect_fault here means a
 * subsequent unexpected fault will take the normal diagnostic-and-halt
 * path rather than being silently attributed to the previous test.
 */
void fault_kill_current(int vec) {
    g_fault_observed = vec;
    g_expect_fault   = -1;
    process_exit();
    __builtin_unreachable();
}

/* The timer preempt handler straddles an ABI boundary that the C compiler
   does not model: irq0_stub pushes a 15-GPR frame on the stack, calls
   this function with rdi = rsp, and then uses the return value in rax as
   the new rsp for the resume path (`mov rsp, rax; pop GPRs; iretq`).

   Historically this function had to be noinline for the contract to hold:
   clang would inline it at -O2 once it became small enough, and the frame
   layout between irq0_stub's pushes and the returned rsp would no longer
   match, producing intermittent user-mode #PF at CR2=0x7FF3xxxxxx inside
   newlib.

   As of the kernel-stack-on-syscall-entry change, the syscall path runs
   on the per-process kernel stack (the same stack TSS.RSP0 points at),
   so a timer that interrupts a syscall is a same-stack kernel-mode
   interrupt and never crosses this boundary with a user-mode frame. The
   noinline is now defensive rather than load-bearing; keep it anyway,
   because the cost is one CALL/RET per tick and the failure mode if it
   is ever needed again is nasty.

   Long-term: this boundary is still an implicit ABI between asm and C.
   The cleaner design is a small asm shim that does the frame save and
   the return-value RSP write itself, and calls the C handler with a
   normal prototype. Not done yet. */
uint64_t __attribute__((noinline))
timer_preempt_handler(uint64_t stack_pointer) {
    g_ticks++;
    #define SCHED_QUANTUM 2

    pcb_t* current = process_get_current();
    preempt_frame_t* frame = (preempt_frame_t*)stack_pointer;

    if (g_ticks <= TIMER_BOOT_TRACE_TICKS) {
        /* Note: this runs in interrupt context. serial_lock() disables
         * interrupts (redundant here, they're already off) and
         * increments the nesting counter, so a nested serial_lock from
         * a called function would be a no-op. */
        serial_lock();
        serial_print("TIMER[");
        serial_print_dec(g_ticks);
        serial_print("] frame=");
        serial_print_hex(stack_pointer);
        serial_print(" cs=");
        serial_print_hex(frame->cs);
        serial_print(" ss=");
        serial_print_hex(frame->ss);
        serial_print(" rip=");
        serial_print_hex(frame->rip);
        serial_print(" rsp=");
        serial_print_hex(frame->rsp);
        serial_print(" rflags=");
        serial_print_hex(frame->rflags);
        serial_print(" cur=");
        if (current) serial_print_dec(current->pid);
        else serial_print("NULL");
        serial_print("\n");
        serial_unlock();
    }

    if (!current || current->state == PROC_STATE_TERMINATED) {
        return stack_pointer;
    }

    current->total_ticks++;
    current->timeslice_ticks++;

    // =======================================================================
    // KERNEL-MODE INTERRUPT
    // =======================================================================
    if ((frame->cs & 3) == 0) {
        /* Any kernel-mode current is subject to the same logic idle
           has always used. For idle, the peek below usually finds a
           READY user task and forces a switch. For a non-idle
           kernel-mode process (e.g. a user shell sitting in
           sys_read's sti; hlt), the peek does the same thing: if
           something is READY, save this process's frame, force its
           quantum to expire, and let the quantum block below switch
           away. */

        // Check if there's a user task waiting.
        extern pcb_t* scheduler_ready_queue_peek_next(void);
        pcb_t* peek = scheduler_ready_queue_peek_next();
        if (!peek || peek->pid == 1) {
            return stack_pointer;
        }

        // Save the interrupt frame into the PCB so this process can
        // be resumed later, whether it's idle or a user process in
        // a syscall.
        current->r15 = frame->r15; current->r14 = frame->r14;
        current->r13 = frame->r13; current->r12 = frame->r12;
        current->r11 = frame->r11; current->r10 = frame->r10;
        current->r9  = frame->r9;  current->r8  = frame->r8;
        current->rbp = frame->rbp; current->rdi = frame->rdi;
        current->rsi = frame->rsi; current->rdx = frame->rdx;
        current->rcx = frame->rcx; current->rbx = frame->rbx;
        current->rax = frame->rax;
        current->rip = frame->rip;
        current->rsp = stack_pointer;

        current->timeslice_ticks = SCHED_QUANTUM;

    }

    if ((frame->cs & 3) == 3) {
        // User-mode interrupt: user tasks are always preemptible.
        current->timeslice_ticks = SCHED_QUANTUM;
    }

    if (current->timeslice_ticks >= SCHED_QUANTUM) {
        current->timeslice_ticks = 0;

        // For user-mode current, save its frame now.
        if ((frame->cs & 3) == 3) {
            current->r15 = frame->r15; current->r14 = frame->r14;
            current->r13 = frame->r13; current->r12 = frame->r12;
            current->r11 = frame->r11; current->r10 = frame->r10;
            current->r9  = frame->r9;  current->r8  = frame->r8;
            current->rbp = frame->rbp; current->rdi = frame->rdi;
            current->rsi = frame->rsi; current->rdx = frame->rdx;
            current->rcx = frame->rcx; current->rbx = frame->rbx;
            current->rax = frame->rax;
            current->rip = frame->rip;
            current->rsp = stack_pointer;
        }

        // Pick next task.
        extern pcb_t* scheduler_ready_queue_next(void);
        pcb_t* next = scheduler_ready_queue_next();
        if (!next) {
            if (current->pid == 1) {
                current->timeslice_ticks = 0;
                return stack_pointer;
            }
            next = process_find_by_pid(1);
            if (!next) {
                return stack_pointer;
            }
        }

        /* Only a RUNNING current goes back on the ready queue. A
           BLOCKED process must stay off it; that is what makes "a
           shell waiting for input does not prevent any other process
           from running" hold. A TERMINATED process is already removed. */
        if (current->pid != 1 && current->state == PROC_STATE_RUNNING) {
            current->state = PROC_STATE_READY;
            scheduler_ready_queue_add(current);
        }

        next->state = PROC_STATE_RUNNING;
        scheduler_set_current(next);

        /* Keep TSS.RSP0 and the syscall entry stack top in lockstep
           with `current`, unconditionally. The previous gate
           (entry_point < KERNEL_BASE) left g_syscall_stack_top at 0
           when the incoming process was a kernel-mode thread,
           breaking the invariant documented in tss.c. Both must
           point at the incoming process's kernel stack at every
           context switch, so a subsequent syscall or interrupt
           lands on the right stack. */
        extern void tss_set_kernel_stack(uint64_t stack);
        extern void tss_set_syscall_stack(uint64_t stack);
        tss_set_kernel_stack(next->kernel_stack_top);
        tss_set_syscall_stack(next->kernel_stack_top);

        /*
         * MSR_FS_BASE is per-process, not per-CPU.  musl sets it once
         * at startup via arch_prctl(ARCH_SET_FS) to its TLS base; if
         * we do not save/restore across context switches, the next
         * musl process to run clobbers the previous one's FS base, and
         * the previous one's next %fs-relative access (the errno load
         * in musl's fork wrapper) dereferences a stale or null pointer.
         * Observed as a user-mode #PF at CR2=0x34 inside fork() on the
         * second musl run from the same shell.
         *
         * Kernel processes have fs_base == 0 and do not use %fs-
         * relative addressing, so writing 0 is harmless for them.
         */
        current->fs_base = rdmsr(0xC0000100);
        wrmsr(0xC0000100, next->fs_base);

        __asm__ volatile("mov %0, %%cr3" : : "r"(next->cr3));

        return next->rsp;
    }

    return stack_pointer;
}

void irq1_handler(void) {
    uint8_t status = inb(KBD_STATUS);
    if (!(status & 0x01)) {
        outb(PIC1_CMD, PIC_EOI);
        return;
    }

    uint8_t sc = inb(KBD_DATA);
    switch (sc) {
        case 0x2A: case 0x36: g_shift = 1; outb(PIC1_CMD, PIC_EOI); return;
        case 0xAA: case 0xB6: g_shift = 0; outb(PIC1_CMD, PIC_EOI); return;
        case 0x3A: g_caps ^= 1; outb(PIC1_CMD, PIC_EOI); return;
    }

    char c = scancode_to_ascii(sc, g_shift, g_caps);
    if (c) {
        kbd_buffer_put(c);
        /* Wake any process blocked in sys_read. Currently a no-op:
           nothing sets PROC_STATE_BLOCKED yet. The call is wired so
           that when sys_read starts blocking, the wake path is in
           place. Do NOT context-switch here — the timer picks the
           woken process up on the next tick. */
        process_wake_all_blocked();
    }
    outb(PIC1_CMD, PIC_EOI);
}

/*
 * #DE — divide by zero.
 *
 * If the self-test has deliberately triggered this vector, take the
 * expected-fault path: record the vector and kill the current process
 * (which will be the faulttest child).  Otherwise print the diagnostic
 * and halt, as before.
 *
 * The frame argument is the top of the GPR save area, same layout as
 * isr13_handler / isr14_handler.  #DE does not use it; it is present
 * so the signature matches the stub's calling convention (isr0_stub
 * sets rdi = rsp before the call).
 */
void isr0_handler(exception_frame_t *frame) {
    (void)frame;
    if (g_expect_fault == 0x00) fault_kill_current(0x00);
    vga_print("\n*** DIVIDE BY ZERO EXCEPTION (#DE) ***\n");
    while (1) __asm__ volatile("hlt");
}

void isr1_handler(void) {
    vga_print("\n*** DEBUG EXCEPTION (#DB) ***\n");
    while (1) __asm__ volatile("hlt");
}

void pit_init(uint32_t freq) {
    uint32_t divisor = 1193180 / freq;
    outb(PIT_CMD, PIT_MODE);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

/*
 * #DF — double fault.
 *
 * The CPU pushes an error code (always 0), then RIP, CS, RFLAGS, RSP,
 * SS. The stub pushes all 15 GPRs before calling us, so the frame from
 * raw[0] is:
 *
 *   [0..14]  GPRs (r15 first, rax last)
 *   [15]     error code
 *   [16]     RIP
 *   [17]     CS
 *   [18]     RFLAGS
 *   [19]     RSP
 *   [20]     SS
 *
 * This handler runs on the IST1 stack (configured in tss.c and set on
 * the #DF gate in idt.c), so it can print even if the kernel stack was
 * the thing that faulted.
 */
void isr8_handler(exception_frame_t *frame) {
    uint64_t *raw = (uint64_t *)frame;
    uint64_t error_code = raw[EXC_OFF_ERROR_CODE];
    uint64_t fault_rip  = raw[EXC_OFF_RIP];
    uint64_t fault_cs   = raw[EXC_OFF_CS];
    uint64_t fault_rsp  = raw[EXC_OFF_RSP];
    uint64_t fault_ss   = raw[EXC_OFF_SS];

    /* Exception handlers run with interrupts off (or on an IST stack
     * with the same effect). serial_lock here is a formality — it makes
     * the handler consistent with the rest of the kernel's print sites
     * and future-proofs the handler if the gate's type_attr ever
     * changes to allow nested interrupts. */
    serial_lock();
    serial_print("\n!!! DOUBLE FAULT (#DF) !!!\n");
    serial_print("  Error Code : 0x"); serial_print_hex(error_code); serial_print("\n");
    serial_print("  RIP        : 0x"); serial_print_hex(fault_rip);  serial_print("\n");
    serial_print("  CS         : 0x"); serial_print_hex(fault_cs);   serial_print("\n");
    serial_print("  RSP        : 0x"); serial_print_hex(fault_rsp);  serial_print("\n");
    serial_print("  SS         : 0x"); serial_print_hex(fault_ss);   serial_print("\n");
    serial_unlock();

    vga_print("\n!!! DOUBLE FAULT (#DF) !!!\n");
    vga_print("  Error Code : 0x"); vga_print_hex_cur(error_code); vga_print("\n");
    vga_print("  RIP        : 0x"); vga_print_hex_cur(fault_rip);  vga_print("\n");
    vga_print("  CS         : 0x"); vga_print_hex_cur(fault_cs);  vga_print("\n");
    vga_print("  RSP        : 0x"); vga_print_hex_cur(fault_rsp); vga_print("\n");
    vga_print("  SS         : 0x"); vga_print_hex_cur(fault_ss);  vga_print("\n");

    while (1) __asm__ volatile("hlt");
}

void isr13_handler(exception_frame_t *frame) {
    if (g_expect_fault == 0x0D) fault_kill_current(0x0D);

    uint64_t *raw = (uint64_t *)frame;
    uint64_t error_code = raw[EXC_OFF_ERROR_CODE];
    uint64_t fault_rip  = raw[EXC_OFF_RIP];
    uint64_t fault_cs   = raw[EXC_OFF_CS];
    uint64_t fault_rsp  = raw[EXC_OFF_RSP];

    serial_lock();
    serial_print("\n=== GENERAL PROTECTION FAULT (#GP) ===\n");
    serial_print("  Faulting RIP : 0x"); serial_print_hex(fault_rip);  serial_print("\n");
    serial_print("  Code Seg (CS): 0x"); serial_print_hex(fault_cs);   serial_print("\n");
    serial_print("  Stack (RSP)  : 0x"); serial_print_hex(fault_rsp);  serial_print("\n");
    serial_print("  Error Code   : 0x"); serial_print_hex(error_code); serial_print("\n");
    serial_unlock();

    vga_print("\n=== GENERAL PROTECTION FAULT (#GP) ===\n");
    vga_print("  Faulting RIP : 0x"); vga_print_hex_cur(fault_rip);  vga_print("\n");
    vga_print("  Code Seg (CS): 0x"); vga_print_hex_cur(fault_cs);   vga_print("\n");
    vga_print("  Stack (RSP)  : 0x"); vga_print_hex_cur(fault_rsp);  vga_print("\n");
    vga_print("  Error Code   : 0x"); vga_print_hex_cur(error_code); vga_print("\n");

    {
        extern pcb_t* process_get_current(void);
        pcb_t* cur = process_get_current();
        if (cur) {
            serial_lock();
            serial_print("  Current PID : "); serial_print_dec(cur->pid); serial_print("\n");
            serial_print("  Name        : "); serial_print(cur->name); serial_print("\n");
            serial_print("  entry_point : 0x"); serial_print_hex(cur->entry_point); serial_print("\n");
            serial_print("  kernel_top  : 0x"); serial_print_hex(cur->kernel_stack_top); serial_print("\n");
            serial_print("  user_stack  : 0x"); serial_print_hex(cur->user_stack_top); serial_print("\n");
            serial_print("  saved rsp   : 0x"); serial_print_hex(cur->rsp); serial_print("\n");
            serial_print("  saved rip   : 0x"); serial_print_hex(cur->rip); serial_print("\n");
            serial_unlock();
        } else {
            serial_print("  Current PID : (null)\n");
        }
    }

    serial_lock();
    serial_print("  --- raw frame dump ---\n");
    for (int i = 0; i < 48; i++) {
        serial_print("    [");
        serial_print_dec(i);
        serial_print("] 0x");
        serial_print_hex(raw[i]);
        serial_print("\n");
    }
    serial_print("  --- end frame dump ---\n");
    serial_unlock();

    while (1) __asm__ volatile("hlt");
}

void isr14_handler(exception_frame_t *frame) {
    if (g_expect_fault == 0x0E) fault_kill_current(0x0E);

    uint64_t *raw = (uint64_t *)frame;
    uint64_t error_code = raw[EXC_OFF_ERROR_CODE];
    uint64_t fault_rip  = raw[EXC_OFF_RIP];
    uint64_t fault_cs   = raw[EXC_OFF_CS];
    uint64_t fault_rsp  = raw[EXC_OFF_RSP];
    uint64_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));

    serial_lock();
    serial_print("\n=== PAGE FAULT (#PF) ===\n");
    serial_print("  CR2 (Bad Address) : 0x"); serial_print_hex(fault_addr); serial_print("\n");
    serial_print("  Faulting RIP      : 0x"); serial_print_hex(fault_rip);  serial_print("\n");
    serial_print("  Raw Error Code    : 0x"); serial_print_hex(error_code); serial_print("\n");
    serial_print("  CS                : 0x"); serial_print_hex(fault_cs);   serial_print("\n");
    serial_print("  RSP               : 0x"); serial_print_hex(fault_rsp);  serial_print("\n");
    serial_unlock();

    vga_print("\n=== PAGE FAULT (#PF) ===\n");
    vga_print("  CR2 (Bad Address) : 0x"); vga_print_hex_cur(fault_addr); vga_print("\n");
    vga_print("  Faulting RIP      : 0x"); vga_print_hex_cur(fault_rip);  vga_print("\n");
    vga_print("  Raw Error Code    : 0x"); vga_print_hex_cur(error_code); vga_print("\n");

    {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

        uint64_t idx_pml4 = (fault_addr >> 39) & 0x1FF;
        uint64_t idx_pdpt = (fault_addr >> 30) & 0x1FF;
        uint64_t idx_pd   = (fault_addr >> 21) & 0x1FF;
        uint64_t idx_pt   = (fault_addr >> 12) & 0x1FF;

        serial_lock();
        serial_print("  CR3               : 0x"); serial_print_hex(cr3); serial_print("\n");
        serial_print("  Walk indices: pml4=");
        serial_print_dec(idx_pml4);
        serial_print(" pdpt=");
        serial_print_dec(idx_pdpt);
        serial_print(" pd=");
        serial_print_dec(idx_pd);
        serial_print(" pt=");
        serial_print_dec(idx_pt);
        serial_print("\n");
        serial_unlock();

        uint64_t* pml4 = (uint64_t*)(0xFFFF800000000000ULL + (cr3 & ~0xFFFULL));
        uint64_t pml4e = pml4[idx_pml4];
        serial_lock();
        serial_print("  pml4e             : 0x"); serial_print_hex(pml4e); serial_print("\n");
        serial_unlock();
        if (!(pml4e & 1)) {
            serial_print("  PML4E NOT PRESENT - stopping walk\n");
            while (1) __asm__ volatile("hlt");
        }

        uint64_t* pdpt = (uint64_t*)(0xFFFF800000000000ULL + (pml4e & ~0xFFFULL));
        uint64_t pdpte = pdpt[idx_pdpt];
        serial_lock();
        serial_print("  pdpte             : 0x"); serial_print_hex(pdpte); serial_print("\n");
        serial_unlock();
        if (!(pdpte & 1)) {
            serial_print("  PDPTE NOT PRESENT - stopping walk\n");
            while (1) __asm__ volatile("hlt");
        }

        uint64_t* pd = (uint64_t*)(0xFFFF800000000000ULL + (pdpte & ~0xFFFULL));
        uint64_t pde = pd[idx_pd];
        serial_lock();
        serial_print("  pde               : 0x"); serial_print_hex(pde); serial_print("\n");
        serial_unlock();
        if (!(pde & 1)) {
            serial_print("  PDE NOT PRESENT - stopping walk\n");
            while (1) __asm__ volatile("hlt");
        }

        if (pde & 0x80) {
            uint64_t big_base = pde & ~0x1FFFFFULL;
            uint64_t big_off  = fault_addr & 0x1FFFFFULL;
            serial_lock();
            serial_print("  PDE IS 2 MB PAGE, phys base 0x");
            serial_print_hex(big_base);
            serial_print(" -> effective phys 0x");
            serial_print_hex(big_base + big_off);
            serial_print("\n");
            serial_unlock();
        } else {
            uint64_t* pt = (uint64_t*)(0xFFFF800000000000ULL + (pde & ~0xFFFULL));
            uint64_t pte = pt[idx_pt];
            serial_lock();
            serial_print("  pte               : 0x"); serial_print_hex(pte); serial_print("\n");
            if (pte & 1) {
                uint64_t phys = (pte & ~0xFFFULL) | (fault_addr & 0xFFF);
                serial_print("  PTE PRESENT, phys 0x"); serial_print_hex(phys); serial_print("\n");
                if (pte & 0x80) {
                    serial_print("  *** PTE HAS PS BIT SET (reserved in a PTE!) ***\n");
                }
                if (pte & 0x8000000000000000ULL) {
                    serial_print("  *** PTE HAS NX BIT SET ***\n");
                }
            } else {
                serial_print("  PTE NOT PRESENT\n");
            }
            serial_unlock();
        }
    }

    /*
     * User-mode #PF: the fault is the process's problem, not the
     * kernel's.  Print the diagnostic above (already done), then kill
     * the faulting process and let the scheduler resume the shell.
     * This is the Linux model: diagnose, SIGSEGV the task, keep the
     * machine.  A kernel-mode #PF still falls through to the halt
     * below -- there is no safe process to kill when the kernel
     * itself is the thing that faulted.
     *
     * The expected-fault path (g_expect_fault == 0x0E) is handled
     * earlier in the function and never reaches this point.
     */
    if (error_code & 4) {
        fault_kill_current(0x0E);
    }

    while (1) __asm__ volatile("hlt");
}

void isr_default_handler(default_frame_t *frame) {
    uint64_t vec = frame->vector;

    serial_lock();
    serial_print("\n*** UNHANDLED INTERRUPT: vector ");
    serial_print_dec(vec);
    serial_print(" ***\n");
    serial_print("  error_code : 0x"); serial_print_hex(frame->error_code); serial_print("\n");
    serial_print("  rip        : 0x"); serial_print_hex(frame->rip);        serial_print("\n");
    serial_print("  cs         : 0x"); serial_print_hex(frame->cs);         serial_print("\n");
    serial_print("  rflags     : 0x"); serial_print_hex(frame->rflags);     serial_print("\n");
    serial_print("  rsp        : 0x"); serial_print_hex(frame->rsp);        serial_print("\n");
    serial_print("  ss         : 0x"); serial_print_hex(frame->ss);         serial_print("\n");
    serial_print("  rax        : 0x"); serial_print_hex(frame->rax);        serial_print("\n");
    serial_print("  rbx        : 0x"); serial_print_hex(frame->rbx);        serial_print("\n");
    serial_print("  rcx        : 0x"); serial_print_hex(frame->rcx);        serial_print("\n");
    serial_print("  rdx        : 0x"); serial_print_hex(frame->rdx);        serial_print("\n");
    serial_print("  rsi        : 0x"); serial_print_hex(frame->rsi);        serial_print("\n");
    serial_print("  rdi        : 0x"); serial_print_hex(frame->rdi);        serial_print("\n");
    serial_unlock();

    vga_print("\n*** UNHANDLED INTERRUPT: vector ");
    vga_print_dec_cur(vec);
    vga_print(" ***\n");
    vga_print("  rip = 0x");
    vga_print_hex_cur(frame->rip);
    vga_print("\n");

    while (1) __asm__ volatile("hlt");
}
