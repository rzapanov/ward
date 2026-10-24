#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ward/support/arena.h"
#include "ward/support/fatal.h"

void *ward_alloc(size_t size) {
    void *result = malloc(size ? size : 1);
    if (!result) {
        WARD_FATAL("out of memory while allocating %zu bytes", size);
    }
    return result;
}

void *ward_realloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size ? size : 1);
    if (!result) {
        WARD_FATAL("out of memory while reallocating %zu bytes", size);
    }
    return result;
}

static void *ward_alloc_blob(WardArena *arena, size_t size) {
    size_t blob_size = sizeof(struct WardArenaBlob);
    uint8_t *memory = ward_alloc(blob_size + size);
    struct WardArenaBlob *blob = (struct WardArenaBlob *) memory;
    blob->next = arena->blob_link;
    arena->blob_link = blob;
    return memory + blob_size;
}

void *ward_arena_alloc(WardArena *arena, size_t size) {
    size = (size + 15) & ~(size_t) 15;
    if ((size_t) (arena->current_end - arena->current_cursor) < size) {
        size_t blob_size = size > WARD_ARENA_MAX_ELEM_SIZE ? size : WARD_ARENA_BLOB_SIZE;
        uint8_t *memory = ward_alloc_blob(arena, blob_size);
        arena->current_cursor = memory;
        arena->current_end = memory + blob_size;
    }
    uint8_t *result = arena->current_cursor;
    arena->current_cursor = result + size;
    return result;
}

void ward_arena_free(WardArena *arena) {
    struct WardArenaBlob *blob = arena->blob_link;
    while (blob) {
        struct WardArenaBlob *next = blob->next;
        free(blob);
        blob = next;
    }
    *arena = (WardArena) {0};
}
