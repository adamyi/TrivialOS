#include <aos/debug.h>
#include <cpio/cpio.h>
#include <elf/elf.h>
#include <fcntl.h>

#include <sel4runtime.h>
#include <sel4runtime/auxv.h>
// #include <clock/clock.h>
#include <clock/device.h>

#include "elfload.h"
#include "process.h"
#include "vmem_layout.h"
#include "vm/frame_table.h"
#include "mapping.h"
#include "ut.h"
#include "utils.h"
#include "vfs/file.h"
#include "vm/pagetable.h"
#include "utils/rolling_id.h"

/**
 * The process table is an el-cheapo hash table. It's indexed by
 * (pid % MAX_PROCS), and only allows one process per slot. If a
 * new pid allocation would cause a hash collision, we just don't
 * use that pid.
 */
pid_t oldpids[MAX_PROCS];
process_t runprocs[MAX_PROCS];
static bool rid_inused[MAX_PID];
static rid_t proc_rid;

static seL4_CPtr ipc_ep;
static seL4_CPtr timer_ep;

pid_t clock_driver_pid = -1;

extern seL4_CPtr timer_cptr;

bool is_clock_driver_ready() {
    return clock_driver_pid >= 0;
}

unsigned get_time() {
    if (is_clock_driver_ready()) {
        seL4_Call(timer_ep, seL4_MessageInfo_new(0, 0, 0, 0));
        return seL4_GetMR(0);
    }
    /* if clock driver gets killed, we auto-respawn it */
    /* for simplicity, we always set stime of clock driver to 0 */
    /* even if it is respawned. */
    /* */
    /* when clock driver gets killed, timestamp counting doesn't stop */
    /* when it comes back, we still have the correct timestamp :-) */
    return 0;
}

void process_init() {
    if (rid_init(&proc_rid, rid_inused, MAX_PID, 1) < 0) {
        ZF_LOGF("can't init pid");
    }
    memset(runprocs, 0, sizeof(runprocs));
    memset(oldpids, 0, sizeof(oldpids));
    global_exit_blocked = NULL;
}

process_t *get_process_by_pid(pid_t pid) {
    if (runprocs[pid % MAX_PROCS].pid == pid)
        return &runprocs[pid % MAX_PROCS];
    return NULL;
}

pid_t get_next_pid() {
    for (int i = 0; i < MAX_PROCS; i++) {
        pid_t ret = rid_get_id(&proc_rid);
        if (ret == -1) return -1;
        if (runprocs[ret % MAX_PROCS].state == PROC_FREE) return ret;
        rid_remove_id(&proc_rid, ret);
    }
    return -1;
}

