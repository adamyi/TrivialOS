#pragma once

#include <cspace/cspace.h>
#include "vfs/file.h"
#include "vfs/vfs.h"
#include "vm/addrspace.h"
#include "vm/pagetable.h"
#include "ut.h"
#include "vm/frame_table.h"

// #define TTY_NAME             "tty_test"
#define TTY_NAME             "sosh"
#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

/* max num of simultaneously-running processes
 * this can't be too large since we'll run out of sos stack space for coroutine. Increase stack space if you want to increase this. */
#define MAX_PROCS 20
/* max possible PID value.
   once we reach max, we start filling from 1 again (with a simple non-collision hashtable) */
#define MAX_PID 65535

#define N_NAME 32

#define PROC_FREE         0
#define PROC_RUNNING      1
#define PROC_BLOCKED      2
#define PROC_TO_BE_KILLED 3
#define PROC_CREATING     4

#define PID_TO_BADGE(pid) ((pid)+(100))
#define BADGE_TO_PID(bad) ((bad)-(100))

typedef int pid_t;

typedef struct {
    pid_t     pid;
    unsigned  size;            /* in pages */
    unsigned  stime;           /* start time in msec since booting */
    char      command[N_NAME]; /* Name of exectuable */
} sos_process_t;

typedef struct runqueue runqueue_t;

struct runqueue {
    runqueue_t *next;
    coro_t coro;
};

runqueue_t *global_exit_blocked;

struct process {
    pid_t pid;
    unsigned stime;
    char command[N_NAME];
    char state;

    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    pte_t ipc_buffer;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    seL4_CPtr kernel_ep;

    cspace_t cspace;
    addrspace_t *addrspace;

    runqueue_t *exit_blocked;
    void (*kill_hook)(void *data);
    void *kill_hook_data;

    coro_t paging_coro;

    fdtable_t fdt;

};

typedef seL4_Error (*proc_create_hook)(struct process *proc, coro_t coro);

extern process_t *currproc;
extern process_t oldprocs[];
extern process_t runprocs[];

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
extern char _cpio_archive_end[];
extern char __eh_frame_start[];

seL4_CPtr sched_ctrl_start;
seL4_CPtr sched_ctrl_end;

void process_init();

unsigned get_time();
bool is_clock_driver_ready();

void kill_process(process_t *proc, coro_t coro);
pid_t wait_for_process_exit(pid_t pid, process_t *me, coro_t coro);

bool start_first_process(cspace_t *cspace, char *app_name, seL4_CPtr _ipc_ep, seL4_CPtr _timer_ep);
pid_t start_process(cspace_t *cspace, char *app_name, proc_create_hook hook, bool pinned, coro_t coro);


process_t *get_process_by_pid(pid_t pid);
