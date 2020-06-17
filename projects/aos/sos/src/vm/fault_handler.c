#include "fault_handler.h"

static inline region_t *get_region(region_t *rg, vaddr_t vaddr) {
    while(rg != NULL) {
        if (rg != NULL && rg->vbase <= vaddr && (rg->vbase + rg->memsize) > vaddr) break;
        rg = rg->next;
    }
    return rg;
}

void handle_vm_fault(cspace_t *cspace, void *vaddr, seL4_Word type, process_t *curr, seL4_CPtr reply, ut_t *reply_ut) {
    //
    addrspace_t *as = curr->addrspace;
    pte_t *pte = get_pte(as, (vaddr_t) vaddr, false);
    if (pte == NULL) {
        region_t *region = get_region(as->regions, (vaddr_t) vaddr);
        if (region == NULL) {
            /* FAULT */
            return;
        }

        /* map frame */
        frame_ref_t frame_ref = alloc_frame();
        seL4_ARM_Page frame_cap = frame_page(frame_ref);
        seL4_Error err = sos_map_frame(as, cspace, frame_cap, curr->vspace, vaddr, region->rights, region->attrs);
        if (err != seL4_NoError) {
            /* NO MEMORY */
        }
    }
    seL4_Send(reply, seL4_MessageInfo_new(0, 0, 0, 0));
    cspace_delete(cspace, reply);
    cspace_free_slot(cspace, reply);
    ut_free(reply_ut);
}
