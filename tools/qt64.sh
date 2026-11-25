#!/usr/bin/env bash
# Build the client and run its tests against Qt 6.4, the floor `find_package(Qt6 6.4)` states.
#
# CI runs on Ubuntu 24.04, whose packages are Qt 6.4.2; a developer machine is usually newer.
# A construct that exists only in the newer Qt compiles at home and fails in CI —
# `std::views::as_const` on a `QKeyValueRange` did, and cost a push to find out. This is the
# CI build, in a container, without the push.
#
#     tools/qt64.sh                    build, then every test
#     tools/qt64.sh build [args]       build only; args go to `cmake --build`
#     tools/qt64.sh test [ctest args]  tests only, in a build that already exists
#
# LEAF_QT64_DIR holds the build tree and the compiler cache (default /tmp/leaf-qt64). A build
# of the client weighs well over a gigabyte: point it at a disk, not at a memory-backed /tmp.
# LEAF_QT64_JOBS caps the compile jobs (default 6).
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
work=${LEAF_QT64_DIR:-/tmp/leaf-qt64}
jobs=${LEAF_QT64_JOBS:-6}
image=leaf-qt64
mkdir -p "$work"

# The package list of .github/actions/qt/action.yml, plus what a runner has and a bare image
# does not: a compiler, git, and what the guards that run under
# ctest need. Read from the action rather than copied, so the two cannot disagree.
if ! docker image inspect "$image" > /dev/null 2>&1; then
    packages=$(python3 - "$root/.github/actions/qt/action.yml" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
block = text.split("--no-install-recommends", 1)[1]
block = block.split("# apt leaves", 1)[0]
words = re.sub(r"-o \S+|\\", " ", block).split()
print(" ".join(words))
PY
)
    docker build -t "$image" - <<DOCKERFILE
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    $packages g++ git ca-certificates python3 python3-yaml zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
fi

step=${1:-all}
[[ $# -gt 0 ]] && shift

run() {
    local script=$1
    shift
    # --init: xvfb-run waits for a SIGUSR1 from Xvfb, which a container's PID 1 ignores —
    # without it the first test hangs before it starts.
    docker run --rm --init --user "$(id -u):$(id -g)" \
        -e HOME=/work/home -e CCACHE_DIR=/work/ccache \
        -e QT_QPA_PLATFORM=offscreen \
        -v "$root":/src:ro -v "$work":/work -w /work \
        "$image" bash -euc "$script" _ "$@"
}

build="mkdir -p /work/home
    cmake -S /src/desktop -B /work/build -G Ninja -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    cmake --build /work/build -j $jobs \"\$@\""
test='cd /work/build && xvfb-run -a ctest --output-on-failure "$@"'

case $step in
    build) run "$build" "$@" ;;
    test)  run "$test" "$@" ;;
    all)   run "$build"; run "$test" ;;
    *)     echo "usage: tools/qt64.sh [build|test [ctest args]]" >&2; exit 2 ;;
esac
