// vmm.c - Using recursive mapping for page table access
#include <stdint.h>
#include <stddef.h>
#include "vmm.h"
#include "pmm.h"
#include "vga.h"
#include "serial.h"
#include "bootinfo.h"
#include "string.h"

#define RECURSIVE_PML4_INDEX 510
#define HHDM_START 0xFFFF800000000000ULL

#define PAGE_PCD (1ULL << 4)
#define PAGE_PWT (1ULL << 3)
#define PAGE_UNCACHED (PAGE_PCD | PAGE_PWT)

uint64_t vmm_max_physical = 0;
static BootInfo* vmm_bootinfo = NULL;

static uint64_t* get_pml4_virt(void) {
    if (vmm_bootinfo && vmm_bootinfo->pml4_virt) {
        return (uint64_t*)vmm_bootinfo->pml4_virt;
    }
    return (uint64_t*)0xFFFFFF7FBFDFE000ULL;
}

static inline uint64_t* phys_to_virt(uint64_t phys) {
    return (uint64_t*)(HHDM_START + phys);
}

void* ensure_hhdm_mapped(uint64_t phys) {
    uint64_t virt = HHDM_START + phys;

    if (!vmm_is_mapped(virt)) {
        vmm_map_page(virt, phys, PT_PRESENT | PT_WRITE | PAGE_UNCACHED);
    }

    return (void*)virt;
}

void vmm_init(BootInfo* info) {
    vmm_bootinfo = info;

    uint64_t max_phys = 0;
    uint64_t total_usable = 0;

    if (info && info->memory_map_count > 0) {
        MemoryMapEntry* entries = (MemoryMapEntry*)info->memory_map_addr;
        for (uint64_t i = 0; i < info->memory_map_count; i++) {
            if (entries[i].type == 1) {
                uint64_t end = entries[i].base + entries[i].length;
                if (end > max_phys) max_phys = end;
                total_usable += entries[i].length;
            }
        }
    } else {
        max_phys = 128ULL * 1024 * 1024;
    }

    vmm_max_physical = max_phys;

    for (uint64_t addr = 0; addr < 0x200000; addr += 0x1000) {
        vmm_map_page(addr, addr, PT_PRESENT | PT_WRITE);
    }

    serial_lock();
    serial_print("VMM: init OK, ");
    serial_print_dec(total_usable / (1024 * 1024));
    serial_print(" MB usable, max phys 0x");
    serial_print_hex(max_phys);
    serial_print("\n");
    serial_unlock();
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    phys &= ~0xFFFULL;

    uint64_t user_flag    = (flags & PT_USER) ? PT_USER : 0;
    uint64_t write_flag   = (flags & PT_WRITE) ? PT_WRITE : 0;
    uint64_t present_flag = PT_PRESENT;
    uint64_t nx_flag      = (flags & PT_NX) ? PT_NX : 0;
    uint64_t cache_flags  = flags & (PAGE_PCD | PAGE_PWT);

    uint64_t* pml4 = get_pml4_virt();

    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t dir_flags = PT_PRESENT | PT_WRITE | PT_USER | cache_flags;

    uint64_t* pdpt;
    if (!(pml4[pml4_idx] & PT_PRESENT)) {
        uint64_t new_pdpt_phys = pmm_alloc_page_for_tables();
        if (!new_pdpt_phys) return;
        memset(phys_to_virt(new_pdpt_phys), 0, PAGE_SIZE);
        pml4[pml4_idx] = new_pdpt_phys | dir_flags;
        pdpt = phys_to_virt(new_pdpt_phys);
    } else {
        pml4[pml4_idx] |= (PT_WRITE | PT_USER);
        pdpt = phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);
    }

    uint64_t* pd;
    if (!(pdpt[pdpt_idx] & PT_PRESENT)) {
        uint64_t new_pd_phys = pmm_alloc_page_for_tables();
        if (!new_pd_phys) return;
        memset(phys_to_virt(new_pd_phys), 0, PAGE_SIZE);
        pdpt[pdpt_idx] = new_pd_phys | dir_flags;
        pd = phys_to_virt(new_pd_phys);
    } else {
        pdpt[pdpt_idx] |= (PT_WRITE | PT_USER);
        pd = phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);
    }

    uint64_t* pt;
    if (!(pd[pd_idx] & PT_PRESENT)) {
        uint64_t new_pt_phys = pmm_alloc_page_for_tables();
        if (!new_pt_phys) return;
        memset(phys_to_virt(new_pt_phys), 0, PAGE_SIZE);
        pd[pd_idx] = new_pt_phys | dir_flags;
        pt = phys_to_virt(new_pt_phys);
    } else {
        pd[pd_idx] |= (PT_WRITE | PT_USER);
        pt = phys_to_virt(pd[pd_idx] & ~0xFFFULL);
    }

    uint64_t pte = phys | present_flag | write_flag | user_flag | cache_flags;
    if (nx_flag) pte |= PT_NX;

    pt[pt_idx] = pte;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t* pml4 = get_pml4_virt();

    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PT_PRESENT)) return;
    uint64_t* pdpt = phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PT_PRESENT)) return;
    uint64_t* pd = phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PT_PRESENT)) return;
    uint64_t* pt = phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t* pml4 = get_pml4_virt();

    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PT_PRESENT)) return 0;
    uint64_t* pdpt = phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PT_PRESENT)) return 0;
    uint64_t* pd = phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PT_PRESENT)) return 0;
    uint64_t* pt = phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    if (!(pt[pt_idx] & PT_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}

int vmm_is_mapped(uint64_t virt) {
    uint64_t* pml4 = get_pml4_virt();

    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PT_PRESENT)) return 0;
    uint64_t* pdpt = phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PT_PRESENT)) return 0;
    uint64_t* pd = phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PT_PRESENT)) return 0;
    uint64_t* pt = phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    if (!(pt[pt_idx] & PT_PRESENT)) return 0;
    return 1;
}

