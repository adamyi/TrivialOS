#include <errno.h>
#include <fcntl.h>
#include "vfs.h"
#include "file.h"
#include "../coroutine/picoro.h"

int fdesc_open(char *filename, int flags, mode_t mode, fdesc_t** result, coro_t me) {
    (void) mode;
    /* Get file stat */
    sos_stat_t file_stat;
    int err = vfs_stat(filename, &file_stat, me);
    if ((err == -1 && (flags & O_ACCMODE) == O_RDONLY) || // if file doesn't exist and we try to read
        (err != -1 && (flags & O_ACCMODE) == O_RDONLY && !(file_stat.st_fmode & FM_READ)) || // read from write-only file
        (err != -1 && (flags & O_ACCMODE) == O_WRONLY && !(file_stat.st_fmode & FM_WRITE))|| // write to read-only file
        (err != -1 && (flags & O_ACCMODE) == O_RDWR && (file_stat.st_fmode & FM_RDWR) != FM_RDWR)) // read-write
        return -1;

    /* Open file */
    struct vnode *result_vnode = NULL;
    // int err = vfs_open(filename, flags, mode, &result_vnode);
    err = vfs_open(filename, flags, &result_vnode, me);
    if (err) return err;

    // we do it here before mallocing fdesc to make error handling easier
    int offset = 0;
    // it seems that O_APPEND is the responsibility of VFS, which doesn't
    // implement this... Comment this out for now...
    /* if (flags & O_APPEND) {
        struct stat info;
        err = VOP_STAT(result_vnode, &info);
        if (err) {
            vfs_close(result_vnode);
            return err;
        }
        offset = info.st_size;
    } */

    fdesc_t* tmp_fdesc;
    tmp_fdesc = malloc(sizeof(*tmp_fdesc));
    if (tmp_fdesc == NULL) {
        vfs_close(result_vnode, me);
        return -ENOMEM;
    }
    tmp_fdesc->vnode = result_vnode;
    tmp_fdesc->flag = flags;
    tmp_fdesc->refcount = 1;
    tmp_fdesc->offset = offset;

    *result = tmp_fdesc;
    return 0;
}

void fdesc_increment(fdesc_t* fd, coro_t me) {
    (void) me;
    fd->refcount++;
}

void fdesc_decrement(fdesc_t* fd, coro_t me) {
    fd->refcount--;
    if (fd->refcount == 0) {
      fdesc_destroy(fd, me);
    }
}

void fdesc_destroy(fdesc_t* fd, coro_t me) {
 if (fd->vnode != NULL) vfs_close(fd->vnode, me);
 free(fd);
}

void fdtable_init(fdtable_t *fdt) {
    for (int i = 0; i < OPEN_MAX; ++i) {
        fdt->fds[i] = NULL;
    }
}

void fdtable_destroy(fdtable_t *ft, coro_t me) {
    if (ft == NULL) return;
    for (int i = 0; i < OPEN_MAX; ++i) {
        if (ft->fds[i] != NULL) {
            fdesc_t* fd = ft->fds[i];
            ft->fds[i] = NULL;
            fdesc_decrement(fd, me);
        }
    }
}

int fdtable_get(fdtable_t *ft, int fd, fdesc_t **result, coro_t me) {
    (void) me;
    if (ft == NULL) return -EBADF;
    if (!IS_VALID_FD(fd)) return -EBADF;
    if (ft->fds[fd] == NULL) return -EBADF;
    *result = ft->fds[fd];
    return 0;
}

int fdtable_append(fdtable_t *ft, fdesc_t *file, coro_t me) {
    if (ft == NULL) return -EFAULT;
    for (int i = MIN_FD; i < OPEN_MAX; i++) {
        if (ft->fds[i] == NULL) {
            fdtable_put(ft, i, file, me);
            return i;
        }
    }
    return -EMFILE;
}

int fdtable_put(fdtable_t *ft, int fd, fdesc_t *file, coro_t me) {
    if (ft == NULL) return -EBADF;
    if (!IS_VALID_FD(fd)) return -EBADF;
    if (ft->fds[fd] != NULL) {
        fdesc_t* tbdfd = ft->fds[fd];
        ft->fds[fd] = NULL;
        fdesc_decrement(tbdfd, me);
    }
    ft->fds[fd] = file;
    return 0;
}
