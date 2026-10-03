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
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_get_phys(uint64_t virt);
int vmm_is_mapped(uint64_t virt);
void vmm_dump_page_table(uint64_t virt);

uint64_t vmm_clone_page_table(uint64_t src_cr3);
uint64_t vmm_get_phys_from_cr3(uint64_t cr3, uint64_t virt);
int vmm_map_page_in_cr3(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page_in_cr3(uint64_t cr3, uint64_t virt);
