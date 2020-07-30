#include <aos/debug.h>
#include <cpio/cpio.h>
#include <elf/elf.h>
#include <fcntl.h>

#include <sel4runtime.h>
#include <sel4runtime/auxv.h>
#include <clock/clock.h>

#include "elfload.h"
#include "process.h"
#include "vmem_layout.h"
#include "vm/frame_table.h"
#include "mapping.h"
#include "threads.h"
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
static rid_t proc_rid;

static seL4_CPtr ipc_ep;

void process_init() {
    if (rid_init(&proc_rid, MAX_PID, 1) < 0) {
        ZF_LOGF("can't init pid");
    }
    memset(runprocs, 0, sizeof(runprocs));
    memset(oldpids, 0, sizeof(oldpids));
}

process_t *get_process_by_pid(pid_t pid) {
    if (runprocs[pid % MAX_PROCS].pid == pid)
        return &runprocs[pid % MAX_PROCS];
    return NULL;
}

pid_t get_next_pid() {
    pid_t ret = rid_get_id(&proc_rid);
    printf("rid_get_id %d\n", ret);
    int count = MAX_PROCS;
    while (count-- && ret != -1 && runprocs[ret % MAX_PROCS].state != PROC_FREE) {
        printf("trivial\n");
        rid_remove_id(&proc_rid, ret);
        ret = rid_get_id(&proc_rid);
    }
    if (count == 0) {
        if (ret != -1) rid_remove_id(&proc_rid, ret);
        return -1;
    }
    return ret;
}

