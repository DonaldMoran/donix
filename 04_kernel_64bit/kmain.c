#include <stdint.h>
#include <stddef.h>
#include "include/bootinfo.h"
#include "include/pmm.h"
#include "include/idt.h"
#include "include/vga.h"
#include "include/keyboard.h"
#include "include/interrupts.h"
#include "include/vmm.h"
#include "include/serial.h"
#include "include/heap.h"
#include "include/tss.h"
#include "include/syscall.h"
#include "include/user_syscall.h"
#include "include/elf.h"
#include "include/gdt.h"
#include "include/debug.h"
#include "include/user_msr.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/ata.h"
#include "include/fat_config.h"
#include "ff.h"

extern void pit_init(uint32_t freq);
extern unsigned char test_program[];
extern unsigned int test_program_len;

static BootInfo *g_bootinfo = NULL;

#define PRINT_BOTH(str) do { vga_print(str); serial_print(str); } while(0)
#define PRINT_BOTH_DEC(val) do { vga_print_dec_cur(val); serial_print_dec(val); } while(0)
#define PRINT_BOTH_HEX(val) do { vga_print_hex_cur(val); serial_print_hex(val); } while(0)

void enable_user_fsgsbase(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 16);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    serial_print("CPU: FSGSBASE active\n");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int validate_bootinfo(BootInfo* info) {
    if (!info || info->magic != BOOTINFO_MAGIC) {
        serial_print("ERR: Bad BootInfo\n");
        return 0;
    }
    serial_print("BootInfo OK\n");
    return 1;
}

void test_process_entry(void) {
    sys_write(1, "Hello from process!\n", 20);
    process_exit();
}

extern void user_syscall_entry(void);

void user_syscall_init(void) {
    uint64_t efer = rdmsr(0xC0000080);
    wrmsr(0xC0000080, efer | 1ULL);
    wrmsr(0xC0000081, ((uint64_t)0x23 << 48) | ((uint64_t)0x18 << 32));
    wrmsr(0xC0000082, (uint64_t)user_syscall_entry);
    wrmsr(0xC0000084, (1ULL << 9));
    serial_print("**RING** 3 syscalls active\n");
}

/*
 * Enable EFER.NXE (bit 11) so the CPU honors bit 63 of a PTE.
 *
 * Without this, bit 63 is architecturally a reserved bit: the Intel
 * SDM says "If IA32_EFER.NXE = 0 and the P flag of a PDE or a PTE is
 * 1, the XD flag (bit 63) is reserved," and a reference that uses
 * such an entry causes a page-fault exception.  The kernel has been
 * writing NX bits into PTEs via vmm_map_page(..., PT_NX) regardless,
 * because KVM's shadow MMU enables NX on the host side and therefore
 * honors the bit even when the guest's EFER.NXE is clear.  That made
 * the behavior look correct under KVM by accident; under TCG the
 * behavior is QEMU-version-dependent, and on bare metal the first
 * access to an NX-marked page would #PF.
 *
 * Setting NXE makes the guest's configuration architecturally valid
 * and consistent across KVM, TCG, and real hardware.
 *
 * The CPUID guard is required, not defensive.  CPUs that do not
 * support CPUID leaf 0x80000001 do not allow IA32_EFER.NXE to be
 * set, and a wrmsr on bit 11 of such a CPU would #GP.  Every x86_64
 * CPU that can run this kernel has NX, so the first branch is the
 * one that runs in practice; the guard makes the code correct rather
 * than lucky.
 *
 * This must run before the first vmm_map_page call that uses PT_NX.
 * In the current kernel nothing maps PT_NX before test_nx / nxtest
 * are invoked, so this placement (before sti, before pmm_init) is
 * more than early enough.  Placing it here also makes the CPU setup
 * contiguous with the other early CPU-feature initialization
 * (idt_init, pit_init).
 *
 * user_syscall_init() runs later and does wrmsr(EFER, efer | 1) to
 * set SCE.  Because it reads the current EFER value first, the NXE
 * bit set here survives that write.  No change is needed there.
 */
static void enable_nx(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0x80000001u));

    if (edx & (1u << 20)) {              /* CPUID.80000001H:EDX.NX */
        uint64_t efer = rdmsr(0xC0000080);
        wrmsr(0xC0000080, efer | (1ULL << 11));
        serial_print("CPU: EFER.NXE enabled\n");
    } else {
        /* Should never happen on x86_64.  If it does, the kernel
           will #PF the moment anything maps a page with PT_NX, so
           say so loudly. */
        serial_print("WARN: CPU lacks NX; PT_NX mappings will #PF\n");
    }
}

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void test_process1(void) {
    for (int i = 0; i < 5; i++) {
        serial_lock();
        PRINT_BOTH("Proc 1: iter ");
        PRINT_BOTH_DEC(i);
        PRINT_BOTH("\n");
        serial_unlock();
        process_yield();
    }
    process_exit();
}

void test_process2(void) {
    for (int i = 0; i < 5; i++) {
        serial_lock();
        PRINT_BOTH("Proc 2: iter ");
        PRINT_BOTH_DEC(i);
        PRINT_BOTH("\n");
        serial_unlock();
        process_yield();
    }
    process_exit();
}

void handle_reboot_sequence(void) {
    __asm__ volatile("cli");
    for (int i = 0; i < 1000; i++) { if ((inb(0x64) & 2) == 0) break; }
    outb(0x64, 0xFE);  
    outb(0xCF9, 0x06); 
    __asm__ volatile("xor %%ax, %%ax\n\t" "ltr %%ax" : : : "ax");
    volatile uint16_t malformed_idt_struct[5] = {0, 0, 0, 0, 0};
    __asm__ volatile("lidt (%0)\n\t" "int $0" : : "r"(malformed_idt_struct) : "memory");
    while (1) { __asm__ volatile("hlt"); }
}

/*
 * Suspend the calling process (the kernel shell) before yielding to a
 * kernel-mode diagnostic process. Without this, scheduler_switch_to
 * re-adds the shell to the ready queue because its state is still
 * RUNNING, and the shell resumes as soon as the diagnostic yields
 * once — long before the diagnostic has finished.
 *
 * The shell is only woken by process_exit's fallback when a kernel
 * diagnostic exits with nothing else runnable. See scheduler.c.
 */
static void suspend_self_for_diagnostic(void) {
    pcb_t* self = process_get_current();
    if (self) self->state = PROC_STATE_BLOCKED;
}

/* =====================================================================
 * Self-test: result codes and forward declarations.
 *
 * Every test returns PASS or FAIL.  There is no INFO today: gdt and
 * tss turned out to have real assertions (see test_gdt and test_tss
 * below), and every other test has a concrete pass/fail criterion.
 * ===================================================================== */

#define SELFTEST_PASS 0
#define SELFTEST_FAIL 1

static int test_gdt(void);
static int test_tss(void);
static int test_pmm(void);
static int test_vmm(void);
static int test_map(void);
static int test_recursive(void);
static int test_heap(void);
static int test_heap_validate(void);
static int test_heap_stress(void);
static int test_nx(void);
static int test_syscall(void);
static int test_ata(void);
static int test_fat_mount(void);
static int test_fat_ls(void);

/* Forward declaration: defined below kmain_shell_loop, called from
 * handle_command (the elfload command) and kmain. */
