/* Bog standard priority queue implementation. */

#pragma once

#include <stdbool.h>


typedef struct {
    int size;
    int capacity;
    void **tree;
    int (*compare)(void *a, void *b);
} heap_t;

/* Create a new linked-heap. Returns 0 on success. */
int heap_init(heap_t *h, void **tree, int size, int (*compare)(void *, void *));

/* Append a value to the heap. Returns 0 on success. */
int heap_insert(heap_t *h, void *data);

/* Returns true if the given heap contains no elements. */
bool heap_is_empty(heap_t *h);

/* Returns the number of elements in the heap. */
int heap_length(heap_t *h);

/* Return the data of the head of the queue
 * Return NULL if the queue is NULL or empty
 */
void *heap_peek(heap_t *h);

/* Remove the head of the queue and return it
 * Return NULL if the queue is NULL or empty
 */
void *heap_pop(heap_t *h);

void heap_destroy(heap_t *h);
