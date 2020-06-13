#pragma once

#include <sel4/sel4.h>
#include <stdlib.h>

#include "pagetable.h"

typedef seL4_Word vaddr_t;
typedef seL4_Word paddr_t;

typedef struct region {
    seL4_CapRights_t rights;
    seL4_ARM_VMAttributes attrs;
    vaddr_t vbase;
    struct region *next;
    size_t memsize;
} region_t;

typedef struct addrspace {
    struct region *regions;
    struct region *stack;
    struct region *heap;
    page_table_t *pagetable;
} addrspace_t;

addrspace_t *as_create();
void as_destroy(addrspace_t *as);

int as_define_region(struct addrspace *as,
        vaddr_t vaddr, size_t sz,
        int readable,
        int writeable,
        int executable);


