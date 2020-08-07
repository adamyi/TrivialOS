#pragma once

#include "../ut.h"
#include "addrspace.h"
#include "../process.h"
#include "../coroutine/picoro.h"
#include <cspace/cspace.h>
#include <sel4/sel4.h>

bool ensure_mapping(cspace_t *cspace, void *vaddr, process_t *proc, addrspace_t *as, coro_t coro);
void handle_vm_fault(cspace_t *cspace, void *vaddr, seL4_Word type, process_t *curr, seL4_CPtr reply, ut_t *reply_ut);
void handle_fault_kill(process_t *proc);
