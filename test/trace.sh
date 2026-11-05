#!/bin/sh
# Exercise diagnostics, not just failure status. addr2line is optional.
set -eu
binary="$1"
debug="${2:-OFF}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

run_failure() {
    status=0
    "$binary" --no-color "$@" > "$work/out" 2>&1 || status=$?
    test "$status" -eq 1
    grep -q 'ward: backtrace' "$work/out"
    grep -Eq '^0x[0-9a-f]|fault registers unavailable' "$work/out"
}

run_failure --exact=negative:assertion_fails
if command -v addr2line >/dev/null 2>&1; then
    grep -q 'ward_negative_assertion_fails' "$work/out"
    if [ "$debug" = ON ]; then
        grep -q 'negative_test.c:[0-9]' "$work/out"
    fi
fi

mkdir "$work/tools"
original_path="$PATH"
PATH="$work/tools"
export PATH
# No executable named addr2line: diagnostics and FAIL must survive.
status=0
"$binary" --no-color --exact=negative:assertion_fails > "$work/out" 2>&1 || status=$?
PATH="$original_path"
export PATH
test "$status" -eq 1
grep -q '^0x[0-9a-f]' "$work/out"
grep -q 'source symbolization unavailable' "$work/out"

if command -v strip >/dev/null 2>&1; then
    cp "$binary" "$work/stripped"
    strip --strip-all "$work/stripped"
    status=0
    "$work/stripped" --no-color --exact=negative:assertion_fails > "$work/out" 2>&1 || status=$?
    test "$status" -eq 1
    grep -q '^0x[0-9a-f]' "$work/out"
    grep -q '\[FAIL\]' "$work/out"
fi

# A tool that starts but fails must be equally harmless.
printf '#!/bin/sh\nexit 1\n' > "$work/tools/addr2line"
chmod +x "$work/tools/addr2line"
PATH="$work/tools:$original_path" run_failure --exact=negative:assertion_fails
grep -q 'source symbolization unavailable' "$work/out"

# A stuck tool must not hang --exact, which has no supervising runner process.
printf '#!/bin/sh\nexec /bin/sleep 30\n' > "$work/tools/addr2line"
status=0
PATH="$work/tools:$original_path" timeout 6 "$binary" --no-color \
    --single --test=negative:assertion_fails > "$work/out" 2>&1 || status=$?
test "$status" -eq 1
grep -q 'source symbolization unavailable' "$work/out"

# Crash isolation and late diagnostics require Linux pidfds.
if [ "${3:-OFF}" = ON ]; then
    run_failure --filter=negative:crashes_with_segv
    grep -q '\[CRASH\]' "$work/out"
    if command -v addr2line >/dev/null 2>&1 && [ "$debug" = ON ]; then
        grep -q 'negative_test.c:21' "$work/out"
    fi
    run_failure --filter=negative:prints_a_lot
    grep -q 'output truncated' "$work/out"
    grep -q 'assertion failed' "$work/out"
fi
# --exact must retain the original fatal signal, not turn it into exit(1).
ulimit -c 0
status=0
"$binary" --no-color --exact=negative:crashes_with_segv > "$work/out" 2>&1 || status=$?
test "$status" -eq 139
grep -q 'ward: backtrace' "$work/out"
echo 'trace.sh: ok'
