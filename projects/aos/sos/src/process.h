#pragma once

#include <cspace/cspace.h>
#include "vfs/file.h"
#include "vfs/vfs.h"
#include "ut.h"

// #define TTY_NAME             "tty_test"
#define TTY_NAME             "sosh"
#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

/* The number of additional stack pages to provide to the initial
 * process */
#define INITIAL_PROCESS_EXTRA_STACK_PAGES 4

typedef struct process {
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;

    ut_t *shared_buffer_ut;
    seL4_CPtr shared_buffer;
    seL4_Word shared_buffer_vaddr;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    cspace_t cspace;

    ut_t *stack_ut;
    seL4_CPtr stack;

    vnode_t *cwd;
    fdtable_t fdt;

} process_t;

/* the one process we start */
process_t tty_test_process;

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
extern char _cpio_archive_end[];
extern char __eh_frame_start[];

seL4_CPtr sched_ctrl_start;
seL4_CPtr sched_ctrl_end;

seL4_Word get_new_shared_buffer_vaddr();
bool start_first_process(cspace_t *cspace, char *app_name, seL4_CPtr ep);