void vmm_dump_page_table(uint64_t virt) {
    uint64_t* pml4 = get_pml4_virt();
    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    serial_lock();
    serial_print("\n=== PAGE TABLE DUMP ===\n");
    if (!(pml4[pml4_idx] & PT_PRESENT)) { serial_unlock(); return; }
    uint64_t* pdpt = phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PT_PRESENT)) { serial_unlock(); return; }
    uint64_t* pd = phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PT_PRESENT)) { serial_unlock(); return; }
    uint64_t* pt = phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    serial_print("  PTE Entry Found: 0x");
    serial_print_hex(pt[pt_idx]);
    serial_print("\n");
    serial_unlock();
}

/*
 * Deep-clone a page table.
 *
 * The version this replaces copied the source PML4's entries verbatim
 * into the new PML4. That shares every PDPT, PD, and PT between the
 * source and the clone. When the caller later calls
 * vmm_map_page_in_cr3 on the clone to map a user page, the walk
 * reaches the shared leaf PT and overwrites the PTE — which is the
 * same PT the source is using. Result: mapping a page for the clone
 * *unmaps or remaps the same page in the source*.
 *
 * This is exactly what happened during SYS_EXEC bring-up: the child's
 * elf_load_into_process and process_create mapped the child's ELF
 * segments and user stack, and every one of those writes also
 * replaced the parent shell's mappings at the same virtual addresses.
 * The shell's code, .rodata, and user stack were silently replaced
 * by the child's, which is why the shell's spawn string got corrupted
 * and why the shell eventually faulted at RIP=0 or RIP=1 after
 * sysret — it was executing the child's code with the child's stack.
 *
 * The fix is a deep copy: allocate fresh PDPT/PD/PT pages for every
 * mapped PML4 entry below index 256, and copy the leaf PTEs verbatim
 * (they still point at the same physical data pages, which is correct
 * for shared kernel mappings and harmless for user mappings, since
 * the child's elf_load_into_process overwrites the entries it cares
 * about).
 *
 * PML4 entries 256..511 are shallow-copied. Those cover the HHDM
 * (0xFFFF800000000000) and the kernel image (0xFFFFFFFF80000000).
 * They are shared by design and are never remapped per-process, so
 * sharing the PDPT/PD/PT is both correct and far cheaper than
 * deep-copying ~130 MB of HHDM page tables.
 */
