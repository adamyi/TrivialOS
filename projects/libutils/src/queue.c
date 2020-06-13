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
#include <utils/list.h>

int queue_init(queue_t *q)
{
    return list_init(q);
}

int queue_enqueue(queue_t *q, void *data)
{
    return list_append(q, data);
}

void *queue_peek(queue_t *q) {
    return queue_is_empty(q) ? NULL : ((list_t *) q)->head->data;
}

bool queue_is_empty(queue_t *q)
{
    return list_is_empty(q);
}

int queue_length(queue_t *q)
{
    return list_length(q);
}

void *queue_dequeue(queue_t *q) {
    if (queue_is_empty(q)) return NULL;
    struct list_node *delete = ((list_t *) q)->head;
    ((list_t *) q)->head = delete->next;
    void *ret = delete->data;
    free(delete);
    return ret;
}

int queue_remove_all(queue_t *q)
{
    return list_remove_all(q);
}

int queue_destroy(queue_t *q)
{
    return list_destroy(q);
}

