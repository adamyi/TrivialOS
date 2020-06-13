// Rolling Array Data Type
// written by Adam Yi <i@adamyi.com>

#include "utils/rollingarray.h"
#include <assert.h>
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

// idx refers to physical index in internal array
// ind refers to logical index in rolling array

size_t ra_ind2idx(rollingarray_t *ra, size_t ind) {
    ind += ra->start;
    if (ind >= ra->capacity) ind -= ra->capacity;
    return ind;
}

size_t ra_idx2ind(rollingarray_t *ra, size_t idx) {
    if (idx < ra->start) return idx + ra->capacity - ra->start;
    return idx - ra->start;
}

size_t ra_ind2idx_backwards(rollingarray_t *ra, size_t ind) {
    return ra_ind2idx(ra, ra->size - 1 - ind);
}

rollingarray_t *new_rollingarray(size_t capacity) {
    rollingarray_t *new = malloc(sizeof(rollingarray_t));
    if (new == NULL) return NULL;
    new->capacity = capacity;
    new->value = malloc(capacity * sizeof(ra_item_t));
    if (new->value == NULL) {
        free(new);
        return NULL;
    }
    memset(new->value, 0, capacity);
    new->size = new->start = 0;
    return new;
}

rollingarray_t *clone_rollingarray(rollingarray_t *old) {
    rollingarray_t *new = malloc(sizeof(rollingarray_t));
    new->capacity = old->capacity;
    new->size = old->size;
    new->start = old->start;
    new->value = malloc(new->capacity * sizeof(ra_item_t));
    memcpy(new->value, old->value, new->capacity * sizeof(ra_item_t));
    return new;
}

void destroy_rollingarray(rollingarray_t *ra) {
    free(ra->value);
    free(ra);
}

ra_item_t rollingarray_get_item(rollingarray_t *ra, size_t ind) {
    if (ind >= ra->size) return RA_UNKNOWN_ITEM;
    return ra->value[ra_ind2idx(ra, ind)];
}

ra_item_t rollingarray_last_item(rollingarray_t *ra) {
    return rollingarray_get_item_backwards(ra, 0);
}

ra_item_t rollingarray_get_item_backwards(rollingarray_t *ra, size_t ind) {
    if (ind >= ra->size) return RA_UNKNOWN_ITEM;
    return ra->value[ra_ind2idx_backwards(ra, ind)];
}

bool rollingarray_add_item(rollingarray_t *ra, ra_item_t val) {
    if (ra->size == ra->capacity) return false;
    ra->value[ra_ind2idx(ra, ra->size++)] = val;
    return true;
}

void rollingarray_remove_first_item(rollingarray_t *ra) {
    if (ra->size == 0) return;
    ra->start++;
    ra->size--;
    if (ra->start == ra->capacity) ra->start = 0;
}

size_t rollingarray_size(rollingarray_t *ra) { return ra->size; }

size_t rollingarray_to_array(rollingarray_t *ra, ra_item_t arr[], bool reversed, size_t len) {
    if (ra->size < len) len = ra->size;
    if (reversed) {
        for (int i = (int)ra->start, j = len - 1; j >= 0;
                 i = (i + 1 == ra->capacity ? 0 : i + 1), j--)
            arr[j] = ra->value[i];
    } else {
        for (int i = (int)ra->start, j = 0; j < len;
                 i = (i + 1 == (int)ra->capacity ? 0 : i + 1), j++)
            arr[j] = ra->value[i];
    }
    for (int i = ra->size; i < ra->capacity; i++) arr[i] = RA_UNKNOWN_ITEM;
    return len;
}

bool rollingarray_has_item(rollingarray_t *ra, ra_item_t item) {
    size_t endidx = (ra->size < ra->capacity) ? ra->size : ra->capacity;
    for (size_t i = 0; i < endidx; i++) {
        if (ra->value[i] == item) return true;
    }
    return false;
}
