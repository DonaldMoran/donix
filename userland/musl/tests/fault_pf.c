/*
 * fault_pf.c — user-mode #PF kill test.
 *
 * Dereference a bad pointer from ring 3.  isr14_handler kills the
 * faulting process on the error-code user bit (error_code & 4),
 * which is set for any ring-3 page fault, so nothing needs to be
 * poked first -- g_expect_fault is the kernel-mode test path and is
 * not involved here.
 *
 * Expected: the kernel prints the #PF diagnostic on the serial
 * port, kills this process, and the shell prompt returns.  The
 * kernel stays up.  If "STILL ALIVE" prints, the kill path failed.
 *
 * ADDRESS CHOICE.  The address must be:
 *
 *   (a) canonical -- on x86_64 with 4-level paging, bits 63:47 must
 *       all equal bit 47.  0xDEADBEEF0000 is NOT canonical: bit 47
 *       is 1 (0xDEAD's top bit) but bits 63:48 are 0.  A store to a
 *       non-canonical address raises #GP, not #PF -- and the ring-3
 *       #GP path halts the machine instead of killing the process,
 *       so the test would take the kernel down before exercising
 *       the #PF handler at all.
 *
 *   (b) unmapped -- so the fault is a genuine translation failure
 *       and not a lucky hit on a present page.
 *
 *   (c) clear of every region donix maps:
 *         ELF image   0x0000000000400000 .. 0x0000000000600000
 *         user stack  0x0000008000000000 ..
 *         brk heap    0x0000008000200000 ..
 *         mmap window 0x0000008010000000 ..
 *
 * 0x0000000010000000 (256 MB) satisfies all three: bit 47 = 0 and
 * bits 63:47 = 0, it is above the 2 MB ELF region and far below the
 * 512 GB user stack, and nothing in donix maps it.
 */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    printf("fault_pf: about to dereference a bad pointer\n");
    fflush(stdout);

    volatile uint64_t *bad = (volatile uint64_t *)0x0000000010000000ULL;
    // volatile uint64_t *bad = (volatile uint64_t *)0xDEADBEEF0000ULL; << use this if  you want to generate a gp fault instead
    *bad = 1;   /* page fault, ring 3 */

    printf("fault_pf: STILL ALIVE -- kill path failed\n");
    fflush(stdout);
    return 1;
}