int get_processes(sos_process_t *processes, int max) {
    if (max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_PROCS; ++i) {
        if (runprocs[i].state != PROC_FREE && runprocs[i].state != PROC_CREATING) {
            processes->pid = runprocs[i].pid;
            processes->size = runprocs[i].addrspace->pagecount;
            processes->stime = runprocs[i].stime / 1000; // time in msec
            strncpy(processes->command, runprocs[i].command, N_NAME);
            processes->command[N_NAME - 1] = '\0';
            if (++count == max) break;
            ++processes;
        }
    }
    return count;
}

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(process_t *proc, cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file, vnode_t *elf_vnode, coro_t coro, bool pinned)
{

    /* virtual addresses in the target process' address space */
    uintptr_t stack_bottom = PROCESS_STACK_BOTTOM;
    uintptr_t stack_top = PROCESS_STACK_BOTTOM - PAGE_SIZE_4K;

    /* virtual addresses in the SOS's address space */
    void *local_stack_bottom  = (seL4_Word *) SOS_SCRATCH;
    uintptr_t local_stack_top = SOS_SCRATCH - PAGE_SIZE_4K;


    /* find the vsyscall table */
    uintptr_t sysinfo;
    if (0 > elf_find_vsyscall(elf_file, elf_vnode, &sysinfo, coro)) {
        ZF_LOGE("could not find syscall table for c library");
        return 0;
    }
    ZF_LOGD("vsyscall table: %p", sysinfo);

    int err = as_define_stack(proc->addrspace, PROCESS_STACK_BOTTOM, PAGE_SIZE_4K);
    if (err) {
        ZF_LOGE("could not create stack region");
        return 0;
    }

    pte_t stack_pte;
    err = alloc_map_frame(proc->addrspace, cspace, stack_top,
                                       seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &stack_pte, coro, pinned);
    

    if (err) {
        ZF_LOGE("Unable to map stack for user app");
        return 0;
    }

    /* allocate a slot to duplicate the stack frame cap so we can map it into our address space */
    seL4_CPtr local_stack_cptr = cspace_alloc_slot(cspace);
    if (local_stack_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack");
        return 0;
    }

    /* copy the stack frame cap into the slot */
    err = cspace_copy(cspace, local_stack_cptr, cspace, stack_pte.cap, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = map_frame(cspace, local_stack_cptr, local_vspace, local_stack_top, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
    if (err != seL4_NoError) {
        cspace_delete(cspace, local_stack_cptr);
        cspace_free_slot(cspace, local_stack_cptr);
        return 0;
    }

    int index = -2;

    /* null terminate the aux vectors */
    index = stack_write(local_stack_bottom, index, 0);
    index = stack_write(local_stack_bottom, index, 0);

    /* write the aux vectors */
    index = stack_write(local_stack_bottom, index, PAGE_SIZE_4K);
    index = stack_write(local_stack_bottom, index, AT_PAGESZ);

    index = stack_write(local_stack_bottom, index, sysinfo);
    index = stack_write(local_stack_bottom, index, AT_SYSINFO);

    index = stack_write(local_stack_bottom, index, PROCESS_IPC_BUFFER);
    index = stack_write(local_stack_bottom, index, AT_SEL4_IPC_BUFFER_PTR);

    /* null terminate the environment pointers */
    index = stack_write(local_stack_bottom, index, 0);

    /* we don't have any env pointers - skip */

    /* null terminate the argument pointers */
    index = stack_write(local_stack_bottom, index, 0);

    /* no argpointers - skip */

    /* set argc to 0 */
    stack_write(local_stack_bottom, index, 0);

    /* adjust the initial stack bottom */
    stack_bottom += (index * sizeof(seL4_Word));

    /* the stack *must* remain aligned to a double word boundary,
     * as GCC assumes this, and horrible bugs occur if this is wrong */
    assert(index % 2 == 0);
    assert(stack_bottom % (sizeof(seL4_Word) * 2) == 0);

    /* unmap our copy of the stack */
    err = seL4_ARM_Page_Unmap(local_stack_cptr);
    assert(err == seL4_NoError);

    /* delete the copy of the stack frame cap */
    err = cspace_delete(cspace, local_stack_cptr);
    assert(err == seL4_NoError);

    /* mark the slot as free */
    cspace_free_slot(cspace, local_stack_cptr);

    /* Exend the stack with extra pages */
    /* for (int page = 0; page < INITIAL_PROCESS_EXTRA_STACK_PAGES; page++) {
        stack_top -= PAGE_SIZE_4K;
        err = alloc_map_frame(proc->addrspace, cspace, stack_top,
                              seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &stack_pte, coro, pinned);
        if (err) {
            ZF_LOGE("Couldn't allocate additional stack frame");
            return 0;
        }
    } */

    return stack_bottom;
}

struct sfp_args {
    cspace_t *cspace;
    char *app_name;
    coro_t coro;
};

static void _delete_process(process_t *proc, coro_t coro);

/* Start process, and return pid if successful
 */
pid_t start_process(cspace_t *cspace, char *app_name, proc_create_hook hook, bool pinned, coro_t coro) {
    sos_stat_t file_stat;
    if (vfs_stat(app_name, &file_stat, coro) < 0) {
        ZF_LOGE("file not exist");
        return -1;
    }
    if (!(file_stat.st_fmode & FM_EXEC)) {
        ZF_LOGE("ok russian hacker, it's not executable");
        return -1;
    }

    pid_t pid = get_next_pid();
    if (pid == -1) return -1;

    process_t *proc = runprocs + (pid % MAX_PROCS);
    proc->pid = pid;
    proc->kill_hook = NULL;
    proc->state = PROC_CREATING;

    /* Create a VSpace */
    proc->vspace_ut = alloc_retype(&(proc->vspace), seL4_ARM_PageGlobalDirectoryObject,
                                              seL4_PGDBits);
    if (proc->vspace_ut == NULL) {
        ZF_LOGE("failed to alloc vspace_ut");
        _delete_process(proc, coro);
        return -1;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, proc->vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        _delete_process(proc, coro);
        return -1;
    }

    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(cspace, &(proc->cspace));
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        _delete_process(proc, coro);
        return -1;
    }

    /* Create an as */
    proc->addrspace = as_create(proc->vspace, coro);
    if (proc->addrspace == NULL) {
        ZF_LOGE("Failed to create addrspace");
        _delete_process(proc, coro);
        return -1;
    }

    /* Create an IPC buffer */
    err = as_define_region(proc->addrspace, PROCESS_IPC_BUFFER, PAGE_SIZE_4K, seL4_AllRights,
                seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, NULL);
    if (err) {
        ZF_LOGE("Failed to define IPC region");
        _delete_process(proc, coro);
        return -1;
    }

    /* Create an IPC frame */
    pte_t ipc_buffer;
    err = alloc_map_frame(proc->addrspace, cspace, PROCESS_IPC_BUFFER,
                                        seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &ipc_buffer, coro, pinned);
    if (err) {
        ZF_LOGE("Failed to alloc map IPC frame");
        _delete_process(proc, coro);
        return -1;
    }

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&(proc->cspace));
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        _delete_process(proc, coro);
        return -1;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&(proc->cspace), user_ep, cspace, ipc_ep, seL4_AllRights, PID_TO_BADGE(proc->pid));
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        _delete_process(proc, coro);
        return -1;
    }

    /* Create a new TCB object */
    proc->tcb_ut = alloc_retype(&(proc->tcb), seL4_TCBObject, seL4_TCBBits);
    if (proc->tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        _delete_process(proc, coro);
        return -1;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(proc->tcb,
                             proc->cspace.root_cnode, seL4_NilData,
                             proc->vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             ipc_buffer.cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        _delete_process(proc, coro);
        return -1;
    }

    /* Create scheduling context */
    proc->sched_context_ut = alloc_retype(&(proc->sched_context), seL4_SchedContextObject,
                                                     seL4_MinSchedContextBits);
    if (proc->sched_context_ut == NULL) {
        ZF_LOGE("Failed to alloc sched context ut");
        _delete_process(proc, coro);
        return -1;
    }

    /* Configure the scheduling context to use the first core with budget equal to period */
    err = seL4_SchedControl_Configure(sched_ctrl_start, proc->sched_context, US_IN_MS, US_IN_MS, 0, 0);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure scheduling context");
        _delete_process(proc, coro);
        return -1;
    }

    /* allocate a new slot in the kernel cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    proc->kernel_ep = cspace_alloc_slot(cspace);
    if (proc->kernel_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc kernel ep slot");
        _delete_process(proc, coro);
        return -1;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(cspace, proc->kernel_ep, cspace, ipc_ep, seL4_AllRights, PID_TO_BADGE(proc->pid));
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        _delete_process(proc, coro);
        return -1;
    }

    /* bind sched context, set fault endpoint and priority
     * In MCS, fault end point needed here should be in current thread's cspace.
     * NOTE this will use the unbadged ep unlike above, you might want to mint it with a badge
     * so you can identify which thread faulted in your fault handler */
    err = seL4_TCB_SetSchedParams(proc->tcb, seL4_CapInitThreadTCB, seL4_MinPrio, TTY_PRIORITY,
                                  proc->sched_context, proc->kernel_ep);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set scheduling params");
        _delete_process(proc, coro);
        return -1;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(proc->tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);

    vnode_t *elf_vnode;
    err = vfs_open(app_name, O_RDONLY, &elf_vnode, coro);
    if (err) {
        ZF_LOGE("can't open file");
        _delete_process(proc, coro);
        return -1;
    }

    /* load in header (56/64 bytes) */
    frame_ref_t headerframe = alloc_frame(coro);
    if (headerframe == NULL_FRAME) {
        ZF_LOGE("can't allocate frame");
        vfs_close(elf_vnode, coro);
        _delete_process(proc, coro);
        return -1;
    }
    pin_frame(headerframe);
    void *headerbytes = frame_data(headerframe);

    uio_t myuio;
    if (uio_kinit(&myuio, headerbytes, PAGE_SIZE_4K, 0, UIO_WRITE)) {
        ZF_LOGE("can't uio_kinit");
        unpin_frame(headerframe);
        free_frame(headerframe);
        vfs_close(elf_vnode, coro);
        _delete_process(proc, coro);
        return -1;
    }
    int headersize = VOP_READ(elf_vnode, &myuio, proc, coro);
    if (headersize < 0) {
        ZF_LOGE("can't read elf file");
        unpin_frame(headerframe);
        free_frame(headerframe);
        vfs_close(elf_vnode, coro);
        _delete_process(proc, coro);
        return -1;
    }
    uio_destroy(&myuio, NULL);

    elf_t elf_file = {};

    /*for (int i = 0; i < 64; i++) {
        printf("%x ", *((char *)headerbytes+i));
    }
    printf("\n");*/

    /* Ensure that the file is an elf file. */
    /* we only check ELF header and program header table without checking section header table */
    if (elf_newFile_maybe_unsafe(headerbytes, headersize, true, false, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        unpin_frame(headerframe);
        free_frame(headerframe);
        vfs_close(elf_vnode, coro);
        _delete_process(proc, coro);
        return -1;
    }

    /* set up the stack */
    seL4_Word sp = init_process_stack(proc, cspace, seL4_CapInitThreadVSpace, &elf_file, elf_vnode, coro, pinned);
    if (sp == 0) {
        ZF_LOGE("Failed to set up stack");
        unpin_frame(headerframe);
        free_frame(headerframe);
        vfs_close(elf_vnode, coro);
        _delete_process(proc, coro);
        return -1;
    }

    vaddr_t heap_start;

    /* load the elf image from NFS*/
    err = elf_load(cspace, proc, proc->vspace, &elf_file, elf_vnode, proc->addrspace, &heap_start, pinned, coro);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        unpin_frame(headerframe);
        free_frame(headerframe);
        vfs_close(elf_vnode, coro);
        _delete_process(proc, coro);
        return -1;
    }
    uintptr_t entrypoint = elf_getEntryPoint(&elf_file);

    unpin_frame(headerframe);
    free_frame(headerframe);
    vfs_close(elf_vnode, coro);

    /* set up the heap */
    err = as_define_heap(proc->addrspace, heap_start);
    if (err) {
        ZF_LOGE("Failed to define heap region");
        _delete_process(proc, coro);
        return -1;
    }

    fdtable_init(&proc->fdt, coro);

    if (hook) {
        err = hook(proc, coro);
        if (err != 0) {
            ZF_LOGE("Failed in proc create hook");
            _delete_process(proc, coro);
            return -1;
        }
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = entrypoint,
        .sp = sp,
    };

    strncpy(proc->command, app_name, N_NAME);
    proc->command[N_NAME - 1] = '\0';
    proc->exit_blocked = NULL;

    ZF_LOGE("Starting %s at %p", app_name, (void *) context.pc);
    proc->stime = get_time();
    proc->state = PROC_RUNNING;
    err = seL4_TCB_WriteRegisters(proc->tcb, 1, 0, 2, &context);
    if (err != seL4_NoError) {
        // free everything
        ZF_LOGE("Failed to write registers");
        _delete_process(proc, coro);
        return -1;
    }
    return proc->pid;
}

