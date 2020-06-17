#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sel4runtime.h>
#include "syscall.h"
#include "files.h"

#include "../vfs/vfs.h"
#include "../vfs/uio.h"

IMPLEMENT_SYSCALL(open, 3) {
    char pathname[PATH_MAX + 1];

    vaddr_t pathname_ptr = seL4_GetMR(1);
    int pathlen = seL4_GetMR(2);
    if (pathlen > PATH_MAX) {
        return return_word(-ENAMETOOLONG);
    }
    int flags = seL4_GetMR(3);

    int err = copy_in(cspace, proc->addrspace, pathname_ptr, pathlen, pathname);
    //int err = copy_in(cspace, proc->vspace, proc->addrspace, pathname_ptr, pathlen, pathname);
    if (err) return return_word(-EINVAL);
    pathname[pathlen + 1] = '\0';

    printf("%s\n", pathname);

    if ((flags & (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_APPEND)) != flags) {
        return return_word(-EINVAL);
    }

    int accmode = flags & O_ACCMODE;
    if (accmode != O_RDONLY && accmode != O_WRONLY && accmode != O_RDWR) {
        return return_word(-EINVAL);
    }

    struct fdesc* fdesc_node = NULL;
    err = fdesc_open(pathname, flags, 0000, &fdesc_node, me);
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

static inline seL4_MessageInfo_t read_write(SYSCALL_PARAMS, int is_write) {
    int fd = seL4_GetMR(1);
    vaddr_t vaddr = seL4_GetMR(2);
    size_t size = seL4_GetMR(3);

    fdesc_t *fdesc_node = NULL;
    int err;
    err = fdtable_get(&proc->fdt, fd, &fdesc_node, me);
    if (err) return return_word(err);

    if ((fdesc_node->flag & O_ACCMODE) == O_WRONLY)
        return return_word(-EBADF);

    printf("va %p\n", vaddr);
    printf("s %d\n", size);

    size_t remaining = size;

    bool cont = true;

    while (remaining > 0 && cont) {
        uio_t myuio;
        size_t rem = remaining < PAGE_SIZE_4K ? remaining : PAGE_SIZE_4K;
        int nb;
        if (is_write) {
            if (uio_uinit(&myuio, vaddr, rem, 0, UIO_READ, cspace, proc->addrspace)) return return_word(-1);
            if (myuio.iovec.len < rem) rem = myuio.iovec.len;
            nb = VOP_WRITE(fdesc_node->vnode, &myuio, me);
        } else {
            if (uio_uinit(&myuio, vaddr, rem, 0, UIO_WRITE, cspace, proc->addrspace)) return return_word(-1);
            if (myuio.iovec.len < rem) rem = myuio.iovec.len;
            nb = VOP_READ(fdesc_node->vnode, &myuio, me);
            printf("%d %d\n", nb, rem);
            if (nb != rem) cont = false;
        }
        
        uio_destroy(&myuio, cspace);
        if (nb < 0) return return_word(nb);
        remaining -= nb;
        printf("read %d bytes from %p, remaining %d\n", nb, vaddr, remaining);
        // uio_destroy doesn't change those
        vaddr += nb;
    }


    printf("rs %d\n", size - remaining);
    return return_word(size - remaining);
}

IMPLEMENT_SYSCALL(read, 3) {
    return read_write(cspace, proc, me, 0);
}

IMPLEMENT_SYSCALL(write, 3) {
    return read_write(cspace, proc, me, 1);
}
