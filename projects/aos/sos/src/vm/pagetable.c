#include "pagetable.h"
#include "addrspace.h"
#include "../frame_table.h"

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
    printf("get_pt\n");
    page_table_t *pt = as->pagetable;
    for (int i = PAGE_TABLE_LEVELS - 2; i >= level; i--) {
        printf("pt %p\n", pt);
        seL4_Word idx = get_vaddr_level_idx(addr, i);
        printf("aaa\n");
        printf("%ld %d\n", idx, PAGE_TABLE_LEVEL_SIZE);
        printf("%p\n", pt->entries[idx]);
        if (pt->entries[idx] == NULL) {
            if (!create) return NULL;
            pt->entries[idx] = create_pt();
        }
        printf("%p\n", pt->entries[idx]);
        pt = pt->entries[idx];
        printf(":)\n");
    }
    printf("get_pt fin\n");
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
    printf("Mapping %p\n", vaddr);
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
        printf("FFFFFFFFFFF\n");
        pte_t *pte = get_pte(as, vaddr, true);
        printf("HHHHHHHHHHHHHHHHHHHH\n");
        if (pte == NULL) err = seL4_NotEnoughMemory;
        else {
            pte->cap = frame_cap;
        }
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
   printf("MMMMMMMMMMMMMMMMMM %ld\n", get_vaddr_level_idx(vaddr, 0));
   pte_t **pte = (pte_t **) &(pdt->entries[get_vaddr_level_idx(vaddr, 0)]);
   printf("BBBBBBBBBBBBBB\n");
   if (!create || *pte != NULL) return *pte;
   *pte = malloc(sizeof(pte_t));
   printf("GGGGGGGG\n");
   return *pte;
}

seL4_CPtr alloc_map_frame(addrspace_t *as, cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr,
                    seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs) {
    
    frame_ref_t frame = alloc_frame();
    if (frame == NULL_FRAME) {
        ZF_LOGE("Couldn't allocate additional stack frame");
        return seL4_CapNull;
    }

    /* allocate a slot to duplicate the stack frame cap so we can map it into the application */
    seL4_CPtr frame_cptr = cspace_alloc_slot(cspace);
    if (frame_cptr == seL4_CapNull) {
        free_frame(frame);
        ZF_LOGE("Failed to alloc slot for stack extra stack frame");
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
    if (pte) {
        /* unmap our pte */
        seL4_Error err = seL4_ARM_Page_Unmap(pte->cap);
        assert(err == seL4_NoError);

        /* delete the frame cap */
        err = cspace_delete(cspace, pte->cap);
        assert(err == seL4_NoError);

        /* mark the slot as free */
        cspace_free_slot(cspace, pte->cap);

        /* Free allocated */
        ut_free(pte->ut);
    }
}