static uint64_t load_elf_into_user_process(pcb_t* pcb, const void* elf_data);

/* =====================================================================
 * GDT validation.
 *
 * Walks the GDT and checks the descriptors the kernel actually depends
 * on.  The layout is built by gdt_init in gdt.c:
 *
 *   0x00  null
 *   0x08  (legacy 32-bit code, unused in long mode)
 *   0x10  (legacy 32-bit data, unused in long mode)
 *   0x18  kernel code,   DPL=0, L=1
 *   0x20  kernel data,   DPL=0
 *   0x28  user data,     DPL=3  (SS = 0x2B when RPL=3)
 *   0x30  user code,     DPL=3, L=1  (CS = 0x33 when RPL=3)
 *   0x38  TSS
 *
 * The assertions catch a stomped GDT or a broken gdt_init.  They do
 * not verify every bit of every descriptor; the load-bearing ones are
 * the kernel code (used in every kernel frame), the user code (used
 * in every Ring 3 return), and the TSS (used on every Ring 3 -> Ring 0
 * transition).
 *
 * The base-address assertion (see the first check inside test_gdt)
 * requires the GDT to be in the higher half, which it is once
 * gdt_init has run.  Before 3h the GDT was the bootloader's, at
 * physical 0x101DC; that assertion was relaxed to "base != 0" so the
 * self-test could pass.  It is now strict again.
 * ===================================================================== */

static int test_gdt(void) {
    gdt_dump();

    struct __attribute__((packed)) {
        uint16_t limit;
        uint64_t base;
    } gdt_ptr;
    __asm__ volatile("sgdt %0" : "=m"(gdt_ptr));

    /* The GDT must be in the kernel's higher half.  gdt_init builds
       kernel_gdt in .bss at 0xFFFFFFFF801xxxxx and lgdt's it; if this
       check fails, the CPU is still using the bootloader's low-memory
       GDT, which means gdt_init did not run or did not take effect.
       See MAINTENANCE.md item 3h. */
    if (gdt_ptr.base < 0xFFFFFFFF80000000ULL) return SELFTEST_FAIL;

    /* At least 8 entries (indices 0..7). */
    if (gdt_ptr.limit < 7 * 8 - 1) return SELFTEST_FAIL;

    uint64_t* gdt = (uint64_t*)gdt_ptr.base;

    /* Descriptor decode, matching gdt_dump's bit layout. */
    #define D_ACCESS(d)  (((d) >> 40) & 0xFF)
    #define D_FLAGS(d)   (((d) >> 52) & 0xF)
    #define D_PRESENT(d) (D_ACCESS(d) & 0x80)
    #define D_DPL(d)     ((D_ACCESS(d) >> 5) & 3)
    #define D_CODE(d)    (D_ACCESS(d) & 0x08)             /* executable */
    #define D_LONG(d)    (D_FLAGS(d) & 0x2)               /* L bit */

    /* Index 0: null descriptor. */
    if (gdt[0] != 0) return SELFTEST_FAIL;

    /* Index 3 (0x18): kernel code, present, DPL=0, code, L=1. */
    if (!D_PRESENT(gdt[3]))       return SELFTEST_FAIL;
    if (D_DPL(gdt[3]) != 0)       return SELFTEST_FAIL;
    if (!D_CODE(gdt[3]))          return SELFTEST_FAIL;
    if (!D_LONG(gdt[3]))          return SELFTEST_FAIL;

    /* Index 4 (0x20): kernel data, present, DPL=0, not code. */
    if (!D_PRESENT(gdt[4]))       return SELFTEST_FAIL;
    if (D_DPL(gdt[4]) != 0)       return SELFTEST_FAIL;
    if (D_CODE(gdt[4]))           return SELFTEST_FAIL;

    /* Index 5 (0x28): user data, present, DPL=3, not code. */
    if (!D_PRESENT(gdt[5]))       return SELFTEST_FAIL;
    if (D_DPL(gdt[5]) != 3)       return SELFTEST_FAIL;
    if (D_CODE(gdt[5]))           return SELFTEST_FAIL;

    /* Index 6 (0x30): user code, present, DPL=3, code, L=1. */
    if (!D_PRESENT(gdt[6]))       return SELFTEST_FAIL;
    if (D_DPL(gdt[6]) != 3)       return SELFTEST_FAIL;
    if (!D_CODE(gdt[6]))          return SELFTEST_FAIL;
    if (!D_LONG(gdt[6]))          return SELFTEST_FAIL;

    /* Index 7 (0x38): TSS, present, system, type 0x9 (available) or
       0xB (busy after ltr). */
    uint8_t tss_access = D_ACCESS(gdt[7]);
    if (!(tss_access & 0x80))     return SELFTEST_FAIL;
    if (tss_access & 0x10)        return SELFTEST_FAIL;   /* S bit must be 0 */
    uint8_t tss_type = tss_access & 0x0F;
    if (tss_type != 0x9 && tss_type != 0xB) return SELFTEST_FAIL;

    #undef D_ACCESS
    #undef D_FLAGS
    #undef D_PRESENT
    #undef D_DPL
    #undef D_CODE
    #undef D_LONG

    return SELFTEST_PASS;
}

/* =====================================================================
 * TSS validation.
 *
 * Checks the fields the CPU and the scheduler depend on:
 *
 *   TR          must be 0x38 (the selector loaded by tss_init)
 *   RSP0        kernel higher-half, 16-byte aligned
 *   IST1        kernel higher-half, 16-byte aligned, non-zero
 *               (item 3b's fix: #DF uses this stack)
 *   iopb_base   == sizeof(tss_t)
 *   g_syscall_stack_top == tss->rsp0
 *               the v0.4.9 invariant: these two move in lockstep
 *               with `current` at every context switch
 *
 * The last assertion is the strong one.  When selftest runs from the
 * kernel shell, both should point at the shell's kernel_stack_top,
 * because scheduler_switch_to updates them together.
 * ===================================================================== */

static int test_tss(void) {
    tss_dump();

    /* TR must be the TSS selector. */
    uint16_t tr;
    __asm__ volatile("str %0" : "=r"(tr));
    if ((tr & 0xFFF8) != GDT_TSS) return SELFTEST_FAIL;

    /* Re-derive the TSS base from the GDT descriptor, the same way
       isr13_handler does, so we do not depend on tss.c's static
       `tss` pointer. */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint64_t base;
    } gdt_ptr;
    __asm__ volatile("sgdt %0" : "=m"(gdt_ptr));
    uint64_t* gdt = (uint64_t*)gdt_ptr.base;
    uint64_t lo = gdt[GDT_TSS / 8];
    uint64_t hi = gdt[GDT_TSS / 8 + 1];
    uint64_t tss_base = ((lo >> 16) & 0xFFFFFF)
                      | (((lo >> 56) & 0xFF) << 24)
                      | ((hi & 0xFFFFFFFF) << 32);
    if (tss_base == 0) return SELFTEST_FAIL;

    tss_t* t = (tss_t*)tss_base;

    /* RSP0 must be a 16-byte-aligned kernel stack top. */
    if (t->rsp0 == 0)                              return SELFTEST_FAIL;
    if (t->rsp0 < 0xFFFFFFFF80000000ULL)           return SELFTEST_FAIL;
    if (t->rsp0 & 0xFULL)                          return SELFTEST_FAIL;

    /* IST1 must be a 16-byte-aligned kernel stack top, non-zero. */
    if (t->ist1 == 0)                              return SELFTEST_FAIL;
    if (t->ist1 < 0xFFFFFFFF80000000ULL)           return SELFTEST_FAIL;
    if (t->ist1 & 0xFULL)                          return SELFTEST_FAIL;

    /* I/O permission bitmap must start right after the TSS struct. */
    if (t->iopb_base != (uint16_t)sizeof(tss_t))   return SELFTEST_FAIL;

    /* The lockstep invariant. */
    if (g_syscall_stack_top != t->rsp0)            return SELFTEST_FAIL;

    return SELFTEST_PASS;
}

