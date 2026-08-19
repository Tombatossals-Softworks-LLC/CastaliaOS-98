#!/bin/sh
# check_warning_sweep.sh - the warnings this tree is clean under, kept clean.
#
# Two real bugs were found by turning on flags that were never enabled:
#
#   ui_msgbox()          allocated a dialog and discarded the answer to
#                        "did the window get created?" with the elaborate
#                        `i = dlg_create(...) ? 0 : 0;`, leaking the
#                        allocation whenever the window pool was full --
#                        which is precisely when a message box is wanted.
#   assoc_type_name()    ended `(a_ext(name) != NULL) ? "File" : "File"`,
#                        so every unknown extension read "File".
#
# Both are -Wduplicated-branches. Neither is visible to -Wall -Wextra, and
# neither would have been found by reading the code, because both were written
# to look deliberate. A one-time sweep found them; this file is what stops the
# next one from being written.
#
# Only flags the tree is ALREADY clean under are listed. Three that it is not
# are deliberately absent, and why is worth writing down so they are not
# "fixed" by someone who assumes an omission:
#
#   -Wshadow       18 hits, all harmless and all checked: main.c's headless
#                  loop has an `int f` frame counter, and scene blocks inside
#                  it declare their own `PlatFile *f` or `CRect f`. Different
#                  types, self-contained blocks -- and mixing the two would
#                  not compile, so the dangerous version cannot happen
#                  silently. Renaming a counter in a 5,000-line function is a
#                  large diff for no behavioural gain.
#   -Wfloat-equal  5 hits, all correct. Four are exact-zero divide guards,
#                  where comparing exactly to zero is the whole point, and one
#                  is calc_core.c's Newton's-method stop, which ends when the
#                  estimate stops moving and is exact on purpose.
#   -Wconversion   thousands of hits, almost all noise in this style.
#
# THE CONTROL AT THE BOTTOM IS NOT OPTIONAL. Writing this scan `cc -w -Wfoo`
# makes it report zero for everything, because -w suppresses a -W that follows
# it -- a mistake made THREE separate times in this project, each time
# producing a confident "no hits" that meant nothing. So the script compiles a
# file containing a deliberate fault and fails if the sweep cannot see it. A
# check that has never been shown to fail is not yet a check.
#
# Usage:  sh tools/check_warning_sweep.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CC="${CC:-cc}"
if ! "$CC" --version 2>/dev/null | grep -qi gcc; then
    # Several of these are GCC-only spellings. Prefer a real gcc if present.
    if command -v gcc >/dev/null 2>&1; then CC=gcc; else
        echo "Warning sweep: no gcc available -- skipping (not a failure)."
        exit 0
    fi
fi

# Every one of these is at zero today. Adding a flag here is a promise to keep
# it there; if a flag ever becomes impractical, delete it WITH a note above
# saying which hits made it so, the way the three exclusions above are noted.
FLAGS="-Wduplicated-branches -Wduplicated-cond -Wlogical-op
       -Wnull-dereference -Wjump-misses-init -Wformat=2 -Wcast-align
       -Wswitch-default -Wwrite-strings -Wundef"

SRC=$(ls src/apps/*.c src/shell/*.c src/gfx/*.c src/ui/*.c src/wm/*.c \
         src/sys/*.c src/cfg/*.c src/net/*.c src/capp/*.c src/install/*.c \
         src/platform/host/*.c src/main.c 2>/dev/null)
n=$(printf '%s\n' "$SRC" | grep -c . || true)

# ---- the control, FIRST -------------------------------------------------
# A file with one deliberate duplicated-branch. If the sweep cannot see this,
# it cannot see anything, and a clean run below would mean nothing at all.
probe="$ROOT/build/.warnprobe.c"
mkdir -p "$ROOT/build"
cat > "$probe" <<'PROBE'
int castalia_warn_probe(int n);
int castalia_warn_probe(int n)
{
    return (n > 0) ? 1 : 1;      /* both arms the same, on purpose */
}
PROBE
seen=$($CC -std=gnu89 $FLAGS -DCASTALIA_HOST -Iinclude -fsyntax-only \
       "$probe" 2>&1 | grep -c 'duplicated-branches' || true)
rm -f "$probe"
if [ "$seen" -eq 0 ]; then
    echo "WARNING SWEEP IS BLIND: the control fault was not reported."
    echo "  Something is suppressing these flags -- check for a -w on the"
    echo "  command line, which silences every -W that follows it."
    exit 1
fi

# ---- the sweep itself ---------------------------------------------------
out="$($CC -std=gnu89 $FLAGS -DCASTALIA_HOST \
       -Iinclude -Isrc/apps -Isrc/platform/host -Itests -fsyntax-only \
       $SRC 2>&1 || true)"
hits=$(printf '%s\n' "$out" | grep -c 'warning:\|error:' || true)
if [ "$hits" -gt 0 ]; then
    echo "WARNING SWEEP IS NOT HAPPY:"
    printf '%s\n' "$out" | grep -A2 'warning:\|error:' | head -40 | sed 's/^/  /'
    echo "These flags found two real bugs. Do not silence one to pass."
    exit 1
fi

flagn=$(printf '%s\n' $FLAGS | grep -c . || true)
echo "Warning sweep OK: $n sources clean under $flagn extra warnings,"
echo "                  and the control fault was seen (the sweep can fail)."
