#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>

#include "utils/list.h"
#include "uio.h"

#define __VOP(vn, sym) (vnode_check(vn, #sym), (vn)->ops->vop_##sym)
#define VOP_OPEN(vn, filepath, flags, ret)   (__VOP(vn, open)(vn, filepath, flags, ret))
#define VOP_RECLAIM(vn)                 (__VOP(vn, reclaim)(vn))
#define VOP_READ(vn, uio)               (__VOP(vn, read)(vn, uio))
#define VOP_WRITE(vn, uio)              (__VOP(vn, write)(vn, uio))

#define VOP_INCREF(vn)          vnode_incref(vn)
#define VOP_DECREF(vn)          vnode_decref(vn)

typedef struct vnode vnode_t;

typedef struct vnode_ops {
    int (*vop_open)(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret);
    int (*vop_read)(vnode_t *file, uio_t *uio);
    int (*vop_write)(vnode_t *file, uio_t *uio);
    int (*vop_reclaim)(vnode_t *vnode);
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
int vfs_open(char *pathname, int flags, vnode_t **res);
int vfs_close(vnode_t *vn);
