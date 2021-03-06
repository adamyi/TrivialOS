// Rolling Array Data Type
// written by Adam Yi <i@adamyi.com>
#include <stdbool.h>
#include <stddef.h>

#ifndef ROLLINGARRAY_H_
#define ROLLINGARRAY_H_

#define RA_UNKNOWN_ITEM (-1)

// idx refers to physical index in internal array
// ind refers to logical index in rolling array

typedef char ra_item_t;

typedef struct rollingarray {
    size_t capacity;
    ra_item_t *value;
    size_t start, size;
} rollingarray_t;

static inline size_t ra_ind2idx(rollingarray_t *ra, size_t ind) {
    ind += ra->start;
    if (ind >= ra->capacity) ind -= ra->capacity;
    return ind;
}

static inline size_t ra_idx2ind(rollingarray_t *ra, size_t idx) {
    if (idx < ra->start) return idx + ra->capacity - ra->start;
    return idx - ra->start;
}

static inline size_t ra_ind2idx_backwards(rollingarray_t *ra, size_t ind) {
    return ra_ind2idx(ra, ra->size - 1 - ind);
}


size_t ra_ind2idx(rollingarray_t *ra, size_t ind);
size_t ra_idx2ind(rollingarray_t *ra, size_t idx);
size_t ra_ind2idx_backwards(rollingarray_t *ra, size_t ind);

rollingarray_t *new_rollingarray(size_t capacity);
rollingarray_t *clone_rollingarray(rollingarray_t *old);
void destroy_rollingarray(rollingarray_t *ra);
ra_item_t rollingarray_get_item(rollingarray_t *ra, size_t ind);
ra_item_t rollingarray_last_item(rollingarray_t *ra);
ra_item_t rollingarray_get_item_backwards(rollingarray_t *ra, size_t ind);
bool rollingarray_add_item(rollingarray_t *ra, ra_item_t val);
void rollingarray_remove_first_item(rollingarray_t *ra);
size_t rollingarray_size(rollingarray_t *ra);
size_t rollingarray_to_array(rollingarray_t *ra, ra_item_t arr[],
                             bool reversed, size_t len);
bool rollingarray_has_item(rollingarray_t *ra, ra_item_t item);

#endif  // !defined (ROLLINGARRAY_H_)
