/// Positive self tests: these must all pass, in every mode.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ward/ward.h>
#include <ward/support/str.h>

static void suite_setup(void);
static void suite_teardown(void);
static int suite_init_count;
static int suite_fini_count;

TestSuite(ward_suite, .init = &suite_setup, .fini = &suite_teardown);

static void suite_setup(void) {
    suite_init_count++;
}

static void suite_teardown(void) {
    suite_fini_count++;
}

Test(ward, arithmetic) {
    ward_assert_eq(2 + 2, 4);
    ward_assert(1 < 2);
    ward_assert_not(1 > 2);
    ward_assert_not_eq(1, 2);
}

Test(ward, pointers) {
    int value = 7;
    int *pointer = &value;
    ward_assert_not_null(pointer);
    ward_assert_null(NULL);
    ward_assert_eq(pointer, &value);
}

Test(ward, strings_compare_by_content) {
    char const *expected = "hello";
    char copy[8];
    strcpy(copy, expected);
    // Different buffers with the same text: pointer comparison would fail.
    ward_assert_str_eq(expected, copy);
    ward_assert_eq(ward_str_c("abc"), ward_str_c("abc"));
}

Test(ward, floating_point) {
    ward_assert_eq(1.5, 1.5);
    ward_assert_not_eq(1.5, 2.5);
}

Test(ward, float_close) {
    ward_assert_close(0.1 + 0.2, 0.3, 1e-9);
    ward_assert_close(1.0 / 3.0 * 3.0, 1.0, 1e-15);
}

Test(ward, runtime_skip, .ignored = false) {
    if (getenv("WARD_TEST_SKIP") == NULL) {
        ward_skip("environment variable not set");
    }
    ward_assert(true);
}

Test(ward, unsigned_and_signed_values) {
    unsigned int large = 4000000000u;
    ward_assert_eq(large, 4000000000u);
    ward_assert_eq((long long) -5, -5);
}

Test(ward, prints_output) {
    printf("this line belongs to a passing test\n");
    ward_assert(true);
}

Test(ward_suite, declared_hooks_run_around_every_case) {
    ward_assert(suite_init_count >= 1);
    ward_assert(suite_fini_count <= suite_init_count);
}

Test(ward, ignored_case_is_skipped, .ignored = true) {
    ward_assert(false);
}

static volatile int alarm_fired;

static void on_alarm(int number) {
    (void) number;
    alarm_fired = 1;
}

Test(ward, own_sigalarm_handler_survives) {
    // The runner uses SIGALRM for timeouts in --single mode; it must hand the
    // disposition back so a case that owns the timer keeps it.
    struct sigaction action = { .sa_handler = &on_alarm };
    sigemptyset(&action.sa_mask);
    ward_assert_eq(sigaction(SIGALRM, &action, NULL), 0);

    struct sigaction current_action;
    sigaction(SIGALRM, NULL, &current_action);
    ward_assert_eq((void *) current_action.sa_handler, (void *) &on_alarm);
}
