#pragma once

#include <cspace/cspace.h>
#include "ut.h"

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
} process_t;

seL4_Word get_new_shared_buffer_vaddr();