seL4_Error clock_hook(process_t *proc, coro_t coro) {
    seL4_Error err = as_define_region(proc->addrspace, CLOCK_DRIVER_ADDR, PAGE_SIZE_4K, seL4_AllRights,
                seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, NULL);
    if (err) return err;
    seL4_CPtr user_ep = cspace_alloc_slot(&(proc->cspace));
    if (user_ep == seL4_CapNull) return seL4_NotEnoughMemory;
    err = cspace_mint(&(proc->cspace), user_ep, &cspace, timer_ep, seL4_AllRights, PID_TO_BADGE(proc->pid));
    if (err) return err;
    ut_t* reply_ut = ut_alloc(seL4_ReplyBits, &cspace);
    seL4_CPtr reply_obj = cspace_alloc_slot(&(proc->cspace));
    if (reply_obj == seL4_CapNull) return seL4_NotEnoughMemory;
    err = cspace_untyped_retype(&(proc->cspace), reply_ut->cap, reply_obj, seL4_ReplyObject, seL4_ReplyBits);
    if (err) return err;
    return sos_clone_and_map_device_frame(proc->addrspace, &cspace, timer_cptr, CLOCK_DRIVER_ADDR, coro);
}

static void start_clock_driver(cspace_t *cspace, coro_t coro) {
    if ((clock_driver_pid = start_process(cspace, "clock_driver", clock_hook, true, coro)) == -1) {
        ZF_LOGE("Failed to start clock_driver");
    }
}

