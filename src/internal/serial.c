/// Sequential in-process runner.
///
/// Everything runs on the main thread of this process: no fork, no pipes, no
/// extra threads. Test output goes straight to the terminal, so `printf`
/// debugging, gdb, rr, sanitizers and valgrind all work normally. The tradeoff
/// is that a hard crash (SIGSEGV, trap, ...) aborts the whole run.
///
/// Per test timeouts use SIGALRM here, so a test that installs its own
/// SIGALRM handler disables its own timeout.
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#endif
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "internal/runner.h"

enum {
    UNWIND_ASSERTION = 1,
    UNWIND_TIMEOUT = 2,
    UNWIND_SKIP = 3,
};

static struct {
    sigjmp_buf *target;
    struct WardCase *test;
    bool body_finished;
} current;

static int const FATAL_SIGNAL_NUMBERS[] = { SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGBUS, SIGSYS };

/// The handler must be async-signal-safe: no stdio, no snprintf (`%m` may
/// allocate), no strsignal (may allocate). Only a fixed table, hand-rolled
/// integer formatting and write().
static char const *signal_name(int number) {
    switch (number) {
        case SIGILL:  return "SIGILL";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGSYS:  return "SIGSYS";
        default:      return "unknown signal";
    }
}

static void write_all(int fd, char const *text, size_t size) {
    while (size > 0) {
        ssize_t written = write(fd, text, size);
        if (written <= 0) {
            return;
        }
        text += written;
        size -= (size_t) written;
    }
}

static void write_str(int fd, char const *text) {
    write_all(fd, text, strlen(text));
}

static void write_int(int fd, int value) {
    char digits[12];
    size_t count = 0;
    unsigned unsigned_value = (unsigned) value;
    do {
        digits[count++] = (char) ('0' + unsigned_value % 10);
        unsigned_value /= 10;
    } while (unsigned_value != 0);

    char reversed[12];
    for (size_t i = 0; i < count; i++) {
        reversed[i] = digits[count - 1 - i];
    }
    write_all(fd, reversed, count);
}

static void report_fatal_signal(int number) {
    int fd = STDERR_FILENO;
    write_str(fd, "\nward: test died from ");
    write_str(fd, signal_name(number));
    write_str(fd, " (");
    write_int(fd, number);
    write_str(fd, ") while running '");
    if (current.test) {
        write_all(fd, current.test->full_name.text, current.test->full_name.length);
    }
    write_str(fd, "'\n");

#if __has_include(<execinfo.h>)
    // glibc-only: musl has no execinfo.h, so the guard keeps the build green.
    // backtrace() walks the frame-pointer chain (hence -fno-omit-frame-pointer);
    // backtrace_symbols_fd() writes straight to the fd. Not strictly
    // async-signal-safe (libunwind may allocate), acceptable here: the process
    // is dying and about to _exit.
    void *frames[32];
    int depth = backtrace(frames, (int) WARD_LENGTHOF(frames));
    if (depth > 1) {
        // Skip frame 0 (this function) and frame 1 (on_fatal_signal): both are
        // ward internals, not useful to the reader.
        write_str(fd, "ward: backtrace:\n");
        backtrace_symbols_fd(frames + 2, depth - 2, fd);
    }
#endif
}

static void on_fatal_signal(int number) {
    report_fatal_signal(number);

    // Restore the default disposition and re-raise, so the shell reports the
    // signal and a core dump can still be produced.
    signal(number, SIG_DFL);
    raise(number);
    _exit(128 + number);
}

static void on_timeout(int number) {
    (void) number;
    if (current.target) {
        siglongjmp(*current.target, UNWIND_TIMEOUT);
    }
}

static void install_handlers(void) {
    static bool installed;
    if (installed) {
        return;
    }
    installed = true;

    for (size_t i = 0; i < WARD_LENGTHOF(FATAL_SIGNAL_NUMBERS); i++) {
        struct sigaction action = { .sa_handler = &on_fatal_signal };
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_NODEFER;
        sigaction(FATAL_SIGNAL_NUMBERS[i], &action, NULL);
    }

    struct sigaction alarm_action = { .sa_handler = &on_timeout };
    sigemptyset(&alarm_action.sa_mask);
    sigaction(SIGALRM, &alarm_action, NULL);
}

void ward_set_current_target(sigjmp_buf *target) {
    current.target = target;
}