/* =====================================================================
 * PMM: allocate one page, verify it is non-zero, free it.
 *
 * The original pmmtest command leaked this page on every invocation.
 * selftest runs pmmtest on every batch, so the leak would accumulate.
 * Freeing here keeps selftest idempotent.
 * ===================================================================== */

static int test_pmm(void) {
    PRINT_BOTH("\n--- PMM Test ---\n");
    PRINT_BOTH("  Total Pages: "); PRINT_BOTH_DEC(pmm_get_total_pages()); PRINT_BOTH("\n");
    PRINT_BOTH("  Free Pages : "); PRINT_BOTH_DEC(pmm_get_free_pages()); PRINT_BOTH("\n");

    uint64_t phys_page = pmm_alloc_page(PAGE_KERNEL);
    if (phys_page == 0) {
        PRINT_BOTH("  Status     : FAILED (OOM)\n");
        return SELFTEST_FAIL;
    }
    PRINT_BOTH("  Allocated  : 0x"); PRINT_BOTH_HEX(phys_page); PRINT_BOTH("\n");
    PRINT_BOTH("  Zone       : "); PRINT_BOTH(phys_page < 0x2000000 ? "LOW\n" : "HIGH\n");
    PRINT_BOTH("  Status     : SUCCESS\n");
    pmm_dump_stats();

    pmm_free_page(phys_page);
    return SELFTEST_PASS;
}

/* =====================================================================
 * VMM: read back the control registers and the NX-enable bit.
 *
 * These are always non-zero when the kernel is running (CR3 is the
 * page table root, CR0 has PG and PE set, EFER.NXE was set by entry.asm).
 * The test is weak but cheap, and it catches a regression in the VMM
 * init path that would otherwise show up only as a crash elsewhere.
 * ===================================================================== */

static int test_vmm(void) {
    PRINT_BOTH("\n=== VMM Dashboard ===\n");
    uint64_t cr3, cr0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    uint64_t efer = rdmsr(0xC0000080);

    PRINT_BOTH("  CR3 Root : 0x"); PRINT_BOTH_HEX(cr3); PRINT_BOTH("\n");
    PRINT_BOTH("  WP Active: "); PRINT_BOTH(cr0 & (1ULL << 16) ? "Yes\n" : "No\n");
    PRINT_BOTH("  NX Active: "); PRINT_BOTH(efer & (1ULL << 11) ? "Yes\n" : "No\n");

    /* CR3 must be a real page-table root. */
    if ((cr3 & ~0xFFFULL) == 0)          return SELFTEST_FAIL;
    /* Paging must be on, or nothing works. */
    if (!(cr0 & (1ULL << 31)))           return SELFTEST_FAIL;

    /* NX must be enabled.  enable_nx() in kmain sets EFER.NXE before
       this test can run; if it is off, either enable_nx failed
       (CPU lacks NX) or something cleared the bit afterward.  Either
       way, PT_NX mappings will #PF on real hardware, so this is a
       real FAIL.  See MAINTENANCE.md item 3g. */
    if (!(efer & (1ULL << 11)))          return SELFTEST_FAIL;

    return SELFTEST_PASS;
}

/* =====================================================================
 * Page mapping: allocate a physical page, write a magic value through
 * the HHDM, map it at a scratch virtual address in the current cr3,
 * read it back through the virtual address, then unmap and free.
 *
 * The original maptest command unmapped but leaked the physical page.
 * Freeing here keeps selftest idempotent.
 * ===================================================================== */

static int test_map(void) {
    PRINT_BOTH("\n=== VMM Map Test ===\n");
    uint64_t phys = pmm_alloc_page(PAGE_KERNEL);
    if (phys == 0) {
        PRINT_BOTH("  Status   : FAILED (OOM)\n");
        return SELFTEST_FAIL;
    }
    PRINT_BOTH("  Phys Page: 0x"); PRINT_BOTH_HEX(phys); PRINT_BOTH("\n");

    *(uint64_t*)(0xFFFF800000000000ULL + phys) = 0xDEADBEEFCAFEBABEULL;
    uint64_t test_virt = 0x40000000ULL;
    uint64_t active_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(active_cr3));

    extern void vmm_map_page_in_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
    vmm_map_page_in_cr3(active_cr3, test_virt, phys, 0x01ULL | 0x02ULL);
    __asm__ volatile("invlpg (%0)" : : "r"(test_virt) : "memory");

    uint64_t res = *(uint64_t*)test_virt;
    PRINT_BOTH("  Virt Read: 0x"); PRINT_BOTH_HEX(res); PRINT_BOTH("\n");

    extern void vmm_unmap_page_in_cr3(uint64_t cr3, uint64_t virt);
    vmm_unmap_page_in_cr3(active_cr3, test_virt);
    __asm__ volatile("invlpg (%0)" : : "r"(test_virt) : "memory");
    pmm_free_page(phys);

    if (res != 0xDEADBEEFCAFEBABEULL) {
        PRINT_BOTH("  Status   : FAILED\n");
        return SELFTEST_FAIL;
    }
    PRINT_BOTH("  Status   : SUCCESS\n");
    return SELFTEST_PASS;
}

/* =====================================================================
 * Recursive paging: read PML4[510] through the recursive window and
 * compare it to CR3.  Both should point at the same physical page.
 * ===================================================================== */

static int test_recursive(void) {
    PRINT_BOTH("\n=== Recursive Paging Test ===\n");
    uint64_t base = 0xFFFF000000000000ULL
                  | (510ULL << 39) | (510ULL << 30)
                  | (510ULL << 21) | (510ULL << 12);

    uint64_t entry = ((uint64_t*)base)[510];

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    PRINT_BOTH("  Extracted: 0x"); PRINT_BOTH_HEX(entry & ~0xFFFULL); PRINT_BOTH("\n");
    PRINT_BOTH("  ActualCR3: 0x"); PRINT_BOTH_HEX(cr3 & ~0xFFFULL); PRINT_BOTH("\n");

    if ((entry & ~0xFFFULL) != (cr3 & ~0xFFFULL)) {
        PRINT_BOTH("  Status   : MISMATCH\n");
        return SELFTEST_FAIL;
    }
    PRINT_BOTH("  Status   : MATCH\n");
    return SELFTEST_PASS;
}

/* =====================================================================
 * Heap: allocate two blocks, write a pattern, read it back, free,
 * then reallocate to confirm the freed space was reused.
 * ===================================================================== */