uint64_t vmm_clone_page_table(uint64_t src_cr3) {
    uint64_t new_pml4_phys = pmm_alloc_page_for_tables();
    if (!new_pml4_phys) return 0;

    uint64_t* src_pml4 = (uint64_t*)phys_to_virt(src_cr3);
    uint64_t* new_pml4 = (uint64_t*)phys_to_virt(new_pml4_phys);
    memset(new_pml4, 0, PAGE_SIZE);

    for (int i = 0; i < 512; i++) {
        if (i == RECURSIVE_PML4_INDEX) continue;

        uint64_t src_pml4e = src_pml4[i];
        if (!(src_pml4e & PT_PRESENT)) continue;

        /* High-half entries (HHDM, kernel image) are shared. */
        if (i >= 256) {
            new_pml4[i] = src_pml4e;
            continue;
        }

        /* Low-half entries: deep-copy the whole hierarchy below. */
        uint64_t new_pdpt_phys = pmm_alloc_page_for_tables();
        if (!new_pdpt_phys) return 0;
        uint64_t* src_pdpt = (uint64_t*)phys_to_virt(src_pml4e & ~0xFFFULL);
        uint64_t* new_pdpt = (uint64_t*)phys_to_virt(new_pdpt_phys);
        memset(new_pdpt, 0, PAGE_SIZE);

        for (int j = 0; j < 512; j++) {
            uint64_t src_pdpte = src_pdpt[j];
            if (!(src_pdpte & PT_PRESENT)) continue;

            uint64_t new_pd_phys = pmm_alloc_page_for_tables();
            if (!new_pd_phys) return 0;
            uint64_t* src_pd = (uint64_t*)phys_to_virt(src_pdpte & ~0xFFFULL);
            uint64_t* new_pd = (uint64_t*)phys_to_virt(new_pd_phys);
            memset(new_pd, 0, PAGE_SIZE);

            for (int k = 0; k < 512; k++) {
                uint64_t src_pde = src_pd[k];
                if (!(src_pde & PT_PRESENT)) continue;

                /* 2 MB huge page: copy verbatim. */
                if (src_pde & 0x80) {
                    new_pd[k] = src_pde;
                    continue;
                }

                uint64_t new_pt_phys = pmm_alloc_page_for_tables();
                if (!new_pt_phys) return 0;
                uint64_t* src_pt = (uint64_t*)phys_to_virt(src_pde & ~0xFFFULL);
                uint64_t* new_pt = (uint64_t*)phys_to_virt(new_pt_phys);

                for (int m = 0; m < 512; m++) {
                    new_pt[m] = src_pt[m];
                }

                new_pd[k] = new_pt_phys | (src_pde & 0xFFF);
            }

            new_pdpt[j] = new_pd_phys | (src_pdpte & 0xFFF);
        }

        new_pml4[i] = new_pdpt_phys | (src_pml4e & 0xFFF);
    }

    new_pml4[RECURSIVE_PML4_INDEX] = new_pml4_phys | PT_PRESENT | PT_WRITE;
    return new_pml4_phys;
}

