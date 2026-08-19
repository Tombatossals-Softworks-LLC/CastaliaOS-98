#!/bin/sh
# check_icon_slots.sh - every DESKTOP icon the shell asks a pack for must exist
# in the default pack, and must be documented.
#
# A desktop icon that a pack does not supply falls back to the shell's own
# procedural drawing. That fallback is deliberate and it is right for the small
# glyphs -- a missing 16px toolbar or tray icon degrades to a line drawing
# nobody looks twice at. At 40px on the desktop it is not: the fallback is a
# flat disc drawn from primitives sitting in a row of detailed pack art, and it
# reads as a broken icon rather than a simpler one.
#
# That is what happened. sh_desktop.c asks for seven desktop slots; the pack
# shipped five. "Media Player" and "Clock" were black discs beside five Tango
# icons for as long as the Tango pack has existed, and nothing said so --
# because the fallback is a FEATURE, so there is no error to notice, and the
# slot table in assets/icons/README.md listed only the five that existed, so
# the documentation agreed with the bug.
#
# Both halves are checked here: present in the default pack, and named in the
# README. The second half is the one that would have caught it -- the list a
# person reads when adding a pack has to be the list the code actually asks
# for, or a complete pack is impossible to author.
#
# Usage:  sh tools/check_icon_slots.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/src/shell/sh_desktop.c"
PACK="$ROOT/assets/icons/tango"
DOC="$ROOT/assets/icons/README.md"
fails=0

# The slot names are the kind_name[] table in draw_icon(): one string list,
# read from the source rather than restated here, so a slot added there is
# checked from the moment it exists.
slots=$(sed -n '/static const char \*const kind_name\[DESK_ICON_COUNT\]/,/};/p' \
        "$DESK" | grep -o '"[a-z0-9-]*"' | tr -d '"' | grep -v '^$' || true)

# ...and that read has to have worked. An empty list would pass every check
# below without looking at anything, which is the failure mode a check like
# this dies of: the table gets renamed, the sed matches nothing, and the
# silence reads as a pass.
n=$(printf '%s\n' "$slots" | grep -c . || true)
if [ "$n" -lt 5 ]; then
    echo "  ICON SLOTS: found only $n slot name(s) in sh_desktop.c --"
    echo "              kind_name[] moved or was renamed; this check is blind."
    exit 1
fi

for s in $slots; do
    [ -n "$s" ] || continue
    if [ ! -f "$PACK/$s.bmp" ]; then
        echo "  MISSING from the default pack: $s.bmp"
        echo "        (the desktop falls back to procedural art for '$s',"
        echo "         which at 40px does not match the rest of the pack)"
        fails=$((fails + 1))
    fi
    if ! grep -q "\`$s\.bmp\`" "$DOC"; then
        echo "  UNDOCUMENTED slot: $s.bmp is requested but not in the"
        echo "        desktop table in assets/icons/README.md"
        fails=$((fails + 1))
    fi
done

# ---- the File Manager toolbar -------------------------------------------
#
# The toolbar's fallback is a wide TEXT button, which is a real fallback and
# not broken art -- but MIXED with icon buttons it is neither. Rename and
# 2-Pane had no icon in any pack, so eight small square glyphs carried two
# word-buttons in the middle of the row: not a design anyone chose, just the
# two slots nobody filled. Nothing said so, because the fallback works.
#
# So every toolbar button must name an icon, and the default pack must have
# it. Adding a button without art now fails the build rather than the row.
FM="$ROOT/src/apps/app_fileman.c"
tb=$(sed -n '/static const char \*const FB_ICON\[FB_COUNT\]/,/};/p' "$FM" |
     grep -o '"tb-[a-z0-9-]*"' | tr -d '"' || true)
tbn=$(printf '%s\n' "$tb" | grep -c . || true)
btns=$(sed -n '/^enum { FB_UP/,/FB_COUNT };/p' "$FM" |
       grep -o 'FB_[A-Z]*' | grep -cv 'FB_COUNT' || true)

if [ "$tbn" -lt 6 ]; then
    echo "  TOOLBAR ICONS: found only $tbn name(s) in FB_ICON[] --"
    echo "                 the table moved or was renamed; this check is blind."
    exit 1
fi
# A button with a 0 instead of an icon name is the bug this exists for: the
# counts diverge and the row grows a word-button.
if [ "$tbn" -ne "$btns" ]; then
    echo "  TOOLBAR ICONS: $btns toolbar button(s) but only $tbn icon name(s)"
    echo "                 in FB_ICON[] -- the ones with no icon are drawn as"
    echo "                 wide text buttons in a row of square glyphs."
    fails=$((fails + 1))
fi
for s in $tb; do
    [ -n "$s" ] || continue
    if [ ! -f "$PACK/$s.bmp" ]; then
        echo "  MISSING from the default pack: $s.bmp (File Manager toolbar)"
        fails=$((fails + 1))
    fi
    if ! grep -q "\`$s\`" "$DOC"; then
        echo "  UNDOCUMENTED slot: $s is requested by the toolbar but is not"
        echo "        in the slot table in assets/icons/README.md"
        fails=$((fails + 1))
    fi
done

if [ "$fails" -gt 0 ]; then
    echo "Icon slot check FAILED: $fails problem(s)."
    exit 1
fi
echo "Icon slots OK: all $n desktop slots and $tbn toolbar slots ship in the"
echo "               default pack and are documented for pack authors."

# ---- the original glyphs must stay original -----------------------------
#
# Four files in the Tango pack are CastaliaOS art, not Tango. They are held
# there by nothing but their absence from build_tango_pack.sh's fetch list --
# add a line for one and the next re-run silently replaces it with the icon it
# was drawn to replace, and the only symptom is that a toolbar button goes
# faint again. So the fetch list is checked for them by name.
TP="$ROOT/tools/build_tango_pack.sh"
for s in tb-rename tb-dual tb-copy m-logview; do
    if grep -q "^$s:" "$TP"; then
        echo "  OVERWRITTEN: build_tango_pack.sh fetches $s from Tango, but"
        echo "        $s.bmp is original CastaliaOS art -- a re-run of that"
        echo "        script would replace it (see THIRD_PARTY_NOTICES.md)."
        exit 1
    fi
    if [ ! -f "$ROOT/assets/icons/castalia/$s.bmp" ]; then
        echo "  MISSING: $s.bmp is not in the original pack, so"
        echo "        make gen-iconpack cannot regenerate it."
        exit 1
    fi
done
echo "Original glyphs OK: 4 first-party icons, none re-fetched"
echo "                    from Tango and all regenerable."
