#include <assert.h>
#include <sel4runtime.h>
#include <clock/clock.h>

#include "syscall.h"
#include "memory.h"

#include "../coroutine/picoro.h"

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
            unalloc_frame(proc->addrspace, &(proc->cspace), curr, me);
        }
        heap->memsize = vaddr - heap->vbase;
    }

    return return_word(heap->vbase + heap->memsize);
}
