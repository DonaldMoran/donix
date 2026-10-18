#include "include/heap.h"
#include "include/serial.h"
#include "include/vga.h"
#include "include/pmm.h"
#include "include/vmm.h"
#include <stdint.h>
#include <stddef.h>

/*
 * HEAP_TRACE_MERGE -- print the coalesce merge arithmetic.
 *
 * Default 0: no output.  Set to 1 for one build to see, for every
 * merge, the current block, its size before, the next block, its
 * size, and the resulting size.  The 2026-10-18 heap_validate
 * prev_mismatch showed a merged block whose size was 0x30 (one
 * heap_header_t) larger than the chain allowed; this trace names
 * whether the += double-counts or the pre-merge size was already
 * wrong.
 */
#ifndef HEAP_TRACE_MERGE
#define HEAP_TRACE_MERGE 0
#endif

/* No local HEAP_START: use the one in heap.h.  A local #define here
 * would silently shadow it and the two could drift. */

static uint64_t heap_start = 0;   /* first byte of the heap region */
static uint64_t heap_brk   = 0;   /* first byte after the mapped region */
static heap_header_t* free_list = NULL;
static uint64_t heap_mapped = 0;  /* bytes of virtual address space mapped */
static uint64_t heap_used_bytes = 0;

/* ---------------------------------------------------------------------
 * heap_extend: map at least `bytes` more, return the start of the new
 * region.
 *
 * Returns the virtual address of the first byte of the newly mapped
 * region, or 0 on failure.  The region is contiguous with the previous
 * region (it starts at the old heap_brk), and its length is rounded up
 * to a page boundary.
 *
 * The caller is responsible for creating a free block covering the
 * new region.  heap_extend does not touch the block list.  This is
 * the key difference from the old implementation, which placed the
 * new block at `heap_brk - requested_size` and thereby lost up to
 * (PAGE_SIZE - 1) bytes per extension.
 *
 * Backing pages come from the PMM's HIGH zone (PAGE_USER_DATA), not
 * LOW (PAGE_KERNEL).  LOW is only one quarter of free physical memory
 * and is shared with kernel page tables and the kernel stack pool;
 * the heap can grow to 24 MB and would exhaust LOW on its own.  The
 * mapping is PT_WRITE with no PT_USER, so the page's PMM type does
 * not grant users access to it.
 * --------------------------------------------------------------------- */
static uint64_t heap_extend(size_t bytes) {
    if (bytes == 0) return heap_brk;

    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t region = pages * PAGE_SIZE;

    if (heap_mapped + region > HEAP_MAX_SIZE) {
        serial_print("HEAP: extend refused, would exceed HEAP_MAX_SIZE\n");
        return 0;
    }

    uint64_t region_start = heap_brk;

    for (size_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page_for_elf();
        if (!phys) {
            serial_print("HEAP: extend failed, out of physical pages\n");
            return 0;
        }

        uint64_t virt = region_start + (i * PAGE_SIZE);
        if (vmm_map_page(virt, phys, PT_PRESENT | PT_WRITE) != 0) {
            /*
             * A page-table allocation failed while mapping this
             * heap page.  The mapping was NOT made.
             *
             * heap_brk and heap_mapped are only advanced after the
             * whole loop succeeds, so on this path the pages already
             * mapped in this call are NOT part of the heap's
             * accounting -- find_free_block/coalesce_blocks will
             * never look at them, and the caller gets back 0 as if
             * the extension never started.
             *
             * So every page this call allocated -- the ones already
             * mapped and the one just allocated (`phys`) -- must be
             * unmapped and freed here, or they leak: the heap's
             * own bookkeeping does not know they exist, and the
             * process's elf_page_list does not track heap pages
             * (heap_extend uses the raw PMM, not elf_add_page_to_pcb).
             *
             * Unmap before free, so no live PTE points at a freed
             * page.  The HHDM window is per-process and this mapping
             * is in the current process's address space, so
             * vmm_unmap_page is the right call.
             */
            serial_print("HEAP: extend failed at virt=0x");
            serial_print_hex(virt);
            serial_print(" (out of page-table pages)\n");
            pmm_free_page(phys);
            for (size_t j = 0; j < i; j++) {
                uint64_t prior = region_start + (j * PAGE_SIZE);
                uint64_t prior_phys = vmm_get_phys(prior);
                if (prior_phys) {
                    vmm_unmap_page(prior);
                    pmm_free_page(prior_phys & ~0xFFFULL);
                }
            }
            return 0;
        }

        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }

    heap_brk    += region;
    heap_mapped += region;
    return region_start;
}

