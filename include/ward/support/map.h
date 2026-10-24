/// Small string-keyed map used to look up test suites by group name.
///
/// Deliberately minimal: the framework only ever inserts a handful of entries
/// and looks them up by name, so a plain open-addressed table with linear
/// probing is enough and avoids vendoring the host project's hashtable macros.
#ifndef WARD_SUPPORT_MAP_H
#define WARD_SUPPORT_MAP_H

#include <stddef.h>

#include "ward/support/str.h"

struct WardMapEntry {
    WardStr key;
    void *value;
};

typedef struct WardMap {
    struct WardMapEntry *entries;
    size_t capacity;
    size_t size;
} WardMap;

/// `map` must be zero initialized.
void *ward_map_get(WardMap *map, WardStr key);
/// Inserts or overwrites. Takes ownership of nothing; `key` must outlive the map.
void ward_map_put(WardMap *map, WardStr key, void *value);
void ward_map_free(WardMap *map);

#endif // WARD_SUPPORT_MAP_H
