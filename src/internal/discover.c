/// Test/suite discovery based on linker generated section bounds.
///
/// `Test(...)` puts a descriptor directly into the `ward_test_desc` section and
/// `TestSuite(...)` into `ward_suite_desc`. Because those names are valid C
/// identifiers, GNU ld and lld emit `__start_<name>` / `__stop_<name>` symbols
/// around them, so no runtime ELF parsing, `/proc/self/exe` access or
/// linker script is required.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/runner.h"
#include "ward/support/fatal.h"

extern const struct WardTestDesc __start_ward_test_desc[] __attribute__((weak));
extern const struct WardTestDesc __stop_ward_test_desc[] __attribute__((weak));
extern const struct WardSuiteDesc __start_ward_suite_desc[] __attribute__((weak));
extern const struct WardSuiteDesc __stop_ward_suite_desc[] __attribute__((weak));

static struct WardSuite *ward_suite_for(struct WardRunner *runner, char const *group,
                                        char const *file, int line, bool *created) {
    WardStr name = ward_str_c(group);
    struct WardSuite *suite = ward_map_get(&runner->suites, name);
    if (suite) {
        *created = false;
        return suite;
    }
    suite = ward_new(&runner->arena, struct WardSuite);
    *suite = (struct WardSuite) {
        .name = name,
        .file = ward_str_c(file),
        .line = line,
        .init = NULL,
        .fini = NULL,
        .declared = false,
    };
    ward_map_put(&runner->suites, name, suite);
    *created = true;
    return suite;
}

void ward_discover(struct WardRunner *runner) {
    if (__start_ward_suite_desc) {
        for (struct WardSuiteDesc const *desc = __start_ward_suite_desc;
             desc != __stop_ward_suite_desc; desc++) {
            bool created;
            struct WardSuite *suite = ward_suite_for(runner, desc->group, desc->file,
                                                     desc->line, &created);
            if (!created) {
                fprintf(stderr,
                        "ward: duplicate test suite '%s'\n"
                        "  already declared at %.*s:%d\n"
                        "  declared again at %s:%d\n",
                        desc->group, (int) suite->file.length, suite->file.text, suite->line,
                        desc->file, desc->line);
                exit(2);
            }
            suite->declared = true;
            suite->init = desc->init;
            suite->fini = desc->fini;
        }
    }

    if (!__start_ward_test_desc) {
        return;
    }

    size_t count = (size_t) (__stop_ward_test_desc - __start_ward_test_desc);
    if (count > WARD_MAX_TESTS_LIMIT) {
        WARD_FATAL("absurd number of test descriptors: %zu", count);
    }

    runner->cases = ward_new_arr(&runner->arena, count, struct WardCase);
    for (struct WardTestDesc const *desc = __start_ward_test_desc;
         desc != __stop_ward_test_desc; desc++) {
        bool created;
        struct WardSuite *suite = ward_suite_for(runner, desc->group, desc->file,
                                                 desc->line, &created);

        size_t full_length = suite->name.length + ward_str_c(desc->name).length + 1;
        char *full_text = ward_new_arr(&runner->arena, full_length + 1, char);
        snprintf(full_text, full_length + 1, "%.*s:%s",
                 (int) suite->name.length, suite->name.text, desc->name);
        WardStr full_name = ward_str(full_text, full_length);

        // Descriptors are file static, so two cases with the same group and
        // name link fine but would be indistinguishable to --exact.
        struct WardCase *previous = ward_map_get(&runner->names, full_name);
        if (previous) {
            fprintf(stderr,
                    "ward: duplicate test name '%.*s'\n"
                    "  declared at %.*s:%d\n"
                    "  declared again at %s:%d\n",
                    (int) full_name.length, full_name.text,
                    (int) previous->file.length, previous->file.text, previous->line,
                    desc->file, desc->line);
            exit(2);
        }

        struct WardCase *stored = &runner->cases[runner->case_count];
        ward_map_put(&runner->names, full_name, stored);
        runner->cases[runner->case_count++] = (struct WardCase) {
            .name = ward_str_c(desc->name),
            .full_name = full_name,
            .file = ward_str_c(desc->file),
            .line = desc->line,
            .timeout_seconds = desc->timeout_seconds,
            .ignored = desc->ignored,
            .suite = suite,
            .run = desc->fn,
        };
    }
}

static bool ward_matches(WardStr full_name, WardStr pattern) {
    if (pattern.length == 0) {
        return true;
    }
    if (pattern.length > full_name.length) {
        return false;
    }
    for (size_t i = 0; i + pattern.length <= full_name.length; i++) {
        if (memcmp(full_name.text + i, pattern.text, pattern.length) == 0) {
            return true;
        }
    }
    return false;
}

void ward_select(struct WardRunner *runner) {
    size_t count = runner->case_count;
    struct WardCase **selected = ward_new_arr(&runner->arena, count ? count : 1,
                                             struct WardCase *);
    size_t size = 0;
    for (size_t i = 0; i < count; i++) {
        struct WardCase *test = &runner->cases[i];
        if (runner->options.exact.length > 0) {
            if (!ward_str_eq(test->full_name, runner->options.exact)) {
                continue;
            }
        } else if (!ward_matches(test->full_name, runner->options.filter)) {
            continue;
        }
        selected[size++] = test;
    }
    runner->selected = selected;
    runner->selected_size = size;
}
