#include <assert.h>
#include <sel4runtime.h>
// #include <clock/clock.h>
#include <errno.h>
#include <sys/mman.h>

#include "syscall.h"
#include "memory.h"

#include "../coroutine/picoro.h"

static inline seL4_CapRights_t get_sel4_rights_from_prot(int prot)
{
    bool canRead = prot & PROT_READ;
    bool canWrite = prot & PROT_WRITE;

    return seL4_CapRights_new(false, false, canRead, canWrite);
}

/* Return new heap end address on success */
/* Return old heap end address on failure */
IMPLEMENT_SYSCALL(brk, 1) {
    (void) proc;
    (void) me;
    vaddr_t vaddr = seL4_GetMR(1);
    region_t *heap = proc->addrspace->heap;
    assert(heap->next != NULL);
    // NOTES: this is different from the actual Linux syscall: we require brk to align
    // this is for simplicity reasons
    if (IS_ALIGNED_4K(vaddr) && heap->vbase <= vaddr && vaddr <= heap->next->vbase) {
        for (vaddr_t curr = IS_ALIGNED_4K(vaddr) ? vaddr : PAGE_ALIGN_4K(vaddr) + 1; curr < heap->vbase + heap->memsize; curr += PAGE_SIZE_4K) {
            unalloc_frame(proc->addrspace, cspace, curr, me);
        }
        heap->memsize = vaddr - heap->vbase;
    }

    return return_word(heap->vbase + heap->memsize);
}

IMPLEMENT_SYSCALL(mmap, 3) {
    vaddr_t vaddr = seL4_GetMR(1);
    /* if mmap is called with NULL, we append the mmap region to the end of addrspace */
    if (vaddr == NULL) {
        region_t *curr = proc->addrspace->regions;
        while (curr->next != NULL) curr = curr->next;
        vaddr = VEND(curr);
    }
    size_t memsize = seL4_GetMR(2);
    int prot = seL4_GetMR(3);
    seL4_ARM_VMAttributes attr = seL4_ARM_Default_VMAttributes;
    if (!(prot & PROT_EXEC)) attr |= seL4_ARM_ExecuteNever;
    seL4_CapRights_t rights = get_sel4_rights_from_prot(prot);
    region_t *r;
    int result = as_define_region(proc->addrspace, vaddr, memsize, rights, attr, &r);
    if (result != 0) return return_word(NULL);
    r->mmaped = true;
    /*printf("mmap region list: ");
    region_t *region = proc->addrspace->regions;
    while (region != NULL) {
        printf("%p - %p\n", region->vbase, region->vbase + region->memsize);
        region = region->next;
    }
    printf("\n");*/
    return return_word(vaddr);
}

IMPLEMENT_SYSCALL(munmap, 2) {
    vaddr_t munmap_start = seL4_GetMR(1);
    size_t length = seL4_GetMR(2);
    // printf("munmap munmap_start %p, %d\n", munmap_start, length);
    if (length == 0) return return_word(0);
    vaddr_t munmap_end = munmap_start + length;
    if (!(IS_ALIGNED_4K(munmap_start) && IS_ALIGNED_4K(munmap_end))) return return_word(-EINVAL);
    region_t *region = get_region(proc->addrspace->regions, munmap_start);
    if (region == NULL || !(region->mmaped)) return return_word(-EINVAL);
    region_t *curr = region;
    /* check if we munmap valid address */
    /* if not valid, we don't do anything */
    while (VEND(curr) < munmap_end) {
        // printf("region [%p-%p]\n", curr->vbase, VEND(curr));
        curr = curr->next;
        if (curr == NULL || !(curr->mmaped) || VEND(curr->prev) != curr->vbase)
            return return_word(-EINVAL);
    }
    /* shrink or destroy the region(s) of munmap addr */
    while (curr->next != region) {
        size_t vend = VEND(region);
        if (munmap_start > region->vbase && munmap_end < vend) {
            region_t *r;
            as_shrink_region(proc->addrspace, cspace, region, region->vbase, munmap_start - region->vbase, false, me);
            int err = as_define_region(proc->addrspace, munmap_end, vend - munmap_end, region->rights, region->attrs, &r);
            if (err) return return_word(-EINVAL);
            r->mmaped = true;
            break;
        }
        vaddr_t keep_start, keep_end;
        /* munmap from munmap_start to the end of region */
        if (munmap_start > region->vbase) {
            keep_start = region->vbase;
            keep_end = munmap_start;
        }
        /* munmap from the start of region to munmap_end */
        else if (munmap_end < vend) {
            keep_start = munmap_end;
            keep_end = vend;
        }
        /* munmap whole region */
        else {
            keep_end = keep_start = region->vbase;
        }
        region_t *shrink = region;
        region = region->next;
        /*printf("before shrink: ");
        region_t *_region = proc->addrspace->regions;
        while (_region != NULL) {
            printf("%p - %p [%p]\n", _region->vbase, _region->vbase + _region->memsize, _region);
            _region = _region->next;
        }*/
        as_shrink_region(proc->addrspace, cspace, shrink, keep_start, keep_end - keep_start, false, me);
        /*printf("after shrink: ");
        _region = proc->addrspace->regions;
        while (_region != NULL) {
            printf("%p - %p [%p]\n", _region->vbase, _region->vbase + _region->memsize, _region);
            _region = _region->next;
        }*/
    }
    /* unallocate frames of munmap region */
    for (vaddr_t start = munmap_start; start < munmap_end; start += PAGE_SIZE_4K) {
        unalloc_frame(proc->addrspace, cspace, start, me);
    }

    /*printf("munmap region list: ");
    region = proc->addrspace->regions;
    while (region != NULL) {
        printf("%p - %p\n", region->vbase, region->vbase + region->memsize);
        region = region->next;
    }
    printf("\n");*/
    return return_word(0);
}
