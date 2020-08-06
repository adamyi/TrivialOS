#include <assert.h>
#include <sel4runtime.h>
//#include <clock/clock.h>

#include "syscall.h"
#include "time.h"

#include "../coroutine/picoro.h"

static coro_t timestampcoro = NULL;
static coro_t drivercoro = NULL;

extern seL4_CPtr timer_ep;
extern seL4_IRQHandler *timer_irq_handler;

typedef void (*timer_callback_t)(uint32_t id, void *data);

IMPLEMENT_SYSCALL(time_stamp, 0) {
    (void) proc;
    (void) me;
    // can't read (terrible) doc, dunno how to use seL4_Call
    seL4_Call(timer_ep, seL4_MessageInfo_new(0, 0, 0, 0));
    //seL4_Word badge = 0;
    //seL4_Recv(timer_ep, &badge);
    return return_word(seL4_GetMR(0));
}

void usleep_callback(unsigned int id, void *data) {
    (void) id;
    resume((coro_t) data, NULL);
}

IMPLEMENT_SYSCALL(usleep, 1) {
    (void) proc;
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
    if (strcmp(proc->command, "clock_driver") != 0)
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
    if (strcmp(proc->command, "clock_driver") != 0)
        return return_word(-1);
    printf("ack\n");
    seL4_IRQHandler_Ack(timer_irq_handler);
    return return_word(0);
}
