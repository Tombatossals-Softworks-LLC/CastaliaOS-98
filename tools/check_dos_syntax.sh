#!/bin/sh
# =====================================================================
#  check_dos_syntax.sh - parse the DOS backend on a machine that cannot
#  build it.
#
#  src/platform/dos is about 2,100 lines: every interrupt call, the VESA
#  driver, the mouse, the keyboard, the packet driver, the Sound Blaster, and
#  the CPUID and machine-inventory reads. Open Watcom is on no runner here, and
#  those files include <i86.h>, <dos.h>, <conio.h> and <direct.h>, which exist
#  on no Linux box. So until this script existed, NOTHING in this repository
#  had ever compiled them -- not the host build, which excludes them by design,
#  not c89_lint.sh, which reads the portable source list, and not CI. They got
#  one attempt to be right, on hardware, with no way back.
#
#  tools/wcshim/ declares enough of Watcom's surface for gcc to parse them.
#  This is a SYNTAX check: a typo, a missing declaration, a wrong struct field,
#  a bad argument count, a C89 violation. Those are most of what goes wrong in
#  code nobody can run.
#
#  What a pass does NOT mean, said plainly because a green check invites the
#  opposite conclusion:
#    - it does not mean Open Watcom will accept the file;
#    - the #pragma aux inline assembly is skipped entirely by gcc, so nothing
#      here says whether those opcodes assemble, or are even opcodes;
#    - the shim's struct layouts are approximate, which does not matter for
#      syntax and would matter enormously for a build.
#
#  If this reports an error, check it against Watcom's documentation before
#  changing the source. Editing correct code to satisfy a stub header is worse
#  than not having the stub.
#
#  src/main.c is parsed here too, under -DCASTALIA_DOS. It is the one file in
#  the tree that is allowed to differ per platform, so it is also the one file
#  whose DOS half nothing else looks at: c89_lint.sh reads it with
#  CASTALIA_HOST set, check_dos_build.sh matches files and public headers
#  rather than what a #ifdef actually leaves behind, and the host build never
#  defines CASTALIA_DOS at all. Both of the errors that broke the first Open
#  Watcom CI run lived in exactly that gap -- a header included inside the
#  CASTALIA_HOST block but used outside it, and a call to a host-backend
#  function from the shared tail -- and each is an ordinary compile error the
#  moment somebody compiles the file the way the DOS product does.
#
#  Usage:  sh tools/check_dos_syntax.sh
# =====================================================================
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"

WARN="-Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas"
WARN="$WARN -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast"
# The traps that actually reach the DOS product: a call with no declaration
# becomes an implicit int and silently wrong ABI, and a declaration after a
# statement is rejected outright by Watcom's C89 front end.
HARD="-Werror=implicit-function-declaration -Werror=implicit-int"
HARD="$HARD -Werror=return-type -Werror=declaration-after-statement"
INCS="-Iinclude -Isrc/apps -Isrc/platform/dos -Itools/wcshim"

fail=0
n=0
noise=0
for f in src/platform/dos/*.c src/main.c; do
    n=$((n + 1))
    err="${TMPDIR:-/tmp}/dossyn_$$.err"
    if ! $CC -fsyntax-only -std=gnu89 -DCASTALIA_DOS $INCS $WARN $HARD \
         "$f" 2>"$err"; then
        echo "DOS SYNTAX ERROR in $f:"
        grep -E 'error:' "$err" | head -6 | sed 's/^/  /'
        fail=$((fail + 1))
    fi
    # Every #pragma aux routine is a prototype whose body is inline assembly
    # gcc never sees, so gcc calls it undefined. That is the shim working as
    # intended, not a defect -- counted rather than hidden.
    if [ -s "$err" ]; then
        left=$(grep -v 'used but never defined' "$err" | grep -c 'warning:' || true)
        noise=$((noise + left))
        if [ "$left" -gt 0 ]; then
            grep -v 'used but never defined' "$err" | grep 'warning:' \
                | head -4 | sed 's/^/  WARN /'
        fi
    fi
    rm -f "$err"
done

if [ "$fail" -gt 0 ]; then
    echo ""
    echo "FAIL: $fail DOS source(s) do not parse. Open Watcom would reject"
    echo "them too, and no other check in this repository looks."
    exit 1
fi
if [ "$noise" -gt 0 ]; then
    echo "DOS syntax: $n sources parse (backend + main.c), with $noise warning(s) above."
    exit 0
fi
echo "DOS syntax OK: all $n sources parse under -DCASTALIA_DOS (the backend"
echo "               plus main.c's DOS half) -- syntax only; inline assembly"
echo "               is not checked by anything, anywhere."
