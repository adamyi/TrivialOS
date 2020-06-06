/* Bog standard priority queue implementation. */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "utils/list.h"
#include "utils/priorityqueue.h"

static struct list_node *make_node(void *data)
{
    struct list_node *n = malloc(sizeof(*n));
    if (n != NULL) {
        n->data = data;
        n->next = NULL;
    }
    return n;
}

/* Create a new linked-list. Returns 0 on success. */
int pqueue_init(pqueue_t *pq, int (*compare)(void *a, void *b)) {
    if (pq == NULL) {
        return -EINVAL;
    }
    pq->list = malloc(sizeof(list_t));
    if (pq->list == NULL) {
        return -ENOMEM;
    }
    list_init(pq->list);
    pq->compare = compare;
    return 0;
}

/* Insert a value to the pqueue. Returns 0 on success. */
int pqueue_insert(pqueue_t *pq, void *data) {
    struct list_node *n = make_node(data);
    if (n == NULL)
        return -ENOMEM;
    struct list_node **curr = &(pq->list->head);
    while(*curr != NULL && pq->compare((*curr)->data, data) < 0) {
        curr = &((*curr)->next);
    }
    n->next = *curr;
    *curr = n;
    return 0;
}

/* Returns true if the given pqueue contains no elements. */
bool pqueue_is_empty(pqueue_t *pq) {
    return pq == NULL || list_is_empty(pq->list);
}

/* Returns true if the given element is in the pqueue. The third argument is a
 * comparator to determine pqueue element equality.
 */
bool pqueue_exists(pqueue_t *pq, void *data) {
    return pq != NULL && list_exists(pq->list, data, pq->compare);
}

/* Returns the number of elements in the pqueue. */
int pqueue_length(pqueue_t *pq) {
    return pq == NULL ? 0 : list_length(pq->list);
}

/* Returns the index of the given element in the pqueue or -1 if the element is
 * not found.
 */
int pqueue_index(pqueue_t *pq, void *data) {
    return pq == NULL ? -1 : list_index(pq->list, data, pq->compare);
}

/* Call the given function on every pqueue element. While traversing the pqueue, if
 * the caller's action ever returns non-zero the traversal is aborted and that
 * value is returned. If traversal completes, this function returns 0.
 */
int pqueue_foreach(pqueue_t *pq, int(*action)(void *, void *), void *token) {
    return pq == NULL ? 0 : list_foreach(pq->list, action, token);
}

/* Return the data of the head of the queue
 * Return NULL if the queue is NULL or empty
 */
void *pqueue_peek(pqueue_t *pq) {
    return (pq == NULL || pq->list->head == NULL) ? NULL : pq->list->head->data;
}

/* Remove the head of the queue and return it
 * Return NULL if the queue is NULL or empty
 */
void *pqueue_pop(pqueue_t *pq) {
    void *head_data = pqueue_peek(pq);
    if (head_data == NULL) return NULL;
    pqueue_remove(pq, head_data);
    return head_data;
}

/* Remove the given element from the pqueue. Returns non-zero if the element is
 * not found.
 */
int pqueue_remove(pqueue_t *pq, void *data) {
    return pq == NULL ? -1 : list_remove(pq->list, data, pq->compare);
}

/* Remove all elements from the pqueue. Returns 0 on success. */
int pqueue_remove_all(pqueue_t *pq) {
    return pq == NULL ? 0 : list_remove_all(pq->list);
}

/* Destroy the pqueue. The caller is expected to have previously removed all
 * elements of the pqueue. Returns 0 on success.
 */
int pqueue_destroy(pqueue_t *pq) {
    if (pq == NULL) return 0;
    list_destroy(pq->list);
    free(pq->list);
    return 0;
}
