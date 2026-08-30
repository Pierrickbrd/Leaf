#!/usr/bin/env bash
# The window opens, and QML found its way to it.
#
# Compiling proves nothing about this: the module's resource path moved between Qt 6.4 and
# 6.5, so a build that links perfectly can still exit at once with "no such file or
# directory". It happened twice in the first hour.
#
#     tests/opens.sh <path-to-leaf-desktop>
set -u

binary="${1:-build/leaf-desktop}"
log=$(mktemp)
trap 'rm -f "$log"' EXIT

# Offscreen, so it runs the same on a machine with no display and in a container.
QT_QPA_PLATFORM=offscreen timeout 6 "$binary" > "$log" 2>&1
outcome=$?

if [[ $outcome -ne 124 ]]; then
    echo "✗ it exited on its own with $outcome — a window that opens does not stop"
    cat "$log"
    exit 1
fi
if grep -qiE "failed to load|no such file|error" "$log"; then
    echo "✗ it ran, and said:"
    cat "$log"
    exit 1
fi
echo "✓ the window opens, and stays"
