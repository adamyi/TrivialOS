#pragma once

#include <sel4runtime.h>
#include <cspace/cspace.h>
#include "../process.h"
#include "../ut.h"
#include "../coroutine/picoro.h"

#define SYSCALL_IMPL_(name) _syscall_##name##_impl

#define DEFINE_SYSCALL(syscall_name) \
    seL4_MessageInfo_t SYSCALL_IMPL_(syscall_name)(process_t *proc, coro_t me); \
    extern syscall_t syscall_##syscall_name; \
    extern int syscall_no_##syscall_name

#define IMPLEMENT_SYSCALL(syscall_name, syscall_args) \
    int syscall_no_##syscall_name = -1; \
    syscall_t syscall_##syscall_name = { \
        .name = #syscall_name, \
        .args = syscall_args, \
        .implementation = SYSCALL_IMPL_(syscall_name) \
    }; \
    seL4_MessageInfo_t SYSCALL_IMPL_(syscall_name)(process_t *proc, coro_t me)

#define START_INSTALLING_SYSCALLS() int sisline = __LINE__
#define INSTALL_SYSCALL(syscall_name) \
    syscall_no_##syscall_name = __LINE__ - sisline - 1; \
    syscalls[syscall_no_##syscall_name] = &syscall_##syscall_name; \
    printf("Registered syscall " #syscall_name " with code %d\n", syscall_no_##syscall_name)

void handle_syscall(cspace_t *cspace, seL4_Word badge, size_t num_args, seL4_CPtr reply, ut_t *reply_ut, process_t *proc);
void init_syscall();

typedef struct syscall {
    char *name;
    size_t args;
    seL4_MessageInfo_t (*implementation)(process_t *proc, coro_t me);
} syscall_t;

