#pragma once

#include <stdlib.h>
#include <sys/types.h>
#include <cspace/cspace.h>
#include "../vm/addrspace.h"
#include "../coroutine/picoro.h"
#include "../vm/pagetable.h"

enum uio_seg {
    UIO_USERSPACE,
    UIO_SYSSPACE,
};

enum uio_rw {
    UIO_READ,
    UIO_WRITE,
};

typedef struct io_vec {
    void *base;
    size_t len;
} iovec_t;

typedef struct uio {
    iovec_t iovec;
    off_t offset;
    enum uio_seg segflag;
    enum uio_rw rw;
    pte_t pte;
} uio_t;

int uio_kinit(uio_t *uio, void *data, size_t size, size_t offset, enum uio_rw rw);
int uio_uinit(uio_t *uio, vaddr_t data, size_t size, size_t offset, enum uio_rw rw, cspace_t *cspace, process_t *proc, addrspace_t *as, coro_t coro);
void uio_destroy(uio_t *uio, cspace_t *cspace);
