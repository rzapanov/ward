/// Public API of the ward unit test framework.
///
/// A test binary links this library and gets `main()` for free: tests are
/// collected at link time from the `ward_test_desc` ELF section, so no
/// registration call is needed anywhere.
///
///   #include <ward/ward.h>
///
///   TestSuite(db, .init = &setup, .fini = &teardown);
///
///   Test(db, add_and_query) {
///       ward_assert_eq(1, 1);
///   }
#ifndef WARD_WARD_H
#define WARD_WARD_H

#include <stdbool.h>
#include <stddef.h>

#include "ward/internal/api.h"

/// Attributes used to place descriptors into the collection section.
///
/// The section name deliberately has no leading dot: GNU ld and lld generate
/// `__start_ward_test_desc` / `__stop_ward_test_desc` symbols for sections
/// whose name is a valid C identifier, which is how tests are discovered.
#define WARD_ATTR(x) __attribute__((x))
#define WARD_SECTION(name) WARD_ATTR(section(name))
#define WARD_VA_EXPAND(x) x
#define WARD_CONS_STRUCT(...) { __VA_ARGS__ }

/// Declares a test case. `group` selects the suite (init/fini) it belongs to.
///
/// Extra designated initializers are accepted for per-test options:
///   Test(db, slow_thing, .timeout_seconds = 120) { ... }
///   Test(db, not_implemented, .ignored = true) { ... }
#define Test(...) WARD_VA_EXPAND(WARD_TEST_(__VA_ARGS__, WARD_TEST_NOTHING()))

#define WARD_TEST_NOTHING()

#define WARD_TEST_(ward_g, ward_n, ...)                                                      \
    WARD_TEST_DECL(ward_g, ward_n);                                                           \
    WARD_ATTR(used) WARD_SECTION("ward_test_desc") WARD_ATTR(aligned(sizeof(void *)))         \
    static const struct WardTestDesc WARD_TEST_SYM(ward_g, ward_n, desc) =                     \
        WARD_VA_EXPAND(WARD_CONS_STRUCT(                                                       \
            .group = #ward_g,                                                                  \
            .name = #ward_n,                                                                   \
            .fn = &WARD_TEST_SYM(ward_g, ward_n, fn),                                          \
            .file = __FILE__,                                                                  \
            .line = __LINE__,                                                                  \
            __VA_ARGS__                                                                        \
        ));                                                                                    \
    WARD_TEST_PROTO(ward_g, ward_n)

/// The test function is emitted with external linkage and an assembler label of
/// `ward_<group>_<name>` so its symbol survives in the dynamic symbol table and a
/// crash backtrace (`backtrace_symbols_fd`) can name the frame. `used` keeps the
/// compiler from dropping it. The label is set on the declaration only; the
/// definition below inherits it.
#define WARD_TEST_DECL(ward_g, ward_n)                                                        \
    WARD_ATTR(used) void WARD_TEST_SYM(ward_g, ward_n, fn)(void)                              \
        __asm__(WARD_TEST_SYMNAME(ward_g, ward_n))
#define WARD_TEST_PROTO(ward_g, ward_n) void WARD_TEST_SYM(ward_g, ward_n, fn)(void)
#define WARD_TEST_SYM(ward_g, ward_n, suffix) ward_test_##ward_g##_##ward_n##_##suffix
#define WARD_TEST_SYMNAME(ward_g, ward_n) "ward_" #ward_g "_" #ward_n

/// Declares suite level setup/teardown. Optional: a suite with no-op init and
/// fini is created implicitly for any `Test(group, ...)` that has no
/// declaration. Both hooks run once per test case, not once per suite.
///
///   TestSuite(db, .init = &setup, .fini = &teardown);
#define TestSuite(...) WARD_VA_EXPAND(WARD_TEST_SUITE_(__VA_ARGS__, WARD_TEST_NOTHING()))

#define WARD_TEST_SUITE_(ward_g, ...)                                                        \
    WARD_ATTR(used) WARD_SECTION("ward_suite_desc") WARD_ATTR(aligned(sizeof(void *)))         \
    static const struct WardSuiteDesc ward_suite_##ward_g##_desc =                              \
        WARD_VA_EXPAND(WARD_CONS_STRUCT(                                                       \
            .group = #ward_g,                                                                  \
            .file = __FILE__,                                                                  \
            .line = __LINE__,                                                                  \
            __VA_ARGS__                                                                        \
        ))

