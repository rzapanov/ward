# How to use ward

Ward is a unit test framework for C23 on Linux. One static library, no
boilerplate: link it, write `Test(...)` blocks, and you have a test binary with
its own `main()`.

- [1. Linking](#1-linking)
- [2. Writing tests](#2-writing-tests)
- [3. Assertions](#3-assertions)
- [4. Running tests](#4-running-tests)
- [5. Debugging a failing test](#5-debugging-a-failing-test)
- [6. CMake integration](#6-cmake-integration)
- [7. How discovery works, and its limits](#7-how-discovery-works-and-its-limits)
- [8. Reference](#8-reference)

---

## 1. Linking

The framework provides `main()`. A test binary is only test code:

```sh
cc -std=c23 -I<path>/include test.c <path>/libward.a -o test
./test
```

Nothing else to add: no pthreads, no `--whole-archive` for ward itself, no
linker script.

**One rule**: never write your own `main()`. If you need custom start-up, call
`ward_main(argc, argv)` from your own `main()` — it runs the full runner with
the same command-line options and returns the exit status.

## 2. Writing tests

```c
#include <ward/ward.h>

Test(stack, push_pop) {
    Stack s = stack_new();
    stack_push(&s, 7);
    ward_assert_eq(stack_pop(&s), 7);
}
```

`Test(group, name)` takes two identifiers. They must be valid C identifiers and
the pair must be unique across the whole binary — the runner aborts at start-up
with both file/line locations if it is not. The full test name is
`group:name`, which is what `--exact=` and the listing use.

Per-test options are extra designated initializers:

```c
Test(parser, huge_file, .timeout_seconds = 120) { ... }
Test(parser, not_written_yet, .ignored = true)  { ... }   // reported as SKIP
```

### Suites (setup / teardown)

```c
static void setup(void)    { db = db_new(); }
static void teardown(void) { db_free(db); }

TestSuite(db, .init = &setup, .fini = &teardown);

Test(db, insert)  { ... }
Test(db, remove)  { ... }
```

Both hooks are optional, and they run **once per test case**, not once per
suite: `insert` and `remove` each get a fresh `db`. A suite with no-op hooks is
created implicitly for any `Test(group, ...)` whose group has no declaration, so
you only write `TestSuite` when you actually need fixtures.

Teardown also runs when an assertion fails, so cleanup cannot be skipped by a
failure.

## 3. Assertions

| Assertion | Fails when |
| --- | --- |
| `ward_assert(cond)` | `cond` is false |
| `ward_assert_not(cond)` | `cond` is true |
| `ward_assert_eq(a, b)` | `a` and `b` differ |
| `ward_assert_not_eq(a, b)` | `a` and `b` are equal |
| `ward_assert_close(a, b, eps)` | `|a - b| > eps` (floating-point tolerance) |
| `ward_assert_str_eq(a, b)` | text of `a` and `b` differs |
| `ward_assert_null(p)` | `p` is not null |
| `ward_assert_not_null(p)` | `p` is null |
| `ward_fail("fmt %s", x)` | always, with your message |

`ward_skip("reason")` abandons the current case as `SKIP` without failing it —
use it when a precondition is not met (missing environment variable, optional
dependency absent). It never returns.

`ward_assert_eq` compares by value for integers, floats, `WardStr` and C
strings, and by identity for other pointers:

```c
ward_assert_eq(ustr_c("abc"), ustr_c("abc"));   // ok: content compare
ward_assert_eq(buffer, buffer);                 // pointer identity
ward_assert_str_eq("expected", actual);         // text compare, documents intent
```

On failure the runner prints `file:line`, the operand expressions and both
values, then abandons **that case only**. The rest of the run continues.

`WardStr` (`<ward/support/str.h>`) is a `{ text, length }` view with
`ward_str_c`, `ward_str_eq`, `ward_str_hash` — a vendored copy, prefixed so it
cannot collide with a host project's own string helpers.

## 4. Running tests

```
--list-tests          List all discovered test names and exit
--exact=<name>        Run one test in this process (name is 'group:name')
--filter=<pattern>    Only run tests whose name contains <pattern>
--single              Run everything sequentially in this process
--jobs=<n>            Number of parallel test processes (0 = all cpus)
--timeout=<seconds>   Per test time limit, default 30
--fail-fast           Stop scheduling after the first failure
--break-on-failure    Raise SIGTRAP after the first failure (for gdb)
--print-repr          Print a copy-paste command for every failure (default on)
--no-print-repr       Do not print reproducer commands
--no-color            Disable colored status labels
--verbose, -v         Framework debug logging
--help, -h            Show the option list
```

Default mode spawns **one process per test case**:

```
$ ./mytests
[PASS] db:insert
[FAIL] db:remove
test.c:41: assertion failed: count != 0
> expected: 0
> actual:   3
  repro: /home/me/proj/build/mytests --exact=db:remove
[CRASH] db:dump_bug
test process died from Segmentation fault (11)
  repro: /home/me/proj/build/mytests --exact=db:dump_bug
[TIME] db:infinite_loop
test process died from Terminated (15)
TOTAL: 5, PASS: 2, FAIL: 1, CRASH: 1, TIMEOUT: 0, SKIP: 1
```

Statuses: `PASS`, `FAIL` (assertion, or a test that ended the process itself),
`SKIP` (`.ignored`), `CRASH` (fatal signal), `TIME` (deadline exceeded).
`TOTAL` counts cases that actually ran; anything left over after `--fail-fast` is
reported as a separate `NOT RUN: n` line.

Output of a case is captured and printed only if it failed; `--verbose` also
prints output of passing cases. Exit status is `0` when everything passed, `1`
when anything failed/crashed/timed out, `2` for a usage error.

Environment: `NO_COLOR=1` disables color, `WARD_NO_FORK=1` forces `--single`
(useful in a debugger or under a tool that cannot follow forks).

## 5. Debugging a failing test

Every failure prints a reproducer line. Copy it: it runs that one case **in the
current process**, with no fork, no pipes and no output buffering.

```sh
# 1. Reproduce exactly
./build/mytests --exact=db:remove

# 2. Under a debugger: single process, real terminal, stops on the assert
gdb --args ./build/mytests --exact=db:remove

# 3. Record/replay
rr record ./build/mytests --exact=db:remove && rr replay

# 4. Sanitizers: always with --single, so reports are attributable
cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g -fno-omit-frame-pointer" \
                       -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

# 5. Sequential run of the whole suite, still one process
./build/mytests --single
```

A failed assertion in `--exact`/`--single` mode does **not** abort: the case is
reported as `FAIL` and the process exits `1`. That keeps gdb attached at the
exit point with the message already printed, instead of stopping inside
`__builtin_trap` with nothing on screen.

In `--single`, timeouts use `SIGALRM` (whole seconds, rounded up). The runner
saves and restores the disposition around each case and warns when a test
replaced the handler, but a test that *blocks* `SIGALRM` can still escape its
deadline — the parallel runner does not have that weakness, which is why it is
the default.

## 6. CMake integration

### Shortcut: CMake helpers

The package ships two CMake functions (in `cmake/wardHelpers.cmake`, included
automatically by the `ward::ward` package):

```cmake
ward_add_test_executable(mytests
    SOURCES test/stack_test.c test/db_test.c
    LIBS    my_core
    TIMEOUT 300
)
```

This creates `mytests_lib` (holding the test sources), `mytests` (the
executable linked against `ward::ward`), and a CTest entry. It handles
`--whole-archive` for you. Add `OBJECT` to use an object library instead of
static (avoids the `--whole-archive` requirement entirely).

For one CTest entry per test case (CI dashboards that need to show *which*
case failed):

```cmake
ward_add_per_case_tests(mytests TIMEOUT 60)
```

This requires a two-step configure: build once to generate the test list, then
re-configure to register the per-case entries.

### Option A: `add_subdirectory`

```cmake
add_subdirectory(third_party/ward)

add_executable(mytests
    test/stack_test.c
    test/db_test.c
)
target_link_libraries(mytests PRIVATE ward::ward)

include(CTest)                       # at the top level, once
add_test(NAME mytests COMMAND mytests --no-color --print-repr)
set_tests_properties(mytests PROPERTIES TIMEOUT 300)
```

Ward's own self tests are configured only when it is the top-level project
(`WARD_BUILD_TESTS` defaults to `PROJECT_IS_TOP_LEVEL`), so adding it as a
subdirectory does not pollute your CTest suite.

### Option B: install + `find_package`

```sh
cmake -S ward -B ward/build -DCMAKE_INSTALL_PREFIX=/opt/ward
cmake --build ward/build && cmake --install ward/build
```

```cmake
find_package(ward REQUIRED)          # -DCMAKE_PREFIX_PATH=/opt/ward
target_link_libraries(mytests PRIVATE ward::ward)
```

### Registering with CTest

The cheap and correct shape is **one CTest entry per test binary** and let ward
do the per-case scheduling and parallelism inside it:

```cmake
add_test(NAME mytests COMMAND mytests --no-color --print-repr)
set_tests_properties(mytests PROPERTIES TIMEOUT 300)
```

Ward already runs one process per case in parallel, so `ctest -j` on top of that
buys little. If you want one CTest entry per case (for a dashboard that tracks
individual cases), generate them from `--list-tests` in a separate step and add
`add_test(NAME "case:${name}" COMMAND mytests --exact=${name})` per line.

Useful targets while developing:

```sh
cmake --build build -j && ctest --test-dir build --output-on-failure
ctest --test-dir build -R mytests        # filter by CTest name
./build/mytests --filter=db:             # filter by ward test name
```

### Things that break discovery

Discovery reads the `ward_test_desc` section of the final executable, so:

- **Tests in a static library that nothing references need `--whole-archive`.**
  Otherwise the linker never pulls the object in and the tests silently vanish.
  With CMake ≥ 3.24:

  ```cmake
  target_link_libraries(mytests PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,my_test_lib>)
  ```

  Tests compiled directly into the executable (`add_executable(mytests test/*.c)`)
  need nothing.
- **`-Wl,--gc-sections`** can collect the descriptor section; add
  `-Wl,--keep-section=ward_test_desc` if you use it.
- **Custom link scripts** that drop unknown sections lose the tests.
- Section *names* must stay dotless: the dotless name is what makes the linker
  emit `__start_ward_test_desc` / `__stop_ward_test_desc`.

## 7. How discovery works, and its limits

`Test(...)` emits a descriptor directly into the `ward_test_desc` section:

```c
__attribute__((used, section("ward_test_desc"), aligned(8)))
static const struct WardTestDesc ward_test_db_insert_desc = {
    .group = "db", .name = "insert", .fn = &ward_test_db_insert_fn,
    .file = "test/db_test.c", .line = 12,
};
```

Because the section name is a valid C identifier, GNU ld and lld automatically
define `__start_ward_test_desc` and `__stop_ward_test_desc` around it. The runner
walks that range. Consequences:

- No registration functions, no `__attribute__((constructor))`, no global
  initialisers.
- No runtime ELF parsing and no `/proc/self/exe` reading for discovery, so a
  stripped binary works and there is nothing async-signal-unsafe near a fork.
- The section bounds are weak symbols, so a binary with zero tests links and
  reports "no tests" instead of failing.

Test/suite descriptors are `const` and placed in a read-only section; the
runner's mutable model (`struct WardCase`, `struct WardSuite`) is built next to
them in an arena.

## 8. Reference

### Exit statuses

| Code | Meaning |
| --- | --- |
| 0 | every selected case passed or was skipped |
| 1 | at least one case failed, crashed or timed out |
| 2 | usage error, unknown/duplicate test name, duplicate suite declaration |

### Internal protocol (do not use from tests)

The parallel runner spawns children as
`<exe> --_ward-child --exact=<name>`, with stdout/stderr on a pipe and stdin on
`/dev/null`. `--_ward-child` tells the child two things:
its stdout/stderr are captured by the parent (so it must not print status lines),
and the parent owns the deadline (so the child disables its own `SIGALRM`). The
child reports success by exiting with status `87` (`WARD_CHILD_OK`); any other
status is a failure. That is why a test calling `exit(0)` itself is reported as
`FAIL` rather than looking like a pass.

`--_ward-program=<exe>` overrides the executable used for children and exists
only for testing the runner.

### Layout

```
include/ward/ward.h            public API: Test, TestSuite, assertions
include/ward/internal/api.h    macro support, descriptors (not for tests)
include/ward/support/          vendored str / arena / map / fatal helpers
src/internal/ward.c            main(), assertion reporting
src/internal/discover.c        section walking, name/suite model, filtering
src/internal/serial.c          --single and --exact runner (unwind + SIGALRM)
src/internal/proc_linux.c      posix_spawn + pidfd + epoll parallel runner
src/internal/report.c          status lines, output capture, summary
test/                          self tests + deliberately broken cases
```
