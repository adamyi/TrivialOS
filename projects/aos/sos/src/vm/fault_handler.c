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

static void clean_up(cspace_t *cspace, seL4_CPtr reply, ut_t *reply_ut, bool sendreply) {
    if (sendreply) seL4_Send(reply, seL4_MessageInfo_new(0, 0, 0, 0));
    cspace_delete(cspace, reply);
    cspace_free_slot(cspace, reply);
    ut_free(reply_ut);
}

struct vm_fault_handler_args {
    cspace_t *cspace;
    void *vaddr;
    seL4_Word type;
    process_t *curr;
    seL4_CPtr reply;
    ut_t *reply_ut;
    coro_t coro;
};

bool ensure_mapping(cspace_t *cspace, void *vaddr, process_t *proc, addrspace_t *as, coro_t coro, bool *kill) {
    region_t *region;

    /* check if it's a stack extension first */
    assert(as->stack->prev != NULL);
    if (VEND(as->stack->prev) <= vaddr && vaddr < as->stack->vbase) {
        as->stack->memsize += as->stack->vbase - (vaddr_t) vaddr;
        as->stack->vbase = vaddr; // it's already aligned :)
        region = as->stack; // optimization: we know it's the stack so we don't need to call get_region. O(1) instead of O(n)
    } else {
        printf("%p\n", vaddr);
        region = get_region(as->regions, (vaddr_t) vaddr);
        if (region == NULL) {
            region = as->regions;
            while (region != NULL) {
                ZF_LOGE("%p - %p", region->vbase, region->vbase + region->memsize);
                region = region->next;
            }
            ZF_LOGE("VM Fault ...  did you overflow your buffer again?!?");
            *kill = true;
            return false;
        }
    }

    vaddr = PAGE_ALIGN_4K((vaddr_t) vaddr);

    pte_t *pte = get_pte(as, (vaddr_t) vaddr, false, NULL);
    printf("%p\n", pte);

    if (pte == NULL) {
        /* Alloc frame */
        seL4_Error err = alloc_map_frame(as, cspace, (vaddr_t) vaddr, region->rights, region->attrs, NULL, coro);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to map new frame");
            return false;
        }
    } else {
        seL4_Error err;
        switch (pte->type) {
            case IN_MEM:
            err = sos_map_frame(as, cspace, pte->frame, (vaddr_t) vaddr, region->rights, region->attrs, NULL, coro);
            if (err != seL4_NoError) {
                ZF_LOGE("Failed to map old pte frame");
                return false;
            }
            break;
            case PAGING_OUT:
            proc->paging_coro = coro;
            pte->frame = currproc->pid;
            printf("paging out coro: %p\n", coro);
            yield(NULL);
            proc->paging_coro = NULL;
            // i think we can directly fallthrough to PAGED_OUT case here
            // but to be on the safe side, we check everything again :)
            return ensure_mapping(cspace, vaddr, proc, as, coro, kill);
            case PAGED_OUT:;
            size_t pfidx = pte->frame;
            seL4_Error err = alloc_map_frame(as, cspace, (vaddr_t) vaddr, region->rights, region->attrs, NULL, coro);
            if (err != seL4_NoError) {
                ZF_LOGE("Failed to map new frame");
                return false;
            } else {
                err = page_in(pte->frame, pfidx, coro);
                if (err != seL4_NoError) {
                    ZF_LOGE("Failed to pagein");
                    return false;
                }
                printf("frame %d pfidx %d\n", pte->frame, pfidx);
                pte->type = IN_MEM;
                set_frame_pte(pte->frame, pte);
            }
            break;
            case DEVICE:
            err = app_map_device(cspace, as, vaddr, pte, coro);
            if (err != seL4_NoError) {
                ZF_LOGE("app_map_device failed");
                return false;
            }
            break;
            case SHARED_VM:
            // TODO
            break;
        }
    }
    return true;
}

void *_handle_vm_fault_impl(void *args) {
    struct vm_fault_handler_args *vmargs = args;
    cspace_t *cspace = vmargs->cspace;
    void *vaddr = vmargs->vaddr;
    seL4_Word type = vmargs->type;
    process_t *curr = vmargs->curr;
    seL4_CPtr reply = vmargs->reply;
    ut_t *reply_ut = vmargs->reply_ut;
    coro_t coro = vmargs->coro;
    bool kill = false;

    // printf("tttttttttttt VM Fault Type %lx\n", type);
    /* Check permisison fault on page */
    if (is_perm_fault(type)) {
        ZF_LOGE("Permission fault on page");
        kill = true;
    } else {
        ensure_mapping(cspace, vaddr, curr, curr->addrspace, coro, &kill);
    }

    if (kill || curr->state == PROC_TO_BE_KILLED) {
        clean_up(cspace, reply, reply_ut, false);
        kill_process(curr, coro);
    } else {
        clean_up(cspace, reply, reply_ut, true);
        curr->state = PROC_RUNNING;
    }

    return NULL;
}

void handle_vm_fault(cspace_t *cspace, void *vaddr, seL4_Word type, process_t *curr, seL4_CPtr reply, ut_t *reply_ut) {
    curr->state = PROC_BLOCKED;
    coro_t c = coroutine(_handle_vm_fault_impl);
    struct vm_fault_handler_args args = {
        .cspace = cspace,
        .vaddr = vaddr,
        .type = type,
        .curr = curr,
        .reply = reply,
        .reply_ut = reply_ut,
        .coro = c
    };
    resume(c, &args);
}
