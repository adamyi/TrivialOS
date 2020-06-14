#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

#include "syscall.h"
#include "files.h"
#include "time.h"

#define SYSCALL_NUM (6)

static syscall_t *syscalls[SYSCALL_NUM];


// REMEMBER TO CHANGE SYSCALL_NUM
void init_syscall() {
    START_INSTALLING_SYSCALLS();
    INSTALL_SYSCALL(open);
    INSTALL_SYSCALL(read);
    INSTALL_SYSCALL(write);
    INSTALL_SYSCALL(close);
    INSTALL_SYSCALL(usleep);
    INSTALL_SYSCALL(time_stamp);
    // did you change SYSCALL_NUM?
}

struct syscall_args {
    cspace_t *cspace;
    seL4_Word badge;
    size_t num_args;
    seL4_CPtr reply;
    ut_t *reply_ut;
    process_t *proc;
    coro_t coro;
};

static void *_handle_syscall_impl(void *args) {
    struct syscall_args *sargs = (struct syscall_args *) args;
    cspace_t *cspace = sargs->cspace;
    seL4_CPtr reply = sargs->reply;
    ut_t *reply_ut = sargs->reply_ut;

    /* get the first word of the message, which in the SOS protocol is the number
     * of the SOS "syscall". */
    seL4_Word syscall_number = seL4_GetMR(0);
    seL4_MessageInfo_t reply_msg;

    if (syscall_number >= SYSCALL_NUM) {
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        reply_msg = return_error();
    } else if (syscalls[syscall_number]->args != sargs->num_args) {
        ZF_LOGE("Unmatch syscall %s\n", syscalls[syscall_number]->name);
        reply_msg = return_error();
    } else {
        ZF_LOGE("Calling syscall %s\n", syscalls[syscall_number]->name);
        reply_msg = syscalls[syscall_number]->implementation(sargs->proc, sargs->coro);
    }
    seL4_Send(reply, reply_msg);
    cspace_delete(cspace, reply);
    cspace_free_slot(cspace, reply);
    ut_free(reply_ut);
    printf("sent\n");
    return NULL;
}

void handle_syscall(cspace_t *cspace, seL4_Word badge, size_t num_args, seL4_CPtr reply, ut_t *reply_ut, process_t *proc) {
    coro_t c = coroutine(_handle_syscall_impl);
    struct syscall_args args = {
        .cspace = cspace,
        .badge = badge,
        .num_args = num_args,
        .reply = reply,
        .reply_ut = reply_ut,
        .proc = proc,
        .coro = c
    };
    resume(c, &args);
    printf("haha\n");
}