sigjmp_buf *ward_current_target(void) {
    return current.target;
}

static unsigned int case_timeout_seconds(struct WardRunner *runner, struct WardCase *test) {
    // In a spawned child the parent owns the deadline, so the alarm stays off:
    // the parent's SIGTERM is what produces a TIMEOUT report.
    if (runner->options.child) {
        return 0;
    }
    // --single is a debugging mode: no timer, so gdb and single-stepping work
    // without the SIGALRM interfering.
    if (runner->options.single_debug) {
        return 0;
    }
    double timeout = test->timeout_seconds > 0 ? test->timeout_seconds
                                               : runner->options.timeout_seconds;
    if (timeout <= 0) {
        return 0;
    }
    if (timeout >= (double) UINT_MAX) {
        return UINT_MAX;
    }
    // Round up, so a `.timeout_seconds = 0.5` case is not killed instantly.
    return (unsigned int) (timeout + 0.999);
}

enum WardStatus ward_run_captured(struct WardRunner *runner, struct WardCase *test) {
    install_handlers();

    sigjmp_buf *previous_target = current.target;
    struct WardCase *previous_test = current.test;
    bool previous_finished = current.body_finished;

    sigjmp_buf target;
    current.target = &target;
    current.test = test;
    current.body_finished = false;

    enum WardStatus status;
    int unwind = sigsetjmp(target, 1);
    if (unwind != 0) {
        // Unwound by a failed assertion, the timeout alarm, or a skip;
        // teardown of the case still runs.
        if (!current.body_finished && test->suite->fini) {
            test->suite->fini();
        }
        if (unwind == UNWIND_TIMEOUT) {
            status = WS_TIMEOUT;
        } else if (unwind == UNWIND_SKIP) {
            status = WS_SKIP;
        } else {
            status = WS_FAIL;
        }
    } else {
        if (runner->options.verbose) {
            fprintf(stderr, "ward: running %.*s at %.*s:%d\n",
                    (int) test->full_name.length, test->full_name.text,
                    (int) test->file.length, test->file.text, test->line);
        }
        unsigned int timeout_seconds = case_timeout_seconds(runner, test);
        struct sigaction saved_alarm;
        if (timeout_seconds != 0) {
            struct sigaction action = { .sa_handler = &on_timeout };
            sigemptyset(&action.sa_mask);
            sigaction(SIGALRM, &action, &saved_alarm);
            alarm(timeout_seconds);
        }

        if (test->suite->init) {
            test->suite->init();
        }
        test->run();
        if (test->suite->fini) {
            test->suite->fini();
        }

        alarm(0);
        if (timeout_seconds != 0) {
            // Restore whatever the test left behind, so a case that installed
            // its own SIGALRM handler does not lose it for the next one.
            struct sigaction now_alarm;
            sigaction(SIGALRM, &saved_alarm, &now_alarm);
            if (now_alarm.sa_handler != &on_timeout) {
                fprintf(stderr, "ward: %.*s replaced the SIGALRM handler, so its "
                                "timeout was not enforced\n",
                        (int) test->full_name.length, test->full_name.text);
            }
        }
        current.body_finished = true;
        status = WS_PASS;
    }

    current.target = previous_target;
    current.test = previous_test;
    current.body_finished = previous_finished;
    return status;
}

static void count_status(struct WardRunner *runner, enum WardStatus status) {
    switch (status) {
        case WS_PASS:    runner->succeeded++; break;
        case WS_SKIP:    runner->ignored++;   break;
        case WS_FAIL:    runner->failed++;    break;
        case WS_TIMEOUT: runner->timed_out++; break;
        case WS_CRASH:   runner->crashed++;   break;
    }
}

int ward_run_serial(struct WardRunner *runner) {
    for (size_t i = 0; i < runner->selected_size; i++) {
        struct WardCase *test = runner->selected[i];

        runner->attempted++;
        enum WardStatus status = test->ignored ? WS_SKIP : ward_run_captured(runner, test);
        count_status(runner, status);
        ward_report_status(runner, test, status);

        if (status == WS_TIMEOUT) {
            ward_report_output(runner, "test exceeded its time limit",
                               strlen("test exceeded its time limit"));
        }
        if (status != WS_PASS && status != WS_SKIP) {
            ward_report_reproducer(runner, test);
            if (runner->options.fail_fast) {
                runner->aborted = true;
                break;
            }
        }
    }
    return 0;
}
