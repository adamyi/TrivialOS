#include "uio.h"
#include "../vm/pagetable.h"

int uio_kinit(uio_t *uio, void *data, size_t size, size_t offset, enum uio_rw rw) {
    uio->rw = rw;
    uio->iovec.base = data;
    uio->iovec.len = size;
    uio->offset = offset;
    uio->segflag = UIO_SYSSPACE;
    return 0;
}

int uio_uinit(uio_t *uio, vaddr_t data, size_t size, size_t offset, enum uio_rw rw, cspace_t *cspace, addrspace_t *as, coro_t coro) {
    uio->rw = rw;
    uio->iovec.base = map_vaddr_to_sos(cspace, as, data, &(uio->cptr), &(uio->iovec.len), coro);
    if (size < uio->iovec.len) uio->iovec.len = size;
    uio->offset = offset;
    uio->segflag = UIO_USERSPACE;
    // printf("uio_uinit %p\n", uio->iovec.base);
    return uio->iovec.base == NULL;
}

void uio_destroy(uio_t *uio, cspace_t *cspace) {
    switch (uio->segflag) {
        case UIO_USERSPACE:
        if (uio->cptr != seL4_CapNull) unmap_vaddr_from_sos(cspace, uio->cptr);
    }
}
