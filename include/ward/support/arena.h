/// Bump allocator used for everything the framework allocates while it
/// discovers and reports tests. Individual allocations are never freed; the
/// whole arena is released at once.
#ifndef WARD_SUPPORT_ARENA_H
#define WARD_SUPPORT_ARENA_H

#include <stddef.h>
#include <stdint.h>

#define WARD_ARENA_BLOB_SIZE 4096
#define WARD_ARENA_MAX_ELEM_SIZE 256

struct WardArenaBlob {
    struct WardArenaBlob *next;
};

typedef struct WardArena {
    struct WardArenaBlob *blob_link;
    uint8_t *current_end;
    uint8_t *current_cursor;
} WardArena;

/// Aborts on out of memory, so callers never have to check for NULL.
void *ward_alloc(size_t size);
void *ward_realloc(void *ptr, size_t size);

/// Zero initialized arena (`WardArena a = {0}`) is ready to use.
/// Allocations larger than WARD_ARENA_MAX_ELEM_SIZE get their own blob.
void *ward_arena_alloc(WardArena *arena, size_t size);
void ward_arena_free(WardArena *arena);

#define ward_new(arena, T) ((T*) ward_arena_alloc((arena), sizeof(T)))
#define ward_new_arr(arena, cnt, T) ((T*) ward_arena_alloc((arena), (cnt) * sizeof(T)))

#endif // WARD_SUPPORT_ARENA_H
