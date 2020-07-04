#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>

#include "utils/list.h"
#include "uio.h"
#include "../coroutine/picoro.h"

#define __VOP(vn, sym) (vnode_check(vn, #sym), (vn)->ops->vop_##sym)
#define VOP_OPEN(vn, filepath, flags, ret, me)    (__VOP(vn, open)(vn, filepath, flags, ret, me))
#define VOP_CLOSE(vn, me)                         (__VOP(vn, close)(vn, me))
#define VOP_READ(vn, uio, me)                     (__VOP(vn, read)(vn, uio, me))
#define VOP_WRITE(vn, uio, me)                    (__VOP(vn, write)(vn, uio, me))
#define VOP_PREAD(vn, uio, me)                     (__VOP(vn, pread)(vn, uio, me))
#define VOP_PWRITE(vn, uio, me)                    (__VOP(vn, pwrite)(vn, uio, me))
#define VOP_STAT(vn, filepath, stat, me)          (__VOP(vn, stat)(vn, filepath, stat, me))
#define VOP_GET_DIRENT(vn, pos, name, nbyte, me)  (__VOP(vn, get_dirent)(vn, pos, name, nbyte, me))

#define VOP_INCREF(vn)          vnode_incref(vn)
#define VOP_DECREF(vn)          vnode_decref(vn)

// definition from sos.h

/* file modes */
#define FM_EXEC  1
#define FM_WRITE 2
#define FM_READ  4
#define FM_RDWR ((FM_READ) | (FM_WRITE))
typedef int fmode_t;

/* stat file types */
#define ST_FILE    1    /* plain file */
#define ST_SPECIAL 2    /* special (console) file */
typedef int st_type_t;

typedef struct {
    st_type_t st_type;    /* file type */
    fmode_t   st_fmode;   /* access mode */
    unsigned  st_size;    /* file size in bytes */
    long      st_ctime;   /* Unix file creation time (ms) */
    long      st_atime;   /* Unix file last access (open) time (ms) */
} sos_stat_t;

typedef struct vnode vnode_t;

typedef struct vnode_ops {
    int (*vop_open)(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret, coro_t me);
    int (*vop_read)(vnode_t *file, uio_t *uio, coro_t me);
    int (*vop_write)(vnode_t *file, uio_t *uio, coro_t me);
    int (*vop_pread)(vnode_t *file, uio_t *uio, coro_t me);
    int (*vop_pwrite)(vnode_t *file, uio_t *uio, coro_t me);
    int (*vop_close)(vnode_t *vnode, coro_t me);
    int (*vop_stat)(vnode_t *vnode, char *pathname, sos_stat_t *stat, coro_t me);
    int (*vop_get_dirent)(vnode_t *vnode, int pos, char *name, size_t nbyte, coro_t me);
    // add more
} vnode_ops_t;

struct vnode {
    const vnode_ops_t *ops;
    void *data;
    int refcount;
};

typedef struct device_vnode {
    char *name;
    struct vnode *vn;
} device_vnode_t;

void register_device(char *device, vnode_t *vn);
void register_rootfs(vnode_t *vn);
vnode_t *lookup_device(char *device);

int vnode_init(vnode_t *vn, const vnode_ops_t *ops, void *data);
void vnode_check(vnode_t *, const char *op);
void vnode_cleanup(vnode_t *vn);
void vnode_incref(vnode_t *vn);
void vnode_decref(vnode_t *vn);
vnode_t *vfs_lookup(char *pathname);
int vfs_open(char *pathname, int flags, vnode_t **res, coro_t me);
int vfs_close(vnode_t *vn, coro_t me);
int vfs_getdirent(int pos, char *name, size_t nbyte, coro_t me);
int vfs_stat(char *name, sos_stat_t *stat, coro_t me);
