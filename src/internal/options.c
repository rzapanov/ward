#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal/runner.h"
#include "ward/support/str.h"

void ward_options_print_usage(char const *program) {
    printf("Usage: %s [options]\n\n"
           "Options:\n"
           "  --list-tests          List all discovered test names and exit\n"
           "  --exact=<name>        Run one test in this process (name is 'group:name')\n"
           "  --filter=<pattern>    Only run tests whose name contains <pattern>\n"
           "  --single              Run one test in-process, no timeout (requires --test)\n"
           "  --test=<name>         Select the test for --single (same as --exact)\n"
           "  --jobs=<n>            Number of parallel test processes (0 = all cpus)\n"
           "  --timeout=<seconds>   Per test time limit, default %.0f\n"
           "  --fail-fast           Stop scheduling after the first failure\n"
           "  --break-on-failure    Raise SIGTRAP after a failure (for gdb)\n"
           "  --print-repr          Print a copy-paste command for every failure\n"
           "  --no-color            Disable colored status labels\n"
           "  --verbose, -v         Framework debug logging\n"
           "  --help, -h            Show this message\n",
           program, WARD_DEFAULT_TIMEOUT_SECONDS);
}

static bool parse_double(char const *text, double *out) {
    char *end = NULL;
    double value = strtod(text, &end);
    if (end == text || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

static bool take_option(char const *arg, char const *prefix, char const **value) {
    size_t length = strlen(prefix);
    if (strncmp(arg, prefix, length) != 0) {
        return false;
    }
    *value = arg + length;
    return true;
}

int ward_options_parse(struct WardRunner *runner, int argc, char **argv) {
    struct WardOptions *options = &runner->options;
    *options = (struct WardOptions) {
        .mode = WARD_MODE_PARALLEL,
        .jobs = 0,
        .timeout_seconds = WARD_DEFAULT_TIMEOUT_SECONDS,
        .print_reproducer = true,
        .use_color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL,
    };

    for (int i = 1; i < argc; i++) {
        char const *arg = argv[i];
        char const *value = NULL;

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            ward_options_print_usage(argv[0]);
            return 1;
        } else if (strcmp(arg, "--list-tests") == 0) {
            options->list_tests = true;
        } else if (strcmp(arg, "--single") == 0) {
            options->single_debug = true;
            options->mode = WARD_MODE_SERIAL;
        } else if (strcmp(arg, "--fail-fast") == 0) {
            options->fail_fast = true;
        } else if (strcmp(arg, "--break-on-failure") == 0) {
            options->break_on_failure = true;
        } else if (strcmp(arg, "--print-repr") == 0) {
            options->print_reproducer = true;
        } else if (strcmp(arg, "--no-print-repr") == 0) {
            options->print_reproducer = false;
        } else if (strcmp(arg, "--no-color") == 0) {
            options->use_color = false;
        } else if (take_option(arg, "--_ward-program=", &value)) {
            options->child_program = value;
        } else if (strcmp(arg, "--_ward-child") == 0) {
            options->child = true;
        } else if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
            options->verbose = true;
        } else if (take_option(arg, "--exact=", &value) || take_option(arg, "--test=", &value)) {
            options->exact = ward_str_c(value);
            options->mode = WARD_MODE_SERIAL;
        } else if (take_option(arg, "--filter=", &value)) {
            options->filter = ward_str_c(value);
        } else if (take_option(arg, "--timeout=", &value)) {
            if (!parse_double(value, &options->timeout_seconds) || options->timeout_seconds < 0) {
                fprintf(stderr, "ward: invalid --timeout value '%s'\n", value);
                return -1;
            }
        } else if (take_option(arg, "--jobs=", &value)) {
            char *end = NULL;
            long jobs = strtol(value, &end, 10);
            if (end == value || *end != '\0' || jobs < 0 || jobs > WARD_MAX_PARALLELISM) {
                fprintf(stderr, "ward: invalid --jobs value '%s' (expected 0..%d)\n",
                        value, WARD_MAX_PARALLELISM);
                return -1;
            }
            options->jobs = jobs;
        } else {
            fprintf(stderr, "ward: unknown option '%s'\n", arg);
            ward_options_print_usage(argv[0]);
            return -1;
        }
    }

    if (options->single_debug && options->exact.length == 0) {
        fprintf(stderr, "ward: --single requires --test=<group:name>\n");
        return -1;
    }
    if (options->exact.length == 0 && getenv("WARD_NO_FORK") != NULL) {
        options->mode = WARD_MODE_SERIAL;
    }
    return 0;
}
