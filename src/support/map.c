#include <stdlib.h>
#include <string.h>

#include "ward/support/arena.h"
#include "ward/support/map.h"

static size_t ward_map_capacity(size_t capacity) {
    size_t result = 16;
    while (result < capacity) {
        result <<= 1;
    }
    return result;
}

static struct WardMapEntry *ward_map_slot(WardMap *map, WardStr key) {
    size_t mask = map->capacity - 1;
    size_t index = ward_str_hash(key) & mask;
    for (;;) {
        struct WardMapEntry *entry = &map->entries[index];
        if (entry->value == NULL || ward_str_eq(entry->key, key)) {
            return entry;
        }
        index = (index + 1) & mask;
    }
}

static void ward_map_grow(WardMap *map) {
    size_t new_capacity = ward_map_capacity((map->size + 1) * 2);
    struct WardMapEntry *entries = ward_alloc(new_capacity * sizeof(struct WardMapEntry));
    memset(entries, 0, new_capacity * sizeof(struct WardMapEntry));

    size_t old_capacity = map->capacity;
    struct WardMapEntry *old = map->entries;

    map->entries = entries;
    map->capacity = new_capacity;
    map->size = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        if (old[i].value) {
            ward_map_put(map, old[i].key, old[i].value);
        }
    }
    free(old);
}

void *ward_map_get(WardMap *map, WardStr key) {
    if (map->size == 0) {
        return NULL;
    }
    struct WardMapEntry *entry = ward_map_slot(map, key);
    return entry->value;
}

void ward_map_put(WardMap *map, WardStr key, void *value) {
    if ((map->size + 1) * 4 >= map->capacity * 3) {
        ward_map_grow(map);
    }
    struct WardMapEntry *entry = ward_map_slot(map, key);
    if (entry->value == NULL) {
        map->size++;
    }
    entry->key = key;
    entry->value = value;
}

void ward_map_free(WardMap *map) {
    free(map->entries);
    *map = (WardMap) {0};
}
