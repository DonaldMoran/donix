#include "pmm.h"
#include "bootinfo.h"
#include "serial.h"
#include "string.h"

#define DBG 0  // Set to 0 to disable, 1 to enable
#define MAX_PHYS_MEM   (128ULL * 1024 * 1024)
#define MAX_PAGES      (MAX_PHYS_MEM / PAGE_SIZE)
#define BITMAP_SIZE    (MAX_PAGES / 8)

/*
 * Safety floor added on top of BootInfo.kernel_phys_end when
 * reserving the kernel's physical range. stage2.asm reports the end
 * of the file-backed portion of the kernel; .bss is NOBITS and has no
 * file representation, so stage2 cannot know its size. The
 * authoritative reservation is computed from _kernel_end (see the
 * reservation block in pmm_init); this margin is a floor so that if
 * BootInfo ever reports a larger range than _kernel_end (which would
 * happen if stage2's constants and the actual kernel image drift),
 * the larger of the two wins.
 */
#define KERNEL_RESERVE_MARGIN  0x4000ULL

static uint8_t pmm_bitmap[BITMAP_SIZE];
static page_info_t pmm_page_info[MAX_PAGES];
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_free_pages = 0;
static uint64_t pmm_max_physical = 0;

static uint64_t pmm_low_start_page = 0;
static uint64_t pmm_low_end_page   = 0;
static uint64_t pmm_high_start_page = 0;
static uint64_t pmm_high_end_page   = 0;

static uint64_t pmm_next_low_page = 0;
static uint64_t pmm_next_high_page = 0;

static uint64_t pages_by_type[PAGE_TYPE_COUNT] = {0};

/*
 * Fault-injection state.  See pmm_debug_fail_next_of_type in pmm.h
 * for the contract.  `pmm_fail_armed` is the one-shot latch;
 * `pmm_fail_type` is the type the next matching allocation must
 * have.  Both are read and written with interrupts off, inside
 * pmm_alloc_page's critical section, except for the setter, which
 * runs on the kernel self-test path with interrupts on.
 *
 * One branch on the hot path: predictable, and the flag is 0 on a
 * healthy boot.
 */
static int pmm_fail_armed = 0;
static page_type_t pmm_fail_type = PAGE_FREE;

void pmm_debug_fail_next_of_type(page_type_t type) {
    pmm_fail_armed = 1;
    pmm_fail_type = type;
}

/*
 * Save RFLAGS into *flags and disable interrupts.
 * Restore with pmm_irq_restore(*flags).
 *
 * The PMM's allocator does a non-atomic test-and-set on the bitmap:
 * bitmap_test(page) followed by bitmap_set(page). If a timer IRQ
 * arrives between those two, and the IRQ handler itself allocates a
 * page (timer_preempt_handler does not, but the scheduler it drives
 * can dispatch a process whose next instruction is a syscall that
 * does, and on some paths the handler itself may run preemptible
 * code), the same page can be handed out twice. That produces silent
 * cross-process aliasing: two PCBs' elf_page_list entries point at
 * the same physical page, and when one process is reclaimed its
 * pages are freed while the other is still using them.
 *
 * This bug class is exactly what produced the flaky, layout-dependent
 * symptoms in the SYS_EXEC bring-up: a corrupted path string, a
 * child that faults at RIP=0/1, and different behavior on every
 * rebuild because the interrupt timing shifts with the code size.
 *
 * The fix is to disable interrupts across the entire critical
 * section. The restore preserves whatever IF was on entry, so the
 * allocator can be called from an IRQ context (IF=0 on entry) or
 * from normal kernel code (IF=1 on entry).
 */
static inline uint64_t pmm_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void pmm_irq_restore(uint64_t flags) {
    if (flags & 0x200) {
        __asm__ volatile("sti" ::: "memory");
    }
}

static void bitmap_set(uint64_t page) {
    uint64_t byte = page / 8;
    uint8_t bit = page % 8;
    pmm_bitmap[byte] |= (1 << bit);
}