void *_start_first_process_impl(void *args) {
    struct sfp_args *sargs = args;
    cspace_t *cspace = sargs->cspace;
    char *app_name = sargs->app_name;
    coro_t coro = sargs->coro;
    start_clock_driver(cspace, coro);
    start_process(cspace, app_name, NULL, false, coro);
}

bool start_first_process(cspace_t *cspace, char *app_name, seL4_CPtr _ipc_ep, seL4_CPtr _timer_ep) {
    ipc_ep = _ipc_ep;
    timer_ep = _timer_ep;
    coro_t c = coroutine(_start_first_process_impl);
    struct sfp_args args = {
        .cspace = cspace,
        .app_name = app_name,
        .coro = c
    };
    resume(c, &args);
    return true;
}

static void exhaust_runqueue(runqueue_t **queue, pid_t pid) {
    while (*queue != NULL) {
        ZF_LOGD("exhaust_runqueue: resume %p", (*queue)->coro);
        if ((*queue)->coro) resume((*queue)->coro, pid);
        runqueue_t *last = *queue;
        *queue = (*queue)->next;
        free(last);
    }
    ZF_LOGD("exhaust_runqueue finish");
}

struct kill_hook_data {
    coro_t coro;
    runqueue_t *queue;
};

static void waiting_proc_kill_hook(void *data) {
    runqueue_t *rq = data;
    coro_t coro = rq->coro;
    rq->coro = NULL;
    resume(coro, -1);
}

