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
#include <clock/clock.h>
#include <stdbool.h>

/* The functions in src/device.h should help you interact with the timer
 * to set registers and configure timeouts. */
#include "device.h"
#include "utils/rolling_id.h"
#include "utils/priorityqueue.h"

#define ALLOWED_VARIANCE 10000

struct timer {
    uint32_t id;
    timer_callback_t callback;
    timestamp_t trigger_time;
    timestamp_t insert_time;
    void *data;
};

static struct {
    volatile meson_timer_reg_t *regs;
    /* Add fields as you see necessary */
    pqueue_t timer_queue;
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

    int res = compareTimeStamp(ta->trigger_time, tb->trigger_time);
    if (res != 0) return res;
    res = compareTimeStamp(ta->insert_time, tb->insert_time);
    if (res != 0) return res;
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

    pqueue_init(&(clock.timer_queue), compareTimer);
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
    struct timer *timer = (struct timer *) pqueue_peek(&(clock.timer_queue));
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

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data)
{
    int id = rid_get_id(&(clock.timer_ids));
    if (id < 0) return 0;

    struct timer *timer = timers + id;
    timer->callback = callback;
    timer->insert_time = get_time();
    timer->trigger_time = timer->insert_time + delay;
    timer->data = data;
    timer->id = id;

    pqueue_insert(&(clock.timer_queue), timer);

    update_timer();

    // printf("registered %u\n", id);

    return id;
}

int remove_timer(uint32_t id)
{
    pqueue_remove(&(clock.timer_queue), timers + id);
    rid_remove_id(&(clock.timer_ids), id);
    // TODO: do we need to free data?

    update_timer();

    return CLOCK_R_OK;
}

int timer_irq(
    void *data,
    seL4_Word irq,
    seL4_IRQHandler irq_handler
)
{
    // printf("irq at %lu\n", get_time());
    struct timer *timer = pqueue_peek(&(clock.timer_queue));
    timestamp_t c = get_time();
    int i = 0;
    while(timer != NULL && get_time() >= timer->trigger_time - ALLOWED_VARIANCE) {
        // printf("timer %p (%u), len %d\n", timer, timer->id, pqueue_length(&(clock.timer_queue)));
        pqueue_remove(&(clock.timer_queue), timer);
        timer->callback(timer->id, timer->data);
        rid_remove_id(&(clock.timer_ids), timer->id);
        timer = pqueue_peek(&(clock.timer_queue));
    }

    update_timer();

    /* Acknowledge that the IRQ has been handled */
    seL4_Error ack_err = seL4_IRQHandler_Ack(irq_handler);
    if (ack_err != seL4_NoError) return ack_err;

    // printf("irq done at %lu\n", get_time());

    return CLOCK_R_OK;
}

int stop_timer(void)
{
    /* Stop the timer from producing further interrupts and remove all
     * existing timeouts */
    if (timer_enabled) {
        configure_timeout(clock.regs, MESON_TIMER_A, false, false, 0, 0);
        configure_timestamp(clock.regs, TIMESTAMP_TIMEBASE_SYSTEM);
        pqueue_destroy(&(clock.timer_queue));
        rid_destroy(&(clock.timer_ids));
        timer_enabled = false;
    }

    return CLOCK_R_OK;
}
