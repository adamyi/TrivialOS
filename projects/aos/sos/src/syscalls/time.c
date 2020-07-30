#include <assert.h>
#include <sel4runtime.h>
#include <clock/clock.h>

#include "syscall.h"
#include "time.h"

#include "../coroutine/picoro.h"

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
    register_timer(seL4_GetMR(1) * 1000, usleep_callback, me);
    yield(NULL);
    return return_word(0);
}
