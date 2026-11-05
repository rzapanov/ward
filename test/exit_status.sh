#!/bin/sh
set -eu
binary="$1"
executors="$2"
has_pidfd="${3:-OFF}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

expect() {
    expected="$1"
    shift
    actual=0
    "$@" --no-color > "$work/out" 2>&1 || actual=$?
    if [ "$actual" -ne "$expected" ]; then
        cat "$work/out" >&2
        echo "expected exit $expected, got $actual: $*" >&2
        exit 1
    fi
}

for exe in "$binary" "$executors"; do
    # WARD_NO_FORK selects the serial suite runner, unlike --exact/--single.
    expect 1 env WARD_NO_FORK=1 "$exe" --filter=negative:assertion_fails
    expect 1 env WARD_NO_FORK=1 "$exe" --filter=negative:message_fails --fail-fast
    expect 0 env WARD_NO_FORK=1 "$exe" --filter=negative:slow_but_fine
    expect 1 "$exe" --exact=negative:assertion_fails
    expect 1 "$exe" --single --test=negative:assertion_fails
    if [ "$has_pidfd" = ON ]; then
        # No options: exercise the framework-provided default main with a mix
        # of passing, failing, crashing, and timed-out cases.
        expect 1 "$exe"
        expect 1 "$exe" --filter=negative:assertion_fails
        expect 1 "$exe" --filter=negative:crashes_with_segv
        expect 1 "$exe" --filter=negative:hangs
        expect 1 "$exe" --filter=negative:exits_the_process
        expect 1 "$exe" --jobs=4 --fail-fast
        expect 0 "$exe" --filter=negative:slow_but_fine
    fi
done
echo 'exit_status.sh: ok'