static void bitmap_clear(uint64_t page) {
    uint64_t byte = page / 8;
    uint8_t bit = page % 8;
    pmm_bitmap[byte] &= ~(1 << bit);
}

static int bitmap_test(uint64_t page) {
    uint64_t byte = page / 8;
    uint8_t bit = page % 8;
    return (pmm_bitmap[byte] >> bit) & 1;
}

static const char* page_type_string(page_type_t type) {
    switch(type) {
        case PAGE_FREE: return "FREE";
        case PAGE_KERNEL: return "KERNEL";
        case PAGE_PAGE_TABLE: return "PAGE_TABLE";
        case PAGE_USER_DATA: return "USER_DATA";
        case PAGE_USER_PAGE_TABLE: return "USER_PT";
        case PAGE_DEVICE: return "DEVICE";
        case PAGE_RESERVED: return "RESERVED";
        default: return "UNKNOWN";
    }
}

/*
 * Scan one zone for a free, allocatable page, wrapping once.
 *
 * `from` is the zone's cursor; `lo` and `hi` are its bounds
 * (inclusive); `min_page` is the lowest page number the caller may
 * use (0, or 0x200 for kernel/table/reserved allocations, which must
 * stay above 2 MB).  `up` is 1 for the LOW zone (scan from `from`
 * toward `hi`) and 0 for HIGH (`from` toward `lo`).
 *
 * A single linear scan from the cursor does NOT find a page that has
 * been free the whole time and sits on the far side of the cursor:
 * the cursor only moves on allocate (past the page just taken) and on
 * free (to the page just freed, if it is on the near side).  A page
 * that was never taken, below a LOW cursor, is invisible to an upward
 * scan -- which is what "Out of LOW-zone memory" with pmm_free_pages
 * healthy means.  It reproduces after adding userland ELFs, because
 * the extra early allocations push the cursor past pages that were
 * free from boot, and it clears on reboot because pmm_compute_zones
 * resets the cursor to the start of the zone.
 *
 * So scan [cursor, far] first; if that finds nothing, scan
 * [near, cursor).  The cursor is not moved by a failed scan.
 *
 * Returns the page index, or 0 if the zone has no usable free page.
 * (Page 0 is never allocatable, so 0 doubles as "not found".)
 *
 * Called with interrupts off (the caller holds pmm_irq_save).
 */
static uint64_t pmm_scan_zone(uint64_t from, uint64_t lo, uint64_t hi,
                              int up, uint64_t min_page) {
    if (up) {
        /* Pass 1: cursor up to the far bound. */
        for (uint64_t page = from; page <= hi; page++) {
            if (page >= min_page && !bitmap_test(page)) return page;
            if (page == hi) break;
        }
        /* Pass 2: the near bound up to just below the cursor. */
        if (from > lo) {
            for (uint64_t page = lo; page < from; page++) {
                if (page >= min_page && !bitmap_test(page)) return page;
                if (page == from - 1) break;
            }
        }
    } else {
        /* Pass 1: cursor down to the near bound. */
        for (uint64_t page = from; page >= lo; page--) {
            if (page >= min_page && !bitmap_test(page)) return page;
            if (page == lo) break;
        }
        /* Pass 2: the far bound down to just above the cursor. */
        if (from < hi) {
            for (uint64_t page = hi; page > from; page--) {
                if (page >= min_page && !bitmap_test(page)) return page;
                if (page == from + 1) break;
            }
        }
    }
    return 0;
}

