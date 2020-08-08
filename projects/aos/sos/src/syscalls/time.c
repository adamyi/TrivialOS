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

IMPLEMENT_SYSCALL(usleep, 1) {
    (void) proc;
    if (!is_clock_driver_ready()) {
        ZF_LOGE("Clock driver is not yet ready! (you bad bad, did you kill my driver? it's probably respawning now)");
        return return_word(-1);
    }
    //register_timer(seL4_GetMR(1) * 1000, usleep_callback, me);
    seL4_SetMR(0, seL4_GetMR(1) * 1000);
    seL4_SetMR(1, usleep_callback);
    seL4_SetMR(2, me);
    seL4_Send(timer_ep, seL4_MessageInfo_new(0, 0, 0, 3));
    yield(NULL);
    return return_word(0);
}

IMPLEMENT_SYSCALL(timer_callback, 3) {
    printf("haha\n");
    if (proc->pid != clock_driver_pid)
        return return_word(-1);
    printf("lmao\n");
    unsigned int id = seL4_GetMR(1);
    timer_callback_t cb = seL4_GetMR(2);
    void *data = seL4_GetMR(3);
    printf("ccb\n");
    cb(id, data);
    printf("cb\n");
    return return_word(0);
}

IMPLEMENT_SYSCALL(timer_ack, 0) {
    printf("haha\n");
    if (proc->pid != clock_driver_pid)
        return return_word(-1);
    printf("ack\n");
    seL4_IRQHandler_Ack(timer_irq_handler);
    return return_word(0);
}
