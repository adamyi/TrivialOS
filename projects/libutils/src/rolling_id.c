#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "utils/rolling_id.h"

 // NOTE: NOT THREAD SAFE 



/* TODO: Use char array for better efficiency (space) */
/* inused needs to be of size (size * sizeof(bool)), caller's responsibility to find a space */
int rid_init(rid_t *rid, bool *inused, size_t size, int start_index) {
    if (rid == NULL) {
        return -EINVAL;
    }
    rid->inused = inused; // malloc(size * sizeof(bool));
    if (rid->inused == NULL) {
        return -ENOMEM;
    }
    memset(rid->inused, 0, size);
    rid->size = size;
    rid->used_count = 0;
    rid->next_id = 0;
    rid->start_id = start_index;
    return 0;
}

/* Return -1 if all ids are in used
 * Return free id on success
 */
int rid_get_id(rid_t *rid) {
    if (rid == NULL || rid_is_full(rid)) {
        return -1;
    }
    int i = rid->next_id;
    while (rid->inused[i]) {
        if (++i == rid->size) i = 0;
    }
    rid->inused[i] = true;
    rid->used_count++;
    rid->next_id = i + 1;
    if (rid->next_id == rid->size) rid->next_id = 0;
    return i + rid->start_id;
}

static bool _rid_is_inused(rid_t *rid, int id) {
    return (rid == NULL || id >= rid->size || id < 0 || rid->inused[id]);
}

bool rid_is_inused(rid_t *rid, int id) {
    return _rid_is_inused(rid, id - rid->start_id);
}

int rid_remove_id(rid_t *rid, int id) {
    id -= rid->start_id;
    if (rid == NULL || !_rid_is_inused(rid, id)) return -1;
    rid->inused[id] = false;
    rid->used_count--;
    return 0;
}

bool rid_is_full(rid_t *rid) {
    return rid == NULL || rid->used_count == rid->size;
}

void rid_remove_all(rid_t *rid) {
    if (rid != NULL) {
        memset(rid->inused, 0, rid->size);
        rid->used_count = 0;
    }
}

/* Expected to call rid_remove_all before */
/* Caller's responsibility to free rid->inused since they passed it in */
int rid_destroy(rid_t *rid) {
    if (rid == NULL) return 0;
    // free(rid->inused);
    rid->inused = NULL;
    rid->size = rid->used_count = 0;
    return 0;
}
