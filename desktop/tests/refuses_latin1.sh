#!/usr/bin/env bash
# A file that must not compile, and must not compile for the stated reason.
#
# `Ascii.h` refuses a non-ASCII byte in a Latin-1 literal at compile time, which means no
# test that runs can show the guard is still there. Only a build that fails can — and a build
# that fails for a typo would pass just as well, so the reason is checked too.
#
#     tests/refuses_latin1.sh <build-directory>
set -u

build="${1:-build}"
log=$(mktemp)
trap 'rm -f "$log"' EXIT

# target, what its failure must say, what it is guarding
set -- \
    "mangles-utf8"   "not ASCII"                        "a non-ASCII byte in a Latin-1 literal" \
    "reaches-for-l1" "deleted function\|is ambiguous"    "Qt's own Latin-1 suffix"

while [ $# -gt 0 ]; do
    target=$1 expected=$2 guards=$3
    shift 3

    cmake --build "$build" --target "$target" > "$log" 2>&1
    if [ $? -eq 0 ]; then
        echo "✗ $target compiled. $guards is no longer refused."
        exit 1
    fi
    if ! grep -q "$expected" "$log"; then
        echo "✗ $target failed, but not for the reason this test is about ($guards):"
        cat "$log"
        exit 1
    fi
    echo "✓ $guards — refused at compile time"
done
