#!/bin/sh
# check_memcheck_control.sh - prove run_memcheck.sh can actually fail.
#
# "Memcheck OK: 13 runs, no invalid access" is worth exactly nothing unless
# the same script would have said otherwise had there been something to find.
# A wrong valgrind flag, a grep that stopped matching the output format, a
# binary that is not the one being run -- each of those produces a clean bill
# of health that means only that nothing was looked at.
#
# So this plants a real defect and requires it to be caught: one byte written
# past a four-byte allocation, on a path every scene executes. It edits a
# source file, builds, runs, and puts the file back whatever happens.
#
# It is deliberately NOT part of `make check`. It rebuilds the tree twice and
# would double the gate's runtime to re-prove something that changes only when
# run_memcheck.sh itself changes. Run it when that script is touched:
#
#   sh tools/check_memcheck_control.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="$ROOT/src/shell/sh_core.c"
SAVED="$ROOT/build/.memcheck-control.bak"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "control: valgrind is not installed -- nothing to prove here."
    exit 0
fi

mkdir -p "$ROOT/build"
cp "$TARGET" "$SAVED"

# Always put the tree back, however this exits.
restore() {
    if [ -f "$SAVED" ]; then
        cp "$SAVED" "$TARGET"
        rm -f "$SAVED"
        (cd "$ROOT" && make -s >/dev/null 2>&1) || true
    fi
}
trap restore EXIT INT TERM

# The defect: a write one byte past a four-byte block, in the frame loop, so
# every scene reaches it. sh_run_frame is where it goes precisely because
# nothing can run without it.
python3 - "$TARGET" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
i = s.index('cbool sh_run_frame(void)')
j = s.index('{', i) + 1
s = s[:j] + '''
    { /* memcheck control: a deliberate one-byte overrun. If this file still
         contains this block, tools/check_memcheck_control.sh did not finish
         and the tree needs restoring from git. */
      static unsigned char *mc_probe = 0;
      if (mc_probe == 0) { mc_probe = (unsigned char *)malloc(4); }
      if (mc_probe != 0) { mc_probe[4] = 1; }
    }
''' + s[j:]
open(p, 'w').write(s)
PY

(cd "$ROOT" && make -s >/dev/null 2>&1) || { echo "control: build failed" >&2; exit 1; }

code=0
sh "$ROOT/tools/run_memcheck.sh" >"$ROOT/build/.memcheck-control.out" 2>&1 || code=$?

if [ "$code" -eq 0 ]; then
    echo "CONTROL FAILED: run_memcheck.sh passed with a one-byte overrun in"
    echo "                sh_run_frame. It is not looking at what it claims to."
    rm -f "$ROOT/build/.memcheck-control.out"
    exit 1
fi

caught=$(grep -c '^  FAIL' "$ROOT/build/.memcheck-control.out" || true)
rm -f "$ROOT/build/.memcheck-control.out"
echo "Control OK: a planted one-byte overrun failed $caught of the memcheck runs."
