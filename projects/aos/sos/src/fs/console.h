#pragma once

#include "../vfs/vfs.h"
#include "../vfs/uio.h"

int console_init();
int console_open(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret);
int console_write(struct vnode *file, struct uio *uio);
int console_read(vnode_t *file, struct uio *uio);
int console_write(vnode_t *file, struct uio *uio);
int console_reclaim(vnode_t *vnode);