static int test_heap(void) {
    PRINT_BOTH("\n=== Heap Integrity Test ===\n");
    uint8_t* p1 = (uint8_t*)kmalloc(32);
    uint8_t* p2 = (uint8_t*)kmalloc(64);
    int ok = (p1 && p2);
    if (ok) {
        for (int i = 0; i < 32; i++) p1[i] = 0xAA;
        for (int i = 0; i < 64; i++) p2[i] = 0xBB;
        for (int i = 0; i < 32; i++) { if (p1[i] != 0xAA) ok = 0; }
        for (int i = 0; i < 64; i++) { if (p2[i] != 0xBB) ok = 0; }
        kfree(p1); kfree(p2);
    }
    uint8_t* p3 = (uint8_t*)kmalloc(40);
    if (!p3) ok = 0; else kfree(p3);

    PRINT_BOTH(ok ? "  Status   : SUCCESS\n" : "  Status   : FAILED\n");
    return ok ? SELFTEST_PASS : SELFTEST_FAIL;
}

/* =====================================================================
 * Heap validator.  Walks the block list and checks every invariant.
 * Returns PASS if heap_validate returns 0.
 * ===================================================================== */
static int test_heap_validate(void) {
    PRINT_BOTH("\n=== Heap Validator ===\n");
    uint64_t bad = heap_validate();
    if (bad == 0) {
        PRINT_BOTH("  Status   : INTACT\n");
        return SELFTEST_PASS;
    }
    PRINT_BOTH("  Status   : CORRUPT at 0x");
    PRINT_BOTH_HEX(bad);
    PRINT_BOTH("\n");
    return SELFTEST_FAIL;
}

/* =====================================================================
 * Heap stress.  Deterministic alloc/free pattern that forces at least
 * one extension.  Returns PASS if no leak and no corruption.
 *
 * The heap grows from 1 MB to roughly 2-3 MB over the course of one
 * run.  It stays grown, so repeated runs do not leak PMM pages.
 * ===================================================================== */
static int test_heap_stress(void) {
    PRINT_BOTH("\n=== Heap Stress (4096 ops, 512 slots) ===\n");
    size_t before = heap_used();
    size_t mapped_before = heap_total();
    size_t leak   = heap_stress();
    size_t after  = heap_used();
    size_t mapped_after = heap_total();

    PRINT_BOTH("  Mapped before: ");
    PRINT_BOTH_DEC((uint64_t)mapped_before);
    PRINT_BOTH(" bytes\n");
    PRINT_BOTH("  Mapped after:  ");
    PRINT_BOTH_DEC((uint64_t)mapped_after);
    PRINT_BOTH(" bytes\n");
    PRINT_BOTH("  Used before:   ");
    PRINT_BOTH_DEC((uint64_t)before);
    PRINT_BOTH(" bytes\n");
    PRINT_BOTH("  Used after:    ");
    PRINT_BOTH_DEC((uint64_t)after);
    PRINT_BOTH(" bytes\n");

    if (mapped_after == mapped_before) {
        PRINT_BOTH("  NOTE     : heap did not extend; stress may be too small\n");
    }

    if (leak != 0) {
        PRINT_BOTH("  Status   : FAILED (sentinel=");
        PRINT_BOTH_DEC((uint64_t)leak);
        PRINT_BOTH(")\n");
        return SELFTEST_FAIL;
    }
    PRINT_BOTH("  Status   : SUCCESS (no leak, heap intact)\n");
    return SELFTEST_PASS;
}

/* =====================================================================
 * NX: allocate a page, map it with the NX bit set, then walk the page
 * tables via HHDM to confirm the NX bit landed in the final PTE.
 *
 * The walk mirrors isr14_handler's, which is the only other place in
 * the kernel that reads a raw PTE.  vmm.h exposes vmm_get_phys_from_cr3
 * (returns the physical address) but not a PTE-returning helper, so we
 * walk manually rather than adding a new API just for the test.
 *
 * The original nxtest command leaked the physical page.  Freeing here
 * keeps selftest idempotent.
 *
 * With EFER.NXE now enabled by enable_nx() in kmain, bit 63 is a real
 * permission bit rather than a reserved bit; the PTE this test maps
 * is genuinely non-executable at the hardware level.
 * ===================================================================== */

static int test_nx(void) {
    PRINT_BOTH("\n=== NX Enforcement Test ===\n");
    uint64_t phys = pmm_alloc_page(PAGE_KERNEL);
    if (phys == 0) {
        PRINT_BOTH("  Status   : FAILED (OOM)\n");
        return SELFTEST_FAIL;
    }
    uint64_t virt = 0xFFFFFFFF82000000ULL;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    extern void vmm_map_page_in_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
    vmm_map_page_in_cr3(cr3, virt, phys, 0x01ULL | 0x02ULL | 0x8000000000000000ULL);
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");

    /* Walk the page tables via HHDM to read the PTE back. */
    uint64_t i4 = (virt >> 39) & 0x1FF;
    uint64_t i3 = (virt >> 30) & 0x1FF;
    uint64_t i2 = (virt >> 21) & 0x1FF;
    uint64_t i1 = (virt >> 12) & 0x1FF;

    int ok = 0;
    uint64_t pte = 0;

    uint64_t* pml4 = (uint64_t*)(0xFFFF800000000000ULL + (cr3 & ~0xFFFULL));
    uint64_t  pml4e = pml4[i4];
    if (pml4e & 1) {
        uint64_t* pdpt = (uint64_t*)(0xFFFF800000000000ULL + (pml4e & ~0xFFFULL));
        uint64_t  pdpte = pdpt[i3];
        if (pdpte & 1) {
            uint64_t* pd = (uint64_t*)(0xFFFF800000000000ULL + (pdpte & ~0xFFFULL));
            uint64_t  pde = pd[i2];
            if (pde & 1) {
                uint64_t* pt = (uint64_t*)(0xFFFF800000000000ULL + (pde & ~0xFFFULL));
                pte = pt[i1];
                if (pte & 1) {
                    ok = (pte & 0x8000000000000000ULL) != 0;
                    PRINT_BOTH("  PTE      : 0x"); PRINT_BOTH_HEX(pte); PRINT_BOTH("\n");
                    PRINT_BOTH("  NX bit in PTE: ");
                    PRINT_BOTH(ok ? "set\n" : "clear\n");
                }
            }
        }
    }

    extern void vmm_unmap_page_in_cr3(uint64_t cr3, uint64_t virt);
    vmm_unmap_page_in_cr3(cr3, virt);
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    pmm_free_page(phys);

    PRINT_BOTH(ok ? "  Status   : SUCCESS\n" : "  Status   : FAILED\n");
    return ok ? SELFTEST_PASS : SELFTEST_FAIL;
}

/* =====================================================================
 * Syscall: read the LSTAR MSR (the syscall entry point) and confirm
 * it is set.  Then issue a SYS_WRITE through the syscall interface to
 * confirm the dispatcher is live.
 * ===================================================================== */

static int test_syscall(void) {
    PRINT_BOTH("\n=== Syscall Verification ===\n");
    uint64_t lstar = rdmsr(0xC0000082);
    PRINT_BOTH("  LSTAR Entry: 0x"); PRINT_BOTH_HEX(lstar); PRINT_BOTH("\n");

    if (lstar < 0xFFFFFFFF80000000ULL) return SELFTEST_FAIL;

    sys_write(1, "  [SYSCALL OK] Execution routed.\n", 33);
    return SELFTEST_PASS;
}

