#pragma once

#include <stdbool.h>

typedef struct queue_node {
    void *data;
    struct queue_node *next;
} queue_node_t;

typedef struct {
    queue_node_t *head;
    queue_node_t *tail;
    size_t len;
} queue_t;


/* Create a new linked-queue. Returns 0 on success. */
int queue_init(queue_t *q);

/* Append a value to the queue. Returns 0 on success. */
int queue_enqueue(queue_t *q, void *data);

/* Returns true if the given queue contains no elements. */
bool queue_is_empty(queue_t *q);

/* Return the data of the head of the queue */
void *queue_peek(queue_t *q);

/* Returns the number of elements in the queue. */
int queue_length(queue_t *q);

/* Remove all elements from the queue. Returns 0 on success. */
int queue_remove_all(queue_t *q);

/* Pop head of the queue and return its data */
void *queue_dequeue(queue_t *q);

/* Destroy the queue. The caller is expected to have previously removed all
 * elements of the queue. Returns 0 on success.
 */
int queue_destroy(queue_t *q);
