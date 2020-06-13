#include "console.h"
#include "../vfs/vfs.h"
#include "utils/zf_log.h"
#include <serial/serial.h>

vnode_ops_t console_ops = {
                .vop_open    = console_open, 
                .vop_read    = console_read,
                .vop_write   = console_write,
                .vop_reclaim = console_reclaim
};

vnode_ops_t root_console_ops = {
                .vop_open    = console_open, 
                .vop_read    = NULL,
                .vop_write   = NULL,
                .vop_reclaim = NULL };

static struct serial *handle;
static char kbuff[PAGE_SIZE_4K];
static int kbuff_idx = 0;

static void console_read_handler(struct serial *serial, char c) {
    (void) serial;
    if (kbuff_idx >= PAGE_SIZE_4K) {
        ZF_LOGE("kbuff is full");
        return;
    }
    kbuff[kbuff_idx++] = c;
}

int console_init() {
    handle = serial_init();
    vnode_t *vnode = malloc(sizeof(vnode_t));
    if (vnode == NULL) {
        ZF_LOGE("Error making vnode");
        return -1;
    }
    vnode_init(vnode, &console_ops, handle);
    serial_register_handler(handle, console_read_handler); 
    register_device("console", vnode);

    return 0;
}

int console_open(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret) {
    (void) pathname;
    (void) flags_from_open;
    if (handle == NULL) {
        ZF_LOGE("Error initializing serial");
        return -1;
    }
    vnode_t *vnode = malloc(sizeof(vnode_t));
    if (vnode == NULL) {
        ZF_LOGE("Error making vnode");
        return -1;
    }
    vnode_init(vnode, &console_ops, handle);
    *ret = vnode;
    return 0;
}

int console_read(vnode_t *file, struct uio *uio) {
    (void) file;
    uio->iovec.len = idx;
    memcpy(uio->iovec.base, kbuff, idx);
    idx = 0;
    return uio->iovec.len;
}

int console_write(vnode_t *file, struct uio *uio) {
    return serial_send(file->data, uio->iovec.base, uio->iovec.len);
}

int console_reclaim(vnode_t *vnode) {
    (void) vnode;
    return 0;
}
