#!/bin/sh
# check_menu_wired.sh - every menu item an app puts on a menu must be handled.
#
# A menu entry with nothing behind it is drawn, highlighted and clickable, and
# does nothing. There is no error and no missing symbol: the id simply falls
# past the end of a switch. That is the same shape of bug that left FreeCell
# and Reversi unreachable from the Start menu, one level down.
#
# CastaliaPaint's Help / About was reached by the switch's `default:` rather
# than a case of its own, which is worse than missing: every id the switch did
# not know opened the About box, so an item added and never wired up would not
# look broken -- it would look like it did something else deliberately.
#
# What counts as handled is `case ID:` or a comparison against ID, because the
# apps use both -- the File Manager's context menu is an if/else chain and its
# toolbar is a switch, and both are correct.
#
# Usage:  sh tools/check_menu_wired.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fails=0
files=0
ids=0

for f in "$ROOT"/src/apps/app_*.c "$ROOT"/src/shell/sh_*.c; do
    [ -f "$f" ] || continue
    # Ids added to a menu in this file. Only plain identifiers: an entry built
    # from an expression (BASE + i) is a family, dispatched by range, and is
    # not what this is about.
    menu_ids=$(sed -n 's/.*ui_menu_add([^,]*,[ \t]*\([A-Z_][A-Z_0-9]*\)[ \t]*,.*/\1/p' \
               "$f" | sort -u)
    [ -n "$menu_ids" ] || continue
    files=$((files + 1))
    for id in $menu_ids; do
        ids=$((ids + 1))
        # UI_MENU_SEPARATOR_ID is the separator marker, not a command.
        [ "$id" = "UI_MENU_SEPARATOR_ID" ] && continue
        if grep -q "case $id:" "$f" ||
           grep -q "== $id" "$f" ||
           grep -q "$id ==" "$f"; then
            continue
        fi
        # The shell's own menus dispatch through sh_dispatch_command, which
        # lives in sh_core.c -- so a shell id counts as handled if any shell
        # source answers it.
        if grep -rq "case $id:" "$ROOT/src/shell/" 2>/dev/null; then
            continue
        fi
        echo "  UNWIRED: $id is put on a menu in $(basename "$f")"
        echo "        but nothing there answers it -- the item is drawn,"
        echo "        clickable, and does nothing."
        fails=$((fails + 1))
    done
done

# A check that matched no menus at all would pass everything.
if [ "$files" -lt 4 ] || [ "$ids" -lt 30 ]; then
    echo "  MENU WIRING: only $ids id(s) across $files file(s) -- the"
    echo "               ui_menu_add calls moved or changed shape, and this"
    echo "               check is blind."
    exit 1
fi

if [ "$fails" -gt 0 ]; then
    echo "Menu wiring check FAILED: $fails unwired item(s)."
    exit 1
fi
echo "Menu wiring OK: all $ids menu item(s) across $files file(s) are answered."
