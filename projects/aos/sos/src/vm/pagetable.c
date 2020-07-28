#include "pagetable.h"
#include "addrspace.h"
#include "fault_handler.h"
#include "frame_table.h"
#include "../vmem_layout.h"
#include "../mapping.h"

#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>

static int level_to_offset[4] = {PTE_BITS + PAGE_TABLE_LEVEL_BITS * 0,
                                 PTE_BITS + PAGE_TABLE_LEVEL_BITS * 1,
                                 PTE_BITS + PAGE_TABLE_LEVEL_BITS * 2,
                                 PTE_BITS + PAGE_TABLE_LEVEL_BITS * 3};

static seL4_Word pt_level2type[4] = {seL4_ARM_PageTableObject, seL4_ARM_PageDirectoryObject, seL4_ARM_PageUpperDirectoryObject, seL4_ARM_PageGlobalDirectoryObject};
//LIBSEL4_INLINE seL4_Error
// seL4_ARM_PageDirectory_Map(seL4_ARM_PageDirectory _service, seL4_CPtr vspace, seL4_Word vaddr, seL4_ARM_VMAttributes attr)
typedef seL4_Error (*pt_mapper)(seL4_ARM_PageDirectory, seL4_CPtr, seL4_Word, seL4_ARM_VMAttributes);
static pt_mapper pt_level2mapper[3] = {seL4_ARM_PageTable_Map, seL4_ARM_PageDirectory_Map, seL4_ARM_PageUpperDirectory_Map};

static inline seL4_Word get_vaddr_level_idx(vaddr_t addr, int level) {
    return (addr >> level_to_offset[level]) & PAGE_TABLE_LEVEL_MASK;
}

static inline void setWord(page_table_t *pt, seL4_Word word, int off) {
    pt->entries[off] &= 0xFFFFFFFFFF0000;
    pt->entries[off++] |= word >> 48;
    pt->entries[off] &= 0xFFFFFFFFFF0000;
    pt->entries[off++] |= (word & 0x0000FFFF00000000) >> 32;
    pt->entries[off] &= 0xFFFFFFFFFF0000;
    pt->entries[off++] |= (word & 0x00000000FFFF0000) >> 16;
    pt->entries[off] &= 0xFFFFFFFFFF0000;
    pt->entries[off] |= (word & 0x000000000000FFFF);
}

static inline seL4_Word getWord(page_table_t *pt, int off) {
    return ((pt->entries[off + 0] & 0xFFFF) << 48) |
           ((pt->entries[off + 1] & 0xFFFF) << 32) |
           ((pt->entries[off + 2] & 0xFFFF) << 16) |
           ((pt->entries[off + 3] & 0xFFFF));
}

static inline void setCap(page_table_t *pt, seL4_CPtr cap) {
    setWord(pt, (seL4_Word) cap, 0);
}
static inline seL4_CPtr getCap(page_table_t *pt) {
    return (seL4_CPtr) getWord(pt, 0);
}

static inline void setUt(page_table_t *pt, ut_t *ut) {
    setWord(pt, (seL4_Word) ut, 4);
}
static inline ut_t *getUt(page_table_t *pt) {
    return (ut_t *) getWord(pt, 4);
}

seL4_Error create_pt(pde_t *entry, coro_t coro) {
    frame_ref_t frame = alloc_frame(coro);
    printf("create_pt %d\n", frame);
    if (frame == NULL_FRAME) {
        ZF_LOGE("Couldn't allocate additional stack frame");
        return seL4_NotEnoughMemory;
    }
    pin_frame(frame);
    memset(frame_data(frame), 0, PAGE_SIZE_4K);
    entry->inuse = true;
    entry->frame = frame;
    return seL4_NoError;
}

static page_table_t *get_pt_level(addrspace_t *as, vaddr_t addr, int level, bool create, coro_t coro) {
    page_table_t *pt = frame_data(as->pagetable);
    for (int i = PAGE_TABLE_LEVELS - 2; i >= level; i--) {
        seL4_Word idx = get_vaddr_level_idx(addr, i);
        pde_t *entry = (pde_t *) (pt->entries + idx);
        if (!entry->inuse) {
            if (!create) return NULL;
            if (create_pt(entry, coro) != seL4_NoError) return NULL;
        }
        assert(entry->inuse);
        pt = frame_data(entry->frame);
    }
    return pt;
}

static seL4_Error retype_pt(cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr, seL4_CPtr ut, seL4_CPtr empty, int level) {
    seL4_Error err = cspace_untyped_retype(cspace, ut, empty, pt_level2type[level], seL4_PageBits);
    if (err) return err;
    return pt_level2mapper[level](empty, vspace, vaddr, seL4_ARM_Default_VMAttributes);
}

