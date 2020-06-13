#include "pagetable.h"
#include "addrspace.h"

#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>

static seL4_Word pt_level2type[4] = {seL4_ARM_PageTableObject, seL4_ARM_PageDirectoryObject, seL4_ARM_PageUpperDirectoryObject, seL4_ARM_PageGlobalDirectoryObject};
//LIBSEL4_INLINE seL4_Error
// seL4_ARM_PageDirectory_Map(seL4_ARM_PageDirectory _service, seL4_CPtr vspace, seL4_Word vaddr, seL4_ARM_VMAttributes attr)
typedef seL4_Error (*pt_mapper)(seL4_ARM_PageDirectory, seL4_CPtr, seL4_Word, seL4_ARM_VMAttributes);
static pt_mapper pt_level2mapper[3] = {seL4_ARM_PageTable_Map, seL4_ARM_PageDirectory_Map, seL4_ARM_PageUpperDirectory_Map};

static inline seL4_Word get_vaddr_level_idx(vaddr_t addr, int level) {
    return addr & (PAGE_TABLE_LEVEL_MASK << level_to_offset[level]);
}

static page_table_t *create_pt_of_level(int level) {
    page_table_t *pt = malloc(sizeof(page_table_t));
    if (pt == NULL) {
        ZF_LOGE("cannot malloc rip");
        return NULL;
    }
    memset(pt->entries, 0, sizeof(pt->entries));
    pt->cap = seL4_CapNull;
    return pt;
}

static page_table_t *get_pt_level(addrspace_t *as, vaddr_t addr, int level) {
    page_table_t *pt = as->pagetable;
    for (int i = PAGE_TABLE_LEVELS - 2; i >= level; i--) {
        seL4_Word idx = get_vaddr_level_idx(addr, i);
        if (pt->entries[idx] == NULL) {
            pt->entries[idx] = create_pt_of_level(i);
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
            page_table_t *pt = get_pt_level(as, vaddr, level);
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
    return err;
}


seL4_Error sos_map_frame(addrspace_t *as, cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr)
{
    return map_frame_impl(as, cspace, frame_cap, vspace, vaddr, rights, attr, NULL, NULL);
}