/* =====================================================================
 * ATA: read the MBR, confirm the 0xAA55 signature, then read the
 * kernel header sector.  Read-only; no writes.  The write leg was
 * removed in v0.4.11 because LBA 1024 is inside the kernel region in
 * single-drive mode.
 * ===================================================================== */

static int test_ata(void) {
    PRINT_BOTH("\n=== ATA Diagnostic ===\n");
    if (!ata_present(ATA_DRIVE_MASTER) && !ata_present(ATA_DRIVE_SLAVE)) {
        PRINT_BOTH("  Status   : FAILED (No devices)\n");
        return SELFTEST_FAIL;
    }
    uint8_t* buf = (uint8_t*)kmalloc(512);
    if (!buf) {
        PRINT_BOTH("  Status   : FAILED (OOM)\n");
        return SELFTEST_FAIL;
    }
    int ok = 1;

    if (ata_present(ATA_DRIVE_MASTER)) {
        PRINT_BOTH("  Master    : ");
        PRINT_BOTH(ata_model(ATA_DRIVE_MASTER));
        PRINT_BOTH("\n");

        if (ata_read_sector_drive(ATA_DRIVE_MASTER, 0, buf) != 0) {
            PRINT_BOTH("  Master MBR: READ_FAILED\n");
            ok = 0;
        } else {
            uint16_t sig = (uint16_t)buf[510] | ((uint16_t)buf[511] << 8);
            PRINT_BOTH(sig == 0xAA55 ? "  Master MBR: OK\n"
                                     : "  Master MBR: BAD\n");
            if (sig != 0xAA55) ok = 0;
        }

        if (ata_read_sector_drive(ATA_DRIVE_MASTER, 128, buf) != 0) {
            PRINT_BOTH("  Kernel hdr: READ_FAILED\n");
            ok = 0;
        } else {
            PRINT_BOTH("  Kernel hdr: 0x");
            PRINT_BOTH_HEX(((uint64_t)buf[0])       |
                           ((uint64_t)buf[1] <<  8) |
                           ((uint64_t)buf[2] << 16) |
                           ((uint64_t)buf[3] << 24));
            PRINT_BOTH("\n");
        }
    } else {
        PRINT_BOTH("  Master    : Not Present\n");
    }

    if (ata_present(ATA_DRIVE_SLAVE)) {
        PRINT_BOTH("  Slave     : ");
        PRINT_BOTH(ata_model(ATA_DRIVE_SLAVE));
        PRINT_BOTH("\n");

        if (ata_read_sector_drive(ATA_DRIVE_SLAVE, 0, buf) != 0) {
            PRINT_BOTH("  Slave Boot: READ_FAILED\n");
            ok = 0;
        } else {
            uint16_t sig = (uint16_t)buf[510] | ((uint16_t)buf[511] << 8);
            PRINT_BOTH(sig == 0xAA55 ? "  Slave Boot: OK\n"
                                     : "  Slave Boot: NO_SIG\n");
            if (sig != 0xAA55) ok = 0;
        }
    } else {
        PRINT_BOTH("  Slave     : Not Present\n");
    }

    kfree(buf);
    PRINT_BOTH(ok ? "  Status   : SUCCESS\n" : "  Status   : FAILED\n");
    return ok ? SELFTEST_PASS : SELFTEST_FAIL;
}

/* =====================================================================
 * FAT mount: f_mount the boot volume.  FR_OK means the BPB parsed and
 * the boot sector was readable.
 * ===================================================================== */

static int test_fat_mount(void) {
    PRINT_BOTH("\n=== FatFs Mount Test ===\n");
    static FATFS fs;
    FRESULT res = f_mount(&fs, "0:", 1);
    if (res == FR_OK) {
        PRINT_BOTH("  Status   : SUCCESS (Volume online)\n");
        return SELFTEST_PASS;
    }
    PRINT_BOTH("  Status   : FAILED (Code "); PRINT_BOTH_DEC((uint64_t)res); PRINT_BOTH(")\n");
    return SELFTEST_FAIL;
}

/* =====================================================================
 * FAT list: open the root directory, count files and subdirectories.
 * Passes if f_opendir succeeded, regardless of how many entries exist.
 * ===================================================================== */