int get_processes(sos_process_t *processes, int max) {
    if (max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_PROCS; ++i) {
        if (runprocs[i].state != PROC_FREE) {
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
static uintptr_t init_process_stack(process_t *proc, cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file, vnode_t *elf_vnode, coro_t coro)
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
    printf("sysinfo: %p\n", sysinfo);

    int err = as_define_stack(proc->addrspace, PROCESS_STACK_BOTTOM, PAGE_SIZE_4K);
    if (err) {
        ZF_LOGE("could not create stack region");
        return 0;
    }

    pte_t stack_pte;
    err = alloc_map_frame(proc->addrspace, cspace, stack_top,
                                       seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &stack_pte, coro);
    

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
    /*for (int page = 0; page < INITIAL_PROCESS_EXTRA_STACK_PAGES; page++) {
        stack_top -= PAGE_SIZE_4K;
        seL4_CPtr frame_cptr = alloc_map_frame(proc->addrspace, cspace, proc->vspace, stack_top,
                                        seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
        if (frame_cptr == seL4_CapNull) {
            ZF_LOGE("Couldn't allocate additional stack frame");
            return 0;
        }
    }*/

    return stack_bottom;
}

struct sfp_args {
    cspace_t *cspace;
    char *app_name;
    coro_t coro;
};

/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
pid_t start_process(cspace_t *cspace, char *app_name, coro_t coro) {
    sos_stat_t file_stat;
    if (vfs_stat(app_name, &file_stat, coro) < 0) {
        ZF_LOGE("file not exist");
        return -1;
    }
    if (!(file_stat.st_fmode & FM_EXEC)) {
        ZF_LOGE("ok russian hacker, it's not executable");
        return -1;
    }
    printf("aaaaaaaaaaaa\n");

    pid_t pid = get_next_pid();
    printf("get_next_pid: %d\n", pid);
    if (pid == -1) return -1;

    process_t *proc = runprocs + (pid % MAX_PROCS);
    proc->pid = pid;

    /* Create a VSpace */
    proc->vspace_ut = alloc_retype(&(proc->vspace), seL4_ARM_PageGlobalDirectoryObject,
                                              seL4_PGDBits);
    if (proc->vspace_ut == NULL) {
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, proc->vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(cspace, &(proc->cspace));
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create an as */
    proc->addrspace = as_create(proc->vspace);
    if (proc->addrspace == NULL) {
        ZF_LOGE("Failed to create addrspace");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create an IPC buffer */
    err = as_define_region(proc->addrspace, PROCESS_IPC_BUFFER, PAGE_SIZE_4K, seL4_AllRights,
                seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, NULL);
    if (err) {
        ZF_LOGE("Failed to define IPC region");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create an IPC frame */
    err = alloc_map_frame(proc->addrspace, cspace, PROCESS_IPC_BUFFER,
                                        seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &(proc->ipc_buffer), coro);
    if (err) {
        ZF_LOGE("Failed to alloc map IPC frame");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&(proc->cspace));
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&(proc->cspace), user_ep, cspace, ipc_ep, seL4_AllRights, proc->pid);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create a new TCB object */
    proc->tcb_ut = alloc_retype(&(proc->tcb), seL4_TCBObject, seL4_TCBBits);
    if (proc->tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Configure the TCB */
    err = seL4_TCB_Configure(proc->tcb,
                             proc->cspace.root_cnode, seL4_NilData,
                             proc->vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             proc->ipc_buffer.cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create scheduling context */
    proc->sched_context_ut = alloc_retype(&(proc->sched_context), seL4_SchedContextObject,
                                                     seL4_MinSchedContextBits);
    if (proc->sched_context_ut == NULL) {
        ZF_LOGE("Failed to alloc sched context ut");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Configure the scheduling context to use the first core with budget equal to period */
    err = seL4_SchedControl_Configure(sched_ctrl_start, proc->sched_context, US_IN_MS, US_IN_MS, 0, 0);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure scheduling context");
        return -1;
    }

    /* allocate a new slot in the kernel cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    proc->kernel_ep = cspace_alloc_slot(cspace);
    if (proc->kernel_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc kernel ep slot");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(cspace, proc->kernel_ep, cspace, ipc_ep, seL4_AllRights, proc->pid);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* bind sched context, set fault endpoint and priority
     * In MCS, fault end point needed here should be in current thread's cspace.
     * NOTE this will use the unbadged ep unlike above, you might want to mint it with a badge
     * so you can identify which thread faulted in your fault handler */
    err = seL4_TCB_SetSchedParams(proc->tcb, seL4_CapInitThreadTCB, seL4_MinPrio, TTY_PRIORITY,
                                  proc->sched_context, proc->kernel_ep);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set scheduling params");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(proc->tcb, app_name);

    printf("aaaaaaaaaaaa\n");
    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);

    vnode_t *elf_vnode;
    err = vfs_open(app_name, O_RDONLY, &elf_vnode, coro);
    if (err) {
        ZF_LOGE("can't open file");
        return -1;
    }

    /* load in header (56/64 bytes) */
    frame_ref_t headerframe = alloc_frame(coro);
    if (headerframe == NULL_FRAME) {
        ZF_LOGE("can't allocate frame");
        return -1;
    }
    pin_frame(headerframe);
    void *headerbytes = frame_data(headerframe);

    uio_t myuio;
    if (uio_kinit(&myuio, headerbytes, PAGE_SIZE_4K, 0, UIO_WRITE)) {
        ZF_LOGE("can't uio_kinit");
        unpin_frame(headerframe);
        free_frame(headerframe);
        return -1;
    }
    int headersize = VOP_READ(elf_vnode, &myuio, coro);
    if (headersize < 0) {
        ZF_LOGE("can't read elf file");
        unpin_frame(headerframe);
        free_frame(headerframe);
        return -1;
    }
    uio_destroy(&myuio, NULL);

    elf_t elf_file = {};

    /* Ensure that the file is an elf file. */
    /* we only check ELF header and program header table without checking section header table */
    char *fuck = headerbytes;
    printf("%x %x %x %x\n%d\n", *fuck, *(fuck+1), *(fuck+2), *(fuck+3), headersize);
    if (elf_newFile_maybe_unsafe(headerbytes, headersize, true, false, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        unpin_frame(headerframe);
        free_frame(headerframe);
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* set up the stack */
    seL4_Word sp = init_process_stack(proc, cspace, seL4_CapInitThreadVSpace, &elf_file, elf_vnode, coro);
    if (sp == 0) {
        ZF_LOGE("Failed to set up stack");
        unpin_frame(headerframe);
        free_frame(headerframe);
        return -1;
    }

    vaddr_t heap_start;

    printf("aaaaaaaaaaaa\n");
    /* load the elf image from the cpio file */
    err = elf_load(cspace, proc->vspace, &elf_file, elf_vnode, proc->addrspace, &heap_start, coro);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        unpin_frame(headerframe);
        free_frame(headerframe);
        return -1;
    }

    unpin_frame(headerframe);
    free_frame(headerframe);

    printf("aaaaaaaaaaaa\n");
    /* set up the heap */
    err = as_define_heap(proc->addrspace, heap_start);
    if (err) {
        ZF_LOGE("Failed to define heap region");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* Map in the IPC buffer for the thread */
    err = sos_map_frame(proc->addrspace, cspace, proc->ipc_buffer.frame, PROCESS_IPC_BUFFER,
                    seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, NULL, coro);
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    fdtable_init(&proc->fdt);

    printf("aaaaaaaaaaaa\n");
    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(&elf_file),
        .sp = sp,
    };

    strncpy(proc->command, app_name, N_NAME);
    proc->command[N_NAME - 1] = '\0';

    printf("Starting %s at %p\n", app_name, (void *) context.pc);
    err = seL4_TCB_WriteRegisters(proc->tcb, 1, 0, 2, &context);
    if (err != seL4_NoError) {
        // free everything
        ZF_LOGE("Failed to write registers");
        return -1;
    }
    proc->stime = get_time();
    proc->state = PROC_RUNNING;
    return proc->pid;
}


void *_start_first_process_impl(void *args) {
    struct sfp_args *sargs = args;
    cspace_t *cspace = sargs->cspace;
    char *app_name = sargs->app_name;
    coro_t coro = sargs->coro;

    start_process(cspace, app_name, coro);
}

bool start_first_process(cspace_t *cspace, char *app_name, seL4_CPtr ep) {
    ipc_ep = ep;
    coro_t c = coroutine(_start_first_process_impl);
    struct sfp_args args = {
        .cspace = cspace,
        .app_name = app_name,
        .coro = c
    };
    resume(c, &args);
    return true;
}

void kill_process(process_t *proc, coro_t coro) {
    seL4_TCB_Suspend(proc->tcb);

    printf("fdtable_destroy\n");

    fdtable_destroy(&(proc->fdt), coro);

    printf("as_detroy\n");
    // as_destroy(proc->addrspace, &cspace);

    printf("tcb\n");
    printf("b\n");
    cspace_delete(&cspace, proc->tcb);
    printf("b\n");
    cspace_free_slot(&cspace, proc->tcb);
    printf("b\n");
    printf("free tcb ut %p\n", proc->tcb_ut);
    ut_free(proc->tcb_ut);

    printf("vspace\n");
    cspace_delete(&cspace, proc->vspace);
    cspace_free_slot(&cspace, proc->vspace);
    ut_free(proc->vspace_ut);

    printf("schedcontext\n");
    cspace_delete(&cspace, proc->sched_context);
    cspace_free_slot(&cspace, proc->sched_context);
    ut_free(proc->sched_context_ut);

    printf("kernelep\n");
    cspace_delete(&cspace, proc->kernel_ep);
    cspace_free_slot(&cspace, proc->kernel_ep);

    printf("cspace\n");
    cspace_destroy(&(proc->cspace));

    printf("move to old\n");
    oldpids[proc->pid % MAX_PROCS] = proc->pid;

    memset(proc, 0, sizeof(process_t));
}
