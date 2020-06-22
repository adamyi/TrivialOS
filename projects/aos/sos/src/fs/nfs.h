#pragma once

#include <stdint.h>

#include "../vfs/vfs.h"
#include "../vfs/uio.h"
#include "../coroutine/picoro.h"

#include <nfsc/libnfs.h>

int sos_nfs_init(struct nfs_context *nfs);
int sos_nfs_open(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret, coro_t me);
int sos_nfs_read(vnode_t *file, struct uio *uio, coro_t me);
int sos_nfs_write(vnode_t *file, struct uio *uio, coro_t me);
int sos_nfs_close(vnode_t *vnode, coro_t me);
int sos_nfs_stat(vnode_t *vnode, char *pathname, sos_stat_t *stat, coro_t me);
int sos_nfs_get_dirent(vnode_t *vnode, int pos, char *name, size_t nbyte, coro_t me);
