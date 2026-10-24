# AGENTS.md

Standalone C23 unit test framework (`ward`), extracted from the `seal` project.
Linux only for the parallel runner; no external dependencies.

## Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure          # full self test suite (7 entries)

./build/test/ward_self_tests --list-tests           # names are group:name
./build/test/ward_self_tests --exact=ward:arithmetic   # single process, no fork
./build/test/ward_self_tests --single               # sequential, in-process
./build/test/ward_self_tests --jobs=4 --filter=ward:

cmake --install build --prefix /tmp/inst            # static lib + ward::ward package
```

- `ctest` works from the root build dir (`include(CTest)` is at the top level).
- ASan/UBSan are **not** available in this container (`libasan` is missing), so
  sanitizer builds fail at link time. Do not chase that.
- `WARD_ENABLE_IPO` (LTO) is OFF by default; `-DWARD_ENABLE_IPO=ON` builds fine
  and discovery survives it.
- Only warning: `-Warray-bounds` on the deliberate null-deref in
  `test/negative/negative_test.c`. That is intentional.

## What lives where

| Path | Role |
| --- | --- |
| `include/ward/ward.h` | Public API: `Test`, `TestSuite`, `ward_assert*` |
| `include/ward/internal/api.h` | Descriptor structs, `_Generic` dispatch used by the macros |
| `include/ward/support/` | Vendored `str`/`arena`/`map`/`fatal`, all `ward_`-prefixed |
| `src/internal/ward.c` | `main()`, assertion formatting, child exit protocol |
| `src/internal/discover.c` | Section walk, suite/case model, duplicate detection, filtering |
| `src/internal/serial.c` | `--single` / `--exact` runner: `sigsetjmp` unwind + `SIGALRM` timeout |
| `src/internal/proc_linux.c` | Parallel runner: `posix_spawn` + pidfd + epoll |
| `src/internal/report.c` | Status lines, captured output, reproducer line, summary |
| `test/` | `basic_test.c` (must stay green), `negative/` (must fail), `*.sh` harnesses |

## Discovery: the thing to understand first

`Test(g, n)` emits a descriptor **directly into the `ward_test_desc` section**;
`TestSuite(g)` into `ward_suite_desc`. The names are dotless on purpose: only a
section whose name is a valid C identifier gets `__start_<name>` /
`__stop_<name>` emitted by GNU ld and lld. `discover.c` walks those ranges
through weak symbols, so a binary with zero tests still links.

- There is **no** runtime ELF parsing, no `/proc/self/exe` mmap, no constructor
  registration. Keep it that way: the previous in-tree design parsed the on-disk
  ELF of every loaded module, which is why it needed a fork-safe environment and
  broke on stripped binaries.
- Descriptors are `const` in a read-only section. Never mutate them; the mutable
  model is `struct WardCase` / `struct WardSuite` built in `runner->arena`.
- Tests compiled into a **static library that nothing references** are dropped by
  the linker unless the consumer uses `--whole-archive`. `test/lib_tests/` covers
  exactly that case; the runner prints a hint when zero tests are discovered.
- `Test`/`TestSuite` are variadic with a `WARD_TEST_NOTHING()` sentinel so
  `Test(g, n)` and `Test(g, n, .timeout_seconds = 5)` both work. Macro parameters
  are named `ward_g`/`ward_n` because `##` pasting would otherwise substitute the
  argument tokens into `.group`/`.name`. Do not rename them back.

## Runner invariants

- **Never `fork()` + stdio + `execv()`.** The parallel path uses `posix_spawn`
  with file actions and `POSIX_SPAWN_SETSIGMASK | SETSIGDEF`; nothing runs in the
  child between fork and exec. The original framework did `fork` → `dup2` →
  `printf`/`malloc`/`fflush` → `execv`, which deadlocked on inherited stdio/malloc
  locks and duplicated buffered output. That is the hang this repo exists to fix.
- The runner is **single threaded on purpose**. No pthreads anywhere: a lost
  thread holding a lock at spawn time is exactly the bug class above.
- Completion is observed on a **pidfd** (`waitid(P_PIDFD, ..., WEXITED|WNOHANG)`),
  never on pipe EOF. A test that closes its own stdout must not look finished.
- Timeouts are absolute deadlines recomputed from `CLOCK_MONOTONIC` every loop
  iteration, escalating `SIGTERM` → 1s grace → `SIGKILL`. Do not subtract the
  epoll timeout from a per-slot counter; that drifts.
- Child result protocol: exit `87` (`WARD_CHILD_OK`) means pass. Anything else is
  a failure, which is what makes a test calling `exit(0)` report `FAIL` instead of
  silently passing. `--_ward-child` also disables the child's own `SIGALRM` timer
  so only the parent produces `TIME`.
- Assertion failures `siglongjmp` back to the case driver (serial) or `_exit(1)`
  (child). They never `abort()`/`__builtin_trap()`, so a failure is a `FAIL` and a
  real crash is a `CRASH` — and gdb stays usable.
- Suite `fini` runs even after a failed assertion (`current.body_finished` gate).
- Output is captured per case and echoed only for non-PASS results, unless
  `--verbose`. Truncation limit is `WARD_MAX_OUTPUT_SIZE` (128 KiB).
- Children get stdin from `/dev/null` (a test must not steal the runner's
  terminal, and a read must be EOF rather than a hang). Keep that file action.
- The fatal-signal handler is hand-rolled async-signal-safe writing (no
  `snprintf`, no `strsignal`, no stdio). Do not "simplify" it back.
- `--single` saves and restores the `SIGALRM` disposition around every case and
  warns if the test replaced the handler; `runner->attempted` drives the summary
  so `--fail-fast` runs print an honest `NOT RUN: n`.

## Test harness conventions

- `test/basic_test.c` must pass in **all** modes; ctest runs it in parallel,
  `--single`, `--jobs=4` and with a filter.
- Anything that crashes, hangs, calls `exit`, or closes stdio goes in
  `test/negative/negative_test.c` and gets asserted by `test/negative.sh`
  (grep for `\[FAIL\]`/`\[CRASH\]`/`\[TIME\]` brackets — the labels are
  bracketed, so a bare `FAIL foo` pattern never matches).
- `negative.sh` takes `<binary> <ON|OFF>`; the second argument is
  `${WARD_HAS_PIDFD}` from `test/CMakeLists.txt`, so the suite degrades instead
  of failing on kernels without pidfd.
- After touching the runner: build, run `ctest`, then stress the parallel path
  (`for i in $(seq 60); do ward_self_tests --jobs=8; done`) to catch races and
  hangs a single run hides.

## Style

- C23 with GNU extensions: `__auto_type`/`auto`, designated initializers,
  compound literals, statement expressions, `_Generic`. GCC/Clang only.
- 4-space indent, ~100 columns, snake_case, `ward_` prefix on everything that
  leaves a translation unit. Static by default; only the assertion helpers and
  `main` are external.
- Internal errors (a broken runner) use `WARD_FATAL(...)` → `abort()`. Test
  failures never use it.
- Commit subjects: `feat: ...` / `fix: ...`, lowercase, short.

## Known gaps

Read [IMPROVEMENTS.md](IMPROVEMENTS.md) before adding features — it is the
reviewed backlog, sorted by impact, and it already lists what is done. Notable
holes today: no `--repeat`/`--shuffle`, no per-case temp dir, no macOS/BSD runner
(falls back to serial), `--single` timeouts rely on `SIGALRM`.
