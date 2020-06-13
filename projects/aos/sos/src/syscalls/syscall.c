#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

#include "syscall.h"
#include "files.h"

#define SYSCALL_NUM 4

static syscall_t *syscalls[SYSCALL_NUM];

static seL4_MessageInfo_t return_error() {
    seL4_SetMR(0, -1);
    return seL4_MessageInfo_new(0, 0, 0, 1);
}

void init_syscall() {
    START_INSTALLING_SYSCALLS();
    INSTALL_SYSCALL(open);
    INSTALL_SYSCALL(read);
    INSTALL_SYSCALL(write);
    INSTALL_SYSCALL(close);
}

void handle_syscall(cspace_t *cspace, seL4_Word badge, size_t num_args, seL4_CPtr reply, process_t *proc)
{
    (void) badge;
    /* get the first word of the message, which in the SOS protocol is the number
     * of the SOS "syscall". */
    seL4_Word syscall_number = seL4_GetMR(0);
    seL4_MessageInfo_t reply_msg;
    if (syscall_number >= SYSCALL_NUM) {
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        reply_msg = return_error();
    } else if (syscalls[syscall_number]->args != num_args) {
        ZF_LOGE("Unmatch syscall %s\n", syscalls[syscall_number]->name);
        reply_msg = return_error();
    } else {
        ZF_LOGE("Calling syscall %s\n", syscalls[syscall_number]->name);
        reply_msg = syscalls[syscall_number]->implementation(proc);
    }
    seL4_Send(reply, reply_msg);
    cspace_free_slot(cspace, reply);
}
