#include "console.h"
#include "../vfs/vfs.h"
#include "utils/zf_log.h"
#include <serial/serial.h>
#include <fcntl.h>
#include "utils/queue.h"
#include "utils/rollingarray.h"
#include "utils/page.h"
#include "../coroutine/picoro.h"
#include <sel4/sel4.h>
#include "../process.h"

#define CONSOLE_BUFFER_SIZE (PAGE_SIZE_4K)

vnode_ops_t console_ops = {
                .vop_open       = NULL, 
                .vop_read       = console_read,
                .vop_write      = console_write,
                .vop_pread      = NULL,
                .vop_pwrite     = NULL,
                .vop_close      = console_close,
                .vop_stat       = NULL,
                .vop_get_dirent = NULL
};

vnode_ops_t root_console_ops = {
                .vop_open       = console_open, 
                .vop_read       = NULL,
                .vop_write      = NULL,
                .vop_pread      = NULL,
                .vop_pwrite     = NULL,
                .vop_close      = NULL,
                .vop_stat       = console_stat,
                .vop_get_dirent = NULL
};

static struct serial *handle;
static rollingarray_t *kbuff;
static queue_t newline_queue;
static coro_t blocked_reader = NULL;

static bool has_reader = false;

static int buffstate = 0; // 0: not full, 1: full without new line, 2: full with new line

static inline bool mode_is_read(seL4_Word flags) {
    seL4_Word accmode = flags & O_ACCMODE;
    return accmode == O_RDONLY || accmode == O_RDWR;
}

static inline bool mode_is_write(seL4_Word flags) {
    seL4_Word accmode = flags & O_ACCMODE;
    return accmode == O_WRONLY || accmode == O_RDWR;
}

static void console_read_handler(struct serial *serial, char c) {
    (void) serial;
    if (!rollingarray_add_item(kbuff, c)) {
        ZF_LOGE("kbuff is full");
        if (buffstate == 0) buffstate = 1;
    } else {
        buffstate = 0;
    }
    if (c == '\n') {
        switch (buffstate) {
            case 1:
            buffstate = 2;
            case 0:
            queue_enqueue(&newline_queue, (void *) ra_ind2idx(kbuff, kbuff->size - 1));
        }
        // check if any read is blocked
        if (blocked_reader) {
            coro_t c = blocked_reader;
            blocked_reader = NULL;
            resume(c, NULL);
        }
    }
}

static void console_kill_hook(void *data) {
    ZF_LOGD("calling console kill hook");
    (void) data;
    coro_t c = blocked_reader;
    blocked_reader = NULL;
    if (c) resume(c, NULL);
}

int console_init() {
    ZF_LOGI("Initializing console...");
    kbuff = new_rollingarray(CONSOLE_BUFFER_SIZE);
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
    (void) object;
    (void) pathname;
    (void) me;
    if (handle == NULL) {
        ZF_LOGE("Error initializing serial");
        return -1;
    }
    if (mode_is_read((seL4_Word) flags_from_open)) {
        if (has_reader) {
            ZF_LOGE("Already have a reader");
            return -1;
        }
        has_reader = true;
    }
    vnode_t *vnode = malloc(sizeof(vnode_t));
    if (vnode == NULL) {
        ZF_LOGE("Error making vnode");
        return -1;
    }
    vnode_init(vnode, &console_ops, (void *) (seL4_Word) flags_from_open);
    *ret = vnode;
    return 0;
}

int console_read(vnode_t *file, struct uio *uio, process_t *proc, coro_t me) {
    if (!mode_is_read((seL4_Word) file->data)) {
        ZF_LOGE("Calling read on non-reader console vnode");
        return -1;
    }
    size_t newline_idx = (size_t) queue_peek(&newline_queue);
    if (newline_idx == 0) { //NULL
        assert (blocked_reader == NULL);
        blocked_reader = me;
        ZF_LOGI("console reader blocking");
        if (proc) proc->kill_hook = console_kill_hook;
        yield(NULL);
        if (proc) proc->kill_hook = NULL;
        ZF_LOGI("console reader resumed");
        newline_idx = (size_t) queue_peek(&newline_queue);
    }
    newline_idx = ra_idx2ind(kbuff, newline_idx) + 1;
    if (newline_idx <= uio->iovec.len) {
        queue_dequeue(&newline_queue);
        uio->iovec.len = newline_idx;
    }
    uio->iovec.len = rollingarray_to_array(kbuff, (char *)uio->iovec.base, false, uio->iovec.len);
    kbuff->start += uio->iovec.len;
    if (kbuff->start >= kbuff->capacity) kbuff->start -= kbuff->capacity;
    kbuff->size -= uio->iovec.len;
    // printf("remaining size %lu\n", kbuff->size);
    // ((char *)uio->iovec.base)[uio->iovec.len] = '\0';
    // printf("read %s\n", (char *)uio->iovec.base);
    return uio->iovec.len;
}

int console_write(vnode_t *file, struct uio *uio, coro_t me) {
    (void) me;
    if (!mode_is_write((seL4_Word) file->data)) {
        ZF_LOGE("Calling write on non-writer console vnode");
        return -1;
    }
    return serial_send(handle, uio->iovec.base, uio->iovec.len);
}

int console_close(vnode_t *vnode, coro_t me) {
    (void) me;
    if (mode_is_read((seL4_Word) vnode->data)) {
        has_reader = false;
    }
    free(vnode);
    return 0;
}

int console_stat(vnode_t *vnode, char *pathname, sos_stat_t *stat, coro_t me) {
    (void) vnode;
    (void) pathname;
    (void) me;
    stat->st_type  = ST_SPECIAL;
    stat->st_fmode = 0666;
    stat->st_size  = 0;
    stat->st_ctime = 0;
    stat->st_atime = 0;
    return 0;
}
