#pragma once

#include <sel4runtime.h>
#include "syscall.h"

DEFINE_SYSCALL(process_create);
DEFINE_SYSCALL(process_delete);
DEFINE_SYSCALL(my_id);
DEFINE_SYSCALL(process_status);
DEFINE_SYSCALL(process_wait);
