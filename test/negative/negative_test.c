/// Cases that are *supposed* to fail, hang or crash. They live in their own
/// binary so that the normal test run stays green, and `negative.sh` asserts
/// the runner reports each of them correctly.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ward/ward.h>

Test(negative, assertion_fails) {
    ward_assert_eq(1, 2);
}

Test(negative, message_fails) {
    ward_fail(__FILE__, __LINE__, "explicit failure for %s", "testing");
}

Test(negative, crashes_with_segv) {
    volatile int *pointer = (volatile int *) (uintptr_t) 0x1;
    *pointer = 0;
}

Test(negative, aborts) {
    abort();
}

Test(negative, hangs, .timeout_seconds = 1) {
    for (;;) {
        pause();
    }
}

Test(negative, exits_the_process) {
    printf("this test calls _exit(0), which must not look like a pass\n");
    fflush(stdout);
    _exit(0);
}

Test(negative, closes_stdout) {
    // A test that closes its own output must not confuse the runner into
    // thinking the process is already done.
    close(STDOUT_FILENO);
    ward_assert_eq(3, 4);
}

Test(negative, prints_a_lot) {
    for (int i = 0; i < 200000; i++) {
        printf("filler line %d to exceed the capture limit\n", i);
    }
    ward_assert_eq(1, 2);
}

Test(negative, slow_but_fine, .timeout_seconds = 20) {
    ward_assert_eq(1, 1);
}

Test(negative, reads_stdin) {
    // stdin must be /dev/null: a read returns EOF instead of blocking on the
    // runner's terminal until the deadline.
    char buffer[8];
    ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
    ward_assert_eq((long long) n, 0);
}
