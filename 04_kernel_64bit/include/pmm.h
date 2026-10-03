#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "bootinfo.h"

#define PAGE_SIZE 4096

// Page type system - each physical page has a purpose
typedef enum {
    PAGE_FREE = 0,          // Available for allocation
    PAGE_KERNEL,            // Kernel code/data (reserved)
    PAGE_PAGE_TABLE,        // Page table page (VMM internal only)
    PAGE_USER_DATA,         // User process data (ELF, stack, heap)
    PAGE_USER_PAGE_TABLE,   // User process page table
    PAGE_DEVICE,            // MMIO/device memory
    PAGE_RESERVED,          // Temporarily reserved
    PAGE_TYPE_COUNT
} page_type_t;

// Page information structure
typedef struct {
    uint64_t phys_addr;
    page_type_t type;
    uint64_t ref_count;     // Number of references (for shared pages)
    uint64_t owner_pid;     // Which process owns this page (0 = kernel)
    uint64_t owner_virt;    // Virtual address it's mapped at (for debugging)
} page_info_t;

// Initialize PMM with memory map
void pmm_init(BootInfo *info);

// Allocate a page with a specific type
uint64_t pmm_alloc_page(page_type_t type);

// Convenience wrappers for common allocations
static inline uint64_t pmm_alloc_page_for_elf(void) {
    return pmm_alloc_page(PAGE_USER_DATA);
}

static inline uint64_t pmm_alloc_page_for_tables(void) {
    return pmm_alloc_page(PAGE_PAGE_TABLE);
}

static inline uint64_t pmm_alloc_page_for_user_tables(void) {
    return pmm_alloc_page(PAGE_USER_PAGE_TABLE);
}

static inline uint64_t pmm_alloc_page_for_kernel(void) {
    return pmm_alloc_page(PAGE_KERNEL);
}

// Free a page (returns to PAGE_FREE)
void pmm_free_page(uint64_t phys_addr);

// Get the type of a page
page_type_t pmm_get_page_type(uint64_t phys_addr);

// Check if a page can be used as a page table
int pmm_can_use_as_page_table(uint64_t phys_addr);

// Reserve a page (temporarily mark as PAGE_RESERVED)
void pmm_reserve_page(uint64_t phys_addr);

// Unreserve a page (restore to previous type)
void pmm_unreserve_page(uint64_t phys_addr);

// Get statistics
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);
void pmm_dump_stats(void);

/*
 * Fault-injection hook for the process_create failure-path test
 * (kmain.c, test_create_fail).
 *
 * Arms a one-shot failure on the next pmm_alloc_page whose `type`
 * matches.  The next matching allocation returns 0 without touching
 * the bitmap, the zone cursor, or any counter; the hook disarms
 * itself on the first match.  A non-matching allocation is served
 * normally and does not disarm.
 *
 * Kernel-side only; no syscall door.  It exists so a test can drive
 * process_create into its allocation-failure exits, which a healthy
 * boot never reaches.  See docs/gotchas.md, "A function that has
 * never run is correct by inspection only."
 */
void pmm_debug_fail_next_of_type(page_type_t type);

#endif
