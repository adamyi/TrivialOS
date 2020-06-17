#include "pagetable.h"
#include "addrspace.h"
#include "../frame_table.h"
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

page_table_t *create_pt() {
    page_table_t *pt = malloc(sizeof(page_table_t));
    if (pt == NULL) {
        ZF_LOGE("cannot malloc rip");
        return NULL;
    }
    memset(pt->entries, 0, sizeof(pt->entries));
    pt->cap = seL4_CapNull;
    pt->ut = NULL;
    return pt;
}

static page_table_t *get_pt_level(addrspace_t *as, vaddr_t addr, int level, bool create) {
    page_table_t *pt = as->pagetable;
    for (int i = PAGE_TABLE_LEVELS - 2; i >= level; i--) {
        seL4_Word idx = get_vaddr_level_idx(addr, i);
        if (pt->entries[idx] == NULL) {
            if (!create) return NULL;
            pt->entries[idx] = create_pt();
        }
        pt = pt->entries[idx];
    }
    return pt;
}

static seL4_Error retype_pt(cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr, seL4_CPtr ut, seL4_CPtr empty, int level) {
    seL4_Error err = cspace_untyped_retype(cspace, ut, empty, pt_level2type[level], seL4_PageBits);
    if (err) return err;
    return pt_level2mapper[level](empty, vspace, vaddr, seL4_ARM_Default_VMAttributes);
}

static seL4_Error map_frame_impl(addrspace_t *as, cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                                 seL4_CapRights_t rights, seL4_ARM_VMAttributes attr,
                                 seL4_CPtr *free_slots, seL4_Word *used) {
    ZF_LOGI("Mapping %p\n", vaddr);
    /* Attempt the mapping */
    seL4_Error err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
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
        seL4_CPtr slot;
        if (used != NULL) {
            slot = free_slots[i];
            *used |= BIT(i);
        } else {
            slot = cspace_alloc_slot(cspace);
        }

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

        err = retype_pt(cspace, vspace, vaddr, ut->cap, slot, level);
        if (!err) {
            page_table_t *pt = get_pt_level(as, vaddr, level, true);
            pt->ut = ut;
            pt->cap = slot;
            if (pt == NULL) {
                err = seL4_NotEnoughMemory;
            } else {
                pt->ut = ut;
                pt->cap = slot;
                /* Try the mapping again */
                err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
            }
        }
    }

    if (!err) {
        /* Create PTE */
        pte_t *pte = get_pte(as, vaddr, true);
        if (pte == NULL)
            err = seL4_NotEnoughMemory;
        else
            pte->cap = frame_cap;
    }

    return err;
}


seL4_Error sos_map_frame(addrspace_t *as, cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr)
{
    return map_frame_impl(as, cspace, frame_cap, vspace, vaddr, rights, attr, NULL, NULL);
}

pte_t *get_pte(addrspace_t *as, vaddr_t vaddr, bool create) {
   page_table_t *pdt = get_pt_level(as, vaddr, 1, create);
   if (pdt == NULL) return NULL;
   pte_t **pte = (pte_t **) &(pdt->entries[get_vaddr_level_idx(vaddr, 0)]);
   if (!create || *pte != NULL) return *pte;
   *pte = malloc(sizeof(pte_t));
   (*pte)->cap = seL4_CapNull;
   return *pte;
}

seL4_CPtr alloc_map_frame(addrspace_t *as, cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr,
                    seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs) {
    
    frame_ref_t frame = alloc_frame();
    if (frame == NULL_FRAME) {
        ZF_LOGE("Couldn't allocate additional stack frame");
        return seL4_CapNull;
    }

    /* allocate a slot to duplicate the frame cap so we can map it into the application */
    seL4_CPtr frame_cptr = cspace_alloc_slot(cspace);
    if (frame_cptr == seL4_CapNull) {
        free_frame(frame);
        ZF_LOGE("Failed to alloc slot for stack extra frame");
        return seL4_CapNull;
    }

    /* copy the stack frame cap into the slot */
    int err = cspace_copy(cspace, frame_cptr, cspace, frame_page(frame), rights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, frame_cptr);
        free_frame(frame);
        ZF_LOGE("Failed to copy cap");
        return seL4_CapNull;
    }

    err = sos_map_frame(as, cspace, frame_cptr, vspace, vaddr, rights, attrs);
    if (err != 0) {
        cspace_delete(cspace, frame_cptr);
        cspace_free_slot(cspace, frame_cptr);
        free_frame(frame);
        ZF_LOGE("Unable to map extra stack frame for user app");
        return seL4_CapNull;
    }

    return frame_cptr;
}

