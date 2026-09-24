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

void vmm_init(BootInfo* info);
void* ensure_hhdm_mapped(uint64_t phys);
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
int vmm_is_mapped(uint64_t virt);
void vmm_dump_page_table(uint64_t virt);

uint64_t vmm_clone_page_table(uint64_t src_cr3);
uint64_t vmm_get_phys_from_cr3(uint64_t cr3, uint64_t virt);
void vmm_map_page_in_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page_in_cr3(uint64_t cr3, uint64_t virt);

/*
 * execve support.  vmm_clone_kernel_half makes a fresh PML4 that
 * shares the kernel high half with src_cr3 and has an empty user low
 * half — the scratch address space execve builds before swapping in.
 * vmm_free_user_page_tables frees the low-half page-table pages and
 * the PML4 itself (not the data pages) after the caller has stopped
 * using the CR3.
 */
uint64_t vmm_clone_kernel_half(uint64_t src_cr3);
void     vmm_free_user_page_tables(uint64_t cr3);