pid_t wait_for_process_exit(pid_t pid, process_t *me, coro_t coro) {
    ZF_LOGD("wait_for_process_exit %d", pid);
    runqueue_t **queue;
    if (pid == -1) {
        queue = &global_exit_blocked;
    } else {
        int bucket = pid % MAX_PROCS;
        if (oldpids[bucket] == pid) return pid;
        if (runprocs[bucket].pid != pid) return -1;
        queue = &(runprocs[bucket].exit_blocked);
    }
    runqueue_t *rq = malloc(sizeof(runqueue_t));
    if (rq == NULL) return -1;
    rq->coro = coro;
    rq->next = *queue;
    *queue = rq;
    me->kill_hook = waiting_proc_kill_hook;
    me->kill_hook_data = rq;
    void *retpid = yield(NULL);
    me->kill_hook_data = me->kill_hook = NULL;
    return (pid_t) retpid;
}

static void _delete_process(process_t *proc, coro_t coro) {
    ZF_LOGD("deleting proc %d", proc->pid);

    rid_remove_id(&proc_rid, proc->pid);

    bool restart_clock = false;
    if (proc->pid == clock_driver_pid) {
        clock_driver_pid = -1;
        restart_clock = true;
    }

    // this is safe to call if everything is null
    fdtable_destroy(&(proc->fdt), coro);

    if (proc->addrspace) as_destroy(proc->addrspace, &cspace, coro);

    if (proc->tcb) {
        cspace_delete(&cspace, proc->tcb);
        cspace_free_slot(&cspace, proc->tcb);
    }
    if (proc->tcb_ut) ut_free(proc->tcb_ut);

    if (proc->vspace) {
        cspace_delete(&cspace, proc->vspace);
        cspace_free_slot(&cspace, proc->vspace);
    }
    if (proc->vspace_ut) ut_free(proc->vspace_ut);

    if (proc->sched_context) {
        cspace_delete(&cspace, proc->sched_context);
        cspace_free_slot(&cspace, proc->sched_context);
    }
    if (proc->sched_context_ut) ut_free(proc->sched_context_ut);

    if (proc->kernel_ep) {
        cspace_delete(&cspace, proc->kernel_ep);
        cspace_free_slot(&cspace, proc->kernel_ep);
    }

    if (proc->cspace.root_cnode != seL4_CapNull) cspace_destroy(&(proc->cspace));

    if (restart_clock) start_clock_driver(&cspace, coro);

    proc->state = PROC_FREE;
}

void kill_process(process_t *proc, coro_t coro) {
    ZF_LOGD("killing proc %d", proc->pid);
    seL4_TCB_Suspend(proc->tcb);

    _delete_process(proc, coro);

    pid_t pid = proc->pid;
    oldpids[pid % MAX_PROCS] = pid;

    // this should fix weird race conditions
    runqueue_t *proc_exit_blocked = proc->exit_blocked;
    runqueue_t *lglobal_exit_blocked = global_exit_blocked;
    global_exit_blocked = NULL;
    memset(proc, 0, sizeof(process_t));

    exhaust_runqueue(&lglobal_exit_blocked, pid);
    exhaust_runqueue(&proc_exit_blocked, pid);
}