void unalloc_frame(addrspace_t *as, cspace_t *cspace, vaddr_t vaddr) {
    pte_t *pte = get_pte(as, vaddr, false);
    if (pte && pte->cap != seL4_CapNull) {
        /* unmap our pte */
        seL4_Error err = seL4_ARM_Page_Unmap(pte->cap);
        assert(err == seL4_NoError);

        /* delete the frame cap */
        err = cspace_delete(cspace, pte->cap);
        assert(err == seL4_NoError);

        /* mark the slot as free */
        cspace_free_slot(cspace, pte->cap);

        pte->cap = seL4_CapNull;
    }
}

void *map_vaddr_to_sos(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, seL4_CPtr *local_cptr, size_t *size) {
    vaddr_t vbase = PAGE_ALIGN_4K(vaddr);
    size_t offset = vaddr - vbase;
    pte_t *pte = get_pte(as, vbase, true);
    if (pte == NULL) {
        ZF_LOGE("pte is null");
        return NULL;
    }
    if (pte->cap == seL4_CapNull) {
        frame_ref_t frame = alloc_frame();
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
    }

    seL4_ARM_Page_GetAddress_t gar = seL4_ARM_Page_GetAddress(pte->cap);
    if (gar.error) {
        ZF_LOGE("failed to get physical addr");
        return NULL;
    }
    void *addr = SOS_PROC_VADDR_MAP + gar.paddr;

    /* allocate a slot to duplicate the sframe cap so we can map it into our address space */
    *local_cptr = cspace_alloc_slot(cspace);
    if (*local_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot");
        return NULL;
    }

    /* copy the stack frame cap into the slot */
    seL4_Error err = cspace_copy(cspace, *local_cptr, cspace, pte->cap, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, *local_cptr);
        ZF_LOGE("Failed to copy cap %d", err);
        return NULL;
    }

    /* map it into the sos address space */
    err = map_frame(cspace, *local_cptr, seL4_CapInitThreadVSpace, addr, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
    if (err != seL4_NoError) {
        cspace_delete(cspace, *local_cptr);
        cspace_free_slot(cspace, *local_cptr);
        ZF_LOGE("Failed to map to SOS %d", err);
        return NULL;
    }
    *size = PAGE_SIZE_4K - offset;
    ZF_LOGE("mapped %p to %p (page %p, %d till end)", vaddr, addr + offset, addr, *size);
    return addr + offset;
}

void unmap_vaddr_from_sos(cspace_t *cspace, seL4_CPtr local_cptr) {
    seL4_ARM_Page_Unmap(local_cptr);
    cspace_delete(cspace, local_cptr);
    cspace_free_slot(cspace, local_cptr);
}

int copy_in(cspace_t *cspace, addrspace_t *as, vaddr_t vaddr, size_t size, void *dest) {
    size_t rs;
    seL4_CPtr lc;
    void *src;
    while (size > 0) {
        // we don't need to check overflow because regions are aligned
        // if we overflow, we won't get pte
        src = map_vaddr_to_sos(cspace, as, vaddr, &lc, &rs);
        printf("sos addr %p\n", src);
        if (src == NULL) return -1;
        if (size < rs) rs = size;
        printf("copying %d bytes from %p to %p\n", rs, src, dest);
        memcpy(dest, src, rs);
        size -= rs;
        dest += rs;
        unmap_vaddr_from_sos(cspace, lc);
    }
    return 0;
}
