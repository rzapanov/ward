#!/bin/sh
# The runner must list every test exactly once, in a stable order.
set -eu

binary="$1"
list=$("$binary" --list-tests)

for expected in \
    ward:arithmetic \
    ward:pointers \
    ward:strings_compare_by_content \
    ward_suite:declared_hooks_run_around_every_case \
    ward:ignored_case_is_skipped
do
    printf '%s\n' "$list" | grep -qx "$expected" || {
        echo "missing test in --list-tests output: $expected" >&2
        exit 1
    }
done

count=$(printf '%s\n' "$list" | wc -l)
duplicates=$(printf '%s\n' "$list" | sort | uniq -d)
if [ -n "$duplicates" ]; then
    echo "duplicate test names reported: $duplicates" >&2
    exit 1
fi

# A filter must narrow the selection without touching the listing.
filtered=$("$binary" --filter=ward:strings --list-tests | wc -l)
if [ "$filtered" -ne "$count" ]; then
    echo "--list-tests must ignore --filter" >&2
    exit 1
fi

echo "list_tests.sh: ok ($count tests)"
