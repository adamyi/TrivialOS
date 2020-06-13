#include <assert.h>
#include <sel4runtime.h>
#include "syscall.h"
#include "files.h"

#include "../vfs/vfs.h"
#include "../vfs/uio.h"

// TODO: fdt
static vnode_t *vn;

IMPLEMENT_SYSCALL(open, 3) {
    // char *pathname = seL4_GetMR(1);
    char *pathname = proc->shared_buffer_vaddr;
    pathname[1023] ='\0';
    printf("%s\n", pathname);
    int flags = seL4_GetMR(2);
    int ret = vfs_open(pathname, flags, &vn);
    printf("vfs opened\n");
    if (ret != 0) {
        //TODO: error handling
    }
    seL4_SetMR(0, 4);
    return seL4_MessageInfo_new(0, 0, 0, 1);
}

IMPLEMENT_SYSCALL(close, 1) {
    (void) proc;
    return seL4_MessageInfo_new(0, 0, 0, 0);
}

IMPLEMENT_SYSCALL(read, 3) {
    size_t size = seL4_GetMR(3);
    if (size > PAGE_SIZE_4K) size = PAGE_SIZE_4K;
    uio_t uio;
    uio_kinit(&uio, (void *)proc->shared_buffer_vaddr, size, 0, UIO_WRITE);
    int ret = VOP_READ(vn, &uio);
    seL4_SetMR(0, ret);
    return seL4_MessageInfo_new(0, 0, 0, 1);
}

IMPLEMENT_SYSCALL(write, 3) {
    size_t size = seL4_GetMR(3);
    if (size > PAGE_SIZE_4K) size = PAGE_SIZE_4K;
    uio_t uio;
    uio_kinit(&uio, (void *)proc->shared_buffer_vaddr, size, 0, UIO_READ);
    int ret = VOP_WRITE(vn, &uio);
    seL4_SetMR(0, ret);
    return seL4_MessageInfo_new(0, 0, 0, 1);
}
