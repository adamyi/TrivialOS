#include "uio.h"
#include "../vm/pagetable.h"

int uio_kinit(uio_t *uio, void *data, size_t size, size_t offset, enum uio_rw rw) {
    memset(uio, 0, sizeof(uio_t));
    uio->rw = rw;
    uio->iovec.base = data;
    uio->iovec.len = size;
    uio->offset = offset;
    uio->segflag = UIO_SYSSPACE;
    return 0;
}

int uio_uinit(uio_t *uio, vaddr_t data, size_t size, size_t offset, enum uio_rw rw, cspace_t *cspace, process_t *proc, addrspace_t *as, coro_t coro) {
    memset(uio, 0, sizeof(uio_t));
    uio->rw = rw;
    region_t *r = get_region_with_possible_stack_extension(as, data);
    if (!r) {
        ZF_LOGE("invalid region");
        return 1;
    }
    if ((uio->rw == UIO_WRITE && !(seL4_CapRights_get_capAllowWrite(r->rights))) ||
        (uio->rw == UIO_READ && !(seL4_CapRights_get_capAllowRead(r->rights)))) {
        ZF_LOGE("permission denied");
        return 1;
    }
    uio->iovec.base = map_vaddr_to_sos(cspace, as, proc, data, &(uio->pte), &(uio->iovec.len), coro);
    if (uio->iovec.base == NULL) return 1;
    if (size < uio->iovec.len) uio->iovec.len = size;
    uio->offset = offset;
    uio->segflag = UIO_USERSPACE;
    // printf("uio_uinit %p\n", uio->iovec.base);
    pin_frame(uio->pte.frame);
    return 0;
}

void uio_destroy(uio_t *uio, cspace_t *cspace) {
    switch (uio->segflag) {
        case UIO_USERSPACE:
        if (uio->pte.inuse) {
            if (uio->rw == UIO_WRITE) {
                flush_frame(uio->pte.frame);
                if (uio->pte.mapped) {
                    seL4_ARM_Page_Invalidate_Data(uio->pte.cap, 0, PAGE_SIZE_4K);
                    seL4_ARM_Page_Unify_Instruction(uio->pte.cap, 0, PAGE_SIZE_4K);
                }
            }
            unmap_vaddr_from_sos(cspace, uio->pte);
            unpin_frame(uio->pte.frame);
        }
    }
}
