#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>

#include "utils/list.h"
#include "uio.h"
#include "../coroutine/picoro.h"

#define __VOP(vn, sym) (vnode_check(vn, #sym), (vn)->ops->vop_##sym)
#define VOP_OPEN(vn, filepath, flags, ret, me)   (__VOP(vn, open)(vn, filepath, flags, ret, me))
#define VOP_RECLAIM(vn, me)                 (__VOP(vn, reclaim)(vn, me))
#define VOP_READ(vn, uio, me)               (__VOP(vn, read)(vn, uio, me))
#define VOP_WRITE(vn, uio, me)              (__VOP(vn, write)(vn, uio, me))

#define VOP_INCREF(vn)          vnode_incref(vn)
#define VOP_DECREF(vn)          vnode_decref(vn)

typedef struct vnode vnode_t;

typedef struct vnode_ops {
    int (*vop_open)(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret, coro_t me);
    int (*vop_read)(vnode_t *file, uio_t *uio, coro_t me);
    int (*vop_write)(vnode_t *file, uio_t *uio, coro_t me);
    int (*vop_reclaim)(vnode_t *vnode, coro_t me);
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
vnode_t *lookup_device(char *device);

int vnode_init(vnode_t *vn, const vnode_ops_t *ops, void *data);
void vnode_check(vnode_t *, const char *op);
void vnode_cleanup(vnode_t *vn);
void vnode_incref(vnode_t *vn);
void vnode_decref(vnode_t *vn);
vnode_t *vfs_lookup(char *pathname);
int vfs_open(char *pathname, int flags, vnode_t **res, coro_t me);
int vfs_close(vnode_t *vn, coro_t me);
