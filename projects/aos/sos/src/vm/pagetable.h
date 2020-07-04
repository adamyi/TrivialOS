#pragma once

#include <stdbool.h>

#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>

#include "addrspace.h"
#include "../coroutine/picoro.h"

#include "../ut.h"
#include "frame_table.h"

#define PAGE_TABLE_LEVELS (4)
#define PAGE_TABLE_LEVEL_BITS (9)
#define PAGE_TABLE_LEVEL_SIZE ((1)<<(PAGE_TABLE_LEVEL_BITS))
#define PAGE_TABLE_LEVEL_MASK ((PAGE_TABLE_LEVEL_SIZE) - (1))
#define PTE_BITS (12)
#define PTE_SIZE ((1)<<(PTE_BITS))

typedef enum {
    IN_MEM = 1,
    PAGED_OUT = 2,
    SHARED_VM = 3,
} pte_type_t;

PACKED struct pde {
    frame_ref_t frame : 20;
    seL4_Word free : 27;
    bool inuse : 1;
    seL4_Word reserved : 16;
};

compile_time_assert("PDE Size", 8 == sizeof(pde_t));

PACKED struct pte {
    frame_ref_t frame : 20;
    seL4_ARM_Page cap : 20;
    pte_type_t type: 2;
    seL4_Word free : 5;
    bool inuse : 1;
    seL4_Word reserved : 16;
};

compile_time_assert("PTE Size", 8 == sizeof(pte_t));

typedef struct page_table {
    seL4_Word entries[PAGE_TABLE_LEVEL_SIZE]; // pte_t if level==0 else pde_t
} page_table_t;

seL4_Error sos_map_frame(struct addrspace *as, cspace_t *cspace, seL4_CPtr frame_cap, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr, pte_t *pte, coro_t coro);

seL4_Error create_pt(pde_t *entry, coro_t coro);
pte_t *get_pte(addrspace_t *as, vaddr_t vaddr, bool create, coro_t coro);
seL4_Error alloc_map_frame(addrspace_t *as, cspace_t *cspace, seL4_Word vaddr,
                    seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs, pte_t *pte, coro_t coro);
void unalloc_frame(addrspace_t *as, cspace_t *cspace, vaddr_t vaddr);
void *map_vaddr_to_sos(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, seL4_CPtr *local_cptr, size_t *size, coro_t coro);
void unmap_vaddr_from_sos(cspace_t *cspace, seL4_CPtr local_cptr);
int copy_in(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, size_t size, void *dest, coro_t coro);
int copy_out(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, size_t size, void *src, coro_t coro);
void pagetable_destroy(addrspace_t *as, cspace_t *cspace);
