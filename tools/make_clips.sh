#!/bin/sh
# make_clips.sh - record scripted product demos and assemble marketing clips.
#
# Drives the headless build through each recorder scene (src/main.c
# run_recorder), capturing one frame per shell frame, then packs the frames
# into an animated GIF (universal) and a WebM (small, high quality).
#
# Usage:  sh tools/make_clips.sh [OUT_DIR]      (default: build/clips)
# Env:    FF=/path/to/ffmpeg   BIN=/path/to/castalia
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build/castalia}"
ICONS="$ROOT/assets/icons/tango"
OUT="${1:-$ROOT/build/clips}"
TMP="$OUT/frames"
mkdir -p "$OUT" "$TMP"

# A demo home: folders + files for the File Manager, WAVs for the Media Player.
HOME_DIR="$TMP/home"
rm -rf "$HOME_DIR"
mkdir -p "$HOME_DIR/SYS" "$HOME_DIR/LOGS" "$HOME_DIR/PHOTOS" "$HOME_DIR/MEDIA"
cp "$ROOT/dist/cdroot/CASTALIA/MEDIA/"*.WAV "$HOME_DIR/MEDIA/" 2>/dev/null || true
for f in README.TXT SONG.WAV CASTLE.BMP NOTES.TXT; do echo "demo file" > "$HOME_DIR/$f"; done

clip() {   # id name extra-env
    id="$1"; name="$2"; xenv="$3"
    fdir="$TMP/s$id"; rm -rf "$fdir"; mkdir -p "$fdir"
    printf '  REC   scene %s  %s\n' "$id" "$name"
    env CASTALIA_HOME="$HOME_DIR" $xenv "$BIN" --headless \
        --record "$fdir" --scene "$id" --icons "$ICONS" >/dev/null 2>&1
    python3 "$ROOT/tools/frames_to_gif.py"  "$fdir" "$OUT/$id-$name.gif" 2 80 480x360
    python3 "$ROOT/tools/frames_to_webm.py" "$fdir" "$OUT/$id-$name.webm" 24 1800k
}

clip 1 desktop-tour   ""
clip 2 file-dragdrop  ""
clip 3 benchmark      ""
clip 4 media-player   ""
clip 5 solitaire-win  "SOL_WIN=1"
clip 6 desktop-icons  ""
clip 7 spreadsheet    ""
clip 8 word-processor ""

rm -rf "$TMP"
echo "Clips written to $OUT"
