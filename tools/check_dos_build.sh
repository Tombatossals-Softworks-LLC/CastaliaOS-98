#!/bin/sh
# check_dos_build.sh - every portable source in the host build must also be
# built by Makefile.dos.
#
# The two makefiles list their objects independently (they use different
# toolchains), so adding a file to one and forgetting the other compiles
# perfectly on the host and only fails later, at Open Watcom link time, with
# an "undefined symbol" that names the symbol rather than the missing file.
# This catches that locally instead.
#
# Host-only sources are listed explicitly below so the exception is visible.

set -e
cd "$(dirname "$0")/.."

# Sources that legitimately never enter the DOS product.
host_only() {
    case "$1" in
        src/capp/mkcapp.c) return 0 ;;   # authoring tool, host binary only
        src/platform/host/*) return 0 ;; # the host backend, by definition
        *) return 1 ;;
    esac
}

missing=0
for src in $(grep -oE 'src/[a-z]+/[a-z_0-9]+\.c' Makefile | sort -u); do
    host_only "$src" && continue
    obj="$(basename "$src" .c).obj"
    if ! grep -q "^${obj}:" Makefile.dos; then
        echo "  MISSING from Makefile.dos: $src  (needs a '${obj}:' rule)"
        missing=$((missing + 1))
    fi
done

# A rule alone is not enough -- the object has to be in the link list too.
for src in $(grep -oE 'src/[a-z]+/[a-z_0-9]+\.c' Makefile | sort -u); do
    host_only "$src" && continue
    obj="$(basename "$src" .c).obj"
    if grep -q "^${obj}:" Makefile.dos; then
        if ! grep -v "^${obj}:" Makefile.dos | grep -q "\b${obj}\b"; then
            echo "  NOT LINKED in Makefile.dos: $obj has a rule but is in no OBJ list"
            missing=$((missing + 1))
        fi
    fi
done

# Every function the public headers declare must be DEFINED somewhere in the
# DOS link set -- not merely have its file listed.
#
# The checks above match FILES, and that is not the same thing. plat_cpu_id()
# was declared in plat.h, called unconditionally by app_sysinfo.c and by
# main.c's --cpu-demo, and implemented only in src/platform/host/ -- which is
# host-only by definition and so is skipped by every loop above. Both callers
# ARE in the DOS product. Every host build passed, because the host has an
# implementation; the DOS link could not have succeeded. A file-level check
# cannot see that, because no file was missing: a function was.
#
# Definitions are looked for across the WHOLE link set rather than only in
# src/platform/dos, because the platform contracts are split on purpose --
# snd_click() and net_checksum() are portable and live in src/sys and src/net,
# and only the device half sits in the backend. Checking the backend alone
# reported nine sound functions and four network functions as missing when
# every one of them links fine.
tmp_decl="${TMPDIR:-/tmp}/cdb_decl_$$"
tmp_defs="${TMPDIR:-/tmp}/cdb_defs_$$"

# The DOS link set: the portable sources, minus the host-only exceptions,
# plus main.c and the DOS backend.
linkset=""
for src in $(grep -oE 'src/[a-z]+/[a-z_0-9]+\.c' Makefile | sort -u); do
    host_only "$src" && continue
    linkset="$linkset $src"
done
linkset="$linkset src/main.c $(ls src/platform/dos/*.c)"

# Declared: a column-zero line in a public header whose first '(' follows an
# identifier. Anchoring at column zero is what excludes comment bodies and
# struct members; taking the name before the first '(' rather than requiring
# the line to end in ');' is what catches the 55 prototypes that wrap onto a
# second line -- an earlier draft of this check required ');' and silently
# skipped every one of them, which is the same kind of hole it exists to find.
grep -hE '^[A-Za-z_][A-Za-z0-9_ *]*[A-Za-z0-9_]\(' include/castalia/*.h \
    | grep -vE '^(typedef|#|\})' \
    | sed 's/(.*//' | sed 's/.*[^A-Za-z0-9_]//' \
    | sort -u > "$tmp_decl"

# Defined: the same shape in a link-set source, minus the lines that end in
# ';' -- those are prototypes, and a prototype resolves nothing.
grep -hE '^[A-Za-z_][A-Za-z0-9_ *]*[A-Za-z0-9_]\(' $linkset \
    | grep -v ';[[:space:]]*$' | grep -vE '^(typedef|#|\})' \
    | sed 's/(.*//' | sed 's/.*[^A-Za-z0-9_]//' \
    | sort -u > "$tmp_defs"

for fn in $(comm -23 "$tmp_decl" "$tmp_defs"); do
    hdr=$(grep -lE "^[A-Za-z_][A-Za-z0-9_ *]*[A-Za-z0-9_]?${fn}\(" include/castalia/*.h \
          | head -1)
    echo "  UNDEFINED on DOS: ${fn}() is declared in ${hdr:-a public header} but"
    echo "                    nothing in the DOS link set defines it"
    missing=$((missing + 1))
done
ndecl=$(wc -l < "$tmp_decl" | tr -d ' ')
rm -f "$tmp_decl" "$tmp_defs"

if [ "$missing" -gt 0 ]; then
    echo "DOS build check FAILED: $missing problem(s) would not link on Open Watcom."
    exit 1
fi

count=$(grep -oE 'src/[a-z]+/[a-z_0-9]+\.c' Makefile | sort -u | wc -l | tr -d ' ')
echo "DOS build OK: all $count host sources are built and linked by Makefile.dos,"
echo "              and all $ndecl declared entry points resolve in the DOS link set."
