/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <utils/queue.h>

int queue_init(queue_t *q)
{
    assert(q != NULL);
    q->head = q->tail = NULL;
    q->len = 0;
    return 0;
}

static queue_node_t *new_node(void *data)
{
    queue_node_t *n = malloc(sizeof(queue_node_t));
    if (n != NULL) {
        n->next = NULL;
        n->data = data;
    }
    return n;
}

int queue_enqueue(queue_t *q, void *data)
{
    if (q == NULL) return 1;
    queue_node_t *n = new_node(data);
    if (n == NULL) return 1;
    if (q->tail == NULL) {
        q->head = q->tail = n;
    } else {
        q->tail->next = n;
        q->tail = n;
    }
    q->len++;
    return 0;
}

void *queue_peek(queue_t *q) {
    return queue_is_empty(q) ? NULL : q->head->data;
}

bool queue_is_empty(queue_t *q)
{
    return q == NULL || q->len == 0;
}

int queue_length(queue_t *q)
{
    return q->len;
}

void *queue_dequeue(queue_t *q) {
    if (queue_is_empty(q)) return NULL;
    q->len--;
    queue_node_t *old = q->head;
    q->head = q->head->next;
    if (q->head == NULL) q->tail = NULL;
    void *ret = old->data;
    free(old);
    return ret;
}

int queue_remove_all(queue_t *q)
{
    while (queue_dequeue(q));
    return 0;
}

int queue_destroy(queue_t *q)
{
    /* Basically do nothing */
    return 0;
}

