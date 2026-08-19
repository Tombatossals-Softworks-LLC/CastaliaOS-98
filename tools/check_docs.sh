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

# Every document that states a count. The press kit is here because it is the
# most public writing in the repository and was the least checked: it claimed
# "1,211 host test checks", "nine end-to-end scenes", "457 tests pass today" and
# "37,800 lines across 145 files" against a tree with 7547 checks, 74 scenes and
# 69,453 lines across 218 files. Being the press kit is exactly why nobody
# re-derived those, and exactly why being wrong there costs the most.
#
# Two lists, and the difference between them is the whole trap. Rule 1 filters
# hard for lines that talk about the run AS A WHOLE, so docs/BACKLOG.md can be
# in it: its "449 checks" for one suite and its "2750 checks" quoted from an old
# README are both filtered out. Rules 2 and 3 have no such filter -- any "N
# suites" or "N scenes" counts -- and BACKLOG.md is a work LOG, where "49
# scenes, up from 32" is a true record of a day in the past, not a claim about
# now. Policing prose that is deliberately historical is how a check starts
# crying wolf, and a check that cries wolf gets its output skimmed.
DOCS_WITH_COUNTS="README.md docs/BUILDING.md docs/TESTING.md docs/BACKLOG.md"
DOCS_WITH_COUNTS="$DOCS_WITH_COUNTS tools/package_release.sh"
DOCS_WITH_TOTALS="README.md docs/BUILDING.md docs/TESTING.md"
for pk in "$ROOT"/presskit/*.md; do
    [ -f "$pk" ] || continue
    DOCS_WITH_COUNTS="$DOCS_WITH_COUNTS presskit/$(basename "$pk")"
    DOCS_WITH_TOTALS="$DOCS_WITH_TOTALS presskit/$(basename "$pk")"
done

# Prose writes 7,547 and **7,547**; neither is a different number from 7547, and
# a reader that thinks so reports drift that is not there -- or, worse, matches
# nothing and passes. Every rule below reads through this.
flatten() { tr -d '*,~' < "$1"; }

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
for f in $DOCS_WITH_COUNTS; do
    [ -f "$ROOT/$f" ] || continue
    flatten "$ROOT/$f" | \
    grep -nE '[0-9]+ (unit )?(checks|tests)' | \
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
for f in $DOCS_WITH_TOTALS; do
    [ -f "$ROOT/$f" ] || continue
    for n in $(flatten "$ROOT/$f" | grep -oE '[0-9]+ suites' | grep -oE '^[0-9]+' | sort -u); do
        if [ "$n" != "$suites" ]; then
            report "$f" "$suites" "$n" "test suite files"
        fi
    done
done

# 3. "N scenes" must be the number of demo scenes the harness runs.
for f in $DOCS_WITH_TOTALS; do
    [ -f "$ROOT/$f" ] || continue
    for n in $(flatten "$ROOT/$f" | grep -oE '[0-9]+ (demo )?scenes' | grep -oE '^[0-9]+' | sort -u); do
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

# 5. The press kit's size-of-the-tree claims, against the generated statistics.
#
#    These are round numbers on purpose -- "~69,400 lines" reads better in a
#    fact sheet than 69,453, and pinning them exactly would turn every commit
#    that adds a line into a press-kit edit nobody would keep up with. So the
#    rule is a tolerance, not an equality: a claim may round, it may not be
#    WRONG. A tenth is wide enough that ordinary work never trips it, and
#    narrow enough to have caught what it was written for -- "37,800 lines
#    across 145 files" describing a tree of 69,453 across 218, off by 46%.
loc=$(sed -n 's/^#define CASTALIA_STAT_LOC  *\([0-9][0-9]*\)L.*/\1/p' \
      "$ROOT/include/castalia/buildstats.h")
nfiles=$(sed -n 's/^#define CASTALIA_STAT_FILES  *\([0-9][0-9]*\) .*/\1/p' \
      "$ROOT/include/castalia/buildstats.h")

# claimed, truth -> true when the claim is off by more than a tenth.
off_by_a_tenth() {
    claimed=$1
    truth=$2
    [ -n "$truth" ] || return 1
    [ "$truth" -gt 0 ] || return 1
    if [ "$claimed" -ge "$truth" ]; then
        d=$((claimed - truth))
    else
        d=$((truth - claimed))
    fi
    [ $((d * 100 / truth)) -gt 10 ]
}

for pk in "$ROOT"/presskit/*.md; do
    [ -f "$pk" ] || continue
    f="presskit/$(basename "$pk")"
    for n in $(flatten "$pk" | grep -oE '[0-9]+ lines (of C|across)' \
               | grep -oE '^[0-9]+' | sort -u); do
        if off_by_a_tenth "$n" "$loc"; then
            report "$f" "$loc" "$n" "lines across src/ + include/"
        fi
    done
    for n in $(flatten "$pk" | grep -oE 'across [0-9]+ files' \
               | grep -oE '[0-9]+' | sort -u); do
        if off_by_a_tenth "$n" "$nfiles"; then
            report "$f" "$nfiles" "$n" ".c + .h files in src/ + include/"
        fi
    done
done

[ -f "$ROOT/build/.docfail" ] && fail=$((fail + $(wc -l < "$ROOT/build/.docfail")))
rm -f "$ROOT/build/.docfail"
if [ "$fail" -gt 0 ]; then
    echo "DOC COUNTS STALE: $fail place(s). Update them, or the docs are lying." >&2
    exit 1
fi
echo "Docs OK: $checks checks, $suites suites, $demos demo scenes, $loc lines across"
echo "         $nfiles files -- README, docs/, the press kit and the release notes agree."
