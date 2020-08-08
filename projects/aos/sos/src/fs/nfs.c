#include <string.h>
#include "nfs.h"
#include "../vfs/vfs.h"
#include "utils/zf_log.h"
#include <serial/serial.h>
#include <fcntl.h>
#include "../coroutine/picoro.h"
#include <sel4/sel4.h>
#include <nfsc/libnfs.h>

vnode_ops_t nfs_ops = {
                .vop_open       = NULL, 
                .vop_read       = sos_nfs_read,
                .vop_write      = sos_nfs_write,
                .vop_pread      = sos_nfs_pread,
                .vop_pwrite     = sos_nfs_pwrite,
                .vop_close      = sos_nfs_close,
                .vop_stat       = NULL,
                .vop_get_dirent = NULL
};

vnode_ops_t root_nfs_ops = {
                .vop_open       = sos_nfs_open, 
                .vop_read       = NULL,
                .vop_write      = NULL,
                .vop_pread      = NULL,
                .vop_pwrite     = NULL,
                .vop_close      = NULL,
                .vop_stat       = sos_nfs_stat,
                .vop_get_dirent = sos_nfs_get_dirent
};

static struct nfs_context *sos_nfs = NULL;

typedef struct res_cb {
    coro_t coro;
    int status;
    void *data;
} res_cb_t;

int sos_nfs_init(struct nfs_context *nfs) {

    sos_nfs = nfs;
    vnode_t *root = malloc(sizeof(vnode_t));
    if (root == NULL) {
        ZF_LOGE("Error making root vnode");
        return -1;
    }
    vnode_init(root, &root_nfs_ops, NULL);
    register_rootfs(root);

    return 0;
}

static void sos_nfs_cb(int status, UNUSED struct nfs_context *nfs, void *data, void *private_data) {
    res_cb_t *ret = (res_cb_t *) private_data;
    ret->status = status;
    ret->data = data;
    resume(ret->coro, NULL);
}

int sos_nfs_open(vnode_t *object, char *pathname, int flags_from_open, vnode_t **ret, coro_t me) {
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_open_async(sos_nfs, pathname, flags_from_open, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status == 0) {
        vnode_t *vnode = malloc(sizeof(vnode_t));
        if (vnode == NULL) {
            ZF_LOGE("Error making vnode");
            return -1;
        }
        vnode_init(vnode, &nfs_ops, cb_ret.data);
        *ret = vnode;
        return 0;
    }
    ZF_LOGE("Error opening NFS: %s", cb_ret.data);
    return cb_ret.status;
}

int sos_nfs_read(vnode_t *file, struct uio *uio, process_t *proc, coro_t me) {
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_read_async(sos_nfs, (struct nfsfh *) file->data, uio->iovec.len, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status >= 0) {
        uio->iovec.len = cb_ret.status;
        memcpy(uio->iovec.base, cb_ret.data, cb_ret.status);
    } else {
        ZF_LOGE("Error reading from NFS: %s", cb_ret.data);
    }
    return cb_ret.status;
}

int sos_nfs_write(vnode_t *file, struct uio *uio, coro_t me) {
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_write_async(sos_nfs, (struct nfsfh *) file->data, uio->iovec.len, uio->iovec.base, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status < 0) {
        ZF_LOGE("Error writing to NFS: %s", cb_ret.data);
    }
    return cb_ret.status;
}

int sos_nfs_pread(vnode_t *file, struct uio *uio, coro_t me) {
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_pread_async(sos_nfs, (struct nfsfh *) file->data, uio->offset, uio->iovec.len, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status >= 0) {
        uio->iovec.len = cb_ret.status;
        memcpy(uio->iovec.base, cb_ret.data, cb_ret.status);
    } else {
        ZF_LOGE("Error reading from NFS: %s", cb_ret.data);
    }
    return cb_ret.status;
}

int sos_nfs_pwrite(vnode_t *file, struct uio *uio, coro_t me) {
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_pwrite_async(sos_nfs, (struct nfsfh *) file->data, uio->offset, uio->iovec.len, uio->iovec.base, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status < 0) {
        ZF_LOGE("Error writing to NFS: %s", cb_ret.data);
    }
    return cb_ret.status;
}

int sos_nfs_close(vnode_t *vnode, coro_t me) {
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_close_async(sos_nfs, (struct nfsfh *) vnode->data, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status == 0) {
        free(vnode);
        return 0;
    }
    ZF_LOGE("Error closing NFS: %s", cb_ret.data);
    return cb_ret.status;
}

int sos_nfs_stat(vnode_t *vnode, char *pathname, sos_stat_t *stat, coro_t me) {
    (void) vnode;
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    if (nfs_stat64_async(sos_nfs, pathname, sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status == 0) {
        stat->st_type  = ((struct nfs_stat_64 *) cb_ret.data)->nfs_mode & 0170000;
        // we only get the owner's permission
        stat->st_fmode = (((struct nfs_stat_64 *) cb_ret.data)->nfs_mode & 0700) >> 6;
        stat->st_size  = ((struct nfs_stat_64 *) cb_ret.data)->nfs_size;
        stat->st_ctime = ((struct nfs_stat_64 *) cb_ret.data)->nfs_ctime;
        stat->st_atime = ((struct nfs_stat_64 *) cb_ret.data)->nfs_atime;
        return 0;
    }
    ZF_LOGE("Error getting stat for NFS: %s", cb_ret.data);
    return -1; 
}

int sos_nfs_get_dirent(vnode_t *vnode, int pos, char *name, size_t nbyte, coro_t me) {
    (void) vnode;
    res_cb_t cb_ret = {
        .coro = me,
        .status = 0,
        .data = NULL
    };
    // currently open root directory
    // TODO: ask which directory to open
    if (nfs_opendir_async(sos_nfs, "/", sos_nfs_cb, &cb_ret) < 0) return -1;
    yield(NULL);
    if (cb_ret.status < 0) {
        ZF_LOGE("Error opening dir NFS: %s", cb_ret.data);
        return -1; 
    }
    struct nfsdir *dir = (struct nfsdir *) cb_ret.data;
    nfs_seekdir(sos_nfs, dir, pos);
    struct nfsdirent *dirent = nfs_readdir(sos_nfs, dir);
    nfs_closedir(sos_nfs, dir);
    if (dirent == NULL) return 0;
    size_t len = strlen(dirent->name) + 1;
    if (len > nbyte) len = nbyte;
    memcpy(name, dirent->name, len);
    name[len] = '\0';
    return len;
}
