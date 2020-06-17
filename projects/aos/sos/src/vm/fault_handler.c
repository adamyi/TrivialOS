#include "fault_handler.h"

#define VEND(x) ((x->vbase)+(x->memsize))

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

static inline region_t *get_last_region(region_t *rg, vaddr_t vaddr) {
    region_t *lst = NULL;
    while(rg != NULL && rg->vbase <= vaddr) {
        lst = rg;
        rg = rg->next;
    }
    return lst;
}

static inline region_t *get_region(region_t *rg, vaddr_t vaddr) {
    region_t *ret = get_last_region(rg, vaddr);
    if (ret != NULL && VEND(ret) > vaddr) return ret;
    return NULL;
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
        ZF_LOGF("Permission fault on page");
        // TODO: kill process
        return;
    }
    addrspace_t *as = curr->addrspace;

    region_t *region;

    /* check if it's a stack extension first */
    assert(as->stack->prev != NULL);
    if (VEND(as->stack->prev) <= vaddr && vaddr < as->stack->vbase) {
        as->stack->memsize += as->stack->vbase - (vaddr_t) vaddr;
        as->stack->vbase = vaddr; // it's already aligned :)
        region = as->stack; // optimization: we know it's the stack so we don't need to call get_region. O(1) instead of O(n)
    } else {
        region = get_region(as->regions, (vaddr_t) vaddr);
        if (region == NULL) {
            ZF_LOGF("VM Fault ...  did you overflow your buffer again?!?");
            // TODO: kill process
            return;
        }
    }


    pte_t *pte = get_pte(as, (vaddr_t) vaddr, false);

    if (pte == NULL || pte->cap == seL4_CapNull) {
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
