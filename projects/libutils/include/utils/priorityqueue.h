/* Bog standard priority queue implementation. */

#pragma once

#include <stdbool.h>

#include "utils/list.h"


typedef struct {
    list_t *list;
    int (*compare)(void *a, void *b);
} pqueue_t;

/* Create a new linked-pqueue. Returns 0 on success. */
int pqueue_init(pqueue_t *pq, int (*compare)(void *, void *));

/* Append a value to the pqueue. Returns 0 on success. */
int pqueue_insert(pqueue_t *pq, void *data);

/* Returns true if the given pqueue contains no elements. */
bool pqueue_is_empty(pqueue_t *pq);

/* Returns true if the given element is in the pqueue. The third argument is a
 * comparator to determine pqueue element equality.
 */
bool pqueue_exists(pqueue_t *pq, void *data);

/* Returns the number of elements in the pqueue. */
int pqueue_length(pqueue_t *pq);

/* Returns the index of the given element in the pqueue or -1 if the element is
 * not found.
 */
int pqueue_index(pqueue_t *pq, void *data);

/* Call the given function on every pqueue element. While traversing the pqueue, if
 * the caller's action ever returns non-zero the traversal is aborted and that
 * value is returned. If traversal completes, this function returns 0.
 */
int pqueue_foreach(pqueue_t *pq, int(*action)(void *, void *), void *token);

/* Return the data of the head of the queue
 * Return NULL if the queue is NULL or empty
 */
void *pqueue_peek(pqueue_t *pq);

/* Remove the head of the queue and return it
 * Return NULL if the queue is NULL or empty
 */
void *pqueue_pop(pqueue_t *pq);

/* Remove the given element from the pqueue. Returns non-zero if the element is
 * not found.
 */
int pqueue_remove(pqueue_t *pq, void *data);

/* Remove all elements from the pqueue. Returns 0 on success. */
int pqueue_remove_all(pqueue_t *pq);

/* Destroy the pqueue. The caller is expected to have previously removed all
 * elements of the pqueue. Returns 0 on success.
 */
int pqueue_destroy(pqueue_t *pq);
