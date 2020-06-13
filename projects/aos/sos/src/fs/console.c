#include "console.h"
#include "../vfs/vfs.h"
#include "utils/zf_log.h"
#include <serial/serial.h>
#include "utils/queue.h"
#include "utils/rollingarray.h"
#include "utils/page.h"
#include "../coroutine/picoro.h"

vnode_ops_t console_ops = {
                .vop_open    = NULL, 
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
static rollingarray_t *kbuff;
static queue_t newline_queue;
static coro_t blocked_reader = NULL;

static void console_read_handler(struct serial *serial, char c) {
    (void) serial;
    if (!rollingarray_add_item(kbuff, c)) {
        ZF_LOGE("kbuff is full");
    }
    if (c == '\n') {
        //TODO: TEST THIS AS WELL
        queue_enqueue(&newline_queue, (void *) ra_ind2idx(kbuff, kbuff->size));
        // check if any read is blocked
        if (blocked_reader) {
            coro_t c = blocked_reader;
            blocked_reader = NULL;
            resume(c, NULL);
        }
    }
}

int console_init() {
    kbuff = new_rollingarray(PAGE_SIZE_4K);
    if (kbuff == NULL) {
        ZF_LOGE("Error can't initialize kbuff");
        return -1;
    }
    queue_init(&newline_queue);
    handle = serial_init();
    vnode_t *vnode = malloc(sizeof(vnode_t));
    if (vnode == NULL) {
        ZF_LOGE("Error making vnode");
        return -1;
    }
    vnode_init(vnode, &root_console_ops, handle);
    serial_register_handler(handle, console_read_handler); 
    register_device("console", vnode);

    return 0;
}

int console_open(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret, coro_t me) {
    printf("CONSOLE OPEN LMAO\n");
    (void) pathname;
    (void) flags_from_open;
    (void) me;
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

int console_read(vnode_t *file, struct uio *uio, coro_t me) {
    (void) file;
    size_t newline_idx = (size_t) queue_peek(&newline_queue);
    if (newline_idx == 0) { //NULL
        assert (blocked_reader == NULL);
        blocked_reader = me;
        printf("blocking\n");
        yield(NULL);
        printf("resumed\n");
        newline_idx = (size_t) queue_peek(&newline_queue);
    }
    // TODO: TEST THIS CODE!!!
    newline_idx = ra_idx2ind(kbuff, newline_idx);
    printf("nlidx %u uio len %u\n", newline_idx, uio->iovec.len);
    if (newline_idx <= uio->iovec.len) {
        queue_dequeue(&newline_queue);
        uio->iovec.len = newline_idx;
    }
    rollingarray_to_array(kbuff, (char *)uio->iovec.base, false, uio->iovec.len);
    kbuff->start += uio->iovec.len;
    if (kbuff->start >= kbuff->capacity) kbuff->start -= kbuff->capacity;
    kbuff->size -= uio->iovec.len;
    printf("remaining size %u\n", kbuff->size);
    ((char *)uio->iovec.base)[uio->iovec.len] = '\0';
    printf("read %s\n", (char *)uio->iovec.base);
    return uio->iovec.len;
}

int console_write(vnode_t *file, struct uio *uio, coro_t me) {
    (void) me;
    return serial_send(file->data, uio->iovec.base, uio->iovec.len);
}

int console_reclaim(vnode_t *vnode, coro_t me) {
    (void) vnode;
    (void) me;
    return 0;
}
