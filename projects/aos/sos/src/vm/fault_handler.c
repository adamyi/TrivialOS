#include "fault_handler.h"

// this is from the CPU doc armv8 
// ISS encoding from Data Abort
// ESR_L1 (page 2460/6666)
static inline bool is_read_fault(seL4_Word fsr) {
    return (fsr & 0b1000000) == 0;
}

// same doc
static inline bool is_perm_fault(seL4_Word fsr) {
    return (fsr & 0b01111) == 0b01111;
}

static inline region_t *get_region(region_t *rg, vaddr_t vaddr) {
    while(rg != NULL) {
        if (rg != NULL && rg->vbase <= vaddr && (rg->vbase + rg->memsize) > vaddr) break;
        rg = rg->next;
    }
    return rg;
}

static void clean_up(cspace_t *cspace, seL4_CPtr reply, ut_t *reply_ut) {
    seL4_Send(reply, seL4_MessageInfo_new(0, 0, 0, 0));
    cspace_delete(cspace, reply);
    cspace_free_slot(cspace, reply);
    ut_free(reply_ut);
}

void handle_vm_fault(cspace_t *cspace, void *vaddr, seL4_Word type, process_t *curr, seL4_CPtr reply, ut_t *reply_ut) {
    printf("tttttttttttt VM Fault Type %lx\n", type);
    vaddr = PAGE_ALIGN_4K((vaddr_t) vaddr);
    /* Check permisison fault on page */
    if (is_perm_fault(type)) {
        /* FAULT */
        ZF_LOGF("Permission fault on page");
        return clean_up(cspace, reply, reply_ut);
        // TODO: kill process
    }
    addrspace_t *as = curr->addrspace;
    region_t *region = get_region(as->regions, (vaddr_t) vaddr);
    if (region == NULL) {
        /* FAULT */
        ZF_LOGE("VM Fault ...  did you overflow your buffer again?!?");
        return clean_up(cspace, reply, reply_ut);
    }

    pte_t *pte = get_pte(as, (vaddr_t) vaddr, false);

    if (pte == NULL) {
        /* Alloc frame */
        seL4_CPtr frame_cptr = alloc_map_frame(curr->addrspace, cspace, curr->vspace, (vaddr_t) vaddr, region->rights, region->attrs);
        if (frame_cptr == seL4_CapNull) {
            /* NO MEMORY */
            ZF_LOGE("Failed to map new frame");
        }
    } else {
        seL4_Error err = sos_map_frame(curr->addrspace, cspace, pte->cap, curr->vspace, (vaddr_t) vaddr, region->rights, region->attrs);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to map old pte frame");
        }
    }

    clean_up(cspace, reply, reply_ut);
}
