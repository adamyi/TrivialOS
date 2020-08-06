/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <stdlib.h>
#include <stdint.h>
#include <clock/device.h>
#include <sel4/sel4.h>
#include <stdbool.h>
#include <sos.h>

#include "clock.h"
#include "device.h"

#include "utils/rolling_id.h"
#include "utils/heap.h"

#define ALLOWED_VARIANCE 10000

struct timer {
    uint32_t id;
    timer_callback_t callback;
    timestamp_t trigger_time;
    timestamp_t insert_time;
    void *data1;
    void *data2;
};

static struct timer *timer_heap[MAX_TIMER_ID + 1];

static struct {
    volatile meson_timer_reg_t *regs;
    /* Add fields as you see necessary */
    heap_t timer_queue;
    rid_t timer_ids;
} clock;

static bool timer_enabled = false;

static int compareTimeStamp(timestamp_t a, timestamp_t b) {
    // i'm concerned huge difference in value for unsigned int
    // could cause issues for the following line
    // haha stupid if go brrrrrrr
    // return ta->timeout - tb->timeout;
    if (a < b)
        return -1;
    if (a == b)
        return 0;
    return 1;
}

static int compareTimer(void *a, void *b) {
    struct timer *ta = (struct timer *) a;
    struct timer *tb = (struct timer *) b;
    seL4_DebugPutChar('W');
    char lol[10];
    sprintf(lol, "%p", ta);
    for (int i = 0; i < strlen(lol); ++i) seL4_DebugPutChar(lol[i]);
    seL4_DebugPutChar('\n');
    sprintf(lol, "%p", tb);
    for (int i = 0; i < strlen(lol); ++i) seL4_DebugPutChar(lol[i]);
    seL4_DebugPutChar('\n');

    int res = compareTimeStamp(ta->trigger_time, tb->trigger_time);
    seL4_DebugPutChar('W');
    if (res != 0) return res;
    res = compareTimeStamp(ta->insert_time, tb->insert_time);
    seL4_DebugPutChar('W');
    if (res != 0) return res;
    seL4_DebugPutChar('W');
    return (int) ta->id - (int) tb->id;
}

static struct timer timers[MAX_TIMER_ID + 1];

int start_timer(unsigned char *timer_vaddr)
{
    int err = stop_timer();
    if (err != 0) {
        return err;
    }

    clock.regs = (meson_timer_reg_t *)(timer_vaddr + TIMER_REG_START);

    heap_init(&(clock.timer_queue), timer_heap, MAX_TIMER_ID, compareTimer);
    rid_init(&(clock.timer_ids), MAX_TIMER_ID, 1);

    configure_timestamp(clock.regs, TIMESTAMP_TIMEBASE_1_US);
    timer_enabled = true;

    return CLOCK_R_OK;
}

timestamp_t get_time(void) {
    return read_timestamp(clock.regs);
}

static void update_timer() {
    uint64_t t = get_time();
    struct timer *timer = (struct timer *) heap_peek(&(clock.timer_queue));
    if (timer == NULL) return;
    timestamp_t current_time = get_time();
    uint64_t remaining_time = timer->trigger_time > current_time ? timer->trigger_time - current_time : 0;
    timestamp_timebase_t timebase;

    if (remaining_time <= UINT16_MAX) {
        timebase = TIMEOUT_TIMEBASE_1_US;
        //printf("update timer: trigger %lu*1us from now\n", remaining_time);
    } else if (remaining_time <= 10 * UINT16_MAX) {
        remaining_time /= 10;
        timebase = TIMEOUT_TIMEBASE_10_US;
        //printf("update timer: trigger %lu*10us from now\n", remaining_time);
    } else if (remaining_time <= 100 * UINT16_MAX) {
        remaining_time /= 100;
        timebase = TIMEOUT_TIMEBASE_100_US;
        //printf("update timer: trigger %lu*100us from now\n", remaining_time);
    } else if (remaining_time <= 1000 * UINT16_MAX) {
        remaining_time /= 1000;
        timebase = TIMEOUT_TIMEBASE_1_MS;
        //printf("update timer: trigger %lu*1ms from now\n", remaining_time);
    } else {
        remaining_time = UINT16_MAX;
        timebase = TIMEOUT_TIMEBASE_1_MS;
        //printf("update timer: trigger %lu*1ms (max) from now\n", remaining_time);
    }

    configure_timeout(clock.regs, MESON_TIMER_A, true, false, timebase, remaining_time);
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data1, void *data2)
{
    int id = rid_get_id(&(clock.timer_ids));
    if (id < 0) return 0;

    struct timer *timer = timers + id;
    timer->callback = callback;
    timer->insert_time = get_time();
    timer->trigger_time = timer->insert_time + delay;
    timer->data1 = data1;
    timer->data2 = data2;
    timer->id = id;

    heap_insert(&(clock.timer_queue), timer);

    seL4_DebugPutChar('L');

    update_timer();

    seL4_DebugPutChar('L');

    // printf("registered %u\n", id);

    return id;
}

