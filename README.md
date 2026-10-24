# ward

A small unit test framework for C23 on Linux. Tests are collected at link time
from an ELF section, each case runs in its own process, and the library provides
`main()`, so a test binary is nothing but test definitions.

## Quickstart

A test file is all you write — the library provides `main()`:

```c
// test/math_test.c
#include <ward/ward.h>

Test(math, add) {
    ward_assert_eq(1 + 1, 2);
}

Test(math, close_enough) {
    ward_assert_close(0.1 + 0.2, 0.3, 1e-9);
}
```

```sh
# build the library (once)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j

# link and run your test
cc -std=c23 -Iinclude test/math_test.c build/lib/libward.a -o math_tests
./math_tests
```

```
[PASS] math:add
[PASS] math:close_enough
TOTAL: 2, PASS: 2, FAIL: 0, CRASH: 0, TIMEOUT: 0, SKIP: 0
```

## Features

- **Zero boilerplate**: no registration calls, no `main()`, no linker script.
- **Link-time discovery**: `Test(...)` places a descriptor in the
  `ward_test_desc` section; the linker's `__start_`/`__stop_` symbols delimit it.
  No runtime ELF parsing, works on a stripped binary.
- **Process isolation**: every case is spawned with `posix_spawn` and tracked by
  a pidfd, so crashes, hangs, `exit()` calls and closed file descriptors are
  reported as that one case failing instead of taking the run down.
- **Per-case timeouts**: `.timeout_seconds = 5`, escalating SIGTERM then SIGKILL.
- **Parallel by default**, `--single` for a plain sequential in-process run that
  works with gdb, rr, sanitizers and valgrind.
- **Reproducer lines**: every failure prints a copy-paste command that reruns
  just that case.
- **Float tolerance**: `ward_assert_close(a, b, eps)` for floating-point
  comparisons.
- **Runtime skip**: `ward_skip("reason")` abandons a case as SKIP when a
  precondition is not met.
- **Own your `main()`**: `ward_main(argc, argv)` for consumers that need custom
  start-up.
- **CMake helpers**: `ward_add_test_executable` wires sources, linking, and
  CTest in one call.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

The library target is `ward::ward` (a static `libward.a`). See
[HOW-TO-USE.md](HOW-TO-USE.md) for the full guide and
[IMPROVEMENTS.md](IMPROVEMENTS.md) for the planned work.
