#include <aos/debug.h>
#include <string.h>

#include "vfs.h"
#include "utils/list.h"

static list_t *device_list = NULL;
static vnode_t *rootfs = NULL;

void register_device(char *device, vnode_t *vn) {
    if (device_list == NULL) {
        device_list = malloc(sizeof(list_t));
        device_list->head = NULL;
    }
    device_vnode_t *n = malloc(sizeof(device_vnode_t));
    //n->name = malloc(strlen(device) + 1);
    //memcpy(n->name, device, strlen(device) + 1);
    n->name = device;
    n->vn = vn;
    list_append(device_list, n);
}

void register_rootfs(vnode_t *vn) {
    rootfs = vn;
}

vnode_t *lookup_device(char *device) {
    if (device_list == NULL) return NULL;
    struct list_node *curr = device_list->head;
    while (curr != NULL) {
        if (strcmp((char *)(((device_vnode_t *)curr->data)->name), device) == 0)
            return ((device_vnode_t *)curr->data)->vn;
        curr = curr->next;
    }
    return NULL;
}

int vnode_init(vnode_t *vn, const vnode_ops_t *ops, void *data) {
    vn->ops = ops;
    vn->refcount = 1;
    vn->data = data;
    return 0;
}

void vnode_check(vnode_t *vn, const char *opstr) {
    if (vn == NULL) ZF_LOGF("vnode is NULL?!?");
    if (vn->ops == NULL) ZF_LOGF("ops ptr is NULL?!?");
    if (vn->refcount < 0) ZF_LOGF("Refcount is negative? Help!");
    printf("check ok\n");
}

void vnode_cleanup(vnode_t *vn) {
    vn->ops = NULL;
    vn->refcount = 0;
    vn->data = NULL;
}

void vnode_incref(vnode_t *vn) {
    vn->refcount++;
}
void vnode_decref(vnode_t *vn) {
    vn->refcount--;
    if (vn->refcount == 0) {
        // remove vnode
        // VOP_RECLAIM(vn);
    }
}

vnode_t *vfs_lookup(char *pathname) {
    vnode_t *res = lookup_device(pathname);
    if (res) return res;
    return rootfs;
}

int vfs_open(char *pathname, int flags, vnode_t **res, coro_t me) {
    vnode_t *vn = vfs_lookup(pathname);
    if (vn == NULL) return -1;
    printf("VFS_OPEN is trivial\n");
    return VOP_OPEN(vn, pathname, flags, res, me);
}

int vfs_close(vnode_t *vn, coro_t me) {
    return VOP_CLOSE(vn, me);
}

int vfs_getdirent(int pos, char *name, size_t nbyte, coro_t me) {
    return VOP_GET_DIRENT(rootfs, pos, name, nbyte, me);
}

int vfs_stat(char *name, sos_stat_t *stat, coro_t me) {
    vnode_t *vn = vfs_lookup(name);
    return VOP_STAT(vn, name, stat, me);
}
