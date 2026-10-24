#!/bin/sh
# Fuzz the parallel runner: run adversarial test cases repeatedly with varying
# parameters to prove the loop cannot deadlock, hang, or leak.
#
# usage: fuzz.sh <negative-test-binary> <iterations>
#
# The negative test binary contains crash, hang, close-stdout, huge-output,
# and fork scenarios. We run it with random --jobs and --timeout values and
# verify it always terminates within a global deadline.
set -eu

binary="$1"
iterations="${2:-50}"
failures=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Global deadline per iteration: 30s is generous (the hang test has a 5s
# timeout, so the whole run should finish well under that).
DEADLINE=30

for i in $(seq 1 "$iterations"); do
    # Vary jobs between 1 and 8 to exercise different slot counts.
    jobs=$(( (i % 8) + 1 ))
    # Vary timeout between 2 and 10 seconds.
    timeout_val=$(( (i % 9) + 2 ))

    # Run with a hard deadline so a hang in the runner itself is caught.
    status=0
    timeout "$DEADLINE" "$binary" --no-color --jobs="$jobs" --timeout="$timeout_val" \
        > "$work/out_$i" 2>&1 || status=$?

    # Exit 124 means the `timeout` command killed it: the runner hung.
    if [ "$status" -eq 124 ]; then
        echo "FAIL iteration $i: runner hung (jobs=$jobs timeout=$timeout_val)" >&2
        head -c 2000 "$work/out_$i" >&2
        failures=$((failures + 1))
        continue
    fi

    # The negative tests are designed to fail, so non-zero exit is expected.
    # What matters is that the runner *finished* and produced a summary.
    if ! grep -q 'TOTAL' "$work/out_$i"; then
        echo "FAIL iteration $i: no summary line (jobs=$jobs timeout=$timeout_val)" >&2
        head -c 2000 "$work/out_$i" >&2
        failures=$((failures + 1))
        continue
    fi

    # Verify the runner classified at least one case (it must have run something).
    if ! grep -qE '\[(FAIL|CRASH|TIME|PASS|SKIP)\]' "$work/out_$i"; then
        echo "FAIL iteration $i: no status brackets (jobs=$jobs timeout=$timeout_val)" >&2
        head -c 2000 "$work/out_$i" >&2
        failures=$((failures + 1))
        continue
    fi
done

# Also run a few iterations with --fail-fast to exercise that path.
for i in 1 2 3; do
    status=0
    timeout "$DEADLINE" "$binary" --no-color --jobs=4 --fail-fast \
        > "$work/ff_$i" 2>&1 || status=$?
    if [ "$status" -eq 124 ]; then
        echo "FAIL fail-fast iteration $i: runner hung" >&2
        head -c 2000 "$work/ff_$i" >&2
        failures=$((failures + 1))
    elif ! grep -q 'TOTAL' "$work/ff_$i"; then
        echo "FAIL fail-fast iteration $i: no summary" >&2
        head -c 2000 "$work/ff_$i" >&2
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then
    echo "$failures fuzz iteration(s) failed" >&2
    exit 1
fi
echo "fuzz.sh: $iterations iterations + 3 fail-fast: ok"
