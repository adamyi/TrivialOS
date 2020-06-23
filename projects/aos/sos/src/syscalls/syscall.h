#pragma once

#include <sel4runtime.h>
#include <sel4/sel4.h>
#include <cspace/cspace.h>
#include "../process.h"
#include "../ut.h"
#include "../coroutine/picoro.h"

#define SYSCALL_IMPL_(name) _syscall_##name##_impl

#define SYSCALL_PARAMS cspace_t *cspace, process_t *proc, coro_t me

#define DEFINE_SYSCALL(syscall_name) \
    seL4_MessageInfo_t SYSCALL_IMPL_(syscall_name)(SYSCALL_PARAMS); \
    extern syscall_t syscall_##syscall_name; \
    extern int syscall_no_##syscall_name

#define IMPLEMENT_SYSCALL(syscall_name, syscall_args) \
    int syscall_no_##syscall_name = -1; \
    syscall_t syscall_##syscall_name = { \
        .name = #syscall_name, \
        .args = syscall_args, \
        .implementation = SYSCALL_IMPL_(syscall_name) \
    }; \
    seL4_MessageInfo_t SYSCALL_IMPL_(syscall_name)(SYSCALL_PARAMS)

#define START_INSTALLING_SYSCALLS() int sisline = __LINE__
#define INSTALL_SYSCALL(syscall_name) \
    syscall_no_##syscall_name = __LINE__ - sisline - 1; \
    syscalls[syscall_no_##syscall_name] = &syscall_##syscall_name; \
    printf("Registered syscall " #syscall_name " with code %d\n", syscall_no_##syscall_name)

static inline seL4_MessageInfo_t return_word(seL4_Word word) {
    // printf("return_word %d\n", word);
    seL4_SetMR(0, word);
    return seL4_MessageInfo_new(0, 0, 0, 1);
}

static inline seL4_MessageInfo_t return_error() {
    return return_word(-1);
}

void handle_syscall(cspace_t *cspace, seL4_Word badge, size_t num_args, seL4_CPtr reply, ut_t *reply_ut, process_t *proc);
void init_syscall();

typedef struct syscall {
    char *name;
    size_t args;
    seL4_MessageInfo_t (*implementation)(SYSCALL_PARAMS);
} syscall_t;

