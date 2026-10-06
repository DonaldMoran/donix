/*
 * mmap_nx.c -- does a mapping made without PROT_EXEC execute?
 *
 * The 7e fault family has been captured with RIP in the mmap
 * window and error code 0x4 or 0x5 -- a data access, not an
 * instruction fetch.  That shape is only possible if the CPU
 * fetched and executed instructions at that RIP without faulting,
 * which means the page containing it was executable.
 *
 * sys_mmap sets PT_NX when PROT_EXEC is absent.  If the page is
 * still executable, the PT_NX bit is being dropped somewhere
 * between sys_mmap and the page walk; if it is not, the fault
 * RIPs in the captures are on pages the process mapped PROT_EXEC.
 *
 * This test makes one non-executable mapping, plants a `ret` in
 * it, and calls it.
 *
 *   - If it returns:  PT_NX is not enforced.  The kernel prints
 *     nothing; the test prints MMAP-NX: EXECUTED and exits 0.
 *   - If it faults:   PT_NX is enforced.  The kernel prints the
 *     #PF diagnostic (RIP = the page, error 0x15), the fault
 *     handler kills the process, and the shell prints
 *     "Segmentation fault".  No MMAP-NX line is printed.
 *
 * The test itself is therefore silent-on-success at the process
 * level: a reader distinguishes the two outcomes by whether the
 * MMAP-NX line appears, not by an exit code, because a fault
 * kills the process before it can exit.
 */

#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
    /* One page, RW, no X.  This is what musl's mallocng asks for. */
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 2;
    }

    /* Plant `ret` (0xC3) at the start of the mapping.  Also plant
     * a short nop sled in case the entry point is offset for any
     * reason.  0x90 is nop. */
    uint8_t *code = (uint8_t *)p;
    code[0] = 0x90;  /* nop */
    code[1] = 0x90;  /* nop */
    code[2] = 0xC3;  /* ret */
    code[3] = 0xC3;  /* ret */

    printf("MMAP-NX: mapping at %p, calling...\n", p);
    fflush(stdout);

    /* Call into the mapping.  If the page is non-executable this
     * faults with error 0x15 (present + user + instruction fetch)
     * and the process is killed before the next printf runs.  If
     * the page is executable, the two nops run, the `ret` pops
     * the caller's return address (which is where control came
     * from), and we are back here. */
    void (*f)(void) = (void (*)(void))p;
    f();

    /* Only reached if the mapping executed. */
    printf("MMAP-NX: EXECUTED -- PT_NX not enforced on this mapping\n");
    fflush(stdout);

    munmap(p, 4096);
    return 0;
}
