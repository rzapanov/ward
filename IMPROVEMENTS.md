# Improvement plan

Backlog only — nothing below is implemented. Sorted by **impact / necessity**:
P0 items are correctness or safety holes that can produce a wrong green or a
stuck CI, P1 items are what makes the framework pleasant enough to adopt, P2/P3
are progressively more optional.

Each item lists the concrete gap today, the proposed shape, and rough cost
(`S` < 1h, `M` a day, `L` multi-day).

## Already handled while extracting this repo

Not backlog, listed so nobody re-proposes them: link-time discovery through
linker section bounds (no runtime ELF parsing, survives stripping); `posix_spawn`
+ pidfd isolation instead of `fork`/`dup2`/`execv`; per-case deadlines with
SIGTERM→SIGKILL escalation; crashes, `exit()` calls and `close(stdout)` contained
to one case; reproducer lines; `--single`/`--exact` single-process debugging;
`.ignored` → SKIP; duplicate test-name and duplicate suite detection; the
static-library `--whole-archive` discovery case; child stdin redirected from
`/dev/null`; async-signal-safe fatal-signal handler; `SIGALRM` disposition saved
and restored per case in `--single`; `NOT RUN:` accounting in the summary;
install + `find_package(ward)`; `ward_assert_close` float tolerance assertion;
`--single` requires `--test` (timeout-free debug mode); process-group signaling
on timeout/fail-fast; `--timeout=0` means no timeout; `ward_main()` for
consumers that own `main()`; `ward_skip()` runtime skip; `ward_add_test_executable`
CMake helper; `ward_add_per_case_tests` per-case CTest entries; `--break-on-failure`
SIGTRAP in `--exact` mode; runner fuzz harness (`test/fuzz.sh`).

---

## P0 — correctness and "never report a false green"

### 4. Suggest close names on a lookup miss
`--exact=typo` and `--filter=stale_prefix:` both exit 2 with a one-line message.
Print the closest known test names (prefix + edit distance) so a rename does not
cost 10 minutes of grepping `--list-tests`.
Impact: medium (developer time). Cost: S.

---

## P1 — usability: what blocks adoption

### 10. Machine-readable output: `--format=junit|tap`
CI wants per-case durations and statuses without scraping colour codes. JUnit XML
is the common denominator (GitLab, Jenkins, TeamCity); TAP is about 20 lines.
Impact: medium-high for CI users. Cost: M.

### 11. `--repeat=N` and `--shuffle[=seed]`
Order-dependent state leaks (shared globals, temp files, caches) are the most
common flakiness in C suites, and today the only way to surface them is to
hand-edit file names. Print the seed so a bad order is reproducible.
Impact: medium-high. Cost: M (mostly serial-mode plumbing).

### 12. Per-case temp dir via `$WARD_TMP`
Tests that write files collide with each other under `--jobs>1` or leave litter.
Create `/tmp/ward-<pid>/<n>`, export it, remove it after the case, keep it on
failure with `--keep-tmp`.
Impact: medium-high (unblocks parallelising file-using tests). Cost: M.

### 13. Golden-file / snapshot assertions
Parser-style tests compare large text dumps inline. Add
`ward_assert_text_eq(actual, "expected/dump.txt")` with `--update-golden` to
rewrite, and a real line diff on mismatch. Big ergonomics win for any project
with textual IR output.
Impact: medium-high for such projects, low for others. Cost: M.

### 14. Better string mismatch reporting
Long strings print in full and unescaped. Add length, first differing offset and
a short window around it; escape control characters.
Impact: medium. Cost: S.

### 15. `--ordered-output`
Parallel mode prints in completion order, so two runs of the same suite look
different and logs do not diff. Buffer per-case reports and flush them in
registration order (or failure-first).
Impact: medium. Cost: M.

### 16. Filter semantics: anchored prefix and globs
`--filter=db` is a substring, so it also matches `dbfoo:bar`. Support
`--filter=db:` (group prefix), `--filter='*:name'` globs, and repeated
`--exact=a --exact=b`.
Impact: medium. Cost: S/M.

