#include "uio.h"

void uio_kinit(uio_t *uio, void *data, size_t size, size_t offset, enum uio_rw rw) {
    uio->rw = rw;
    uio->iovec.base = data;
    uio->iovec.len = size;
    uio->offset = offset;
    uio->offset = 0;
}
