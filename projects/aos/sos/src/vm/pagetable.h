#pragma once

#include <stdbool.h>

#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>

#include "addrspace.h"

#include "../ut.h"

#define PAGE_TABLE_LEVELS (4)
#define PAGE_TABLE_LEVEL_BITS (9)
#define PAGE_TABLE_LEVEL_SIZE ((1)<<(PAGE_TABLE_LEVEL_BITS))
#define PAGE_TABLE_LEVEL_MASK ((PAGE_TABLE_LEVEL_SIZE) - (1))
#define PTE_BITS (12)
#define PTE_SIZE ((1)<<(PTE_BITS))

typedef struct pte {
  seL4_CPtr cap;
  ut_t *ut;
} pte_t;

typedef struct page_table {
  int level;
  void *entries[PAGE_TABLE_LEVEL_SIZE]; // pte_t if level==0 else page_table_t
  seL4_CPtr cap;
  ut_t *ut;
} page_table_t;

seL4_Error sos_map_frame(struct addrspace *as, cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr);

page_table_t *create_pt();
pte_t *get_pte(addrspace_t *as, vaddr_t vaddr, bool create);
seL4_CPtr alloc_map_frame(addrspace_t *as, cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr,
                    seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs);
void unalloc_frame(addrspace_t *as, cspace_t *cspace, vaddr_t vaddr);
void *map_vaddr_to_sos(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, seL4_CPtr *local_cptr, size_t *size);
void unmap_vaddr_from_sos(cspace_t *cspace, seL4_CPtr local_cptr);
int copy_in(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, size_t size, void *dest);
