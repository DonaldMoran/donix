#include <stddef.h>
#include "include/tss.h"
#include "include/gdt.h"
#include "include/vga.h"
#include "include/serial.h"
#include "include/vmm.h"

static tss_t* tss = (tss_t*)TSS_PHYS_ADDR;
static uint8_t* iomap = (uint8_t*)(TSS_PHYS_ADDR + sizeof(tss_t));

uint64_t kernel_stack[4096] __attribute__((aligned(16)));
uint64_t *kernel_stack_top = &kernel_stack[4096];

/* Kernel stack top read by user_syscall_entry.asm on every syscall.
   Updated whenever `current` changes: process_init (idle), 
   scheduler_switch_to, process_exit, and timer_preempt_handler. */
uint64_t g_syscall_stack_top = 0;

/* Dedicated 4 KB stack for the #DF handler, loaded via IST1.
 *
 * A double fault is, by construction, a situation where something has
 * already gone wrong: either the CPU took a fault while delivering a
 * fault, or (more commonly) the current kernel stack is corrupted or
 * unmapped. If we handled #DF on the normal kernel stack, the push of
 * the exception frame itself could fault — turning a diagnosable
 * double fault into a silent triple-fault reset.
 *
 * The CPU switches to tss->ist1 before pushing the #DF frame, so this
 * stack must be:
 *   - mapped at whatever address the CPU will use (physical is fine;
 *     the kernel's identity map covers low RAM)
 *   - 16-byte aligned
 *   - at least as large as the #DF frame (5 CPU-pushed fields + 15
 *     GPRs pushed by the stub + one error code = ~200 bytes)
 *
 * 4 KB gives a lot of headroom; if the handler recurses (it shouldn't),
 * the extra space buys one or two frames before the stack itself
 * underflows. */
static uint8_t df_ist_stack[4096] __attribute__((aligned(16)));

void tss_init_ist(void) {
    uint64_t top = (uint64_t)df_ist_stack + sizeof(df_ist_stack);
    top &= ~0xFULL;  /* 16-byte alignment for the SysV ABI */
    tss->ist1 = top;

    serial_print("TSS: IST1 (#DF) top = 0x");
    serial_print_hex(top);
    serial_print("\n");
}

void tss_init(void) {
    // Zero the entire TSS region (TSS + I/O Permission Bitmap)
    uint8_t* buffer_ptr = (uint8_t*)tss;
    for (size_t i = 0; i < TSS_TOTAL_SIZE; i++) {
        buffer_ptr[i] = 0;
    }

    // I/O Permission Bitmap terminator: all bits set at the end of the
    // bitmap means every port is permitted.
    iomap[TSS_IOMAP_SIZE] = 0xFF;

    // RSP0 = top of the kernel stack (used on ring-3 -> ring-0 transitions
    // and by the scheduler when switching to a user task)
    tss->rsp0 = (uint64_t)&kernel_stack[4096];

    // I/O bitmap base = end of the TSS structure
    tss->iopb_base = sizeof(tss_t);

    // Configure IST1 for the #DF handler.
    tss_init_ist();

    // Point the GDT TSS descriptor at our TSS
    gdt_set_tss((uint64_t)tss, TSS_TOTAL_SIZE - 1);

    // Load TR with selector 0x38
    __asm__ volatile (
        "cli\n\t"
        "ltr %%ax\n\t"
        "sti"
        :
        : "a"(0x38)
        : "memory"
    );

    // Verify TR loaded correctly
    uint16_t tr;
    __asm__ volatile ("str %%ax" : "=a"(tr) : : "memory");

    if (tr == 0x38) {
        serial_print("TSS: TR loaded correctly (0x38)\n");
    } else {
        serial_print("TSS: ERROR - TR register is 0x");
        serial_print_hex(tr);
        serial_print(" (expected 0x38)\n");
    }
}


/* Called from the timer preempt path. Keep as a real function call (no
   inlining) so the surrounding frame layout stays exactly what the
   assembly stub expects. See timer_preempt_handler in interrupts.c for
   the full explanation of this boundary. */
void __attribute__((noinline))
tss_set_kernel_stack(uint64_t stack) {
	if (stack & 0xF) {
        serial_print("TSS: kernel_stack_top not 16-aligned: 0x");
        serial_print_hex(stack);
        serial_print("\n");
    }
    tss->rsp0 = stack;
}

/* Set the kernel stack top that user_syscall_entry.asm will load on
   entry. Must be called in lockstep with tss_set_kernel_stack: both
   should point at the same per-process kernel stack. */
void tss_set_syscall_stack(uint64_t stack) {
    g_syscall_stack_top = stack;
}

/* Print the current TSS fields on demand. Called from the kernel
   shell's tssdump command. Prints to both serial and VGA. */
void tss_dump(void) {
    serial_print("\n=== TSS DUMP ===\n");
    vga_print("=== TSS DUMP ===\n");

    serial_print("TSS base=0x"); serial_print_hex((uint64_t)tss);
    serial_print(" iomap=0x"); serial_print_hex((uint64_t)iomap);
    serial_print(" size=0x"); serial_print_hex((uint64_t)sizeof(tss_t));
    serial_print("\n");
    vga_print("TSS base=0x"); vga_print_hex_cur((uint64_t)tss);
    vga_print(" size=0x"); vga_print_hex_cur((uint64_t)sizeof(tss_t));
    vga_print("\n");

    serial_print("  rsp0 = 0x"); serial_print_hex(tss->rsp0);
    serial_print("\n");
    vga_print("  rsp0 = 0x"); vga_print_hex_cur(tss->rsp0);
    vga_print("\n");

    serial_print("  rsp1 = 0x"); serial_print_hex(tss->rsp1);
    serial_print(" rsp2 = 0x"); serial_print_hex(tss->rsp2);
    serial_print("\n");

    serial_print("  ist1 = 0x"); serial_print_hex(tss->ist1);
    serial_print(" ist2 = 0x"); serial_print_hex(tss->ist2);
    serial_print("\n");
    serial_print("  ist3 = 0x"); serial_print_hex(tss->ist3);
    serial_print(" ist4 = 0x"); serial_print_hex(tss->ist4);
    serial_print("\n");
    serial_print("  ist5 = 0x"); serial_print_hex(tss->ist5);
    serial_print(" ist6 = 0x"); serial_print_hex(tss->ist6);
    serial_print("\n");
    serial_print("  ist7 = 0x"); serial_print_hex(tss->ist7);
    serial_print("\n");

    serial_print("  iopb_base = 0x"); serial_print_hex(tss->iopb_base);
    serial_print("\n");
    vga_print("  iopb_base = 0x"); vga_print_hex_cur(tss->iopb_base);
    vga_print("\n");

    /* Current TR, for sanity. */
    uint16_t tr;
    __asm__ volatile ("str %0" : "=r"(tr));
    serial_print("  TR = 0x"); serial_print_hex(tr);
    serial_print("\n");
    vga_print("  TR = 0x"); vga_print_hex_cur(tr);
    vga_print("\n");

    /* Cross-check: g_syscall_stack_top should equal rsp0 for the
       current process once the scheduler has run, or idle's kernel
       stack top before that. */
    extern uint64_t g_syscall_stack_top;
    serial_print("  g_syscall_stack_top = 0x");
    serial_print_hex(g_syscall_stack_top);
    serial_print("\n");
    vga_print("  g_syscall_stack_top = 0x");
    vga_print_hex_cur(g_syscall_stack_top);
    vga_print("\n");

    serial_print("=== END TSS DUMP ===\n\n");
    vga_print("=== END TSS DUMP ===\n");
}