static int test_fat_ls(void) {
    PRINT_BOTH("\n=== FatFs Directory Listing ===\n");

    DIR dj;
    FILINFO fno;
    FRESULT res;

    res = f_opendir(&dj, "0:/");
    if (res != FR_OK) {
        PRINT_BOTH("  Status   : FAILED (Cannot open root. Code ");
        PRINT_BOTH_DEC((uint64_t)res);
        PRINT_BOTH(")\n");
        return SELFTEST_FAIL;
    }

    int file_count = 0;
    int dir_count = 0;

    for (;;) {
        res = f_readdir(&dj, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;

        if (fno.fattrib & AM_DIR) {
            PRINT_BOTH("  <DIR>  ");
            PRINT_BOTH(fno.fname);
            PRINT_BOTH("\n");
            dir_count++;
        } else {
            PRINT_BOTH("  FILE   ");
            PRINT_BOTH(fno.fname);
            PRINT_BOTH("  (");
            PRINT_BOTH_DEC((uint64_t)fno.fsize);
            PRINT_BOTH(" bytes)\n");
            file_count++;
        }
    }

    PRINT_BOTH("\nTotal: ");
    PRINT_BOTH_DEC((uint64_t)file_count);
    PRINT_BOTH(" file(s), ");
    PRINT_BOTH_DEC((uint64_t)dir_count);
    PRINT_BOTH(" directory(ies)\n");

    f_closedir(&dj);
    return SELFTEST_PASS;
}

/* =====================================================================
 * Expected-fault triggers and driver.
 *
 * The three fault triggers are tiny kernel functions that take one
 * exception each.  They are the entry points of child processes
 * spawned by test_exception_proc().  If the fault fires, the exception
 * handler sees g_expect_fault set, records the vector in
 * g_fault_observed, and calls fault_kill_current(), which terminates
 * the child and resumes the kernel shell.  If the fault does not fire,
 * the trigger reaches its trailing process_exit() and the child ends
 * cleanly; test_exception_proc() then sees g_fault_observed still at
 * -1 and reports FAIL.
 *
 * All three triggers must remain kernel-mode functions (entry_point
 * >= KERNEL_BASE).  process_exit() uses that distinction to choose
 * between "resume the kernel shell" and "halt"; a user-mode trigger
 * would halt instead of resuming, and the self-test would never get
 * control back.  See item 3f in MAINTENANCE.md.
 * ===================================================================== */

static void fault_de_trigger(void) {
    __asm__ volatile(
        "xor %%rax, %%rax\n\t"
        "xor %%rbx, %%rbx\n\t"
        "div %%rbx\n\t"
        ::: "rax", "rbx", "rdx"
    );
    process_exit();
}

static void fault_pf_trigger(void) {
    *(volatile uint64_t*)0xFFFFFFFF00000000ULL = 0xDEADBEEF;
    process_exit();
}

static void fault_gp_trigger(void) {
    *(volatile uint64_t*)0x000FFFFF00000000ULL = 0xDEADBEEF;
    process_exit();
}

/*
 * Run one expected-fault test.
 *
 * Spawns a kernel-mode child whose entry point is `trigger`, sets the
 * global expected-fault marker, suspends the shell, and switches to the
 * child.  The child either faults (handler calls fault_kill_current,
 * which terminates it and resumes the shell) or returns (trigger's
 * trailing process_exit, same effect).  Either way control comes back
 * here when the shell resumes, and g_fault_observed tells us which path
 * was taken.
 */
static int test_exception_proc(int vec, void (*trigger)(void)) {
    pcb_t* child = process_create("faulttest", (uint64_t)trigger, 0);
    if (!child) return SELFTEST_FAIL;

    g_fault_observed = -1;
    g_expect_fault   = vec;

    suspend_self_for_diagnostic();
    scheduler_switch_to(child);

    g_expect_fault = -1;
    return (g_fault_observed == vec) ? SELFTEST_PASS : SELFTEST_FAIL;
}

static int test_exception_de(void) { return test_exception_proc(0x00, fault_de_trigger); }
static int test_exception_pf(void) { return test_exception_proc(0x0E, fault_pf_trigger); }
static int test_exception_gp(void) { return test_exception_proc(0x0D, fault_gp_trigger); }

/* =====================================================================
 * Shell command dispatch.
 *
 * Each test command is a thin wrapper around its test_xxx function.
 * The wrapper ignores the return value: the human already saw the
 * "Status : SUCCESS" line, and the prompt is all that's needed.
 * selftest calls the same functions and reads the return value.
 * ===================================================================== */

static void handle_command(const char *cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        vga_print("> ");
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        vga_print("\nCmds:\n  help, clear, version, reboot, pmmtest, info, mem, test,\n  vmmtest, serialtest, heapstat, maptest, testrec, heaptest,\n  heapcheck, heapstress, nxtest, syscall, elfload, proclist,\n  proccreate, vmmclone, runproc, schstat, testyield,\n  gdtdump, tssdump, atatest, fatmount, fatls, fatcat <file>,\n  selftest\n> ");
    } else if (strcmp(cmd, "clear") == 0) {
        vga_clear(); vga_print("donix v0.6.5\nType 'help'\n> ");
    } else if (strcmp(cmd, "version") == 0) {
        vga_print("\ndonix v0.6.5 (64-bit Core)\n> ");
    } else if (strcmp(cmd, "info") == 0) {
        vga_print("\n=== Boot Telemetry ===\n");
        if (g_bootinfo) {
            vga_print("  Magic : 0x"); vga_print_hex_cur(g_bootinfo->magic); vga_print("\n");
            vga_print("  PML4  : 0x"); vga_print_hex_cur(g_bootinfo->pml4_addr); vga_print("\n");
            vga_print("  MMap  : 0x"); vga_print_hex_cur(g_bootinfo->memory_map_addr);
            vga_print(" ("); vga_print_dec_cur(g_bootinfo->memory_map_count); vga_print(" entries)\n");
        } else {
            vga_print("  No boot info struct found.\n");
        }
        vga_print("> ");
    } else if (strcmp(cmd, "mem") == 0) {
        vga_print("\nMem Info (4KB Pages):\n");
        if (g_bootinfo && g_bootinfo->memory_map_count > 0) {
            MemoryMapEntry *m = (MemoryMapEntry *)g_bootinfo->memory_map_addr;
            uint64_t total_usable = 0;
            for (uint64_t i = 0; i < g_bootinfo->memory_map_count; i++) {
                if (m[i].type == 1) total_usable += m[i].length;
            }
            vga_print("  Usable RAM: "); vga_print_dec_cur(total_usable / (1024 * 1024)); vga_print(" MB\n");
        } else { vga_print("  MMap unavailable\n"); }
        vga_print("> ");
    } else if (strcmp(cmd, "reboot") == 0) {
        vga_print("\nResetting...\n");
        handle_reboot_sequence();
    } else if (strcmp(cmd, "pmmtest") == 0) {
        test_pmm();
        vga_print("> ");
    } else if (strcmp(cmd, "test") == 0) {
        vga_print("\n=== Exception Test ===\n  1 - #DE\n  2 - #PF\n  3 - #GP\nSelect: ");
        char c = 0;
        while (!kbd_buffer_get(&c)) { asm volatile("hlt"); }
        vga_putc(c); vga_print("\n");

        if (c == '1') {
            __asm__ volatile("xor %%rax, %%rax\n\txor %%rbx, %%rbx\n\tdiv %%rbx" : : : "rax","rbx","rdx");
        } else if (c == '2') {
            *(volatile uint64_t*)0xFFFFFFFF00000000ULL = 0xDEADBEEF;
        } else if (c == '3') {
            *(volatile uint64_t*)0x000FFFFF00000000ULL = 0xDEADBEEF;
        } else {
            vga_print("Aborted.\n");
        }
        vga_print("> ");
    } else if (strcmp(cmd, "vmmtest") == 0) {
        test_vmm();
        vga_print("> ");
    } else if (strcmp(cmd, "serialtest") == 0) {
        vga_print("\nSending COM1 packets...\n");
        serial_print("\n=== COM1 UART TEST PASSED ===\n");
        vga_print("Done.\n> ");
    } else if (strcmp(cmd, "heapstat") == 0) {
        vga_print("\nHeap stats streamed to serial.\n");
        heap_stats();                 // serial only, includes its own banner
        vga_print("> ");
    } else if (strcmp(cmd, "maptest") == 0) {
        test_map();
        vga_print("> ");
    } else if (strcmp(cmd, "testrec") == 0) {
        test_recursive();
        vga_print("> ");
    } else if (strcmp(cmd, "heaptest") == 0) {
        test_heap();
        vga_print("> ");
    } else if (strcmp(cmd, "heapcheck") == 0) {
        test_heap_validate();
        vga_print("> ");
    } else if (strcmp(cmd, "heapstress") == 0) {
        test_heap_stress();
        vga_print("> ");
    } else if (strcmp(cmd, "nxtest") == 0) {
        test_nx();
        vga_print("> ");
    } else if (strcmp(cmd, "syscall") == 0) {
        test_syscall();
        vga_print("> ");
    } else if (strcmp(cmd, "elfload") == 0) {
        vga_print("\n--- ELF Loader ---\n");
        if (test_program_len == 0) { vga_print("Binary missing.\n> "); return; }
        pcb_t* proc = process_create("elf_prog", 0x8000000000ULL, 0);
        if (proc) {
            uint64_t entry = load_elf_into_user_process(proc, test_program);
            if (entry != 0) {
                keyboard_buffer_flush();
                scheduler_ready_queue_add(proc);
                suspend_self_for_diagnostic();
                scheduler_switch_to(proc);
            } else { vga_print("Load failure.\n"); process_destroy(proc); }
        }
        vga_print("> ");
    } else if (strcmp(cmd, "proclist") == 0) {
        vga_print("\nTrace streamed to serial logs.\n"); process_dump_all(); vga_print("> ");
    } else if (strcmp(cmd, "vmmclone") == 0) {
        process_test_clone(); vga_print("> ");
    } else if (strcmp(cmd, "proccreate") == 0) {
        pcb_t* test = process_create("testproc", 0xDEADBEEF, 0);
        if (test) { scheduler_ready_queue_remove(test); vga_print("\nPCB alloc OK\n"); }
        vga_print("> ");
    } else if (strcmp(cmd, "runproc") == 0) {
        pcb_t* proc = process_create("testproc", (uint64_t)test_process_entry, 0);
        if (proc) {
            suspend_self_for_diagnostic();
            scheduler_switch_to(proc);
        }
        vga_print("> ");
    } else if (strcmp(cmd, "schstat") == 0) {
        PRINT_BOTH("\n=== Scheduler Dashboard ===\n");
        scheduler_stats();
        extern volatile uint64_t g_ticks;
        vga_print("  Total CPU Ticks: "); vga_print_dec_cur(g_ticks); vga_print("\n> ");
    } else if (strcmp(cmd, "testyield") == 0) {
        PRINT_BOTH("\n--- Thread Yield Test ---\n");
        pcb_t* p1 = process_create("test1", (uint64_t)test_process1, 0);
        pcb_t* p2 = process_create("test2", (uint64_t)test_process2, 0);
        if (p1 && p2) {
            suspend_self_for_diagnostic();
            scheduler_switch_to(p1);
            PRINT_BOTH("  Status   : SUCCESS\n");
        } else {
            PRINT_BOTH("  Status   : FAILED\n");
            if (p1) process_destroy(p1);
            if (p2) process_destroy(p2);
        }
        vga_print("> ");
    } else if (strcmp(cmd, "gdtdump") == 0) {
        gdt_dump(); vga_print("> ");
    } else if (strcmp(cmd, "tssdump") == 0) {
        tss_dump(); vga_print("> ");
    } else if (strcmp(cmd, "atatest") == 0) {
        test_ata();
        vga_print("> ");
    } else if (strcmp(cmd, "fatmount") == 0) {
        test_fat_mount();
        vga_print("> ");
    } else if (strcmp(cmd, "fatls") == 0) {
        test_fat_ls();
        vga_print("> ");
    } else if (strcmp(cmd, "fatcat") == 0 || strncmp(cmd, "fatcat ", 7) == 0) {
        PRINT_BOTH("\n=== FatFs File Viewer ===\n");

        const char* filename = NULL;
        if (cmd[6] == ' ') {
            filename = cmd + 7;
            while (*filename == ' ') filename++;
        }

        if (!filename || *filename == '\0') {
            PRINT_BOTH("  Usage    : fatcat <filename>\n> ");
            return;
        }

        FIL file;
        FRESULT res;
        UINT bytes_read;

        char* file_buf = (char*)kmalloc(512);
        if (!file_buf) {
            PRINT_BOTH("  Error    : Out of memory\n> ");
            return;
        }

        char path[300];
        path[0] = '0'; path[1] = ':'; path[2] = '/'; path[3] = '\0';

        int p_idx = 3;
        while (*filename && p_idx < 63) {
            path[p_idx++] = *filename++;
        }
        path[p_idx] = '\0';

        res = f_open(&file, path, FA_READ);

        if (res == FR_OK) {
            PRINT_BOTH("--- Content of "); PRINT_BOTH(path); PRINT_BOTH(" ---\n");

            while (1) {
                res = f_read(&file, file_buf, 511, &bytes_read);
                if (res != FR_OK || bytes_read == 0) break;

                file_buf[bytes_read] = '\0';
                PRINT_BOTH(file_buf);
            }

            PRINT_BOTH("\n-------------------------------\n");
            f_close(&file);
        } else {
            PRINT_BOTH("  Status   : FAILED (Cannot open file. Code ");
            PRINT_BOTH_DEC((uint64_t)res);
            PRINT_BOTH(")\n");
        }

        kfree(file_buf);
        vga_print("> ");
    } else if (strcmp(cmd, "selftest") == 0) {
        PRINT_BOTH("\n=== Kernel Self-Test ===\n");
        int pass = 0, fail = 0;

        #define RUN(name) do {                                              \
            PRINT_BOTH("  [" #name "] ... ");                               \
            int r = test_##name();                                          \
            if (r == SELFTEST_PASS)      { PRINT_BOTH("PASS\n"); pass++; }  \
            else                         { PRINT_BOTH("FAIL\n"); fail++; }  \
        } while (0)

        RUN(gdt);
        RUN(tss);
        RUN(pmm);
        RUN(vmm);
        RUN(map);
        RUN(recursive);
        RUN(heap);
        RUN(heap_validate);
        RUN(heap_stress);
        RUN(nx);
        RUN(syscall);
        RUN(ata);
        RUN(fat_mount);
        RUN(fat_ls);
        RUN(exception_de);
        RUN(exception_pf);
        RUN(exception_gp);

        #undef RUN

        PRINT_BOTH("  ---\n  ");
        PRINT_BOTH_DEC((uint64_t)pass); PRINT_BOTH(" passed, ");
        PRINT_BOTH_DEC((uint64_t)fail); PRINT_BOTH(" failed\n");
        vga_print("> ");
    } else {
        vga_print("\nUnknown command.\n> ");
    }
}
__attribute__((noreturn)) void kmain_shell_loop(void) {
    vga_print("donix v0.6.5\n> ");
    char cmd_buffer[128]; int cmd_pos = 0;
    for (;;) {
        asm volatile("hlt"); char c;
        if (kbd_buffer_get(&c)) {
            /*
             * Input bytes are the Unix terminal convention, matching
             * what keyboard.c delivers since session 31 and what every
             * other reader in the system (musl_sh, busybox ash) already
             * expects:
             *
             *   Backspace -> 0x7F (DEL), not 0x08 (BS)
             *   Enter     -> 0x0D (CR),  not 0x0A (LF)
             *
             * The previous comparisons ('\b' and '\n') predate that
             * change.  With DEL and CR arriving, neither byte matched:
             * Enter was dropped (CR < ' '), so the line never
             * terminated, and Backspace was dropped (DEL > '~'), so
             * nothing was erased.
             *
             * The echo calls below still use '\b' and '\n' -- those
             * are VGA OUTPUT control codes, not input bytes, and are
             * unaffected by what the keyboard delivers.
             */
            if (c == 0x7F) {
                if (cmd_pos > 0) { cmd_pos--; vga_putc('\b'); }
                continue;
            }
            if (c == '\r') {
                vga_putc('\n'); cmd_buffer[cmd_pos] = '\0';
                handle_command(cmd_buffer); cmd_pos = 0; continue;
            }
            if (c >= ' ' && c <= '~' && cmd_pos < 127) { cmd_buffer[cmd_pos++] = c; vga_putc(c); }
        }
    }
}

