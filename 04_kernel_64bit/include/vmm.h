#pragma once
#include <stdint.h>
#include "bootinfo.h"

#define HHDM_START 0xFFFF800000000000ULL
#define PAGE_SIZE 4096

#define PT_PRESENT  0x001
#define PT_WRITE    0x002
#define PT_USER     0x004
#define PT_HUGE     0x080
#define PT_NX       0x8000000000000000ULL

#define RECURSIVE_PML4_INDEX 510

/*
 * Mask a page-table entry down to its physical address.
 *
 * The NX bit lives at bit 63.  A raw `entry & ~0xFFFULL` leaves it
 * set, producing a "physical" address with bit 63 set, which is
 * non-canonical and faults when dereferenced through the HHDM.  Use
 * this everywhere an entry is masked to a physical address; do not
 * write `entry & ~0xFFFULL` directly.
 *
 * Moved here from vmm.c so exception handlers (interrupts.c) can use
 * it too.  interrupts.c already includes vmm.h.
 */
static inline uint64_t pte_phys(uint64_t entry) {
    return entry & ~0xFFFULL & ~PT_NX;
}

void vmm_init(BootInfo* info);
void* ensure_hhdm_mapped(uint64_t phys);
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
int vmm_is_mapped(uint64_t virt);
void vmm_dump_page_table(uint64_t virt);

uint64_t vmm_clone_page_table(uint64_t src_cr3);
/*
 * Build a fresh address space with ONLY the shared kernel mappings
 * (PML4 entries 256..511 shallow-copied from src_cr3) and an empty
 * low half.  This is the exec primitive: vmm_clone_page_table
 * deep-copies the low half (right for fork, wrong for exec on a
 * vfork child, where the new image must be loaded into a clean
 * space rather than over the parent's user PTEs).
 */
uint64_t vmm_create_address_space(uint64_t src_cr3);
uint64_t vmm_get_phys_from_cr3(uint64_t cr3, uint64_t virt);
int vmm_map_page_in_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page_in_cr3(uint64_t cr3, uint64_t virt);
uint64_t* vmm_pte_ptr_in_cr3(uint64_t cr3, uint64_t virt);