static void pmm_compute_zones(uint64_t max_pages) {
    uint64_t first_free = max_pages;
    uint64_t last_free  = 0;

    for (uint64_t page = 0; page < max_pages; page++) {
        if (!bitmap_test(page)) {
            if (page < first_free) first_free = page;
            if (page > last_free)  last_free  = page;
        }
    }

    if (first_free == max_pages) {
        pmm_low_start_page = pmm_low_end_page = 0;
        pmm_high_start_page = pmm_high_end_page = 0;
        pmm_next_low_page = 0;
        pmm_next_high_page = 0;
        serial_print("PMM: WARNING - No free pages to compute zones\n");
        return;
    }

    uint64_t span = last_free - first_free;
    uint64_t low_span = span / 4;

    pmm_low_start_page  = first_free;
    pmm_low_end_page    = first_free + low_span;
    if (pmm_low_end_page < pmm_low_start_page) {
        pmm_low_end_page = pmm_low_start_page;
    }

    pmm_high_start_page = pmm_low_end_page + 1;
    pmm_high_end_page   = last_free;

    if (pmm_high_start_page > pmm_high_end_page) {
        pmm_high_start_page = pmm_high_end_page = 0;
    }

    pmm_next_low_page  = pmm_low_start_page;
    pmm_next_high_page = pmm_high_end_page;
}