/*
 * Load an ELF file from the FAT into a freshly kmalloc'd buffer and
 * return it.  Caller frees.  Returns NULL on any failure (open, read,
 * size out of range, kmalloc).
 *
 * This duplicates the read-a-file-into-a-buffer logic from sys_spawn
 * in user_syscall.c.  It is intentionally not factored out yet; when a
 * third caller needs it, move it to a shared helper.
 */
static uint8_t* load_file_to_buffer(const char* path, size_t* out_size) {
    FIL file;
    if (f_open(&file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) return NULL;
    FSIZE_t sz = f_size(&file);
    if (sz == 0 || sz > 4ULL * 1024 * 1024) { f_close(&file); return NULL; }
    uint8_t* buf = (uint8_t*)kmalloc((size_t)sz);
    if (!buf) { f_close(&file); return NULL; }
    UINT total = 0;
    while (total < sz) {
        UINT br = 0;
        UINT want = (UINT)(sz - total);
        if (want > 4096) want = 4096;
        if (f_read(&file, buf + total, want, &br) != FR_OK) {
            kfree(buf); f_close(&file); return NULL;
        }
        if (br == 0) break;
        total += br;
    }
    f_close(&file);
    if (total != sz) { kfree(buf); return NULL; }
    *out_size = (size_t)sz;
    return buf;
}

/*
 * Load an ELF into a user process and repair the resume frame.
 *
 * process_create bakes its `entry_point` argument into the resume
 * frame that context_switch.asm restores; elf_load_into_process does
 * not touch that frame, it only maps the ELF's segments and returns
 * e_entry.  If the caller passed anything other than the ELF's real
 * entry to process_create, the scheduler resumes the process at the
 * wrong RIP and it #PFs on a page that was never mapped.
 *
 * This was invisible while the only caller was the embedded newlib
 * shell, which is linked at exactly 0x8000000000 — the same value
 * kmain passed as entry_point, so the stale frame slot was already
 * correct by coincidence.  musl_sh is linked at 0x400000, and the
 * coincidence no longer holds.
 *
 * The frame layout is whatever process_create builds (20 slots, first
 * push = highest address):
 *
 *   [rsp + 0x00] r15    [rsp + 0x08] r14
 *   [rsp + 0x10] r13    [rsp + 0x18] r12
 *   [rsp + 0x20] r11    [rsp + 0x28] r10
 *   [rsp + 0x30] r9     [rsp + 0x38] r8
 *   [rsp + 0x40] rbp    [rsp + 0x48] rdi
 *   [rsp + 0x50] rsi    [rsp + 0x58] rdx
 *   [rsp + 0x60] rcx    [rsp + 0x68] rbx
 *   [rsp + 0x70] rax    [rsp + 0x78] RIP
 *   [rsp + 0x80] CS     [rsp + 0x88] RFLAGS
 *   [rsp + 0x90] RSP    [rsp + 0x98] SS
 *
 * This is not the syscall-entry frame that sys_execve rewrites (that
 * one is 16 slots and uses ktop[-7] / ktop[-9]).  Both frames are 20
 * slots here because context_switch.asm's .pop_frame path pops
 * exactly 20, and this is the layout it consumes.
 *
 * Returns 0 on failure, or the ELF's entry point on success.  The
 * process is left off the ready queue on success; the caller decides
 * when to enqueue.  (process_create enqueues it immediately, so the
 * helper removes it first.)
 */
static uint64_t load_elf_into_user_process(pcb_t* pcb, const void* elf_data) {
    if (!pcb || !elf_data) return 0;

    scheduler_ready_queue_remove(pcb);

    uint64_t entry = elf_load_into_process(pcb, elf_data);
    if (entry == 0) return 0;

    pcb->entry_point = entry;
    pcb->rip         = entry;

    /*
     * Rewrite the resume frame's RIP slot.  RSP at [rsp + 0x90] is
     * already pcb->user_stack_top for a user-mode entry_point, which
     * is what the ELF was loaded against; no change needed there.
     */
    uint64_t* frame = (uint64_t*)pcb->rsp;
    frame[0x78 / 8] = entry;

    return entry;
}

void kmain(BootInfo *info) {
    g_bootinfo = info;
    vga_clear();
    serial_init();
    
    validate_bootinfo(info);
    vga_set_cursor_shape(0x00, 0x0F);
    gdt_init();
    idt_init();
    pit_init(100);

    /*
     * Enable EFER.NXE before sti, before pmm_init, before anything
     * that could map a page with PT_NX.  See enable_nx() above for
     * the full rationale.
     */
    enable_nx();

    asm volatile("sti");
    pmm_init(g_bootinfo);
    vmm_init(info);
    heap_init(HEAP_START, HEAP_INITIAL_SIZE);
    ata_init();    
    scheduler_init();
    tss_init();
    process_init();
    keyboard_init();
    enable_user_fsgsbase();
    user_syscall_init();

    __asm__ volatile (
        "mov %%cr0, %%rax\n\tand $0xFFFB, %%ax\n\tor $0x2, %%ax\n\tmov %%rax, %%cr0\n\t"
        "mov %%cr4, %%rax\n\tor $0x600, %%eax\n\tmov %%rax, %%cr4" : : : "rax", "cc", "memory"
    );
    PRINT_BOTH("CPU: SSE extensions enabled.\n");

    static FATFS boot_fs;

    if (f_mount(&boot_fs, "0:", 1) == FR_OK) {
#if FAT_CONFIG_SINGLE_DRIVE
        PRINT_BOTH("Storage: single-drive, FAT@LBA 2048\n");
#else
        PRINT_BOTH("Storage: dual-drive, FAT@LBA 0 on slave\n");
#endif
    } else {
        PRINT_BOTH("WARN: Auto-mounting boot device failed.\n");
    }

    vga_clear();
    serial_print("Prompt: press 'k' for debug loop\n");
    vga_print("\nPress 'k' for kernel shell, else launching user shell...\n");

    {
        extern volatile uint64_t g_ticks;
        uint64_t deadline = g_ticks + 200;
        char choice = 0;
        while (g_ticks < deadline) {
            char c; if (kbd_buffer_get(&c)) { choice = c; break; }
            asm volatile("hlt");
        }
        if (choice == 'k' || choice == 'K') {
            extern void kmain_shell_loop(void);
            pcb_t* shell = process_create("kshell",
                                          (uint64_t)kmain_shell_loop, 0);
            if (!shell) {
                serial_print("PANIC: no shell PCB\n");
                while (1) asm volatile("hlt");
            }
            scheduler_ready_queue_remove(shell);
            process_set_kernel_shell(shell);
            scheduler_switch_to(shell);
        }
    }

    /*
     * Default boot shell: musl_sh, loaded from the FAT as
     * 0:/MUSL_SH.ELF.
     *
     * The embedded newlib shell fallback was removed at A5 step 6.
     * If the FAT read fails, the file is missing, or the ELF load
     * fails, there is no second shell to fall back to; the kernel
     * panics with a clear message instead of dropping to a console
     * that cannot accept commands.
     */
    {
        size_t fat_len = 0;
        uint8_t* fat_buf = load_file_to_buffer("0:/MUSL_SH.ELF", &fat_len);
        if (!fat_buf) {
            serial_print("PANIC: 0:/MUSL_SH.ELF not loadable; no shell available\n");
            while (1) asm volatile("hlt");
        }

        serial_print("Shell: booting musl_sh from FAT\n");

        pcb_t* shell = process_create("musl_sh", 0x400000ULL, 0);
        if (!shell) {
            serial_print("PANIC: no shell PCB\n");
            kfree(fat_buf);
            while (1) asm volatile("hlt");
        }

        uint64_t shell_entry = load_elf_into_user_process(shell, fat_buf);
        kfree(fat_buf);

        if (shell_entry == 0) {
            serial_print("PANIC: musl_sh ELF load failed\n");
            while (1) asm volatile("hlt");
        }

        scheduler_ready_queue_add(shell);
    }

    kernel_idle_loop();
}