void heap_init(uint64_t start, size_t size) {
    heap_start      = start;
    heap_brk        = start;
    heap_mapped     = 0;
    heap_used_bytes = 0;
    free_list       = NULL;

    /* Round the initial size down to a page boundary, so heap_brk
       lands exactly at start + size and the initial block covers
       every mapped byte. */
    size_t pages = size / PAGE_SIZE;
    if (pages == 0) pages = 1;
    size_t initial = pages * PAGE_SIZE;

    uint64_t region = heap_extend(initial);
    if (region == 0) {
        serial_print("HEAP: init failed to map initial region\n");
        return;
    }

    heap_header_t* header = (heap_header_t*)heap_start;
    header->magic = HEAP_MAGIC;
    header->size  = initial - sizeof(heap_header_t);
    header->used  = 0;
    header->next  = NULL;
    header->prev  = NULL;
    header->_pad  = 0;
    free_list     = header;

    serial_print("HEAP: init OK, ");
    serial_print_dec(initial / 1024);
    serial_print(" KB (max ");
    serial_print_dec(HEAP_MAX_SIZE / 1024);
    serial_print(" KB)\n");
}

static heap_header_t* find_free_block(size_t size) {
    heap_header_t* current = free_list;

    /*
     * Bounded walk.  A corrupt block whose next points at itself (or
     * any cycle) makes the unbounded walk spin forever with interrupts
     * off, hanging the kernel.  The heap has at most
     * heap_mapped / sizeof(heap_header_t) blocks, so a walk longer
     * than that is a cycle.  Return NULL -- the callers handle a failed
     * allocation -- and name the block.
     */
    uint64_t steps = 0;
    uint64_t max_steps = (heap_mapped / sizeof(heap_header_t)) + 1;
    while (current) {
        if (++steps > max_steps) {
            serial_print("HEAP: free-list cycle at 0x");
            serial_print_hex((uint64_t)current);
            serial_print("\n");
            return NULL;
        }
        if (!current->used && current->size >= size) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

/* Split `block` so that its payload is exactly `size` bytes, with a
 * new free block after it holding the remainder.
 *
 * The new block's header goes at `block + sizeof(header) + size`.
 * Its payload size is `block->size - size - sizeof(header)`.
 *
 * Only split if the new block can hold at least HEAP_ALIGNMENT bytes
 * of payload.  Otherwise the caller gets a slightly larger block than
 * it asked for, which is correct and cheaper than a tiny fragment.
 */
static void split_block(heap_header_t* block, size_t size) {
    if (block->size <= size + sizeof(heap_header_t) + HEAP_ALIGNMENT) {
        return;     /* not enough room to be worth splitting */
    }

    size_t remaining = block->size - size - sizeof(heap_header_t);

    heap_header_t* new_block =
        (heap_header_t*)((uint8_t*)block + sizeof(heap_header_t) + size);

    /* A corrupt block->size can make new_block land at or before block.
     * A self-link is never a valid split.  Refuse both; the caller
     * keeps the larger block, which is correct and safe. */
    if ((uint64_t)new_block <= (uint64_t)block) return;
    if (block->next == new_block) return;

    new_block->magic = HEAP_MAGIC;
    new_block->size  = remaining;
    new_block->used  = 0;
    new_block->next  = block->next;
    new_block->prev  = block;
    new_block->_pad  = 0;

    if (block->next) {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size;
}

static void coalesce_blocks(void) {
    heap_header_t* current = free_list;

    while (current) {
        heap_header_t* next = current->next;

        if (!current->used && next && !next->used) {
            uint8_t* current_end =
                (uint8_t*)current + sizeof(heap_header_t) + current->size;

            if ((heap_header_t*)current_end == next) {
#if HEAP_TRACE_MERGE
                serial_lock();
                serial_print("MERGE cur=0x");
                serial_print_hex((uint64_t)current);
                serial_print(" csz_before=0x");
                serial_print_hex(current->size);
                serial_print(" next=0x");
                serial_print_hex((uint64_t)next);
                serial_print(" nsz=0x");
                serial_print_hex(next->size);
                serial_print(" end=0x");
                serial_print_hex((uint64_t)current_end);
#endif
                current->size += sizeof(heap_header_t) + next->size;
                current->next = next->next;

                if (current->next) {
                    current->next->prev = current;
                }

#if HEAP_TRACE_MERGE
                serial_print(" csz_after=0x");
                serial_print_hex(current->size);
                serial_print(" next_after=0x");
                serial_print_hex((uint64_t)current->next);
                serial_print("\n");
                serial_unlock();
#endif
                continue;   /* re-check current against its new next */
            }
        }

        current = current->next;
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = (size + HEAP_ALIGNMENT - 1) & ~(size_t)(HEAP_ALIGNMENT - 1);

    heap_header_t* block = find_free_block(size);

    if (!block) {
        /*
         * No free block large enough.  Map more memory and retry.
         *
         * The new region is contiguous with the previous one (it
         * starts at heap_brk).  We create one free block covering the
         * ENTIRE new region, link it at the tail of the list, and
         * re-run find_free_block.  The new block's header goes at the
         * region's start, not at `heap_brk - size`; this is the fix
         * for the old implementation's per-extension leak.
         *
         * The requested size is rounded up to a page multiple so the
         * new region holds at least the request plus its header.
         */
        size_t needed = size + sizeof(heap_header_t);
        uint64_t region_start = heap_extend(needed);
        if (region_start == 0) {
            serial_print("HEAP: kmalloc failed, cannot extend\n");
            return NULL;
        }

        /* The new region is [region_start, heap_brk).  Its length is
           heap_brk - region_start, a multiple of PAGE_SIZE. */
        size_t region_size = (size_t)(heap_brk - region_start);

        heap_header_t* new_block = (heap_header_t*)region_start;
        new_block->magic = HEAP_MAGIC;
        new_block->size  = region_size - sizeof(heap_header_t);
        new_block->used  = 0;
        new_block->next  = NULL;
        new_block->prev  = NULL;
        new_block->_pad  = 0;

        if (!free_list) {
            free_list = new_block;
        } else {
            heap_header_t* cur = free_list;
            while (cur->next) cur = cur->next;
            cur->next = new_block;
            new_block->prev = cur;
        }

        /* Merge with the previous block if it was free and is
           physically adjacent.  Without this, a heap that has been
           fully freed but never coalesced would accumulate adjacent
           free blocks, and find_free_block would still find a block
           large enough only by luck. */
        coalesce_blocks();

        block = find_free_block(size);
        if (!block) {
            serial_print("HEAP: kmalloc failed even after extension\n");
            return NULL;
        }
    }

    block->used = 1;
    split_block(block, size);
    heap_used_bytes += block->size;

    return (void*)((uint8_t*)block + sizeof(heap_header_t));
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_header_t* block = (heap_header_t*)((uint8_t*)ptr - sizeof(heap_header_t));

    if (block->magic != HEAP_MAGIC) {
        serial_print("HEAP: kfree called with invalid pointer! ptr=0x");
        serial_print_hex((uint64_t)ptr);
        serial_print("\n");
        return;
    }

    if (!block->used) {
        serial_print("HEAP: kfree called on already freed block! ptr=0x");
        serial_print_hex((uint64_t)ptr);
        serial_print("\n");
        return;
    }

    block->used = 0;
    heap_used_bytes -= block->size;

    /*
     * The heap has no lock.  coalesce_blocks() and the validate below
     * mutate and then read the SAME list, and a tick between them runs
     * the timer's scheduler, which can call kmalloc/kfree and mutate
     * the list out from under the validate.  That is what produced the
     * 2026-10-18 HEAP-VALIDATE FAIL next_not_end: the validator read a
     * block whose size was pre-merge while its next was post-merge --
     * a state only an interleaved second kfree can create.
     *
     * Hold cli across coalesce AND validate, so the two see one list
     * state.  Save and restore the caller's flag rather than a bare
     * cli/sti: kfree is reached from contexts that must stay
     * interrupts-off (sys_fork holds cli across process_create's
     * allocations), and a bare sti here would re-enable them.
     */
    uint64_t saved_flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(saved_flags));

    coalesce_blocks();

    {
        uint64_t bad = heap_validate();
        if (bad != 0) {
            /*
             * Name the failing block AND its fields.  heap_validate
             * returns the block it landed on; the field that is
             * wrong says which check failed (magic / size / used /
             * next / prev) and therefore which direction the
             * overflow came from.  Read via the HHDM/VMA alias the
             * heap is mapped at -- bad is a valid heap pointer.
             */
            heap_header_t* b = (heap_header_t*)bad;
            serial_print("HEAP: corrupt after coalesce block=0x");
            serial_print_hex(bad);
            serial_print(" magic=0x"); serial_print_hex(b->magic);
            serial_print(" size=0x");  serial_print_hex(b->size);
            serial_print(" used=");    serial_print_dec(b->used);
            serial_print(" next=0x");  serial_print_hex((uint64_t)b->next);
            serial_print(" prev=0x");  serial_print_hex((uint64_t)b->prev);
            serial_print(" end=0x");   serial_print_hex(
                (uint64_t)b + sizeof(heap_header_t) + b->size);
            serial_print("\n");
        }
    }

    __asm__ volatile("pushq %0; popfq" :: "r"(saved_flags));
}

size_t heap_used(void)  { return heap_used_bytes; }
size_t heap_free(void)  { return heap_mapped - heap_used_bytes; }
size_t heap_total(void) { return heap_mapped; }

void heap_stats(void) {
    serial_lock();

    serial_print("\n=== HEAP STATS ===\n");
    serial_print("  Mapped: ");
    serial_print_dec(heap_mapped / 1024);
    serial_print(" KB\n");
    serial_print("  Used:   ");
    serial_print_dec(heap_used_bytes / 1024);
    serial_print(" KB\n");
    serial_print("  Free:   ");
    serial_print_dec((heap_mapped - heap_used_bytes) / 1024);
    serial_print(" KB\n");

    size_t free_blocks = 0;
    size_t largest_free = 0;
    heap_header_t* current = free_list;
    while (current) {
        if (!current->used) {
            free_blocks++;
            if (current->size > largest_free) largest_free = current->size;
        }
        current = current->next;
    }
    serial_print("  Free blocks: ");
    serial_print_dec(free_blocks);
    serial_print(" (largest ");
    serial_print_dec(largest_free / 1024);
    serial_print(" KB)\n");
    serial_print("  Validator: ");
    uint64_t bad = heap_validate();
    if (bad == 0) {
        serial_print("intact\n");
    } else {
        serial_print("CORRUPT at 0x");
        serial_print_hex(bad);
        serial_print("\n");
    }
    serial_print("==================\n");

    serial_unlock();

    /* No trailing prompt: the shell owns the prompt. */
}

/* ---------------------------------------------------------------------
 * heap_validate: walk the block list and check every invariant.
 *
 * Returns 0 if intact, or the address of the first block that failed
 * (nonzero).  Does not allocate, so it is safe to call when the heap
 * is suspected of being corrupt.
 * --------------------------------------------------------------------- */
/* Instrumented return: print the failing check and the two values it
 * compared, then return the block.  Gated so a healthy heap prints
 * nothing.  This is the diagnostic that names WHICH invariant failed;
 * the block address alone does not. */
#define HEAP_FAIL(cur_, check_, exp_, got_)                              \
    do {                                                                 \
        serial_lock();                                                   \
        serial_print("HEAP-VALIDATE FAIL check=");                       \
        serial_print(check_);                                            \
        serial_print(" block=0x"); serial_print_hex((uint64_t)(cur_));    \
        serial_print(" exp=0x");   serial_print_hex((uint64_t)(exp_));    \
        serial_print(" got=0x");   serial_print_hex((uint64_t)(got_));    \
        serial_print("\n");                                              \
        serial_unlock();                                                 \
        return (uint64_t)(cur_);                                         \
    } while (0)

uint64_t heap_validate(void) {
    if (heap_start == 0 || heap_brk == 0) {
        /* Heap not initialized.  Treat as intact so selftest does not
           fail before heap_init runs. */
        return 0;
    }

    if (free_list == NULL) {
        HEAP_FAIL(heap_start, "free_list_null", heap_start, free_list);
    }

    /* The first block must start at heap_start. */
    if ((uint64_t)free_list != heap_start) {
        HEAP_FAIL(free_list, "head_not_heapstart", heap_start, free_list);
    }

    heap_header_t* prev = NULL;
    heap_header_t* cur  = free_list;

    while (cur) {
        if (cur->magic != HEAP_MAGIC)
            HEAP_FAIL(cur, "magic", HEAP_MAGIC, cur->magic);
        if (cur->used != 0 && cur->used != 1)
            HEAP_FAIL(cur, "used", 1, cur->used);
        if ((uint64_t)cur < heap_start)
            HEAP_FAIL(cur, "below_start", heap_start, (uint64_t)cur);
        if ((uint64_t)cur + sizeof(heap_header_t) > heap_brk)
            HEAP_FAIL(cur, "past_brk", heap_brk,
                      (uint64_t)cur + sizeof(heap_header_t));
        if (cur->size > heap_brk - (uint64_t)cur - sizeof(heap_header_t))
            HEAP_FAIL(cur, "size_overrun",
                      heap_brk - (uint64_t)cur - sizeof(heap_header_t),
                      cur->size);
        if (cur->prev != prev)
            HEAP_FAIL(cur, "prev_mismatch", (uint64_t)prev,
                      (uint64_t)cur->prev);

        uint8_t* end = (uint8_t*)cur + sizeof(heap_header_t) + cur->size;

        if (cur->next) {
            if (cur->next != (heap_header_t*)end)
                HEAP_FAIL(cur, "next_not_end", (uint64_t)end,
                          (uint64_t)cur->next);
            if (cur->next->prev != cur)
                HEAP_FAIL(cur, "next_prev_mismatch", (uint64_t)cur,
                          (uint64_t)cur->next->prev);
        } else {
            /* Last block: its end must be exactly heap_brk, so no
               mapped byte is unaccounted for.  This is the invariant
               the old implementation violated. */
            if ((uint64_t)end != heap_brk)
                HEAP_FAIL(cur, "last_end_not_brk", heap_brk, (uint64_t)end);
        }

        prev = cur;
        cur  = cur->next;
    }

    return 0;
}

/* ---------------------------------------------------------------------
 * heap_stress: allocate, write, verify, free, in a deterministic
 * pattern that forces at least one extension.
 *
 * Design:
 *   - 64 slots, each holding a pointer and a pattern seed.
 *   - A 64-bit LCG picks the next operation.
 *   - Allocation sizes are 16..4095, so a run of ~300 allocations
 *     exceeds the 1 MB initial region and forces extension.
 *   - The first byte and the last byte of each block are written with
 *     a per-block pattern derived from the seed, and verified on
 *     free.  (Not the whole block: writing 4 KB x 300 blocks is slow
 *     and does not test anything the endpoints miss, since the bug
 *     class is "the allocator gave overlapping regions.")
 *   - Every allocation is checked for 16-byte alignment.
 *   - At the end, all slots are freed, then heap_validate runs.
 *
 * Returns 0 on success.  Returns a nonzero sentinel on failure:
 *   1  content mismatch
 *   2  kmalloc returned NULL
 *   3  heap_validate failed
 *   4  misaligned allocation
 *   5  leak detected
 *   6  heap not initialized
 * --------------------------------------------------------------------- */
size_t heap_stress(void) {
    //enum { SLOTS = 64, ITERS = 1024 };
    enum { SLOTS = 512, ITERS = 4096 };
    void*     ptrs[SLOTS];
    size_t    sizes[SLOTS];
    uint64_t  seeds[SLOTS];

    if (heap_start == 0) return 6;

    for (int i = 0; i < SLOTS; i++) {
        ptrs[i]  = NULL;
        sizes[i] = 0;
        seeds[i] = 0;
    }

    size_t used_before = heap_used_bytes;

    uint64_t rng = 0x123456789ABCDEF0ULL;
    #define NEXT_RNG() (rng = rng * 6364136223846793005ULL + 1442695040888963407ULL)

    for (int iter = 0; iter < ITERS; iter++) {
        NEXT_RNG();
        int slot = (int)(rng % SLOTS);

        if (ptrs[slot] != NULL) {
            /* Verify the endpoints. */
            uint8_t* p = (uint8_t*)ptrs[slot];
            uint8_t expect_first = (uint8_t)(seeds[slot] & 0xFF);
            uint8_t expect_last  = (uint8_t)((seeds[slot] >> 8) & 0xFF);

            if (p[0] != expect_first ||
                p[sizes[slot] - 1] != expect_last) {
                serial_print("HEAP STRESS: content mismatch at slot ");
                serial_print_dec(slot);
                serial_print(", ptr=0x");
                serial_print_hex((uint64_t)p);
                serial_print("\n");
                return 1;
            }

            kfree(ptrs[slot]);
            ptrs[slot]  = NULL;
            sizes[slot] = 0;
            seeds[slot] = 0;
        } else {
            NEXT_RNG();
            //size_t sz = (size_t)(rng % 4080) + 16;   /* 16..4095 */
            size_t sz = (size_t)(rng % 16368) + 16;  /* 16..16383 */
            sz = (sz + HEAP_ALIGNMENT - 1) & ~(size_t)(HEAP_ALIGNMENT - 1);

            void* p = kmalloc(sz);
            if (!p) {
                serial_print("HEAP STRESS: kmalloc(");
                serial_print_dec(sz);
                serial_print(") failed at iter ");
                serial_print_dec(iter);
                serial_print("\n");
                return 2;
            }

            if (((uint64_t)p & (HEAP_ALIGNMENT - 1)) != 0) {
                serial_print("HEAP STRESS: misaligned allocation at 0x");
                serial_print_hex((uint64_t)p);
                serial_print(" (size ");
                serial_print_dec(sz);
                serial_print(")\n");
                return 4;
            }

            NEXT_RNG();
            uint64_t seed = rng;

            uint8_t* bp = (uint8_t*)p;
            bp[0]        = (uint8_t)(seed & 0xFF);
            bp[sz - 1]   = (uint8_t)((seed >> 8) & 0xFF);

            ptrs[slot]  = p;
            sizes[slot] = sz;
            seeds[slot] = seed;
        }
    }

    /* Free everything still held. */
    for (int i = 0; i < SLOTS; i++) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
            ptrs[i] = NULL;
        }
    }

    #undef NEXT_RNG

    /* Validate.  A corrupt heap returns a nonzero address. */
    uint64_t bad = heap_validate();
    if (bad != 0) {
        serial_print("HEAP STRESS: heap_validate failed at 0x");
        serial_print_hex(bad);
        serial_print("\n");
        return 3;
    }

    /* Leak check: used must be back where it started. */
    size_t used_after = heap_used_bytes;
    if (used_after != used_before) {
        serial_print("HEAP STRESS: leak detected, before=");
        serial_print_dec(used_before);
        serial_print(" after=");
        serial_print_dec(used_after);
        serial_print("\n");
        return 5;
    }

    return 0;
}
