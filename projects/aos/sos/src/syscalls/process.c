#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sel4runtime.h>
#include "syscall.h"

#include "process.h"
#include "../vfs/uio.h"
#include "../process.h"

IMPLEMENT_SYSCALL(process_create, 2) {
    char pathname[PATH_MAX + 1];

    vaddr_t pathname_ptr = seL4_GetMR(1);
    int pathlen = seL4_GetMR(2);
    if (pathlen > PATH_MAX) {
        return return_word(-ENAMETOOLONG);
    }

    int err = copy_in(cspace, proc->addrspace, pathname_ptr, pathlen, pathname, me);
    if (err) return return_word(-EINVAL);
    pathname[pathlen] = '\0';

    pid_t pid = start_process(cspace, pathname, me);

    return return_word(pid);
}

IMPLEMENT_SYSCALL(process_delete, 1) {
    pid_t pid = seL4_GetMR(1);
    printf("Killing process %d\n", pid);
    process_t *check_proc = get_process_by_pid(pid);
    if (check_proc == NULL) return return_error();
    if (check_proc == proc || check_proc->state == PROC_RUNNING) {
        printf("Killing myself\n");
        kill_process(check_proc);
    } else if (check_proc->state == PROC_BLOCKED) {
        check_proc->state = PROC_TO_BE_KILLED;
        //
    }
    return return_word(0);
}

IMPLEMENT_SYSCALL(my_id, 0) {
    return return_word(proc->pid);
}

IMPLEMENT_SYSCALL(process_status, 2) {
    uintptr_t dest = seL4_GetMR(1);
    int max = seL4_GetMR(2);
    if (max <= 0) return return_word(0);
    if (max > MAX_PROCS) max = MAX_PROCS;
    
    sos_process_t *processes = malloc(max * sizeof(sos_process_t));
    int count = get_processes(processes, max);
    int err = copy_out(cspace, proc->addrspace, dest, count * sizeof(sos_process_t), processes, me);
    free(processes);
    if (err) return return_word(-EINVAL);
    return return_word(count);
}

IMPLEMENT_SYSCALL(process_wait, 3) {
    return return_word(0);
}
