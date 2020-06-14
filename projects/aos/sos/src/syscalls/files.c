#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sel4runtime.h>
#include "syscall.h"
#include "files.h"

#include "../vfs/vfs.h"
#include "../vfs/uio.h"

IMPLEMENT_SYSCALL(open, 3) {
    // char *pathname = seL4_GetMR(1);
    char *pathname = proc->shared_buffer_vaddr;
    pathname[1023] ='\0';
    printf("%s\n", pathname);
    int flags = seL4_GetMR(2);

    if ((flags & (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_APPEND)) != flags) {
        return return_word(-EINVAL);
    }

    int accmode = flags & O_ACCMODE;
    if (accmode != O_RDONLY && accmode != O_WRONLY && accmode != O_RDWR) {
        return return_word(-EINVAL);
    }

    struct fdesc* fdesc_node = NULL;
    int err = fdesc_open(pathname, flags, 0000, &fdesc_node, me);
    if (err) {
        return return_word(err);
    }

    int fd = fdtable_append(&proc->fdt, fdesc_node, me);
    if (fd < 0) {
        fdesc_destroy(fdesc_node, me);
    }

    return return_word(fd);
}

IMPLEMENT_SYSCALL(close, 1) {
    int fd = seL4_GetMR(0);
    struct fdesc* fdesc_node = NULL;
    int err = fdtable_get(&proc->fdt, fd, &fdesc_node, me);
    if (err) return return_word(err);
    proc->fdt.fds[fd] = NULL;
    fdesc_decrement(fdesc_node, me);
    return return_word(0);
}

IMPLEMENT_SYSCALL(read, 3) {
    int fd = seL4_GetMR(1);
    size_t size = seL4_GetMR(3);
    if (size > PAGE_SIZE_4K) size = PAGE_SIZE_4K;

    fdesc_t *fdesc_node = NULL;
    int err;
    err = fdtable_get(&proc->fdt, fd, &fdesc_node, me);
    if (err) return return_word(err);

    if ((fdesc_node->flag & O_ACCMODE) == O_WRONLY)
        return return_word(-EBADF);

    uio_t myuio;
    uio_kinit(&myuio, (void *)proc->shared_buffer_vaddr, size, 0, UIO_WRITE);
    err = VOP_READ(fdesc_node->vnode, &myuio, me);

    return return_word(err);
}

IMPLEMENT_SYSCALL(write, 3) {
    int fd = seL4_GetMR(1);
    size_t size = seL4_GetMR(3);
    if (size > PAGE_SIZE_4K) size = PAGE_SIZE_4K;

    fdesc_t *fdesc_node = NULL;
    int err;
    err = fdtable_get(&proc->fdt, fd, &fdesc_node, me);
    if (err) return return_word(err);

    if ((fdesc_node->flag & O_ACCMODE) == O_RDONLY)
        return return_word(-EBADF);

    uio_t myuio;
    uio_kinit(&myuio, (void *)proc->shared_buffer_vaddr, size, 0, UIO_READ);
    err = VOP_WRITE(fdesc_node->vnode, &myuio, me);

    return return_word(err);
}
