#!/bin/sh
# check_docs.sh - the numbers in the docs must be the numbers the build produces.
#
# The README said "1211 checks across 25 suites" for a long time after both had
# stopped being true. Nobody lied; the counts simply drifted, the way every
# hand-copied number in a document eventually does, and there was nothing to
# notice it.
#
# So this compares what the docs claim against what the build actually reports,
# and `make lint` runs it. A stale number is now a build failure rather than a
# small dishonesty nobody spots for months. The rule it enforces is narrow on
# purpose: only counts that a script can derive are checked, because a check
# that has to guess at prose would either miss real drift or cry wolf.
#
# Usage:  sh tools/check_docs.sh        (part of: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTS="$ROOT/build/run_tests"
fail=0
rm -f "$ROOT/build/.docfail"
mkdir -p "$ROOT/build"

if [ ! -x "$TESTS" ]; then
    echo "check_docs: $TESTS is missing -- run make test first" >&2
    exit 1
fi

# The truth, taken from the things themselves.
checks=$("$TESTS" 2>/dev/null | sed -n 's/^\([0-9][0-9]*\) checks, .*/\1/p')
suites=$(ls "$ROOT"/tests/test_*.c | grep -v 'test_main\.c' | wc -l | tr -d ' ')
demos=$(sed -n "/^DEMOS='/,/'$/p" "$ROOT/tools/run_demos.sh" | grep -c ':')

if [ -z "$checks" ]; then
    echo "check_docs: could not read a check count out of run_tests" >&2
    exit 1
fi

# Every place a count is written down, and what it should say.
report() {
    printf '  STALE %s says %s, should be %s (%s)\n' "$1" "$3" "$2" "$4"
    fail=$((fail + 1))
}

# 1. The total check count. Which numbers are TOTALS and which are per-suite
#    counts written into prose is the whole difficulty here: matching every
#    "N checks" catches "(tests/test_lzss.c, 449 checks)" and cries wolf, while
#    matching too narrowly misses "(1993 checks)" -- which an earlier version of
#    this script did, and it passed while the README was wrong.
#
#    So: a line is a total-count claim if it talks about the run as a whole,
#    and is not one if it names a specific suite file.
#
#    "N unit tests" counts too, and did not used to: the selecting grep already
#    listed that phrasing, but the regex that pulls the NUMBER out only knew
#    about "checks", so BACKLOG.md's "277 unit tests pass" was picked up, found
#    to contain no number, and silently skipped -- while being wrong by a factor
#    of eight. A filter that matches and an extractor that does not is the same
#    failure as no rule at all, and looks like a passing one.
#
#    tools/package_release.sh is in this list because the release notes it
#    writes are a published claim like any other, and being in a script rather
#    than a document is precisely why nobody re-reads it: it said "457 unit
#    tests" for as long as it had existed.
for f in README.md docs/BUILDING.md docs/TESTING.md docs/BACKLOG.md \
         tools/package_release.sh; do
    [ -f "$ROOT/$f" ] || continue
    grep -nE '[0-9]+ (unit )?(checks|tests)' "$ROOT/$f" | \
    grep -vE 'tests/test_[a-z_]*\.c' | \
    grep -E 'run_tests|make test|unit checks|unit tests|checks, 0 failures|checks pass' | \
    while IFS= read -r line; do
        n=$(printf '%s' "$line" \
            | grep -oE '[0-9]+ (unit )?checks|[0-9]+ unit tests' \
            | grep -oE '^[0-9]+')
        for one in $n; do
            if [ "$one" != "$checks" ]; then
                printf '  STALE %s says %s, should be %s (total unit checks)\n' \
                       "$f" "$one" "$checks"
                echo x >> "$ROOT/build/.docfail"
            fi
        done
    done
done

# 2. "N suites" must be the number of suite files.
for f in README.md docs/BUILDING.md docs/TESTING.md; do
    [ -f "$ROOT/$f" ] || continue
    for n in $(grep -oE '[0-9]+ suites' "$ROOT/$f" | grep -oE '^[0-9]+' | sort -u); do
        if [ "$n" != "$suites" ]; then
            report "$f" "$suites" "$n" "test suite files"
        fi
    done
done

# 3. "N scenes" must be the number of demo scenes the harness runs.
for f in README.md docs/BUILDING.md docs/TESTING.md; do
    [ -f "$ROOT/$f" ] || continue
    for n in $(grep -oE '[0-9]+ (demo )?scenes' "$ROOT/$f" | grep -oE '^[0-9]+' | sort -u); do
        if [ "$n" != "$demos" ]; then
            report "$f" "$demos" "$n" "demo scenes"
        fi
    done
done

# 4. The generated source statistics must match the source they describe.
#    The About window prints them, so a stale buildstats.h is wrong information
#    shown to a user -- and it had drifted by four thousand lines and eighteen
#    files before anything checked it.
gen="$ROOT/build/buildstats.check.h"
sh "$ROOT/tools/gen_buildstats.sh" "$gen" >/dev/null 2>&1 || true
if [ -f "$gen" ]; then
    if ! cmp -s "$gen" "$ROOT/include/castalia/buildstats.h"; then
        echo "  STALE include/castalia/buildstats.h -- run: make gen-stats"
        diff "$ROOT/include/castalia/buildstats.h" "$gen" 2>/dev/null \
            | grep '^>' | sed 's/^/        now: /' | head -6
        fail=$((fail + 1))
    fi
    rm -f "$gen"
fi

[ -f "$ROOT/build/.docfail" ] && fail=$((fail + $(wc -l < "$ROOT/build/.docfail")))
rm -f "$ROOT/build/.docfail"
if [ "$fail" -gt 0 ]; then
    echo "DOC COUNTS STALE: $fail place(s). Update them, or the docs are lying." >&2
    exit 1
fi
echo "Docs OK: $checks checks, $suites suites, $demos demo scenes, build stats current."
