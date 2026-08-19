#!/bin/sh
# run_sanitize.sh - the unit tests under AddressSanitizer and UBSan.
#
# This tree already runs thirteen valgrind passes, so a second memory checker
# looks redundant. It is not, and the reason is a bug it found on its first
# run:
#
#     calc_press() tested `label[1] == '\0'` BEFORE testing label[0], so the
#     empty label "" -- which its own test suite passes deliberately -- read
#     one byte past the end of a one-byte string literal.
#
# Valgrind never saw it in thirteen runs, and could not: "" is a string
# literal in .rodata, and valgrind does not put redzones around globals.
# AddressSanitizer does. The two tools overlap on the heap and barely at all
# on globals and the stack, so running one is not running the other.
#
# UBSan is the other half and is entirely new coverage here: signed overflow,
# bad shifts, misaligned access, and NULL arithmetic are undefined behaviour
# that valgrind is not looking for at all. -fno-sanitize-recover makes those
# fatal rather than a line of output that scrolls past.
#
# LeakSanitizer comes with ASan and is a third thing this gate did not have:
# tools/run_memcheck.sh runs with --leak-check=no, so its "13 runs, no invalid
# access and no uninitialised reads" is exactly true and says nothing at all
# about leaks. On the first run this found a 12 KB canvas abandoned by
# tests/test_paint.c re-initialising an undo ring that still held snapshots.
#
# Builds into a separate directory so it never disturbs the ordinary build.
#
# THE CONTROL IS NOT OPTIONAL, for the same reason as in
# tools/check_warning_sweep.sh: a checker that has never been seen to fire is
# not a checker. This one compiles a deliberate out-of-bounds read and fails
# if the sanitizer stays quiet about it.
#
# Usage:  sh tools/run_sanitize.sh   (run by: make sanitize, make check)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CC="${CC:-cc}"
SAN="-fsanitize=undefined,address -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

# Not every toolchain ships the sanitizer runtimes; skip rather than fail a
# machine that cannot run this at all.
probe="$ROOT/build/.sanprobe.c"
mkdir -p "$ROOT/build"
echo 'int main(void){return 0;}' > "$probe"
if ! $CC $SAN -o "$ROOT/build/.sanprobe" "$probe" >/dev/null 2>&1; then
    rm -f "$probe" "$ROOT/build/.sanprobe"
    echo "Sanitizers: not available with $CC -- skipping (not a failure)."
    exit 0
fi

# ---- the control, FIRST -------------------------------------------------
# A read one past the end of a global array -- the exact shape of the bug
# this script was written after. If ASan does not report this, a clean run
# below proves nothing.
cat > "$probe" <<'PROBE'
#include <stdio.h>
static const char small[1] = { 'x' };
int main(void)
{
    volatile int i = 1;                 /* volatile: not folded at compile time */
    printf("%d\n", (int)small[i]);      /* one past the end, on purpose */
    return 0;
}
PROBE
$CC $SAN -g -O1 -o "$ROOT/build/.sanprobe" "$probe" >/dev/null 2>&1
if "$ROOT/build/.sanprobe" >/dev/null 2>&1; then
    rm -f "$probe" "$ROOT/build/.sanprobe"
    echo "SANITIZER IS BLIND: the control fault ran without complaint."
    echo "  ASan is not actually instrumenting this build; a clean result"
    echo "  below would mean nothing."
    exit 1
fi
rm -f "$probe" "$ROOT/build/.sanprobe"

# ---- the tests themselves ------------------------------------------------
OUT="$ROOT/build-san"

# The objects in $OUT are only trustworthy if they were built with exactly
# these flags. Anything else -- most easily a hand-run `make BUILD=build-san`
# with different options while debugging -- leaves objects that make will
# happily link without rebuilding, and a mixed build can report a fault that
# is not in the source OR pass while a real one is.
#
# This was not hypothetical: during the ring-buffer work this directory
# produced `index 528 out of bounds for long int [120]` in hist_core.c, which
# vanished on a clean rebuild and could not be reproduced afterwards. The
# stamp costs nothing and removes the doubt; a green run means the tree, not
# whatever was lying in the directory.
stamp="$OUT/.sanflags"
want="$SAN|$CC"
if [ ! -f "$stamp" ] || [ "$(cat "$stamp" 2>/dev/null)" != "$want" ]; then
    rm -rf "$OUT"
    mkdir -p "$OUT"
    printf '%s' "$want" > "$stamp"
fi
make BUILD="$OUT" OPT="-g -O1 $SAN" "$OUT/run_tests" >/dev/null 2>&1 || {
    echo "SANITIZER BUILD FAILED:"
    make BUILD="$OUT" OPT="-g -O1 $SAN" "$OUT/run_tests" 2>&1 | tail -20 | sed 's/^/  /'
    exit 1
}

log="$ROOT/build/.sanitize.log"
if ! "$OUT/run_tests" > "$log" 2>&1; then
    echo "SANITIZER FOUND SOMETHING IN THE UNIT TESTS:"
    grep -E 'runtime error|ERROR: |SUMMARY: |FAIL ' "$log" | head -20 | sed 's/^/  /'
    echo "  (full output: $log)"
    exit 1
fi
checks=$(grep -o '^[0-9]* checks' "$log" | head -1 || true)

# ---- and the shell, running every demo scene -----------------------------
#
# The unit tests are pure logic. The scenes are where the window manager, the
# shell, the renderer and the twenty-four apps actually run, which is most of
# the tree by volume and none of it covered above.
#
# The scene list is READ FROM run_demos.sh rather than repeated here, so a
# scene added there is sanitized without anyone remembering to. A copy would
# drift, and the copy that drifts is always the one nobody is watching.
make BUILD="$OUT" OPT="-g -O1 $SAN" "$OUT/castalia" >/dev/null 2>&1 || {
    echo "SANITIZER BUILD FAILED (shell):"
    make BUILD="$OUT" OPT="-g -O1 $SAN" "$OUT/castalia" 2>&1 | tail -20 | sed 's/^/  /'
    exit 1
}

scenes=$(sed -n "/^DEMOS='/,/'$/p" "$ROOT/tools/run_demos.sh" \
         | sed "s/^DEMOS='//" | sed "s/'\$//" | grep ':' | sed 's/:.*//')
nscene=0
sbad=0
W="$ROOT/build/.sanworld"
for flag in $scenes; do
    rm -rf "$W"; mkdir -p "$W/LOGS" "$W/SYS" "$W/DOCS" "$W/PICTURES"
    out=$(cd "$W" && CASTALIA_HOME="$W" "$OUT/castalia" --headless \
          "--$flag" --frames 6 --shot /dev/null 2>&1 || true)
    nscene=$((nscene + 1))
    if printf '%s\n' "$out" | grep -qE 'runtime error|ERROR: AddressSanitizer|ERROR: LeakSanitizer'; then
        sbad=$((sbad + 1))
        echo "SANITIZER FOUND SOMETHING IN --$flag:"
        printf '%s\n' "$out" | grep -E 'runtime error|ERROR: |SUMMARY: ' \
            | head -5 | sed 's/^/  /'
    fi
done
rm -rf "$W"
if [ "$sbad" -gt 0 ]; then
    echo "$sbad of $nscene scenes reported a sanitizer finding."
    exit 1
fi

rm -f "$log"
echo "Sanitizers OK: $checks and $nscene scenes under ASan + UBSan +"
echo "               LeakSanitizer; the control fault was seen (this can fail)."
exit 0