static seL4_Error map_frame_impl(addrspace_t *as, cspace_t *cspace, seL4_CPtr frame_cap, frame_ref_t frame_ref, seL4_Word vaddr,
                                 seL4_CapRights_t rights, seL4_ARM_VMAttributes attr, pte_t *ret, coro_t coro) {
    // ZF_LOGE("Mapping %p (frame %d)", vaddr, frame_ref);
    /* Attempt the mapping */
    seL4_Error err = seL4_ARM_Page_Map(frame_cap, as->vspace, vaddr, rights, attr);
    for (size_t i = 0; i < MAPPING_SLOTS && err == seL4_FailedLookup; i++) {
        /* save this so nothing else trashes the message register value */
        seL4_Word failed = seL4_MappingFailedLookupLevel();

        /* Assume the error was because we are missing a paging structure */
        ut_t *ut = ut_alloc_4k_untyped(NULL);
        if (ut == NULL) {
            ZF_LOGE("Out of 4k untyped");
            return -1;
        }

        /* figure out which cptr to use to retype into*/
        seL4_CPtr slot = cspace_alloc_slot(cspace);

        if (slot == seL4_CapNull) {
            ZF_LOGE("No cptr to alloc paging structure");
            return -1;
        }

        int level = -1;

        switch (failed) {
        case SEL4_MAPPING_LOOKUP_NO_PT:
            level = 0;
            break;
        case SEL4_MAPPING_LOOKUP_NO_PD:
            level = 1;
            break;
        case SEL4_MAPPING_LOOKUP_NO_PUD:
            level = 2;
            break;
       default:
           ZF_LOGE("i don't recognize that error. earth must be flat");
           return -1;
        }

        err = retype_pt(cspace, as->vspace, vaddr, ut->cap, slot, level);
        if (!err) {
            page_table_t *pt = get_pt_level(as, vaddr, level, true, coro);
            if (pt == NULL) {
                err = seL4_NotEnoughMemory;
            } else {
                setUt(pt, ut);
                setCap(pt, slot);
                /* Try the mapping again */
                err = seL4_ARM_Page_Map(frame_cap, as->vspace, vaddr, rights, attr);
            }
        }
    }

    if (!err) {
        /* Create PTE */
        pte_t *pte = get_pte(as, vaddr, true, coro);
        if (pte == NULL) {
            err = seL4_NotEnoughMemory;
        } else {
            pte->cap = frame_cap;
            pte->frame = frame_ref;
            pte->inuse = true;
            pte->type = IN_MEM;
            set_frame_pte(frame_ref, pte);
        }
        if (ret != NULL) *ret = *pte;
    }

    return err;
}


seL4_Error sos_map_frame(addrspace_t *as, cspace_t *cspace, frame_ref_t frame_ref, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr, pte_t *ret, coro_t coro) {
    /* allocate a slot to duplicate the frame cap so we can map it into the application */
    seL4_CPtr frame_cptr = cspace_alloc_slot(cspace);
    if (frame_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack extra frame");
        return seL4_NotEnoughMemory;
    }

    /* copy the stack frame cap into the slot */
    int err = cspace_copy(cspace, frame_cptr, cspace, frame_page(frame_ref), rights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, frame_cptr);
        ZF_LOGE("Failed to copy cap");
        return err;
    }

    err = map_frame_impl(as, cspace, frame_cptr, frame_ref, vaddr, rights, attr, ret, coro);
    if (err != 0) {
        cspace_delete(cspace, frame_cptr);
        cspace_free_slot(cspace, frame_cptr);
        ZF_LOGE("Unable to map extra stack frame for user app");
        return err;
    }

    return seL4_NoError;
}

pte_t *get_pte(addrspace_t *as, vaddr_t vaddr, bool create, coro_t coro) {
    page_table_t *pdt = get_pt_level(as, vaddr, 1, create, coro);
    if (pdt == NULL) return NULL;
    pte_t *pte = (pte_t *) (pdt->entries + get_vaddr_level_idx(vaddr, 0));
    if (create || pte->inuse) return pte;
    return NULL;
}

seL4_Error alloc_map_frame(addrspace_t *as, cspace_t *cspace, seL4_Word vaddr,
                    seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs, pte_t *pte, coro_t coro) {
    frame_ref_t frame = alloc_frame(coro);
    if (frame == NULL_FRAME) {
        ZF_LOGE("Couldn't allocate additional stack frame");
        return seL4_NotEnoughMemory;
    }
    // printf("alloc map frame %d\n", frame);
    pin_frame(frame);
    seL4_Error err = sos_map_frame(as, cspace, frame, vaddr, rights, attrs, pte, coro);
    unpin_frame(frame);
    // printf("alloc map frame %d %p\n", frame, frame_from_ref(frame)->pte);
    if (err != seL4_NoError) {
        free_frame(frame);
    }
    return err;
}

static inline void unalloc_frame_impl(pte_t *pte, cspace_t *cspace) {
    if (pte && pte->inuse) {
        /* unmap our pte */
        seL4_Error err = seL4_ARM_Page_Unmap(pte->cap);
        assert(err == seL4_NoError);

        /* delete the frame cap */
        err = cspace_delete(cspace, pte->cap);
        assert(err == seL4_NoError);

        /* mark the slot as free */
        cspace_free_slot(cspace, pte->cap);

        /* free frame */
        free_frame(pte->frame);

        pte->inuse = false;
    }
}

