/// Internal state of the runner: test/suite model, options, reporting.
#ifndef WARD_INTERNAL_RUNNER_H
#define WARD_INTERNAL_RUNNER_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>

#include "ward/internal/api.h"
#include "ward/support/arena.h"
#include "ward/support/map.h"
#include "ward/support/str.h"

#define WARD_MAX_TESTS_LIMIT (1 << 20)
#define WARD_DEFAULT_TIMEOUT_SECONDS 30.0
#define WARD_MAX_OUTPUT_SIZE (128 * 1024)
#define WARD_MAX_PARALLELISM 256
/// Exit status a spawned test child uses to report success. Any other status
/// (including a test calling `exit(0)` itself) counts as a failure, so a test
/// that terminates the process early can never look like a pass.
#define WARD_CHILD_OK 87
/// Exit status a spawned test child uses to report a failed assertion.
#define WARD_CHILD_FAIL 1
/// Exit status a spawned test child uses to report a runtime skip.
#define WARD_CHILD_SKIP 88
/// Grace period between SIGTERM and SIGKILL after a test exceeded its timeout.
#define WARD_KILL_GRACE_MS 1000

#define WARD_LENGTHOF(arr) (sizeof(arr) / sizeof((arr)[0]))

enum WardStatus {
    WS_PASS,
    WS_FAIL,
    WS_SKIP,
    WS_TIMEOUT,
    WS_CRASH,
};

struct WardSuite {
    WardStr name;
    WardStr file;
    int line;
    void (*init)(void);
    void (*fini)(void);
    bool declared;
};

struct WardCase {
    WardStr name;
    WardStr full_name;
    WardStr file;
    int line;
    double timeout_seconds;
    bool ignored;
    struct WardSuite *suite;
    void (*run)(void);
};

enum WardOptionsMode {
    WARD_MODE_PARALLEL,
    WARD_MODE_SERIAL,
};

struct WardOptions {
    enum WardOptionsMode mode;
    bool verbose;
    bool print_reproducer;
    bool list_tests;
    bool use_color;
    bool fail_fast;
    bool single_debug;
    bool break_on_failure;
    /// Set by the runner when spawning a child, so the child knows it must
    /// report its outcome through the exit status.
    bool child;
    long jobs;
    double timeout_seconds;
    WardStr filter;
    WardStr exact;
    /// Overrides the executable used for spawned children (tests only).
    char const *child_program;
};

struct WardRunner {
    WardArena arena;
    struct WardOptions options;
    WardMap suites;
    /// full name -> case, used to reject duplicate names.
    WardMap names;
    struct WardCase *cases;
    struct WardCase **selected;
    size_t selected_size;
    size_t case_count;

    /// Absolute path of this executable, used to print reproducer commands.
    WardStr self_path;

    size_t attempted;
    int succeeded;
    int failed;
    int ignored;
    int timed_out;
    int crashed;
    bool aborted;
};

// options.c
int ward_options_parse(struct WardRunner *runner, int argc, char **argv);
void ward_options_print_usage(char const *program);

// discover.c
void ward_discover(struct WardRunner *runner);
void ward_select(struct WardRunner *runner);

// report.c
void ward_report_status(struct WardRunner *runner, struct WardCase *test,
                        enum WardStatus status);
void ward_report_output(struct WardRunner *runner, char const *bytes, size_t size);
void ward_report_reproducer(struct WardRunner *runner, struct WardCase *test);
void ward_report_summary(struct WardRunner *runner);

// serial.c
int ward_run_serial(struct WardRunner *runner);

// proc.c / proc_linux.c
int ward_run_parallel(struct WardRunner *runner);
bool ward_parallel_supported(void);

// ward.c
/// Resolves and returns the absolute path of the running executable.
WardStr ward_self_path(void);

/// Runs one case (suite init, body, suite fini) in the current process and
/// reports the outcome. Assertion failures and timeouts do not escape.
enum WardStatus ward_run_captured(struct WardRunner *runner, struct WardCase *test);

/// Redirects assertion failures to `target`; NULL means "exit the process".
void ward_set_current_target(sigjmp_buf *target);
sigjmp_buf *ward_current_target(void);

#endif // WARD_INTERNAL_RUNNER_H