void pmm_init(BootInfo *info) {
    for (uint64_t i = 0; i < BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    for (uint64_t i = 0; i < MAX_PAGES; i++) {
        pmm_page_info[i].phys_addr = i * PAGE_SIZE;
        pmm_page_info[i].type = PAGE_RESERVED;
        pmm_page_info[i].ref_count = 0;
        pmm_page_info[i].owner_pid = 0;
        pmm_page_info[i].owner_virt = 0;
    }
    memset(pages_by_type, 0, sizeof(pages_by_type));

    pmm_total_pages = 0;
    pmm_free_pages = 0;
    pmm_max_physical = 0;
    pmm_next_low_page = 0;
    pmm_next_high_page = 0;
    pmm_low_start_page = pmm_low_end_page = 0;
    pmm_high_start_page = pmm_high_end_page = 0;

    if (!info || info->memory_map_count == 0) {
        pmm_max_physical = 128ULL * 1024 * 1024;
        uint64_t max_pages = pmm_max_physical / PAGE_SIZE;
        for (uint64_t addr = 0; addr < pmm_max_physical; addr += PAGE_SIZE) {
            uint64_t page = addr / PAGE_SIZE;
            if (page < MAX_PAGES) {
                if (addr < 16ULL * 1024 * 1024) {
                    bitmap_set(page);
                    pmm_page_info[page].type = PAGE_KERNEL;
                    pages_by_type[PAGE_KERNEL]++;
                } else {
                    bitmap_clear(page);
                    pmm_page_info[page].type = PAGE_FREE;
                    pmm_total_pages++;
                    pmm_free_pages++;
                    pages_by_type[PAGE_FREE]++;
                }
            }
        }
        pmm_compute_zones(max_pages);
        serial_lock();
        serial_print("PMM: init OK (fallback), free=");
        serial_print_dec(pmm_free_pages);
        serial_print(" pages\n");
        serial_unlock();
        return;
    }

    MemoryMapEntry *m = (MemoryMapEntry *)info->memory_map_addr;

    for (uint64_t i = 0; i < info->memory_map_count; i++) {
        uint64_t end = m[i].base + m[i].length;
        if (end > pmm_max_physical) {
            pmm_max_physical = end;
        }
    }

    if (pmm_max_physical > MAX_PHYS_MEM) {
        pmm_max_physical = MAX_PHYS_MEM;
    }

    uint64_t max_pages = pmm_max_physical / PAGE_SIZE;
    if (max_pages > MAX_PAGES) max_pages = MAX_PAGES;

    for (uint64_t i = 0; i < max_pages; i++) {
        bitmap_set(i);
        pmm_page_info[i].type = PAGE_RESERVED;
    }

    for (uint64_t i = 0; i < info->memory_map_count; i++) {
        if (m[i].type != 1) continue;

        uint64_t base = m[i].base;
        uint64_t length = m[i].length;
        uint64_t end = base + length;

        base = (base / PAGE_SIZE) * PAGE_SIZE;
        end = ((end + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

        for (uint64_t addr = base; addr < end && addr < pmm_max_physical; addr += PAGE_SIZE) {
            uint64_t page = addr / PAGE_SIZE;
            if (page < max_pages) {
                if (bitmap_test(page)) {
                    bitmap_clear(page);
                    pmm_page_info[page].type = PAGE_FREE;
                    pmm_total_pages++;
                    pmm_free_pages++;
                    pages_by_type[PAGE_FREE]++;
                }
            }
        }
    }

    for (uint64_t addr = 0; addr < 0x100000; addr += PAGE_SIZE) {
        uint64_t page = addr / PAGE_SIZE;
        if (page < max_pages) {
            if (!bitmap_test(page)) {
                bitmap_set(page);
                pmm_page_info[page].type = PAGE_KERNEL;
                pmm_free_pages--;
                pages_by_type[PAGE_FREE]--;
                pages_by_type[PAGE_KERNEL]++;
            }
        }
    }

    /*
     * Reserve the kernel's physical footprint, .bss included.
     *
     * BootInfo.kernel_phys_end describes what stage2 loaded from disk.
     * That covers the file-backed portion of the image (.text, .rodata,
     * .data, .userelf) but NOT .bss, because .bss is NOBITS and takes
     * no space in the file. stage2 has no way to know its size.
     *
     * The kernel's .bss begins immediately after .userelf and can be
     * several megabytes (pmm_page_info alone is 1.25 MB,
     * kernel_stack_pool is 512 KB). If .bss extends past the reserved
     * range, the PMM will hand its pages out for page tables and
     * user data, silently corrupting the kernel's own state.
     *
     * _kernel_end, emitted by linker.ld after .bss, is the end of the
     * kernel's virtual address range. Its physical address is what
     * needs to be reserved. The higher-half base subtracted here must
     * match the linker script's .text base.
     *
     * KERNEL_RESERVE_MARGIN is kept as a floor so that a mismatch
     * between stage2's constant and the actual kernel image (in
     * either direction) still reserves the larger of the two ranges.
     */
    {
        extern char _kernel_end;
        uint64_t kend_virt = (uint64_t)&_kernel_end;
        uint64_t kend_phys = kend_virt - 0xFFFFFFFF80000000ULL;

        uint64_t kstart = info->kernel_phys_start;
        uint64_t kend   = kend_phys;

        if (info->kernel_phys_end + KERNEL_RESERVE_MARGIN > kend) {
            kend = info->kernel_phys_end + KERNEL_RESERVE_MARGIN;
        }

        kstart = (kstart / PAGE_SIZE) * PAGE_SIZE;
        kend = ((kend + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

        serial_lock();
        serial_print("PMM: reserving kernel [0x");
        serial_print_hex(kstart);
        serial_print(", 0x");
        serial_print_hex(kend);
        serial_print(") via _kernel_end\n");
        serial_unlock();

        for (uint64_t addr = kstart; addr < kend && addr < pmm_max_physical; addr += PAGE_SIZE) {
            uint64_t page = addr / PAGE_SIZE;
            if (page < max_pages) {
                if (!bitmap_test(page)) {
                    bitmap_set(page);
                    pmm_page_info[page].type = PAGE_KERNEL;
                    pmm_free_pages--;
                    pages_by_type[PAGE_FREE]--;
                    pages_by_type[PAGE_KERNEL]++;
                }
            }
        }
    }

    for (uint64_t i = 0; i < info->memory_map_count; i++) {
        if (m[i].type == 2 || m[i].type == 3 || m[i].type == 4 || m[i].type == 5) {
            uint64_t base = m[i].base;
            uint64_t end = base + m[i].length;
            base = (base / PAGE_SIZE) * PAGE_SIZE;
            end = ((end + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
            for (uint64_t addr = base; addr < end && addr < pmm_max_physical; addr += PAGE_SIZE) {
                uint64_t page = addr / PAGE_SIZE;
                if (page < max_pages) {
                    if (!bitmap_test(page)) {
                        bitmap_set(page);
                        pmm_page_info[page].type = PAGE_RESERVED;
                        pmm_free_pages--;
                        pages_by_type[PAGE_FREE]--;
                        pages_by_type[PAGE_RESERVED]++;
                    }
                }
            }
        }
    }

    pmm_compute_zones(max_pages);

    serial_lock();
    serial_print("PMM: init OK, free=");
    serial_print_dec(pmm_free_pages);
    serial_print(" pages (");
    serial_print_dec(pmm_free_pages * PAGE_SIZE / (1024 * 1024));
    serial_print(" MB)\n");
    serial_unlock();
}

/*
 * TEMPORARY DIAGNOSTIC — PMM HIGH-zone accounting anomaly.
 *
 * Prints every call to pmm_alloc_page before the IRQ-save critical
 * section, with the caller's return address.  The point is to
 * distinguish three cases:
 *
 *   (a) A flood of legitimate allocations from a kernel path we have
 *       not identified.  The RA distribution tells us which caller.
 *
 *   (b) A small number of calls with pmm_free_pages corrupted by an
 *       out-of-bounds write elsewhere in the kernel.  The call count
 *       will not match the free-page delta between consecutive prints.
 *
 *   (c) The counter jumping by thousands between two consecutive
 *       prints with the same RA — corruption happening in the window
 *       between calls, not inside them.
 *
 * Caveat: this print itself perturbs timing (serial_lock holds IF=0
 * for the duration of the line).  If the anomaly is preemption-
 * dependent, it may not reproduce with the diagnostic in place.  If
 * it does not reproduce, that is itself a signal.
 *
 * Remove this block once the anomaly is understood.
 */
#define PMM_ALLOC_DIAG 0
#if PMM_ALLOC_DIAG
static uint64_t pmm_alloc_diag_count = 0;
#endif

uint64_t pmm_alloc_page(page_type_t type) {
    uint64_t max_pages = pmm_max_physical / PAGE_SIZE;
    if (max_pages > MAX_PAGES) max_pages = MAX_PAGES;
    (void)max_pages;

    /*
     * Fault-injection hook.  Checked before the diagnostic block and
     * before the interrupt-save critical section: the hook is armed
     * and disarmed only on the kernel self-test path, which runs
     * single-threaded from the kernel shell, so no locking is needed
     * for the flag itself.  A match consumes the arm and returns 0
     * exactly as an out-of-memory allocation would.
     */
    if (pmm_fail_armed && type == pmm_fail_type) {
        pmm_fail_armed = 0;
        return 0;
    }

#if PMM_ALLOC_DIAG
    /*
     * Filter: suppress lines whose caller is inside sys_brk.
     *
     * sys_brk's growth loop calls pmm_alloc_page_for_elf once per
     * page.  When musl mis-uses the old increment-based brk ABI, that
     * loop runs for tens of thousands of iterations and floods the
     * log.  The interesting events are everything else.
     *
     * The comparison range [0xFFFFFFFF8010BBD0, 0xFFFFFFFF8010BDAF)
     * is sys_brk's symbol extent in the current kernel.elf.  If the
     * kernel layout changes, this range must be updated.  It is a
     * diagnostic filter, not a correctness check.
     */
    uint64_t ra = (uint64_t)__builtin_return_address(0);
    if (ra < 0xFFFFFFFF8010BBD0ULL || ra > 0xFFFFFFFF8010BDAFULL) {
        serial_lock();
        serial_print("pmm_alloc[");
        serial_print_dec(pmm_alloc_diag_count++);
        serial_print("]: type=");
        serial_print_dec((uint64_t)type);
        serial_print(" free=");
        serial_print_dec(pmm_free_pages);
        serial_print(" next_low=");
        serial_print_dec(pmm_next_low_page);
        serial_print(" next_high=");
        serial_print_dec(pmm_next_high_page);
        serial_print(" ra=0x");
        serial_print_hex(ra);
        serial_print("\n");
        serial_unlock();
    }
#endif

    /* Interrupts off: the test-and-set on the bitmap below must be
     * atomic with respect to any other allocation path. See the
     * pmm_irq_save() comment for the failure mode. */
    uint64_t flags = pmm_irq_save();
    uint64_t result = 0;

    if (type == PAGE_USER_DATA || type == PAGE_USER_PAGE_TABLE) {
        if (pmm_high_start_page == 0 && pmm_high_end_page == 0) {
            serial_print("PMM: ERROR - No HIGH zone for user allocations\n");
            goto done;
        }

        uint64_t page = pmm_scan_zone(pmm_next_high_page,
                                      pmm_high_start_page, pmm_high_end_page,
                                      0, 0);
        if (page != 0) {
            bitmap_set(page);
            pmm_free_pages--;
            pmm_next_high_page = (page > pmm_high_start_page) ? (page - 1) : pmm_high_start_page;

            pmm_page_info[page].type = type;
            pmm_page_info[page].ref_count = 1;
            pmm_page_info[page].owner_pid = 0;
            pmm_page_info[page].owner_virt = 0;

            pages_by_type[PAGE_FREE]--;
            pages_by_type[type]++;

            result = page * PAGE_SIZE;
            goto done;
        }

        serial_lock();
        serial_print("PMM: ERROR - Out of HIGH-zone memory for user type ");
        serial_print(page_type_string(type));
        serial_print("! Free pages: ");
        serial_print_dec(pmm_free_pages);
        serial_print("\n");
        serial_unlock();
    } else {
        if (pmm_low_start_page == 0 && pmm_low_end_page == 0) {
            serial_print("PMM: ERROR - No LOW zone for kernel/table allocations\n");
            goto done;
        }

        /*
         * Kernel, page-table, and reserved allocations must stay
         * above 2 MB, so the scan's min_page is 0x200 for them and 0
         * for everything else.  The skip is a property of the
         * allocation type, not of one scan pass, so it is a scan
         * parameter rather than a check inside the loop.
         */
        uint64_t min_page = 0;
        if (type == PAGE_KERNEL || type == PAGE_PAGE_TABLE
            || type == PAGE_RESERVED) {
            min_page = 0x200;
        }

        uint64_t page = pmm_scan_zone(pmm_next_low_page,
                                      pmm_low_start_page, pmm_low_end_page,
                                      1, min_page);
        if (page != 0) {
            bitmap_set(page);
            pmm_free_pages--;
            pmm_next_low_page = (page < pmm_low_end_page) ? (page + 1) : pmm_low_end_page;

            pmm_page_info[page].type = type;
            pmm_page_info[page].ref_count = 1;
            pmm_page_info[page].owner_pid = 0;
            pmm_page_info[page].owner_virt = 0;

            pages_by_type[PAGE_FREE]--;
            pages_by_type[type]++;

            result = page * PAGE_SIZE;
            goto done;
        }

        serial_lock();
        serial_print("PMM: ERROR - Out of LOW-zone memory for kernel/table type ");
        serial_print(page_type_string(type));
        serial_print("! Free pages: ");
        serial_print_dec(pmm_free_pages);
        serial_print("\n");
        serial_unlock();
    }

done:
    pmm_irq_restore(flags);
    return result;
}

void pmm_free_page(uint64_t phys_addr) {
    if (phys_addr == 0) return;

    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return;

    uint64_t flags = pmm_irq_save();

    if (!bitmap_test(page)) {
        serial_lock();
        serial_print("PMM: WARNING - Double free of page 0x");
        serial_print_hex(phys_addr);
        serial_print("\n");
        serial_unlock();
        pmm_irq_restore(flags);
        return;
    }

    if (pmm_page_info[page].ref_count > 0) {
        pmm_page_info[page].ref_count--;
    }

    if (pmm_page_info[page].ref_count == 0) {
        page_type_t old_type = pmm_page_info[page].type;
        bitmap_clear(page);
        pmm_page_info[page].type = PAGE_FREE;
        pmm_free_pages++;
        pages_by_type[old_type]--;
        pages_by_type[PAGE_FREE]++;

        if (page >= pmm_low_start_page && page <= pmm_low_end_page) {
            if (page < pmm_next_low_page) {
                pmm_next_low_page = page;
            }
        }
        if (page >= pmm_high_start_page && page <= pmm_high_end_page) {
            if (page > pmm_next_high_page) {
                pmm_next_high_page = page;
            }
        }
    }

    pmm_irq_restore(flags);
}

page_type_t pmm_get_page_type(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return PAGE_RESERVED;
    return pmm_page_info[page].type;
}

int pmm_can_use_as_page_table(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return 0;

    page_type_t type = pmm_get_page_type(phys_addr);

    if (type != PAGE_FREE) return 0;
    if (page < pmm_low_start_page || page > pmm_low_end_page) return 0;
    return 1;
}

void pmm_reserve_page(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return;

    uint64_t flags = pmm_irq_save();

    if (bitmap_test(page)) {
        page_type_t old_type = pmm_page_info[page].type;
        pmm_page_info[page].type = PAGE_RESERVED;
        pages_by_type[old_type]--;
        pages_by_type[PAGE_RESERVED]++;
    } else {
        bitmap_set(page);
        pmm_page_info[page].type = PAGE_RESERVED;
        pmm_page_info[page].ref_count = 1;
        pmm_free_pages--;
        pages_by_type[PAGE_FREE]--;
        pages_by_type[PAGE_RESERVED]++;
    }

    pmm_irq_restore(flags);
}

void pmm_unreserve_page(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return;

    uint64_t flags = pmm_irq_save();

    if (pmm_page_info[page].type == PAGE_RESERVED) {
        pmm_page_info[page].type = PAGE_FREE;
        bitmap_clear(page);
        pmm_free_pages++;
        pages_by_type[PAGE_RESERVED]--;
        pages_by_type[PAGE_FREE]++;

        if (page >= pmm_low_start_page && page <= pmm_low_end_page) {
            if (page < pmm_next_low_page) {
                pmm_next_low_page = page;
            }
        }
        if (page >= pmm_high_start_page && page <= pmm_high_end_page) {
            if (page > pmm_next_high_page) {
                pmm_next_high_page = page;
            }
        }
    }

    pmm_irq_restore(flags);
}

uint64_t pmm_get_free_pages(void) {
    return pmm_free_pages;
}

uint64_t pmm_get_total_pages(void) {
    return pmm_total_pages;
}

void pmm_dump_stats(void) {
    serial_lock();
    serial_print("\n=== PMM STATS ===\n");
    serial_print("Total pages: ");
    serial_print_dec(pmm_total_pages);
    serial_print("\nFree pages: ");
    serial_print_dec(pmm_free_pages);
    serial_print("\n");
    serial_print("Pages by type:\n");
    serial_print("  FREE: ");
    serial_print_dec(pages_by_type[PAGE_FREE]);
    serial_print("\n  KERNEL: ");
    serial_print_dec(pages_by_type[PAGE_KERNEL]);
    serial_print("\n  PAGE_TABLE: ");
    serial_print_dec(pages_by_type[PAGE_PAGE_TABLE]);
    serial_print("\n  USER_DATA: ");
    serial_print_dec(pages_by_type[PAGE_USER_DATA]);
    serial_print("\n  USER_PAGE_TABLE: ");
    serial_print_dec(pages_by_type[PAGE_USER_PAGE_TABLE]);
    serial_print("\n  RESERVED: ");
    serial_print_dec(pages_by_type[PAGE_RESERVED]);
    serial_print("\n");
    serial_print("Allocation pointers:\n");
    serial_print("  LOW  zone: ");
    serial_print_dec(pmm_low_start_page);
    serial_print(" - ");
    serial_print_dec(pmm_low_end_page);
    serial_print("\n  HIGH zone: ");
    serial_print_dec(pmm_high_start_page);
    serial_print(" - ");
    serial_print_dec(pmm_high_end_page);
    serial_print("\n  Next low:  ");
    serial_print_dec(pmm_next_low_page);
    serial_print("\n  Next high: ");
    serial_print_dec(pmm_next_high_page);
    serial_print("\n");
    serial_print("==================\n\n");
    serial_unlock();
}
