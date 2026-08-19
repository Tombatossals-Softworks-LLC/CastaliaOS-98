#!/bin/sh
# check_demos_listed.sh - every demo scene must actually be run by something.
#
# A scene in src/main.c that tools/run_demos.sh does not list is dead code that
# looks like coverage. Nineteen of them had accumulated that way, several with
# real assertions in them, and one -- the File Manager's two-pane view -- could
# be broken outright without a single check anywhere noticing, because the only
# scene that exercised it had never been executed.
#
# Nothing else in this repository looks. `make demos` runs the list, so it can
# only ever be as complete as the list is, and a missing entry is invisible by
# construction: the harness reports "no mismatches" over the scenes it knows
# about and says nothing about the ones it does not.
#
# Usage:  sh tools/check_demos_listed.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$ROOT/src/main.c"
LIST="$ROOT/tools/run_demos.sh"

# Scenes that deliberately are NOT in the gate, with the reason. These are the
# "-keep" variants: they do what their sibling does and then leave a window
# open for a screenshot, so running them in the gate would only duplicate the
# sibling's checks. Each one must have that sibling actually listed, which is
# checked below -- an exception whose sibling also went missing is not an
# exception, it is two holes.
EXCEPT='clock-demo-keep filedlg-demo-keep sysmenu-keep'

# Small hand-driving helpers: they open a window or press one key so a person
# can look at something, assert nothing, and are not scenes in the sense this
# check is about. They are listed BY NAME rather than excluded by a pattern,
# so that adding a real scene whose name happens not to contain "demo" fails
# this check instead of slipping past it.
MANUAL='alttab ctx desk-bench icon-sel maximize nav-close np-wrap resize tb-max'

# Not scenes at all: these select a MODE for whatever else is running.
# --safe is the boot profile CBOOT passes; --net-loopback swaps the null
# network backend for the simulated wire and is implied by the net scenes that
# are in the gate. Both are documented in docs/BUILDING.md.
MODES='safe net-loopback headless'

# Every flag main.c parses that names a scene: anything with "demo" or "check"
# in it, plus nothing else -- the pure "--open-<app>" flags just open a window
# for a screenshot and are excluded by name below.
scenes=$(sed -n 's/.*strcmp(argv\[i\], "--\([a-z0-9-]*\)") == 0).*/\1/p' "$MAIN" \
         | grep -Ev '^open-' | sort -u)
for m in $MANUAL $MODES; do
    scenes=$(printf '%s\n' "$scenes" | grep -vx "$m" || true)
done

# Every flag run_demos.sh drives. The list is "flag:name" rows inside one
# quoted string, so the flag is whatever precedes the first colon on a row.
listed=$(sed -n "/^DEMOS='/,/'\$/p" "$LIST" \
         | sed "s/^DEMOS='//" \
         | sed 's/:.*//' \
         | sed "s/'\$//" \
         | grep -E '^[a-z0-9-]+$' | sort -u)

# run_abuse.sh drives scenes too, and one of them (--screen-check) asserts. A
# scene run by either harness is covered; only one run by neither is not.
listed="$listed
$(sed -n 's/.*--\([a-z0-9-]*\).*/\1/p' "$ROOT/tools/run_abuse.sh" | sort -u)"

missing=''
for s in $scenes; do
    if ! printf '%s\n' "$listed" | grep -qx "$s"; then
        skip=0
        for e in $EXCEPT; do
            [ "$s" = "$e" ] && skip=1
        done
        [ "$skip" = "1" ] || missing="$missing $s"
    fi
done

# An exception is only legitimate while the sibling it defers to is in the gate.
orphaned=''
for e in $EXCEPT; do
    # "clock-demo-keep" defers to "clock-demo"; "sysmenu-keep" to "sysmenu-demo".
    case "$e" in
        *-demo-keep) sib="${e%-keep}" ;;
        *-keep)      sib="${e%-keep}-demo" ;;
        *)           sib="$e" ;;
    esac
    if ! printf '%s\n' "$listed" | grep -qx "$sib"; then
        orphaned="$orphaned $e(needs $sib)"
    fi
done

nscenes=$(printf '%s\n' "$scenes" | grep -c . || true)
ngate=$(sed -n "/^DEMOS='/,/'\$/p" "$LIST" | grep -c ':' || true)

if [ -n "$missing" ] || [ -n "$orphaned" ]; then
    [ -n "$missing" ] && {
        echo "DEMO SCENES NOT RUN BY ANYTHING:"
        for m in $missing; do echo "    --$m   (add it to tools/run_demos.sh)"; done
    }
    [ -n "$orphaned" ] && {
        echo "EXEMPTED SCENES WHOSE SIBLING IS ALSO MISSING:"
        for o in $orphaned; do echo "    --$o"; done
    }
    echo "A scene nothing runs is dead code that looks like coverage."
    exit 1
fi

echo "Demo scenes OK: $nscenes scene(s), every one run by a harness"
echo "                ($ngate in make demos; the rest by make abuse or exempt)."
