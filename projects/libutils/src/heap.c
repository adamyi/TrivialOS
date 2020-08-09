/* Bog standard priority queue implementation. */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <sel4/sel4.h>

#include "utils/heap.h"

static void heap_fixup(heap_t *h, int idx) {
    while (idx > 1 && h->compare(h->tree[idx / 2], h->tree[idx]) > 0) {
        void *data = h->tree[idx / 2];
        h->tree[idx / 2] = h->tree[idx];
        h->tree[idx] = data;
        idx /= 2;
    }
}

static void heap_fixdown(heap_t *h, int idx) {
    while (idx * 2 <= h->size) {
        int i = idx * 2;
        if (i < h->size && h->compare(h->tree[i], h->tree[i + 1]) > 0) ++i;
        if (h->compare(h->tree[idx], h->tree[i]) <= 0) break;
        void *data = h->tree[idx];
        h->tree[idx] = h->tree[i];
        h->tree[i] = data;
        idx = i;
    }
}

/* Create a new linked-list. Returns 0 on success. */
int heap_init(heap_t *h, void **tree, int size, int (*compare)(void *a, void *b)) {
    if (h == NULL) {
        return -EINVAL;
    }
    h->tree = tree;
    h->size = 0;
    h->capacity = size;
    h->compare = compare;
    return 0;
}

/* Insert a value to the heap. Returns 0 on success. */
int heap_insert(heap_t *h, void *data) {
    if (h->size == h->capacity) return -1;
    h->tree[++(h->size)] = data;
    heap_fixup(h, h->size);
    return 0;
}

/* Returns true if the given heap contains no elements. */
bool heap_is_empty(heap_t *h) {
    return h == NULL || h->size == 0;
}

/* Returns the number of elements in the heap. */
int heap_length(heap_t *h) {
    return h == NULL ? 0 : h->size;
}

/* Return the data of the head of the queue
 * Return NULL if the queue is NULL or empty
 */
void *heap_peek(heap_t *h) {
    return (h == NULL || h->size == 0) ? NULL : h->tree[1];
}

/* Remove the head of the queue and return it
 * Return NULL if the queue is NULL or empty
 */
void *heap_pop(heap_t *h) {
    if (h == NULL || h->size == 0) return NULL;
    if (h->size == 1) {
        h->size = 0;
        return h->tree[1];
    }
    void *ret = h->tree[1];
    h->tree[1] = h->tree[(h->size)--];
    heap_fixdown(h, 1);
    return ret;
}

void heap_remove(heap_t *h, void *data) {
    for (int i = 1; i <= h->size; ++i) {
        if (h->tree[i] == data) {
            h->tree[i] = h->tree[(h->size)--];
            heap_fixdown(h, 1);
            break;
        }
    }
}

void heap_destroy(heap_t *h) {
    h->size = 0;
}
