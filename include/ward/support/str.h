/// Vendored string view used by the framework internals and assertion formatting.
///
/// This is a private copy, deliberately prefixed with `ward_` so that the
/// framework can be linked next to a host project that ships its own string
/// helpers without symbol collisions.
#ifndef WARD_SUPPORT_STR_H
#define WARD_SUPPORT_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WardStr {
    char const *text;
    size_t length;
} WardStr;

#define WARD_STR(lit) ((WardStr) { .text = (lit), .length = sizeof(lit) - 1 })

/// Empty, non-owning view of a NUL terminated string. `NULL` becomes an empty view.
WardStr ward_str_c(char const *text);
WardStr ward_str(char const *text, size_t length);

/// Content equality. Also used as the equality predicate of the internal maps.
bool ward_str_eq(WardStr f, WardStr s);

/// Compares the *contents* of two NUL terminated strings.
/// Unlike `ward_str_eq` this accepts plain `char const *` and treats `NULL`
/// as distinct from "".
bool ward_cstr_eq(char const *a, char const *b);

size_t ward_str_hash(WardStr str);

/// Splits `haystack` at the first occurrence of `needle`.
bool ward_str_split(WardStr haystack, WardStr needle, WardStr *left, WardStr *right);

#endif // WARD_SUPPORT_STR_H