### 17. Suite scopes: once-per-suite and per-case fixtures
`init`/`fini` run per case, which is the safe default but forces expensive setup
(parse a corpus, build an index) to run N times. Add `.suite_init` /
`.suite_fini` (once per group, with a documented caveat about parallelism), or a
`ward_once()` helper.
Impact: medium. Cost: M.

### 18. Fixture context pointers
`init`/`fini` take no arguments, so fixtures must be file-scope globals — which is
exactly why `--single` plus `--repeat` misbehaves. Add
`void (*init)(void **ctx)` / `void (*fini)(void *ctx)` and expose `ctx` to the
case via `ward_ctx()`.
Impact: medium. Cost: M (API change; do it before 1.0).

### 19. `--quiet`, `--show-output`, colour control
`--quiet` prints only failures plus the summary; `--show-output` echoes output of
passing cases without full `--verbose` framework logging. Colour should also
honour `TERM=dumb` and allow `WARD_COLOR=always` when piped.
Impact: medium. Cost: S.

---

## P2 — runner capability and portability

### 20. macOS/BSD parallel runner
`proc.c` falls back to serial everywhere except Linux, so macOS users lose
isolation and timeouts entirely. `fork` + `execv` (with the same "nothing between
fork and exec" discipline), `kqueue`/`EVFILT_PROC` and `waitpid` give the same
guarantees without pidfd.
Impact: medium-high for mixed teams. Cost: L.

### 21. Death tests: `ward_assert_dies(signal, expr)`
Verifying that a bad input aborts rather than corrupts is currently impossible
inside the framework. Needs a forked sub-case reusing the capture plumbing.
Impact: medium. Cost: M.

### 22. Per-case durations and `--slowest=N`
`TOTAL`/`PASS` says nothing about why CI is slow. Record start/stop per slot,
print the N slowest, and warn when a case finishes within 20% of its deadline.
Impact: medium. Cost: S/M.

### 23. `--retry=N` for known-flaky cases
Re-run a failing case before reporting FAIL, and label it `FLAKY` when the retry
passes so it stays visible instead of being hidden.
Impact: medium. Cost: M.

### 24. Env hygiene for children
Set `WARD_TEST=1` and `WARD_TEST_NAME=group:name`; unset `MAKEFLAGS`,
`NINJA_STATUS`, `CTEST_*` so a test that shells out to a build tool cannot
recurse into the build that is running it.
Impact: low-medium. Cost: S.

### 25. Configurable capture limit
`WARD_MAX_OUTPUT_SIZE` is a compile-time 128 KiB. Make it `--max-output=<KiB>`
and print the first *and* last lines when truncating (the interesting part of a
failing dump is often at the end).
Impact: low-medium. Cost: S.

### 26. Windows runner
`posix_spawn`, pidfd, epoll and ELF sections are all Linux/POSIX specific.
Windows needs `CreateProcess` + job objects and a different registration
mechanism (`.CRT$XCU` or a linker `.sect` directive). Only worth it with a real
consumer.
Impact: low today. Cost: L.

---

## P3 — polish

### 28. `ward_assert_mem_eq`, `ward_assert_ptr_eq`, `ward_assert_in_range`
Small expressiveness additions; `mem_eq` should print a 16-byte hex diff.
Cost: S.

### 29. Coverage helper target
A `ward_coverage` target that configures with `--coverage`, runs the suite and
calls `lcov`/`gcovr`. Cost: S.

### 30. CI workflow plus a sanitizer job
Matrix over Debug/Release, LTO on/off, ASan/UBSan. The dev container here has no
`libasan`, so this needs a runner image that ships sanitizers. Cost: S.

### 32. Docs and packaging extras
Porting guide from the in-tree `ward` (the `f_assert*` aliases are kept, but
`Test`/`TestSuite` semantics changed slightly), a `CHANGELOG.md`, and a `LICENSE`
(currently absent). Cost: S.

---

## Suggested order

1. **Batch A (P0):** #4 close names. A few hours, closes the remaining
   "wrong or confusing" outcomes.
2. **Batch B (CI):** #10 JUnit/TAP, #22 durations, #30 CI job, #11 shuffle/repeat.
3. **Batch C (API freeze before 1.0):** #18 fixture contexts, #17 suite scopes,
   #16 filter semantics, #13 golden files.
4. **Then** #20 macOS runner and whatever of P2/P3 real usage demands.
