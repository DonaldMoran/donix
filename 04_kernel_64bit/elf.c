#include "include/elf.h"
#include "include/vga.h"
#include "include/serial.h"
#include "include/pmm.h"
#include "include/vmm.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/heap.h"
#include "include/user_space.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "include/debug.h"

#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#endif

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

static int elf_validate(const Elf64_Ehdr* ehdr) {
    if (ehdr->e_ident[0] != ELF_MAGIC0 ||
        ehdr->e_ident[1] != ELF_MAGIC1 ||
        ehdr->e_ident[2] != ELF_MAGIC2 ||
        ehdr->e_ident[3] != ELF_MAGIC3) {
        return -1;
    }
    if (ehdr->e_ident[4] != 2) return -1;
    if (ehdr->e_ident[5] != 1) return -1;
    return 0;
}

void elf_add_page_to_pcb(pcb_t* pcb, uint64_t phys) {
    if (!pcb) return;
    if (!pcb->elf_page_list) {
        pcb->elf_page_list = (uint64_t*)kmalloc(64 * sizeof(uint64_t));
        if (!pcb->elf_page_list) {
            serial_print("ELF: Failed to allocate page list!\n");
            return;
        }
        pcb->elf_num_pages = 0;
    }

    for (uint64_t i = 0; i < pcb->elf_num_pages; i++) {
        if (pcb->elf_page_list[i] == phys) {
            return;
        }
    }

    if ((pcb->elf_num_pages % 64) == 0 && pcb->elf_num_pages > 0) {
        uint64_t* new_list = (uint64_t*)kmalloc((pcb->elf_num_pages + 64) * sizeof(uint64_t));
        if (!new_list) {
            serial_print("ELF: Failed to reallocate page list!\n");
            return;
        }
        for (uint64_t i = 0; i < pcb->elf_num_pages; i++) {
            new_list[i] = pcb->elf_page_list[i];
        }
        kfree(pcb->elf_page_list);
        pcb->elf_page_list = new_list;
    }

    pcb->elf_page_list[pcb->elf_num_pages++] = phys;
    if (pcb->elf_num_pages == 1) {
        pcb->elf_base_phys = phys;
    }
}

uint64_t elf_load_into_process(pcb_t* pcb, const void* elf_data) {
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)elf_data;

    if (elf_validate(ehdr) < 0) {
        serial_print("ELF: Validation checks failed!\n");
        return 0;
    }

    const Elf64_Phdr* phdr = (const Elf64_Phdr*)((uintptr_t)elf_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uint64_t vaddr  = phdr[i].p_vaddr;
        uint64_t memsz  = phdr[i].p_memsz;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t offset = phdr[i].p_offset;

        uint64_t start_page = vaddr & ~0xFFFULL;
        uint64_t end_page   = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t virt = start_page; virt < end_page; virt += 4096) {
            uint64_t phys = pmm_alloc_page(PAGE_USER_DATA);
            if (!phys) {
                serial_print("ELF: Out of physical page frames!\n");
                return 0;
            }

            uint64_t map_flags = 0x1FULL;
            if (vmm_map_page_in_cr3(pcb->cr3, virt, phys, map_flags) != 0) {
                /*
                 * A page-table allocation failed while mapping this
                 * segment page.  The mapping was NOT made.
                 *
                 * The page just allocated for the segment (`phys`)
                 * has not been added to pcb->elf_page_list yet, so
                 * process_cleanup_elf_pages will not free it.  Free
                 * it here before returning, or it leaks.
                 *
                 * The pages mapped for earlier iterations of this
                 * loop ARE in elf_page_list (elf_add_page_to_pcb was
                 * called for each), so the caller's cleanup frees
                 * them.  That is why this returns 0 rather than
                 * trying to unwind: the caller already knows how.
                 *
                 * Returning 0 is elf_load_into_process's existing
                 * "load failed" contract.  Both callers check it:
                 * sys_execve treats it as fatal (the old address
                 * space is already torn down by the time this runs
                 * -- see the sys_execve header comment), and
                 * kmain.c's load_elf_into_user_process returns 0 and
                 * the caller destroys the process.  So the failure
                 * is reported; what the caller does with it is the
                 * caller's business.
                 */
                serial_print("ELF: map failed at vaddr=");
                serial_print_hex(virt);
                serial_print(" (out of page-table pages)\n");
                pmm_free_page(phys);
                return 0;
            }
            ensure_hhdm_mapped(phys);
            elf_add_page_to_pcb(pcb, phys);
        }

        const uint8_t* src = (const uint8_t*)elf_data + offset;
        if (filesz > 0) {
            uint64_t copied = 0;
            while (copied < filesz) {
                uint64_t cur_virt = vaddr + copied;
                uint64_t phys = vmm_get_phys_from_cr3(pcb->cr3, cur_virt);
                if (phys == 0) {
                    serial_print("ELF: COPY-FAIL phys=0 at vaddr=");
                    serial_print_hex(cur_virt);
                    serial_print("\n");
                    return 0;
                }
                uint64_t page_off = cur_virt & 0xFFF;
                size_t chunk = filesz - copied;
                size_t page_rem = 4096 - page_off;
                if (chunk > page_rem) chunk = page_rem;
                uint8_t* dest = (uint8_t*)(HHDM_START + phys);
                for (size_t j = 0; j < chunk; j++) {
                    dest[j] = src[copied + j];
                }
                copied += chunk;
            }
        }

        if (memsz > filesz) {
            uint64_t bss_start = vaddr + filesz;
            uint64_t bss_bytes = memsz - filesz;
            uint64_t zeroed = 0;
            while (zeroed < bss_bytes) {
                uint64_t cur_virt = bss_start + zeroed;
                uint64_t phys = vmm_get_phys_from_cr3(pcb->cr3, cur_virt);
                if (phys == 0) {
                    serial_print("ELF: BSS-FAIL phys=0 at vaddr=");
                    serial_print_hex(cur_virt);
                    serial_print("\n");
                    return 0;
                }
                uint64_t page_off = cur_virt & 0xFFF;
                size_t chunk = bss_bytes - zeroed;
                size_t page_rem = 4096 - page_off;
                if (chunk > page_rem) chunk = page_rem;
                uint8_t* dest = (uint8_t*)(HHDM_START + phys);
                for (size_t j = 0; j < chunk; j++) {
                    dest[j] = 0;
                }
                zeroed += chunk;
            }
        }
    }

    return ehdr->e_entry;
}
