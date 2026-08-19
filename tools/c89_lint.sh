#!/bin/sh
# =====================================================================
#  c89_lint.sh - Guard the Open Watcom (strict C89) DOS build from a host.
#
#  The host build uses gcc -std=gnu89, which SILENTLY accepts a handful of
#  C99-isms that Open Watcom's strict C89 front end rejects -- most commonly a
#  declaration after a statement inside a block. That kind of slip compiles
#  fine on the host and in host CI, then breaks the real DOS product build,
#  which no host runner exercises unless Watcom is installed.
#
#  This script compiles every PORTABLE source (the exact set Makefile.dos feeds
#  to wcc386, read from the host Makefile's CORE_SRC) plus src/main.c with
#  gcc's -Werror=declaration-after-statement, so the trap is caught on any host
#  -- in CI and locally -- without needing the Watcom toolchain.
#
#  Usage:  tools/c89_lint.sh        (uses $CC or cc)
#  Exit:   non-zero if any portable source has a C89 violation.
# =====================================================================
set -e
CC="${CC:-cc}"
INCS="-Iinclude -Isrc/apps"
GUARD="-std=gnu89 -Wall -Wdeclaration-after-statement -Werror=declaration-after-statement"

# The portable upper-stack sources, straight from the host Makefile's CORE_SRC
# block (kept as the single source of truth for "what Watcom compiles").
CORE=$(grep -A40 '^CORE_SRC' Makefile | sed '/^$/q' | grep -oE 'src/[a-zA-Z0-9_/]+\.c')

fail=0
n=0
for f in $CORE; do
    n=$((n + 1))
    if ! $CC $GUARD $INCS -c "$f" -o /dev/null 2>/tmp/c89_$$.err; then
        echo "C89 VIOLATION in $f:"
        grep -E 'declaration-after|error:' /tmp/c89_$$.err | head -6
        fail=$((fail + 1))
    fi
done

# main.c is the per-platform integration seam; lint its host-active form (the
# shared code is what Watcom also compiles).
if ! $CC $GUARD -DCASTALIA_HOST $INCS -Isrc/platform/host -c src/main.c -o /dev/null 2>/tmp/c89_$$.err; then
    echo "C89 VIOLATION in src/main.c:"
    grep -E 'declaration-after|error:' /tmp/c89_$$.err | head -6
    fail=$((fail + 1))
fi
rm -f /tmp/c89_$$.err

if [ "$fail" -ne 0 ]; then
    echo ""
    echo "FAIL: $fail portable source(s) use C89-illegal constructs that Open"
    echo "Watcom will reject. Move declarations to the top of their block."
    exit 1
fi
echo "C89 OK: $((n + 1)) portable sources are strict-C89 clean (Watcom-safe)."
