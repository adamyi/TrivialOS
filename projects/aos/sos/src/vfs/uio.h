#pragma once

#include <stdlib.h>
#include <sys/types.h>

enum uio_seg {
    UIO_USERSPACE,
    UIO_SYSSPACE,
};

enum uio_rw {
    UIO_READ,
    UIO_WRITE,
};

typedef struct iovec {
    void *base;
    size_t len;
} iovec_t;

typedef struct uio {
    iovec_t iovec;
    off_t offset;
    enum uio_seg segflag;
    enum uio_rw rw;
} uio_t;

void uio_kinit(uio_t *uio, void *data, size_t size, size_t offset, enum uio_rw rw);
