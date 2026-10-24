#include <stdio.h>

#include "internal/runner.h"

static char const *status_label(enum WardStatus status, bool colored) {
    switch (status) {
        case WS_PASS:    return colored ? "\033[42mPASS\033[0m" : "PASS";
        case WS_FAIL:    return colored ? "\033[41mFAIL\033[0m" : "FAIL";
        case WS_SKIP:    return colored ? "\033[43mSKIP\033[0m" : "SKIP";
        case WS_TIMEOUT: return colored ? "\033[45mTIME\033[0m"  : "TIME";
        case WS_CRASH:   return colored ? "\033[45mCRASH\033[0m" : "CRASH";
    }
    return "?";
}

void ward_report_status(struct WardRunner *runner, struct WardCase *test, enum WardStatus status) {
    fflush(stdout);
    printf("[%s] %.*s\n", status_label(status, runner->options.use_color),
           (int) test->full_name.length, test->full_name.text);
    fflush(stdout);
}

void ward_report_output(struct WardRunner *runner, char const *bytes, size_t size) {
    if (size == 0) {
        return;
    }
    fwrite(bytes, 1, size, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    (void) runner;
}

void ward_report_reproducer(struct WardRunner *runner, struct WardCase *test) {
    if (!runner->options.print_reproducer) {
        return;
    }
    char const *self = runner->self_path.length > 0 ? runner->self_path.text : "$0";
    printf("  repro: %s --exact=%.*s\n", self,
           (int) test->full_name.length, test->full_name.text);
    fflush(stdout);
}

void ward_report_summary(struct WardRunner *runner) {
    size_t not_run = runner->selected_size - runner->attempted;
    printf("TOTAL: %zu, PASS: %d, FAIL: %d, CRASH: %d, TIMEOUT: %d, SKIP: %d\n",
           runner->attempted, runner->succeeded, runner->failed,
           runner->crashed, runner->timed_out, runner->ignored);
    if (not_run > 0) {
        printf("NOT RUN: %zu\n", not_run);
    }
    if (runner->aborted) {
        printf("(stopped early: --fail-fast)\n");
    }
    fflush(stdout);
}
