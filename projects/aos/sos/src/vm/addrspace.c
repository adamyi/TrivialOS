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
    r->mmaped = false;
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
    ZF_LOGD("as_define_regine: %p-%p", vaddr, vaddr+sz);
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
    /* :bigbrain: */
    if (as->regions) new_region->prev = (void *)curr - (void *)&(((region_t *)NULL)->next);
    if (*curr != NULL) (*curr)->prev = new_region;
    *curr = new_region;
    if (ret != NULL) *ret = new_region;
    return 0;
}

void as_destroy_region(struct addrspace *as, cspace_t *cspace, region_t *reg, bool unallocate, coro_t me) {
    ZF_LOGD("as_destroy_region: %p-%p", reg->vbase, reg->vbase + reg->memsize);
    if (reg->prev) reg->prev->next = reg->next;
    else as->regions = as->regions->next;
    if (reg->next) reg->next->prev = reg->prev;
    
    if (unallocate) {
        for (vaddr_t curr = reg->vbase; curr < VEND(reg); curr += PAGE_SIZE_4K) {
            unalloc_frame(as, cspace, curr, me);
        }
    }
    free(reg);
}

void as_shrink_region(struct addrspace *as, cspace_t *cspace, region_t *reg, vaddr_t vaddr, size_t sz, bool unallocate, coro_t me) {
    ZF_LOGD("as_shrink_region: [%p-%p] -> [%p-%p]", reg->vbase, reg->vbase + reg->memsize, vaddr, sz);
    assert(vaddr >= reg->vbase);
    assert(vaddr + sz <= reg->vbase + reg->memsize);
    if (sz == 0) {
        as_destroy_region(as, cspace, reg, unallocate, me);
        return;
    }
    if (unallocate) {
        for (vaddr_t curr = reg->vbase; curr < vaddr; curr += PAGE_SIZE_4K) {
            unalloc_frame(as, cspace, curr, me);
        }
        for (vaddr_t curr = vaddr + sz; curr < reg->vbase + reg->memsize; curr += PAGE_SIZE_4K) {
            unalloc_frame(as, cspace, curr, me);
        }
    }
    reg->vbase = vaddr;
    reg->memsize = sz;
}
