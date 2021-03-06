#pragma once

#include <sel4/sel4.h>
#include <stdlib.h>
#include <utils/util.h>

#include "frame_table.h"
#include "../coroutine/picoro.h"

#define VEND(x) ((x->vbase)+(x->memsize))


typedef seL4_Word vaddr_t;
typedef seL4_Word paddr_t;

typedef struct region {
    seL4_CapRights_t rights;
    seL4_ARM_VMAttributes attrs;
    vaddr_t vbase;
    struct region *prev;
    struct region *next;
    size_t memsize;
    bool mmaped;
} region_t;

typedef struct addrspace {
    struct region *regions;
    struct region *stack;
    struct region *heap;
    frame_ref_t pagetable;
    seL4_CPtr vspace;
    size_t pagecount;
} addrspace_t;

addrspace_t *as_create(seL4_CPtr vspace, coro_t coro);
void as_destroy(addrspace_t *as, cspace_t *cspace, coro_t coro);
int as_define_stack(struct addrspace *as, vaddr_t bottom, size_t sz);
int as_define_heap(struct addrspace *as, vaddr_t start);
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
        seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs, region_t **ret);
void as_destroy_region(struct addrspace *as, cspace_t *cspace, region_t *reg, bool unallocate, coro_t me);
void as_shrink_region(struct addrspace *as, cspace_t *cspace, region_t *reg, vaddr_t vaddr, size_t sz, bool unallocate, coro_t me);

static inline region_t *get_last_region(region_t *rg, vaddr_t vaddr) {
    region_t *lst = NULL;
    while(rg != NULL && rg->vbase <= vaddr) {
        lst = rg;
        rg = rg->next;
    }
    return lst;
}

static inline region_t *get_region(region_t *rg, vaddr_t vaddr) {
    region_t *ret = get_last_region(rg, vaddr);
    if (ret != NULL && VEND(ret) > vaddr) return ret;
    return NULL;
}

static inline region_t *get_region_with_possible_stack_extension(addrspace_t *as, vaddr_t vaddr) {
    assert(as->stack->prev != NULL);
    vaddr_t aligned = PAGE_ALIGN_4K(vaddr);
    if (VEND(as->stack->prev) <= aligned && aligned < as->stack->vbase) {
        as->stack->memsize += as->stack->vbase - aligned;
        as->stack->vbase = aligned;
        return as->stack;
    }
    return get_region(as->regions, vaddr);
}
