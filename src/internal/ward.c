/// Entry point, assertion reporting and value formatting.
///
/// The library owns `main()`: a test binary is just a set of `Test(...)`
/// definitions linked against ward.
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal/runner.h"
#include "ward/support/str.h"
#include "ward/ward.h"

// Implemented in serial.c.
void ward_set_current_target(sigjmp_buf *target);
sigjmp_buf *ward_current_target(void);
enum WardStatus ward_run_captured(struct WardRunner *runner, struct WardCase *test);

WardValue ward_value_bool(bool b)              { return (WardValue) {.kind = WARD_V_BOOL, .b = b}; }
WardValue ward_value_ll(long long v)           { return (WardValue) {.kind = WARD_V_INT, .i = v}; }
WardValue ward_value_ull(unsigned long long v) { return (WardValue) {.kind = WARD_V_UINT, .u = v}; }
WardValue ward_value_double(double v)          { return (WardValue) {.kind = WARD_V_DOUBLE, .d = v}; }
WardValue ward_value_ptr(void const *p)        { return (WardValue) {.kind = WARD_V_PTR, .p = p}; }
WardValue ward_value_cstr(char const *s)       { return (WardValue) {.kind = WARD_V_CSTR, .cp = s}; }
WardValue ward_value_str(WardStr s)            { return (WardValue) {.kind = WARD_V_STR, .s = s}; }

bool ward_eq_ll(long long a, long long b)                   { return a == b; }
bool ward_eq_ull(unsigned long long a, unsigned long long b){ return a == b; }
bool ward_eq_double(double a, double b)                     { return a == b; }
bool ward_eq_ptr(void const *a, void const *b)              { return a == b; }
bool ward_eq_cstr(char const *a, char const *b)             { return ward_cstr_eq(a, b); }
bool ward_eq_str(WardStr a, WardStr b)                      { return ward_str_eq(a, b); }

static void print_value(WardValue value) {
    switch (value.kind) {
        case WARD_V_NONE:
            fputs("<none>", stderr);
            break;
        case WARD_V_BOOL:
            fputs(value.b ? "true" : "false", stderr);
            break;
        case WARD_V_INT:
            fprintf(stderr, "%lld", value.i);
            break;
        case WARD_V_UINT:
            fprintf(stderr, "%llu", value.u);
            break;
        case WARD_V_DOUBLE:
            fprintf(stderr, "%g", value.d);
            break;
        case WARD_V_PTR:
            fprintf(stderr, "%p", (void const *) value.p);
            break;
        case WARD_V_CSTR:
            if (value.cp) {
                fprintf(stderr, "\"%s\"", value.cp);
            } else {
                fputs("(null)", stderr);
            }
            break;
        case WARD_V_STR:
            fprintf(stderr, "\"%.*s\"", (int) value.s.length, value.s.text);
            break;
    }
}

/// Ends the current test case.
///
/// Inside the runner process the failure unwinds to the case driver, so the
/// remaining tests still execute. Inside a forked test child there is nothing
/// to unwind to, so the child exits with a non-zero status and the parent
/// records the failure.
static _Noreturn void abandon_case(void) {
    fflush(stdout);
    fflush(stderr);
    sigjmp_buf *target = ward_current_target();
    if (target) {
        siglongjmp(*target, 1);
    }
    _exit(1);
}

