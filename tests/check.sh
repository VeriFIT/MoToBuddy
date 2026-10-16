#!/usr/bin/env bash
#
# Run the unit-test suites under tests/ (same contract as examples/check.sh).
#
#   tests/check.sh [-b BUILD_DIR] [SUITE ...]
#
# BUILD_DIR defaults to "build". With no SUITE arguments every subdirectory
# that has a runtest script is checked.

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
build="$root/build"

while getopts "b:h" opt; do
    case $opt in
        b) build=$OPTARG ;;
        *) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

if [ ! -d "$build/tests" ]; then
    echo "error: $build/tests does not exist; build the tests first" >&2
    exit 2
fi

if [ $# -eq 0 ]; then
    set -- $(cd "$here" && ls -d */ | tr -d / | sort)
fi

if ! command -v tcsh >/dev/null; then
    echo "error: tcsh is required by the runtest scripts" >&2
    exit 2
fi

passed=0
failed=0
for name in "$@"; do
    src="$here/$name"
    bin="$build/tests/$name"
    if [ ! -f "$src/runtest" ] && [ ! -f "$src/runtest.in" ]; then
        continue
    fi
    if [ ! -f "$bin/runtest" ]; then
        echo "$name FAILED : $bin/runtest missing (not built?)"
        failed=$((failed + 1))
        continue
    fi

    rm -f "$bin/result"
    if head -1 "$bin/runtest" | grep -q tcsh; then
        (cd "$bin" && tcsh -f ./runtest) >/dev/null 2>&1
    else
        (cd "$bin" && bash ./runtest) >/dev/null 2>&1
    fi
    rc=$?

    expected="$bin/expected"
    [ -f "$expected" ] || expected="$src/expected"

    if [ ! -f "$bin/result" ]; then
        echo "$name FAILED : no result file produced"
        failed=$((failed + 1))
    elif [ ! -f "$expected" ]; then
        if [ "$rc" -eq 0 ]; then
            echo "$name PASSED"
            passed=$((passed + 1))
        else
            echo "$name FAILED : runtest exited with status $rc"
            tail -5 "$bin/result" | sed 's/^/    /'
            failed=$((failed + 1))
        fi
    elif diff -q "$bin/result" "$expected" >/dev/null; then
        echo "$name PASSED"
        passed=$((passed + 1))
    else
        echo "$name FAILED : output differs from expected (left: actual, right: expected)"
        diff "$bin/result" "$expected" | head -20 | sed 's/^/    /'
        failed=$((failed + 1))
    fi
done

echo
echo "Passed: $passed   Failed: $failed"
[ "$failed" -eq 0 ]
