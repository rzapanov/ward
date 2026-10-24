#!/bin/sh
# Verifies the runner's failure reporting by running deliberately broken cases
# one at a time, so that a crash cannot take the whole suite down with it.
#
# usage: negative.sh <negative-test-binary> <has-pidfd:ON|OFF>
set -eu

binary="$1"
has_pidfd="${2:-ON}"
failures=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# expect <label> <expected-text> <args...>
# The runner must exit non-zero and print the expected text.
expect() {
    label="$1"
    expected="$2"
    shift 2
    status=0
    "$binary" "$@" --no-color > "$work/out" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        echo "FAIL $label: expected a non-zero exit status" >&2
        head -c 2000 "$work/out" >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -q "$expected" "$work/out"; then
        echo "FAIL $label: expected to see '$expected'" >&2
        head -c 2000 "$work/out" >&2
        failures=$((failures + 1))
        return
    fi
    echo "ok   $label"
}

# Single process, no fork: exercises the unwind path of a failed assertion.
expect assertion        '\[FAIL\] negative:assertion_fails'    --exact=negative:assertion_fails
expect message          '\[FAIL\] negative:message_fails'      --exact=negative:message_fails
expect single_mode      '\[FAIL\] negative:assertion_fails'    --single --test=negative:assertion_fails
expect unknown_test     'no test named'                    --exact=negative:nope

if [ "$has_pidfd" = "ON" ]; then
    # Only the multi-process runner can survive a crashing, hanging or
    # self-terminating case, so those go through --filter (one test per run).
    expect segv           '\[CRASH\] negative:crashes_with_segv' --filter=negative:crashes_with_segv
    expect abort          '\[CRASH\] negative:aborts'            --filter=negative:aborts
    expect timeout        '\[TIME\] negative:hangs'              --filter=negative:hangs
    expect early_exit     '\[FAIL\] negative:exits_the_process'  --filter=negative:exits_the_process
    expect closed_stdout  '\[FAIL\] negative:closes_stdout'      --filter=negative:closes_stdout
    expect truncated      'output truncated'                 --filter=negative:prints_a_lot

    # A case with a generous timeout must pass even while its siblings crash.
    status=0
    "$binary" --no-color --filter=negative:slow_but_fine > "$work/out" 2>&1 || status=$?
    if [ "$status" -ne 0 ] || ! grep -q '\[PASS\] negative:slow_but_fine' "$work/out"; then
        echo "FAIL slow_but_fine: expected a clean pass" >&2
        head -c 2000 "$work/out" >&2
        failures=$((failures + 1))
    else
        echo "ok   slow_but_fine"
    fi

    # Everything at once: the runner must finish, classify every kind of
    # outcome, and keep going after crashes and timeouts.
    status=0
    "$binary" --no-color --jobs=4 > "$work/batch" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        echo "FAIL batch: expected non-zero exit" >&2
        failures=$((failures + 1))
    fi
    for expected in '\[CRASH\] negative:crashes_with_segv' '\[CRASH\] negative:aborts' \
                    '\[TIME\] negative:hangs' '\[FAIL\] negative:assertion_fails' \
                    '\[FAIL\] negative:exits_the_process' \
                    'repro: .*--exact=negative:assertion_fails'
    do
        if ! grep -q "$expected" "$work/batch"; then
            echo "FAIL batch: missing '$expected'" >&2
            head -c 2000 "$work/batch" >&2
            failures=$((failures + 1))
        fi
    done
    if grep -q '\[PASS\] negative:prints_a_lot' "$work/batch"; then
        echo "FAIL batch: a failing case must not be reported as PASS" >&2
        failures=$((failures + 1))
    fi
    echo "ok   batch"
else
    echo "skip: no pidfd support, multi-process checks not applicable"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures negative-case check(s) failed" >&2
    exit 1
fi
echo "negative.sh: ok"
