# ward

A small unit test framework for C23 on Linux. Tests are collected at link time
from an ELF section, each case runs in its own process, and the library provides
`main()`, so a test binary is nothing but test definitions.

```c
#include <ward/ward.h>

TestSuite(db, .init = &setup, .fini = &teardown);

Test(db, add_and_query) {
    ward_assert_eq(2 + 2, 4);
}
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

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

The library target is `ward::ward` (a static `libward.a`). See
[HOW-TO-USE.md](HOW-TO-USE.md) for the full guide and
[IMPROVEMENTS.md](IMPROVEMENTS.md) for the planned work.
