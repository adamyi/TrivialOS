#include <sel4/sel4.h>
#include <aos/debug.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>

#include "addrspace.h"
#include "pagetable.h"
#include "../vmem_layout.h"

region_t *region_create(vaddr_t vaddr, size_t sz,
        seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs) {
    region_t *r = malloc(sizeof(region_t));
    if (r == NULL) {
        ZF_LOGE("Can't initialize region");
        return NULL;
    }
    r->vbase = vaddr;
    r->memsize = sz;
    r->rights = rights;
    r->attrs = attrs;
    r->prev = r->next = NULL;
    return r;
}

addrspace_t *as_create(seL4_CPtr vspace, coro_t coro) {
    addrspace_t *ret = malloc(sizeof(addrspace_t));
    if (ret == NULL) return NULL;
    bzero(ret, sizeof(addrspace_t));

    ret->pagetable = alloc_frame(coro);
    pin_frame(ret->pagetable);
    if (ret->pagetable == NULL_FRAME) {
        free(ret);
        return NULL;
    }
    memset(frame_data(ret->pagetable), 0, PAGE_SIZE_4K);
    ret->vspace = vspace;
    ret->pagecount = 0;

    return ret;
}

void as_destroy(addrspace_t *as, cspace_t *cspace, coro_t coro) {
    pagetable_destroy(as, cspace, coro);
    free(as);
}

int as_define_stack(struct addrspace *as, vaddr_t bottom, size_t sz) {
    assert(as->stack == NULL);
    return as_define_region(as, bottom - sz, sz, seL4_ReadWrite,
                seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &(as->stack));
}

int as_define_heap(struct addrspace *as, vaddr_t start) {
    assert(as->heap == NULL);
    return as_define_region(as, start, 0, seL4_ReadWrite,
                seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &(as->heap));
}

int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
        seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs, region_t **ret) {
    region_t **curr = &(as->regions);
    while (*curr != NULL) {
        if (((*curr)->vbase + (*curr)->memsize) > vaddr) break;
        curr = &((*curr)->next);
    }

    if (*curr != NULL && (vaddr + sz) > (*curr)->vbase)
        return -EINVAL;
    
    region_t *new_region = region_create(vaddr, sz, rights, attrs);
    if (new_region == NULL)
        return -ENOMEM;

    new_region->next = *curr;
    if (*curr != NULL) {
        new_region->prev = (*curr)->prev;
        (*curr)->prev = new_region;
    }
    *curr = new_region;
    if (ret != NULL) *ret = new_region;
    return 0;
}
