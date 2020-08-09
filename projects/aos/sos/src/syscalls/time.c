#include <assert.h>
#include <sel4runtime.h>
//#include <clock/clock.h>

#include "syscall.h"
#include "time.h"

#include "../coroutine/picoro.h"
#include "../process.h"

extern seL4_CPtr timer_ep;
extern seL4_IRQHandler *timer_irq_handler;
extern pid_t clock_driver_pid;

typedef void (*timer_callback_t)(uint32_t id, void *data);

IMPLEMENT_SYSCALL(time_stamp, 0) {
    (void) proc;
    (void) me;
    return return_word(get_time());
}

void usleep_callback(unsigned int id, void *data) {
    (void) id;
    resume((coro_t) data, NULL);
}

struct usleep_kill_hook_data {
    coro_t coro;
    seL4_Word timeoutid;
};

void usleep_kill_hook(void *data) {
    struct usleep_kill_hook_data *hd = data;
    ZF_LOGD("removing timeout %ld", hd->timeoutid);
    if (is_clock_driver_ready()) {
        seL4_SetMR(0, hd->timeoutid);
        seL4_Call(timer_ep, seL4_MessageInfo_new(0, 0, 0, 2));
    } else {
        ZF_LOGE("tried to remove timeout but it looks like clock driver died");
    }
    resume(hd->coro, NULL);
}

IMPLEMENT_SYSCALL(usleep, 1) {
    if (!is_clock_driver_ready()) {
        ZF_LOGE("Clock driver is not yet ready! (you bad bad, did you kill my driver? it's probably respawning now)");
        return return_word(-1);
    }
    //register_timer(seL4_GetMR(1) * 1000, usleep_callback, me);
    seL4_SetMR(0, seL4_GetMR(1) * 1000);
    seL4_SetMR(1, usleep_callback);
    seL4_SetMR(2, me);
    seL4_Call(timer_ep, seL4_MessageInfo_new(0, 0, 0, 3));
    seL4_Word tid = seL4_GetMR(0);
    if (tid == 0) {
        ZF_LOGE("register timeout failed - driver returned 0");
        return return_word(-1);
    }
    struct usleep_kill_hook_data d = {
        .timeoutid = tid,
        .coro = me
    };
    proc->kill_hook = usleep_kill_hook;
    proc->kill_hook_data = &d;
    yield(NULL);
    proc->kill_hook = proc->kill_hook_data = NULL;
    return return_word(0);
}

IMPLEMENT_SYSCALL(timer_callback, 3) {
    if (proc->pid != clock_driver_pid)
        return return_word(-1);
    unsigned int id = seL4_GetMR(1);
    timer_callback_t cb = seL4_GetMR(2);
    void *data = seL4_GetMR(3);
    ZF_LOGD("Calling timer callback");
    cb(id, data);
    return return_word(0);
}

IMPLEMENT_SYSCALL(timer_ack, 0) {
    if (proc->pid != clock_driver_pid)
        return return_word(-1);
    seL4_IRQHandler_Ack(timer_irq_handler);
    return return_word(0);
}
