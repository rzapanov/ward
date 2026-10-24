/// Referenced by the public macros in `ward/ward.h`. Not meant to be included
/// or used directly by tests.
#ifndef WARD_INTERNAL_API_H
#define WARD_INTERNAL_API_H

#include <stdbool.h>
#include <stddef.h>

#include "ward/support/str.h"

#define _WardNoreturn _Noreturn

/// One `Test(...)` declaration. Laid out directly inside the
/// `ward_test_desc` section; the linker delimits it with
/// `__start_ward_test_desc` / `__stop_ward_test_desc`.
struct WardTestDesc {
    char const *group;
    char const *name;
    void (*fn)(void);
    char const *file;
    int line;

    // Optional, set with designated initializers:
    //   Test(g, t, .timeout_seconds = 5, .ignored = true)
    double timeout_seconds;
    bool ignored;
};

/// One `TestSuite(...)` declaration.
struct WardSuiteDesc {
    char const *group;
    char const *file;
    int line;
    void (*init)(void);
    void (*fini)(void);
};

/// Failure kinds reported by the runner.
enum WardAssertKind {
    WARD_ASSERT_FALSE,
    WARD_ASSERT_NOT_EQUAL,
    WARD_ASSERT_NULL,
    WARD_ASSERT_NOT_NULL,
};

/// Tagged value used to print the operands of a failed assertion.
enum WardValueKind {
    WARD_V_NONE,
    WARD_V_BOOL,
    WARD_V_INT,
    WARD_V_UINT,
    WARD_V_DOUBLE,
    WARD_V_PTR,
    WARD_V_CSTR,
    WARD_V_STR,
};

typedef struct WardValue WardValue;
struct WardValue {
    enum WardValueKind kind;
    union {
        bool b;
        long long i;
        unsigned long long u;
        double d;
        void const *p;
        char const *cp;
        WardStr s;
    };
};

WardValue ward_value_bool(bool b);
WardValue ward_value_ll(long long v);
WardValue ward_value_ull(unsigned long long v);
WardValue ward_value_double(double v);
WardValue ward_value_ptr(void const *p);
WardValue ward_value_cstr(char const *s);
WardValue ward_value_str(WardStr s);

#define ward_value_of(v) _Generic((v),                                       \
    _Bool:              ward_value_bool,                                     \
    char:               ward_value_ll,                                       \
    signed char:        ward_value_ll,                                       \
    short:              ward_value_ll,                                       \
    int:                ward_value_ll,                                       \
    long:               ward_value_ll,                                       \
    long long:          ward_value_ll,                                       \
    unsigned char:      ward_value_ull,                                      \
    unsigned short:     ward_value_ull,                                      \
    unsigned int:       ward_value_ull,                                      \
    unsigned long:      ward_value_ull,                                      \
    unsigned long long: ward_value_ull,                                      \
    float:              ward_value_double,                                   \
    double:             ward_value_double,                                   \
    WardStr:            ward_value_str,                                      \
    char *:             ward_value_cstr,                                     \
    char const *:       ward_value_cstr,                                     \
    default:            ward_value_ptr)(v)

bool ward_eq_ll(long long a, long long b);
bool ward_eq_ull(unsigned long long a, unsigned long long b);
bool ward_eq_double(double a, double b);
bool ward_eq_ptr(void const *a, void const *b);
bool ward_eq_cstr(char const *a, char const *b);
bool ward_eq_str(WardStr a, WardStr b);

#define ward_eq_generic(a, b) _Generic((a),                                  \
    _Bool:              ward_eq_ll,                                          \
    char:               ward_eq_ll,                                          \
    signed char:        ward_eq_ll,                                          \
    short:              ward_eq_ll,                                          \
    int:                ward_eq_ll,                                          \
    long:               ward_eq_ll,                                          \
    long long:          ward_eq_ll,                                          \
    unsigned char:      ward_eq_ull,                                         \
    unsigned short:     ward_eq_ull,                                         \
    unsigned int:       ward_eq_ull,                                         \
    unsigned long:      ward_eq_ull,                                         \
    unsigned long long: ward_eq_ull,                                         \
    float:              ward_eq_double,                                      \
    double:             ward_eq_double,                                      \
    WardStr:            ward_eq_str,                                         \
    char *:             ward_eq_cstr,                                        \
    char const *:       ward_eq_cstr,                                        \
    default:            ward_eq_ptr)((a), (b))

/// Reports a failed assertion. Abandons the current test case.
_WardNoreturn void ward_fail_report(char const *file, int line,
                                    enum WardAssertKind kind,
                                    char const *expected_expr,
                                    char const *actual_expr,
                                    WardValue expected, WardValue actual);

/// Skips the current test case with a reason. Never returns.
_WardNoreturn void ward_skip(char const *fmt, ...);

/// Entry point for consumers that own `main()`.
int ward_main(int argc, char **argv);


#endif // WARD_INTERNAL_API_H
