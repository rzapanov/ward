#include <string.h>

#include "ward/support/str.h"

WardStr ward_str_c(char const *text) {
    if (!text) {
        return (WardStr) { .text = "", .length = 0 };
    }
    return (WardStr) { .text = text, .length = strlen(text) };
}

WardStr ward_str(char const *text, size_t length) {
    return (WardStr) { .text = text, .length = length };
}

bool ward_str_eq(WardStr f, WardStr s) {
    if (f.length != s.length) {
        return false;
    }
    if (f.length == 0) {
        return true;
    }
    return memcmp(f.text, s.text, f.length) == 0;
}

bool ward_cstr_eq(char const *a, char const *b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return strcmp(a, b) == 0;
}

size_t ward_str_hash(WardStr str) {
    // FNV-1a
    size_t hash = (size_t) 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < str.length; i++) {
        hash ^= (unsigned char) str.text[i];
        hash *= (size_t) 0x100000001b3ULL;
    }
    return hash;
}

bool ward_str_split(WardStr haystack, WardStr needle, WardStr *left, WardStr *right) {
    if (needle.length == 0) {
        return false;
    }
    if (needle.length > haystack.length) {
        return false;
    }
    size_t last = haystack.length - needle.length;
    for (size_t i = 0; i <= last; i++) {
        if (memcmp(haystack.text + i, needle.text, needle.length) == 0) {
            *left = ward_str(haystack.text, i);
            *right = ward_str(haystack.text + i + needle.length, haystack.length - i - needle.length);
            return true;
        }
    }
    return false;
}
