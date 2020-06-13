#pragma once

#include <sel4runtime.h>
#include "syscall.h"

DEFINE_SYSCALL(open);
DEFINE_SYSCALL(close);
DEFINE_SYSCALL(read);
DEFINE_SYSCALL(write);