uint64_t vmm_get_phys_from_cr3(uint64_t cr3, uint64_t virt) {
    uint64_t* pml4 = (uint64_t*)phys_to_virt(cr3);
    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PT_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PT_PRESENT)) return 0;
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PT_PRESENT)) return 0;

    /* If the PDE is a 2 MB huge page (PS bit set), compute the
       physical address directly.  Without this, the walk would
       dereference the huge page's physical base as a page-table
       pointer and return garbage. */
    if (pd[pd_idx] & 0x80) {
        uint64_t huge_phys = pd[pd_idx] & ~0x1FFFFFULL;
        uint64_t offset_2m = virt & 0x1FFFFFULL;
        return huge_phys + offset_2m;
    }

    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    if (!(pt[pt_idx] & PT_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}

void vmm_map_page_in_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags) {
    phys &= ~0xFFFULL;

    uint64_t user_flag    = (flags & PT_USER) ? PT_USER : 0;
    uint64_t write_flag   = (flags & PT_WRITE) ? PT_WRITE : 0;
    uint64_t present_flag = PT_PRESENT;
    uint64_t nx_flag      = (flags & PT_NX) ? PT_NX : 0;
    uint64_t cache_flags  = flags & (PAGE_PCD | PAGE_PWT);

    uint64_t* pml4 = (uint64_t*)phys_to_virt(cr3);

    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t dir_flags = PT_PRESENT | PT_WRITE | PT_USER | cache_flags;

    uint64_t* pdpt;
    if (!(pml4[pml4_idx] & PT_PRESENT)) {
        uint64_t new_pdpt_phys = pmm_alloc_page_for_tables();
        if (!new_pdpt_phys) return;
        memset(phys_to_virt(new_pdpt_phys), 0, PAGE_SIZE);
        pml4[pml4_idx] = new_pdpt_phys | dir_flags;
        pdpt = (uint64_t*)phys_to_virt(new_pdpt_phys);
    } else {
        pml4[pml4_idx] |= (PT_WRITE | PT_USER);
        pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);
    }

    uint64_t* pd;
    if (!(pdpt[pdpt_idx] & PT_PRESENT)) {
        uint64_t new_pd_phys = pmm_alloc_page_for_tables();
        if (!new_pd_phys) return;
        memset(phys_to_virt(new_pd_phys), 0, PAGE_SIZE);
        pdpt[pdpt_idx] = new_pd_phys | dir_flags;
        pd = (uint64_t*)phys_to_virt(new_pd_phys);
    } else {
        pdpt[pdpt_idx] |= (PT_WRITE | PT_USER);
        pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);
    }

    /* If the PDE is a 2 MB huge page (PS bit set), split it into
       512 4 KB PTEs before continuing.  The bootloader installs
       2 MB huge pages for the low-memory identity map and the HHDM.
       Without this split, the code below would treat the huge page's
       physical base as a page-table pointer and write the new PTE
       into memory that isn't a page table.  Splits are on-demand. */
    if (pd[pd_idx] & 0x80) {
        uint64_t huge_phys  = pd[pd_idx] & ~0x1FFFFFULL;
        uint64_t huge_flags = pd[pd_idx] & 0xFFF;

        uint64_t new_pt_phys = pmm_alloc_page_for_tables();
        if (!new_pt_phys) return;
        uint64_t* split_pt = (uint64_t*)phys_to_virt(new_pt_phys);

        uint64_t carry = huge_flags & (PT_PRESENT | PT_WRITE | PT_USER);
        for (int m = 0; m < 512; m++) {
            split_pt[m] = (huge_phys + (uint64_t)m * 0x1000) | carry;
        }
        pd[pd_idx] = new_pt_phys | carry;

        uint64_t active_cr3_split;
        asm volatile("mov %%cr3, %0" : "=r"(active_cr3_split));
        if ((active_cr3_split & ~0xFFFULL) == (cr3 & ~0xFFFULL)) {
            uint64_t base = virt & ~0x1FFFFFULL;
            for (uint64_t a = base; a < base + 0x200000; a += 0x1000) {
                asm volatile("invlpg (%0)" : : "r"(a) : "memory");
            }
        }
    }

    uint64_t* pt;
    if (!(pd[pd_idx] & PT_PRESENT)) {
        uint64_t new_pt_phys = pmm_alloc_page_for_tables();
        if (!new_pt_phys) return;
        memset(phys_to_virt(new_pt_phys), 0, PAGE_SIZE);
        pd[pd_idx] = new_pt_phys | dir_flags;
        pt = (uint64_t*)phys_to_virt(new_pt_phys);
    } else {
        pd[pd_idx] |= (PT_WRITE | PT_USER);
        pt = (uint64_t*)phys_to_virt(pd[pd_idx] & ~0xFFFULL);
    }

    uint64_t pte = phys | present_flag | write_flag | user_flag | cache_flags;
    if (nx_flag) pte |= PT_NX;

    pt[pt_idx] = pte;

    uint64_t active_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(active_cr3));
    if ((active_cr3 & ~0xFFFULL) == (cr3 & ~0xFFFULL)) {
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

void vmm_unmap_page_in_cr3(uint64_t cr3, uint64_t virt) {
    uint64_t* pml4 = (uint64_t*)phys_to_virt(cr3);
    uint32_t pml4_idx = (virt >> 39) & 0x1FF;
    uint32_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PT_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PT_PRESENT)) return;
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PT_PRESENT)) return;
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0;

    uint64_t active_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(active_cr3));
    if ((active_cr3 & ~0xFFFULL) == (cr3 & ~0xFFFULL)) {
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

/*
 * Create a fresh PML4 that shares the high half (kernel) with
 * `src_cr3` and has an empty low half (user).  Used by execve to
 * build a new address space in a scratch CR3 while the old one
 * remains live, so a failed execve can return -errno without having
 * torn down the caller.
 *
 * The returned physical address is a new PML4 page.  Its high-half
 * entries point at the same PDPTs/PDs/PTs as src_cr3 (shared, which
 * is correct: the kernel half is never per-process).  Its low-half
 * entries are all zero.  The recursive entry is installed.
 *
 * Returns 0 on allocation failure.
 *
 * Do NOT confuse this with vmm_clone_page_table, which deep-copies
 * the low half (used by fork, where the child must see the parent's
 * user mappings).  execve wants the opposite: an empty user half.
 */
uint64_t vmm_clone_kernel_half(uint64_t src_cr3) {
    uint64_t new_pml4_phys = pmm_alloc_page_for_tables();
    if (!new_pml4_phys) return 0;

    uint64_t* src_pml4 = (uint64_t*)phys_to_virt(src_cr3);
    uint64_t* new_pml4 = (uint64_t*)phys_to_virt(new_pml4_phys);
    memset(new_pml4, 0, PAGE_SIZE);

    /* Share the kernel half verbatim.  Entries 0..255 (user) stay
       zeroed; entry 510 (recursive) is set below; entries 256..511
       except 510 are copied. */
    for (int i = 256; i < 512; i++) {
        if (i == RECURSIVE_PML4_INDEX) continue;
        new_pml4[i] = src_pml4[i];
    }

    new_pml4[RECURSIVE_PML4_INDEX] = new_pml4_phys | PT_PRESENT | PT_WRITE;
    return new_pml4_phys;
}

/*
 * Free a user address space: walk the low half of `cr3`, free every
 * PDPT / PD / PT page below it, then free the PML4 page itself.  The
 * high half is shared with the kernel and is NOT touched.
 *
 * Caller is responsible for having already freed the physical data
 * pages (the ELF segments, user stack, brk, mmap) — this function
 * only frees page-table pages.  Do NOT call it on the live CR3 of a
 * running process; the process must first be off this CR3 (see
 * sys_execve's swap sequence).
 *
 * Only handles 4 KB PTEs; 2 MB huge pages in the user half are not
 * expected (the ELF loader, brk, and mmap all map 4 KB) and are
 * skipped at the PD level (no PT to free).  If a future code path
 * maps huge pages in the user half, extend this.
 */
void vmm_free_user_page_tables(uint64_t cr3) {
    if (!cr3) return;
    cr3 &= ~0xFFFULL;

    uint64_t* pml4 = (uint64_t*)phys_to_virt(cr3);

    for (int i = 0; i < 256; i++) {
        uint64_t pml4e = pml4[i];
        if (!(pml4e & PT_PRESENT)) continue;
        uint64_t pdpt_phys = pml4e & ~0xFFFULL;
        uint64_t* pdpt = (uint64_t*)phys_to_virt(pdpt_phys);

        for (int j = 0; j < 512; j++) {
            uint64_t pdpte = pdpt[j];
            if (!(pdpte & PT_PRESENT)) continue;
            uint64_t pd_phys = pdpte & ~0xFFFULL;
            uint64_t* pd = (uint64_t*)phys_to_virt(pd_phys);

            for (int k = 0; k < 512; k++) {
                uint64_t pde = pd[k];
                if (!(pde & PT_PRESENT)) continue;
                /* 2 MB huge page: no PT to free. */
                if (pde & 0x80) continue;
                uint64_t pt_phys = pde & ~0xFFFULL;
                pmm_free_page(pt_phys);
            }
            pmm_free_page(pd_phys);
        }
        pmm_free_page(pdpt_phys);
    }
    pmm_free_page(cr3);
}