/// Reports a failure and abandons the current test case. Never returns.
_WardNoreturn void ward_fail(char const *file, int line, char const *fmt, ...);

/// Assertion helpers. All of them abandon the current test case on failure and
/// let the runner continue with the next one.

#define ward_assert(condition) \
    do {                                                                        \
        if (!(condition)) {                                                     \
            ward_fail_report(__FILE__, __LINE__, WARD_ASSERT_FALSE, #condition, \
                             NULL, (WardValue) {0}, (WardValue) {0});           \
        }                                                                       \
    } while (0)

#define ward_assert_not(condition) ward_assert(!(condition))

#define ward_assert_not_null(ptr)                                                   \
    do {                                                                            \
        __auto_type ward__p = (ptr);                                                \
        if (ward__p == NULL) {                                                      \
            ward_fail_report(__FILE__, __LINE__, WARD_ASSERT_NOT_NULL, #ptr, NULL,    \
                             (WardValue) {0}, ward_value_of(ward__p));                \
        }                                                                           \
    } while (0)

#define ward_assert_null(ptr)                                                       \
    do {                                                                            \
        __auto_type ward__p = (ptr);                                                \
        if (ward__p != NULL) {                                                      \
            ward_fail_report(__FILE__, __LINE__, WARD_ASSERT_NULL, #ptr, NULL,        \
                             (WardValue) {0}, ward_value_of(ward__p));                \
        }                                                                           \
    } while (0)

/// Compares two values of the same type. Pointers compare by identity; use
/// `ward_assert_str_eq` to compare text.
#define ward_assert_eq(expected, actual)                                              \
    do {                                                                              \
        __auto_type ward__expected = (expected);                                      \
        __auto_type ward__actual = (actual);                                          \
        if (!ward_eq_generic(ward__expected, ward__actual)) {                         \
            ward_fail_report(__FILE__, __LINE__, WARD_ASSERT_NOT_EQUAL, #expected,    \
                             #actual, ward_value_of(ward__expected),                  \
                             ward_value_of(ward__actual));                            \
        }                                                                             \
    } while (0)

#define ward_assert_not_eq(expected, actual)                                          \
    do {                                                                              \
        __auto_type ward__expected = (expected);                                      \
        __auto_type ward__actual = (actual);                                          \
        if (ward_eq_generic(ward__expected, ward__actual)) {                          \
            ward_fail(__FILE__, __LINE__, "%s should differ from %s", #expected,      \
                      #actual);                                                       \
        }                                                                             \
    } while (0)

/// Compares two floating-point values within an absolute tolerance.
/// Use this instead of `ward_assert_eq` for `float`/`double` operands that
/// result from arithmetic: `ward_assert_eq(0.1 + 0.2, 0.3)` will fail.
#define ward_assert_close(expected, actual, eps)                                        \
    do {                                                                                \
        double ward__expected = (expected);                                             \
        double ward__actual = (actual);                                                 \
        double ward__eps = (eps);                                                       \
        if (ward__expected != ward__expected || ward__actual != ward__actual ||         \
            ward__expected - ward__actual > ward__eps ||                                \
            ward__actual - ward__expected > ward__eps) {                                \
            ward_fail_report(__FILE__, __LINE__, WARD_ASSERT_NOT_EQUAL, #expected,     \
                             #actual, ward_value_double(ward__expected),               \
                             ward_value_double(ward__actual));                         \
        }                                                                               \
    } while (0)

/// Compares the *contents* of two strings. Accepts `char const *` and `WardStr`.
/// Textual operands of `ward_assert_eq` already compare by content, so this is
/// a spelling that documents intent.
#define ward_assert_str_eq(expected, actual) ward_assert_eq(expected, actual)

// Legacy spellings kept so code written against the in-tree framework keeps
// compiling. Define WARD_NO_LEGACY_NAMES to hide them.
#ifndef WARD_NO_LEGACY_NAMES
#define f_assert(condition) ward_assert(condition)
#define f_assert_not(condition) ward_assert_not(condition)
#define f_assert_eq(expected, actual) ward_assert_eq(expected, actual)
#define f_assert_str_eq(expected, actual) ward_assert_str_eq(expected, actual)
#define f_assert_null(ptr) ward_assert_null(ptr)
#define f_assert_not_null(ptr) ward_assert_not_null(ptr)
#endif

#endif // WARD_WARD_H
