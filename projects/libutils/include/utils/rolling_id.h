#pragma once

#include <stdbool.h>
#include <stdlib.h>

typedef struct {
    bool *inused;
    size_t size;
    size_t used_count;
    int next_id;
    int start_id;
} rid_t;

int rid_init(rid_t *rid, bool *inused, size_t size, int start_index);

/* Return -1 if all ids are in used
 * Return free id on success
 */
int rid_get_id(rid_t *rid);

int rid_remove_id(rid_t *rid, int id);

bool rid_is_inused(rid_t *rid, int id);

bool rid_is_full(rid_t *rid);

void rid_remove_all(rid_t *rid);

int rid_destroy(rid_t *rid);
