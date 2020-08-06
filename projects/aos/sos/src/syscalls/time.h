#pragma once

#include <sel4runtime.h>
#include "syscall.h"

DEFINE_SYSCALL(usleep);
DEFINE_SYSCALL(time_stamp);
DEFINE_SYSCALL(timer_callback);
DEFINE_SYSCALL(timer_ack);
