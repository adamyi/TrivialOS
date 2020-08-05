#pragma once

#include <sel4runtime.h>
#include "syscall.h"

DEFINE_SYSCALL(brk);
DEFINE_SYSCALL(mmap);
DEFINE_SYSCALL(munmap);
