#pragma once

#include "../ut.h"
#include "addrspace.h"
#include "../process.h"
#include <cspace/cspace.h>
#include <sel4/sel4.h>

void handle_vm_fault(cspace_t *cspace, void *vaddr, seL4_Word type, process_t *curr, seL4_CPtr reply, ut_t *reply_ut);
