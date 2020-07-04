#include <aos/debug.h>
#include <cpio/cpio.h>
#include <elf/elf.h>

#include <sel4runtime.h>
#include <sel4runtime/auxv.h>

#include "elfload.h"
#include "process.h"
#include "vmem_layout.h"
#include "vm/frame_table.h"
#include "mapping.h"
#include "threads.h"
#include "ut.h"
#include "vfs/file.h"
#include "vm/pagetable.h"

static seL4_Word shared_buffer_curr = SOS_SHARED_BUFFER;

seL4_Word get_new_shared_buffer_vaddr() {
    seL4_Word ret = shared_buffer_curr;
    shared_buffer_curr += PAGE_SIZE_4K;
    return ret;
}

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file, coro_t coro)
{

    /* virtual addresses in the target process' address space */
    uintptr_t stack_bottom = PROCESS_STACK_BOTTOM;
    uintptr_t stack_top = PROCESS_STACK_BOTTOM - PAGE_SIZE_4K;

    /* virtual addresses in the SOS's address space */
    void *local_stack_bottom  = (seL4_Word *) SOS_SCRATCH;
    uintptr_t local_stack_top = SOS_SCRATCH - PAGE_SIZE_4K;


    /* find the vsyscall table */
    uintptr_t sysinfo = *((uintptr_t *) elf_getSectionNamed(elf_file, "__vsyscall", NULL));
    if (sysinfo == 0) {
        ZF_LOGE("could not find syscall table for c library");
        return 0;
    }

    int err = as_define_stack(tty_test_process.addrspace, PROCESS_STACK_BOTTOM, PAGE_SIZE_4K);
    if (err) {
        ZF_LOGE("could not create stack region");
        return 0;
    }

    pte_t stack_pte;
    err = alloc_map_frame(tty_test_process.addrspace, cspace, stack_top,
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
        seL4_CPtr frame_cptr = alloc_map_frame(tty_test_process.addrspace, cspace, tty_test_process.vspace, stack_top,
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
    seL4_CPtr ep;
    coro_t coro;
};

/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
void *_start_first_process_impl(void *args);

bool start_first_process(cspace_t *cspace, char *app_name, seL4_CPtr ep) {
    coro_t c = coroutine(_start_first_process_impl);
    struct sfp_args args = {
        .cspace = cspace,
        .app_name = app_name,
        .ep = ep,
        .coro = c
    };
    resume(c, &args);
    return true;
}

void *_start_first_process_impl(void *args) {
    struct sfp_args *sargs = args;
    cspace_t *cspace = sargs->cspace;
    char *app_name = sargs->app_name;
    seL4_CPtr ep = sargs->ep;
    coro_t coro = sargs->coro;
    printf("aaaaaaaaaaaa\n");
    /* Create a VSpace */
    tty_test_process.vspace_ut = alloc_retype(&tty_test_process.vspace, seL4_ARM_PageGlobalDirectoryObject,
                                              seL4_PGDBits);
    if (tty_test_process.vspace_ut == NULL) {
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, tty_test_process.vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(cspace, &tty_test_process.cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create an as */
    tty_test_process.addrspace = as_create(tty_test_process.vspace);
    if (tty_test_process.addrspace == NULL) {
        ZF_LOGE("Failed to create addrspace");
        return 0;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create an IPC buffer */
    err = as_define_region(tty_test_process.addrspace, PROCESS_IPC_BUFFER, PAGE_SIZE_4K, seL4_AllRights,
                seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, NULL);
    if (err) {
        ZF_LOGE("Failed to define IPC region");
        return 0;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create an IPC frame */
    err = alloc_map_frame(tty_test_process.addrspace, cspace, PROCESS_IPC_BUFFER,
                                        seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, &(tty_test_process.ipc_buffer), coro);
    if (err) {
        ZF_LOGE("Failed to alloc map IPC frame");
        return 0;
    }

    printf("aaaaaaaaaaaa\n");
    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&tty_test_process.cspace);
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&tty_test_process.cspace, user_ep, cspace, ep, seL4_AllRights, TTY_EP_BADGE);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create a new TCB object */
    tty_test_process.tcb_ut = alloc_retype(&tty_test_process.tcb, seL4_TCBObject, seL4_TCBBits);
    if (tty_test_process.tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Configure the TCB */
    err = seL4_TCB_Configure(tty_test_process.tcb,
                             tty_test_process.cspace.root_cnode, seL4_NilData,
                             tty_test_process.vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             tty_test_process.ipc_buffer.cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Create scheduling context */
    tty_test_process.sched_context_ut = alloc_retype(&tty_test_process.sched_context, seL4_SchedContextObject,
                                                     seL4_MinSchedContextBits);
    if (tty_test_process.sched_context_ut == NULL) {
        ZF_LOGE("Failed to alloc sched context ut");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Configure the scheduling context to use the first core with budget equal to period */
    err = seL4_SchedControl_Configure(sched_ctrl_start, tty_test_process.sched_context, US_IN_MS, US_IN_MS, 0, 0);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure scheduling context");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* bind sched context, set fault endpoint and priority
     * In MCS, fault end point needed here should be in current thread's cspace.
     * NOTE this will use the unbadged ep unlike above, you might want to mint it with a badge
     * so you can identify which thread faulted in your fault handler */
    err = seL4_TCB_SetSchedParams(tty_test_process.tcb, seL4_CapInitThreadTCB, seL4_MinPrio, TTY_PRIORITY,
                                  tty_test_process.sched_context, ep);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set scheduling params");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(tty_test_process.tcb, app_name);

    printf("aaaaaaaaaaaa\n");
    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    elf_t elf_file = {};
    unsigned long elf_size;
    size_t cpio_len = _cpio_archive_end - _cpio_archive;
    char *elf_base = cpio_get_file(_cpio_archive, cpio_len, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }
    /* Ensure that the file is an elf file. */
    if (elf_newFile(elf_base, elf_size, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        return -1;
    }

    printf("aaaaaaaaaaaa\n");
    /* set up the stack */
    seL4_Word sp = init_process_stack(cspace, seL4_CapInitThreadVSpace, &elf_file, coro);

    vaddr_t heap_start;

    printf("aaaaaaaaaaaa\n");
    /* load the elf image from the cpio file */
    err = elf_load(cspace, tty_test_process.vspace, &elf_file, tty_test_process.addrspace, &heap_start, coro);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    /* set up the heap */
    err = as_define_heap(tty_test_process.addrspace, heap_start);
    if (err) {
        ZF_LOGE("Failed to define heap region");
        return 0;
    }

    printf("aaaaaaaaaaaa\n");
    /* Map in the IPC buffer for the thread */
    err = sos_map_frame(tty_test_process.addrspace, cspace, tty_test_process.ipc_buffer.frame, PROCESS_IPC_BUFFER,
                    seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever, NULL, coro);
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return false;
    }

    printf("aaaaaaaaaaaa\n");
    fdtable_init(&tty_test_process.fdt);

    printf("aaaaaaaaaaaa\n");
    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(&elf_file),
        .sp = sp,
    };
    printf("Starting ttytest at %p\n", (void *) context.pc);
    err = seL4_TCB_WriteRegisters(tty_test_process.tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    return err == seL4_NoError;
}
