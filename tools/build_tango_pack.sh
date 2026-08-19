#!/bin/sh
# build_tango_pack.sh - (Re)build assets/icons/tango/ from the Public-Domain
# Tango Icon Library. Documents provenance and makes the pack reproducible.
#
# Fetches the rasterized 64x64 PNGs from the Tango PNG mirror and converts each
# to the CastaliaOS keyed-BMP convention with tools/png2bmp.py (stdlib only).
# Desktop icons are baked at 32 px, toolbar/Quick Launch glyphs at 16 px.
#
# Source (Public Domain -- see licenses/tango-COPYING.txt):
#   https://github.com/nigeltao/tango-icon-library-pngs
#
# Requires: curl, python3. Run from the repo root:  sh tools/build_tango_pack.sh
set -e

OUT=assets/icons/tango
BASE=https://raw.githubusercontent.com/nigeltao/tango-icon-library-pngs/master/png-64
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# pack-name : tango-source-name : size
SET="
computer:devices-computer:32
folder:places-folder:32
settings:categories-preferences-system:32
document:mimetypes-text-x-generic:32
trash:places-user-trash:32
media:mimetypes-audio-x-generic:32
clock:apps-office-calendar:32
tb-up:actions-go-up:16
tb-refresh:actions-view-refresh:16
tb-newfolder:actions-folder-new:16
tb-cut:actions-edit-cut:16
tb-paste:actions-edit-paste:16
tb-delete:places-user-trash:16
tb-find:actions-system-search:16
# NOTE: tb-rename.bmp, tb-dual.bmp, tb-copy.bmp and m-logview.bmp are NOT
# fetched here --
# they are original CastaliaOS art baked by tools/gen_iconpack.c, so this
# script must NOT list them or a re-run would overwrite them.
#   rename / 2-pane: Tango has no glyph for either. Its nearest "rename" is
#     the text-editor icon this pack already uses for Notepad, and there is
#     no split-view icon at all.
#   copy: Tango's actions-edit-copy is a single near-white page with no
#     outline. On the toolbar's glossy button face -- (238,242,247), nearly
#     white itself -- 9% of its pixels clear a contrast of 60, against 47%
#     or better for every other icon in the row. It read as a smudge, and
#     it showed one page, which is not what copying looks like.
#   log viewer: Tango's text-x-generic is a white page with pale rules and
#     no outline. The Start menu paints its columns white and pale blue, so
#     FOUR of that icon's 256 pixels stood out against the panel behind it;
#     the next faintest icon in the menu manages 23 and the median is 104.
#     Measure with tools/icon_contrast.py before mapping a new Tango icon.
# Regenerate all three with:
#   make gen-iconpack && cp assets/icons/castalia/tb-rename.bmp \
#       assets/icons/castalia/tb-dual.bmp assets/icons/castalia/tb-copy.bmp \
#       assets/icons/tango/
ql-fileman:apps-system-file-manager:16
ql-notepad:apps-accessories-text-editor:16
m-sysinfo:devices-computer:16
m-fileman:apps-system-file-manager:16
m-control:categories-preferences-system:16
m-taskman:apps-utilities-system-monitor:16
m-notepad:apps-accessories-text-editor:16
m-app:categories-applications-other:16
m-viewer:mimetypes-image-x-generic:16
m-compare:actions-edit-find-replace:16
m-calc:apps-accessories-calculator:16
m-mines:actions-process-stop:16
m-terminal:apps-utilities-terminal:16
m-help:apps-help-browser:16
m-restart:actions-view-refresh:16
m-shutdown:actions-system-log-out:16
m-paint:apps-preferences-desktop-wallpaper:16
m-write:mimetypes-x-office-document:16
m-sheet:mimetypes-x-office-spreadsheet:16
m-media:mimetypes-audio-x-generic:16
m-bench:apps-utilities-system-monitor:16
m-clock:apps-office-calendar:16
m-charmap:apps-accessories-character-map:16
m-network:devices-network-wired:16
m-capture:devices-camera-photo:16
m-run:actions-go-jump:16
m-cards:categories-applications-games:16
m-diskuse:devices-drive-harddisk:16
m-theme:apps-preferences-desktop-theme:16
tray-sound-on:status-audio-volume-high:12
tray-sound-off:status-audio-volume-muted:12
tray-net-on:status-network-idle:12
tray-net-off:status-network-offline:12
tray-clip-on:actions-edit-paste:12
"

for row in $SET; do
    name=$(echo "$row" | cut -d: -f1)
    src=$(echo  "$row" | cut -d: -f2)
    size=$(echo "$row" | cut -d: -f3)
    curl -fsSL "$BASE/$src.png" -o "$TMP/$src.png"
    python3 tools/png2bmp.py "$TMP/$src.png" "$OUT/$name.bmp" --size "$size" >/dev/null
    echo "  $name.bmp  <- $src ($size px)"
done

# "Empty clipboard" tray state: a desaturated paste icon (source already fetched).
python3 tools/png2bmp.py "$TMP/actions-edit-paste.png" "$OUT/tray-clip-off.bmp" \
    --size 12 --gray >/dev/null
echo "  tray-clip-off.bmp  <- actions-edit-paste (12 px, gray)"

echo "Tango pack rebuilt in $OUT"
