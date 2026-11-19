#!/usr/bin/env bash
# melopresto-score: run a test binary from a scratch working directory.
#
# MuseScore's test suites (engraving_tests, iex_musicxml_tests, ...) write
# their byproducts (*.mscx, *.xml, *.png, *.json) into whatever directory they
# are launched from. Run from a repo root or a home directory and they litter
# it with hundreds of files (this happened three times during the MeloStaff
# milestones). This wrapper always runs the binary from
# <build dir>/test-cwd, which is inside the git-ignored build tree.
#
# Usage:
#   tools/run-tests.sh <test> [gtest args...]
#   BUILD_DIR=build.qt610 tools/run-tests.sh <test> [gtest args...]
#
# <test> is a binary name (engraving_tests, iex_musicxml_tests, ...) looked up
# under the build tree, or a path to a binary. BUILD_DIR is relative to the
# fork root and defaults to build.release. Test inputs are found by the
# compiled-in data roots, so the working directory does not affect them.
#
# Examples:
#   tools/run-tests.sh engraving_tests --gtest_filter='Engraving_MeloStaff*'
#   tools/run-tests.sh iex_musicxml_tests --gtest_filter='MusicXml_Melo_Tests.*'

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-build.release}"
case "$build" in /*) ;; *) build="$root/$build" ;; esac

if (($# < 1)); then
    sed -n '2,22p' "$0" >&2
    exit 2
fi
test="$1"
shift

if [[ -f "$test" && -x "$test" ]]; then
    bin="$(cd "$(dirname "$test")" && pwd)/$(basename "$test")"
else
    bin="$(find "$build/src" -type f -perm -u+x -name "$test" 2>/dev/null | head -n 1 || true)"
    if [[ -z "$bin" ]]; then
        echo >&2 "$0: no test binary named '$test' under $build/src (build it first, e.g. cmake --build $build --target $test)"
        exit 1
    fi
fi

cwd="$build/test-cwd"
mkdir -p "$cwd"
cd "$cwd"
exec "$bin" "$@"