static void pagetable_destroy_impl(page_table_t *pt, cspace_t *cspace, int level) {
    for (int i = 0; i < PAGE_TABLE_LEVEL_SIZE; i++) {
        if (level == 0) {
            unalloc_frame_impl(pt->entries + i, cspace);
        } else {
            pde_t *entry = (pde_t *) (pt->entries + i);
            pagetable_destroy_impl(frame_data(entry->frame), cspace, level - 1);
            free_frame(entry->frame);
        }
    }
    seL4_CPtr cap = getCap(pt);
    ut_t *ut = getUt(pt);
    seL4_Error err = cspace_delete(cspace, cap);
    assert(err == seL4_NoError);
    cspace_free_slot(cspace, cap);
    ut_free(ut);
}

void pagetable_destroy(addrspace_t *as, cspace_t *cspace) {
    pagetable_destroy_impl(frame_data(as->pagetable), cspace, PAGE_TABLE_LEVELS - 1);
    free_frame(as->pagetable);
}


void unalloc_frame(addrspace_t *as, cspace_t *cspace, vaddr_t vaddr) {
    // give null coro since not create pte
    unalloc_frame_impl(get_pte(as, vaddr, false, NULL), cspace);
}

void *map_vaddr_to_sos(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, seL4_CPtr *local_cptr, size_t *size, coro_t coro) {
    vaddr_t vbase = PAGE_ALIGN_4K(vaddr);
    size_t offset = vaddr - vbase;
    pte_t *pte = get_pte(as, vbase, true, coro);
    if (pte == NULL) {
        ZF_LOGE("pte is null");
        return NULL;
    }
    if (!pte->inuse) {
        frame_ref_t frame = alloc_frame(coro);
        if (frame == NULL_FRAME) {
            ZF_LOGE("Couldn't allocate additional stack frame");
            return NULL;
        }

        /* allocate a slot to duplicate the frame cap so we can map it into the application */
        seL4_CPtr frame_cptr = cspace_alloc_slot(cspace);
        if (frame_cptr == seL4_CapNull) {
            free_frame(frame);
            ZF_LOGE("Failed to alloc slot for stack extra frame");
            return NULL;
        }

        /* copy the stack frame cap into the slot */
        int err = cspace_copy(cspace, frame_cptr, cspace, frame_page(frame), seL4_AllRights);
        if (err != seL4_NoError) {
            cspace_free_slot(cspace, frame_cptr);
            free_frame(frame);
            ZF_LOGE("Failed to copy cap");
            return NULL;
        }

        pte->cap = frame_cptr;
        pte->frame = frame;
        pte->inuse = true;
        pte->type = IN_MEM;
        set_frame_pte(frame, pte);
    }

    //printf("%d\n", pte->frame);
    switch (pte->type) {
        case PAGED_OUT:;
        if (!ensure_mapping(cspace, (void *) vaddr, as, coro)) {
            ZF_LOGE("Failed ensure_mapping");
            return NULL;
        }
    }

    void *addr = frame_data(pte->frame);
    //printf("%p\n", addr);

    *local_cptr = frame_page(pte->frame);

    *size = PAGE_SIZE_4K - offset;
    //ZF_LOGE("mapped %p to %p (page %p, %d till end)", vaddr, addr + offset, addr, *size);
    return addr + offset;
}

void unmap_vaddr_from_sos(cspace_t *cspace, seL4_CPtr local_cptr) {
    /* we don't do anything */
}

int copy_in(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, size_t size, void *dest, coro_t coro) {
    size_t rs;
    seL4_CPtr lc;
    void *src;
    while (size > 0) {
        // we don't need to check overflow because regions are aligned
        // if we overflow, we won't get pte
        src = map_vaddr_to_sos(cspace, as, vaddr, &lc, &rs, coro);
        //printf("sos addr %p\n", src);
        if (src == NULL) return -1;
        if (size < rs) rs = size;
        //printf("copying %d bytes from %p to %p\n", rs, src, dest);
        memcpy(dest, src, rs);
        size -= rs;
        dest += rs;
        unmap_vaddr_from_sos(cspace, lc);
    }
    return 0;
}

int copy_out(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, size_t size, void *src, coro_t coro) {
    size_t rs;
    seL4_CPtr lc;
    void *dest;
    while (size > 0) {
        // we don't need to check overflow because regions are aligned
        // if we overflow, we won't get pte
        dest = map_vaddr_to_sos(cspace, as, vaddr, &lc, &rs, coro);
        if (dest == NULL) return -1;
        if (size < rs) rs = size;
        memcpy(dest, src, rs);
        size -= rs;
        src += rs;
        unmap_vaddr_from_sos(cspace, lc);
    }
    return 0;
}
