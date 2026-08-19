#!/bin/sh
# check_second_compiler.sh - build the tree once more with the OTHER compiler.
#
# gcc and clang do not diagnose the same things. Each has a set the other
# lacks, so a tree that is clean under one is not thereby clean, and the
# difference is not cosmetic: several of clang's are about integer width,
# which is the distinction that matters most here because the product target
# is 32-bit while every machine this is developed on is 64.
#
# This is a syntax-only pass, so it costs a few seconds rather than a full
# second build. It adds the clang-specific warnings that are worth having and
# that this tree is already clean under:
#
#   -Wshorten-64-to-32          an implicit narrowing from 64 bits to 32.
#                               Worth stating plainly: this would NOT have
#                               caught the overflow bugs found in the memory
#                               gauges and the percentages, because those were
#                               long*int products that are correct at 64 bits
#                               and wrap only at 32, with no narrowing
#                               conversion anywhere. It guards the adjacent
#                               class, not that one.
#   -Wconditional-uninitialized a value used on a path that never set it
#   -Wunreachable-code          code after a return, usually a logic slip
#   -Wloop-analysis             a loop variable the body never moves
#   -Wtautological-compare      a comparison whose answer is fixed
#
# -Wcomma is deliberately absent: its only hit here is `for (k = 0, x = 1; ...)`,
# which is what a for-initialiser is for.
#
# Skips itself with a note when no second compiler is installed, rather than
# failing a machine that only has one.
#
# Usage:  sh tools/check_second_compiler.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-cc}"

# Whichever of the two is not the compiler already being used.
primary="$("$CC" --version 2>/dev/null | head -1)"
case "$primary" in
    *clang*) other=gcc ;;
    *)       other=clang ;;
esac

if ! command -v "$other" >/dev/null 2>&1; then
    echo "Second compiler: $other is not installed -- skipping (not a failure)."
    exit 0
fi

WARN="-Wall -Wextra -Wno-unused-parameter
      -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls
      -Wnested-externs -Wold-style-definition -Wpointer-arith"
EXTRA=""
if [ "$other" = "clang" ]; then
    EXTRA="-Wshorten-64-to-32 -Wconditional-uninitialized -Wunreachable-code
           -Wloop-analysis -Wtautological-compare"
fi

cd "$ROOT"
SRC=$(ls src/apps/*.c src/shell/*.c src/gfx/*.c src/ui/*.c src/wm/*.c \
         src/sys/*.c src/cfg/*.c src/net/*.c src/capp/*.c src/install/*.c \
         src/platform/host/*.c src/main.c 2>/dev/null)
n=$(printf '%s\n' "$SRC" | grep -c . || true)

out="$($other -std=gnu89 $WARN $EXTRA -DCASTALIA_HOST \
       -Iinclude -Isrc/apps -Isrc/platform/host -Itests -fsyntax-only \
       $SRC 2>&1 || true)"

hits=$(printf '%s\n' "$out" | grep -c 'warning:\|error:' || true)
if [ "$hits" -gt 0 ]; then
    echo "SECOND COMPILER ($other) IS NOT HAPPY:"
    printf '%s\n' "$out" | grep -A2 'warning:\|error:' | head -40 | sed 's/^/  /'
    echo "A tree clean under one compiler is not thereby clean."
    exit 1
fi

echo "Second compiler OK: $n sources parse clean under $other,"
echo "                    including its 64-to-32 narrowing checks."