_WardNoreturn void ward_fail(char const *file, int line, char const *fmt, ...) {
    fprintf(stderr, "%s:%d: ", file, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abandon_case();
}

_WardNoreturn void ward_fail_report(char const *file, int line, enum WardAssertKind kind,
                                    char const *expected_expr, char const *actual_expr,
                                    WardValue expected, WardValue actual) {
    fprintf(stderr, "%s:%d: assertion failed: ", file, line);
    switch (kind) {
        case WARD_ASSERT_FALSE:
            fprintf(stderr, "%s is false", expected_expr);
            break;
        case WARD_ASSERT_NOT_EQUAL:
            fprintf(stderr, "%s != %s\n> expected: ", expected_expr, actual_expr);
            print_value(expected);
            fprintf(stderr, "\n> actual:   ");
            print_value(actual);
            if (expected.kind == WARD_V_DOUBLE && actual.kind == WARD_V_DOUBLE) {
                fprintf(stderr, "\n  hint: for floating point, use ward_assert_close(a, b, eps)");
            }
            break;
        case WARD_ASSERT_NULL:
            fprintf(stderr, "%s is not null: ", actual_expr);
            print_value(actual);
            break;
        case WARD_ASSERT_NOT_NULL:
            fprintf(stderr, "%s is null", actual_expr);
            break;
    }
    fputc('\n', stderr);
    abandon_case();
}

_WardNoreturn void ward_skip(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ward: skip: ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stdout);
    fflush(stderr);
    sigjmp_buf *target = ward_current_target();
    if (target) {
        siglongjmp(*target, 3); // UNWIND_SKIP
    }
    _exit(WARD_CHILD_SKIP);
}

WardStr ward_self_path(void) {
    static char buffer[4096];
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        return (WardStr) {0};
    }
    buffer[n] = '\0';
    return ward_str(buffer, (size_t) n);
}

static void print_test_list(struct WardRunner *runner) {
    for (size_t i = 0; i < runner->case_count; i++) {
        printf("%.*s\n", (int) runner->cases[i].full_name.length,
               runner->cases[i].full_name.text);
    }
}

int ward_main(int argc, char *argv[]) {
    struct WardRunner runner = {0};

    int parsed = ward_options_parse(&runner, argc, argv);
    if (parsed != 0) {
        return parsed < 0 ? 2 : 0;
    }

    ward_discover(&runner);
    runner.self_path = ward_self_path();

    if (runner.options.list_tests) {
        print_test_list(&runner);
        ward_arena_free(&runner.arena);
        return 0;
    }

    ward_select(&runner);

    if (runner.options.exact.length > 0) {
        if (runner.selected_size == 0) {
            fprintf(stderr, "ward: no test named '%.*s'\n",
                    (int) runner.options.exact.length, runner.options.exact.text);
            ward_arena_free(&runner.arena);
            return 2;
        }
        struct WardCase *test = runner.selected[0];
        if (test->ignored) {
            ward_arena_free(&runner.arena);
            return runner.options.child ? WARD_CHILD_OK : 0;
        }
        enum WardStatus status = ward_run_captured(&runner, test);
        if (!runner.options.child) {
            // A human is reading this run, so report the outcome explicitly and
            // point at the file/line. A spawned child stays quiet: its parent
            // prints the status line and the reproducer.
            ward_report_status(&runner, test, status);
            if (status != WS_PASS) {
                ward_report_reproducer(&runner, test);
            }
            if (runner.options.break_on_failure && status != WS_PASS && status != WS_SKIP) {
                raise(SIGTRAP);
            }
        }
        ward_arena_free(&runner.arena);
        if (runner.options.child) {
            if (status == WS_PASS) return WARD_CHILD_OK;
            if (status == WS_SKIP) return WARD_CHILD_SKIP;
            return WARD_CHILD_FAIL;
        }
        return (status == WS_PASS || status == WS_SKIP) ? 0 : 1;
    }

    if (runner.selected_size == 0) {
        if (runner.case_count == 0) {
            fprintf(stderr, "ward: no tests were discovered; if the tests live in a\n"
                            "       static library, link it with --whole-archive\n");
        } else {
            fprintf(stderr, "ward: no tests match the filter\n");
        }
        return 2;
    }

    if (runner.options.mode == WARD_MODE_PARALLEL && ward_parallel_supported()) {
        ward_run_parallel(&runner);
    } else {
        ward_run_serial(&runner);
    }

    ward_report_summary(&runner);
    int failures = runner.failed + runner.crashed + runner.timed_out;
    ward_arena_free(&runner.arena);
    ward_map_free(&runner.suites);
    return failures > 0 ? 1 : 0;
}

#ifndef WARD_NO_MAIN
int main(int argc, char *argv[]) {
    return ward_main(argc, argv);
}
#endif
