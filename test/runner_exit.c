/// Linker wrappers expose the executor's own return code to the shell harness,
/// independently of ward_main's summary/accounting. No replacement test runner.
#include <unistd.h>
#include "internal/runner.h"

int __real_ward_run_serial(struct WardRunner *runner);
int __real_ward_run_parallel(struct WardRunner *runner);
int __wrap_ward_run_serial(struct WardRunner *runner);
int __wrap_ward_run_parallel(struct WardRunner *runner);

int __wrap_ward_run_serial(struct WardRunner *runner) {
    _exit(__real_ward_run_serial(runner));
}

int __wrap_ward_run_parallel(struct WardRunner *runner) {
    _exit(__real_ward_run_parallel(runner));
}
