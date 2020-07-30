#pragma once

#include "../vfs/vfs.h"
#include "../vfs/uio.h"
#include "../coroutine/picoro.h"

int console_init();
int console_open(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret, coro_t me);
int console_read(vnode_t *file, struct uio *uio, coro_t me);
int console_write(vnode_t *file, struct uio *uio, coro_t me);
int console_close(vnode_t *vnode, coro_t me);
int console_stat(vnode_t *vnode, char *pathname, sos_stat_t *stat, coro_t me);