int remove_timer(uint32_t id)
{
    heap_remove(&(clock.timer_queue), timers + id);
    rid_remove_id(&(clock.timer_ids), id);
    // TODO: do we need to free data?

    update_timer();

    return CLOCK_R_OK;
}

int timer_irq() {
    // printf("irq at %lu\n", get_time());
    seL4_DebugPutChar('i');
    seL4_DebugPutChar('r');
    seL4_DebugPutChar('q');
    seL4_DebugPutChar('\n');
    struct timer *timer = heap_peek(&(clock.timer_queue));
    timestamp_t c = get_time();
    int i = 0;
    while(timer != NULL && get_time() >= timer->trigger_time - ALLOWED_VARIANCE) {
        // printf("timer %p (%u), len %d\n", timer, timer->id, heap_length(&(clock.timer_queue)));
        seL4_DebugPutChar('A');
        heap_remove(&(clock.timer_queue), timer);
        seL4_DebugPutChar('B');
        timer->callback(timer->id, timer->data1, timer->data2);
        seL4_DebugPutChar('C');
        rid_remove_id(&(clock.timer_ids), timer->id);
        seL4_DebugPutChar('D');
        timer = heap_peek(&(clock.timer_queue));
        seL4_DebugPutChar('E');
    }
    //seL4_SetMR(0, 17);
    seL4_DebugPutChar('i');
    seL4_DebugPutChar('r');
    seL4_DebugPutChar('q');
    seL4_DebugPutChar('f');
    seL4_DebugPutChar('\n');
    seL4_Send(2, seL4_MessageInfo_new(0, 0, 0, 0));
    update_timer();
    seL4_DebugPutChar('i');
    seL4_DebugPutChar('r');
    seL4_DebugPutChar('q');
    seL4_DebugPutChar('r');
    seL4_DebugPutChar('\n');
}

int stop_timer(void)
{
    /* Stop the timer from producing further interrupts and remove all
     * existing timeouts */
    if (timer_enabled) {
        configure_timeout(clock.regs, MESON_TIMER_A, false, false, 0, 0);
        configure_timestamp(clock.regs, TIMESTAMP_TIMEBASE_SYSTEM);
        heap_destroy(&(clock.timer_queue));
        rid_destroy(&(clock.timer_ids));
        timer_enabled = false;
    }

    return CLOCK_R_OK;
}

struct cb {
    seL4_Word callback;
    seL4_Word data;
};

void sleep_callback(unsigned int id, void *data1, void *data2) {
    //printf("sleep_callback\n");
    //printf("%d %d %d\n", id, cb->callback, cb->data);
    seL4_DebugPutChar('s');
    seL4_DebugPutChar('c');
    seL4_DebugPutChar('\n');
    //seL4_SetMR(0, 16);
    seL4_SetMR(0, id);
    seL4_SetMR(1, data1);
    seL4_SetMR(2, data2);
    seL4_Send(2, seL4_MessageInfo_new(0, 0, 0, 3));
    //printf("sleep_callback called\n");
    seL4_DebugPutChar('s');
    seL4_DebugPutChar('d');
    seL4_DebugPutChar('\n');
}

int main(void) {
    sosapi_init_syscall_table();
    printf("Clock driver starting...\n");
    start_timer((unsigned char *)0xC000000000);
    while(1) {
        seL4_DebugPutChar('l');
        seL4_DebugPutChar('o');
        seL4_DebugPutChar('o');
        seL4_DebugPutChar('p');
        seL4_DebugPutChar('\n');
        // printf("test\n");
        seL4_Word badge = 0;
        seL4_MessageInfo_t message = seL4_Recv(2, &badge, 3);
        // printf("received\n");
        seL4_DebugPutChar('r');
        seL4_DebugPutChar('c');
        seL4_DebugPutChar('v');
        seL4_DebugPutChar('0' + seL4_MessageInfo_get_length(message));
        seL4_DebugPutChar('\n');
        switch (seL4_MessageInfo_get_length(message)) {
            case 0:
            seL4_SetMR(0, get_time());
            seL4_Send(3, seL4_MessageInfo_new(0, 0, 0, 1));
            break;
            case 1:
            timer_irq();
            break;
            case 3:;
            register_timer(seL4_GetMR(0), sleep_callback, seL4_GetMR(1), seL4_GetMR(2));
            break;
        }
    }
    return 0;
}
