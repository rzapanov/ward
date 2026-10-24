/// Portable fallback: no pidfd support, so the parallel mode degrades to the
/// sequential in-process runner instead of failing to build.
#include "internal/runner.h"

#if !defined(__linux__)

bool ward_parallel_supported(void) {
    return false;
}

int ward_run_parallel(struct WardRunner *runner) {
    return ward_run_serial(runner);
}

#endif
