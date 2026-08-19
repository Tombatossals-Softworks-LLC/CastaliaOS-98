#!/bin/sh
# =====================================================================
#  make_release_folder.sh - Assemble the committed, ready-to-use RELEASE/
#  folder: the fully compiled CastaliaOS 98 PE, laid out exactly as it
#  installs to C:\CASTALIA, with the licensed DOS/4GW extender bundled in.
#
#  Unlike tools/package_release.sh (which zips a versioned download for a
#  GitHub Release), this populates the in-repo RELEASE/ tree so the compiled
#  product ships with the source and can be copied straight to a DOS drive.
#
#  What lands in RELEASE/:
#      CASTALIA/                the C:\CASTALIA install tree
#        BIN/CASTALIA.EXE       the graphical desktop (DOS/4G)
#        BIN/CBOOT.EXE          the safe launcher
#        BIN/INSTALL.EXE        the on-target installer
#        BIN/DOS4GW.EXE         the DOS/4GW extender (licensed -- see LEGAL.md)
#        APPS/ THEMES/ SYS/ ... data, config, and runtime dirs
#      CONFIG.SYS  AUTOEXEC.BAT  boot templates
#      README.md  LEGAL.md  RECOVERY.md  INSTALL.txt
#
#  Prerequisite: build the DOS product first so the .exe exist:
#      wmake -f Makefile.dos            (under an Open Watcom environment)
#
#  Usage:  tools/make_release_folder.sh
#  Output: RELEASE/  (regenerated from scratch each run)
# =====================================================================
set -e
cd "$(dirname "$0")/.."

BIN="dist/cdroot/CASTALIA/BIN"
OUT="RELEASE"
. tools/release_common.sh

# The DOS product must be present -- never assemble a RELEASE without the exes.
for e in castalia.exe cboot.exe install.exe; do
    if [ ! -f "$BIN/$e" ]; then
        echo "ERROR: $BIN/$e missing -- run 'wmake -f Makefile.dos' first." >&2
        exit 1
    fi
done
DOS4GW="$(find_dos4gw)" || exit 1
echo "bundling DOS/4GW from $DOS4GW"

rm -rf "$OUT"
mkdir -p "$OUT"

# The install tree, with the .exe uppercased to match README/AUTOEXEC.
cp -r dist/cdroot/CASTALIA "$OUT/CASTALIA"
for f in "$OUT"/CASTALIA/BIN/*.exe; do
    [ -e "$f" ] || continue
    mv "$f" "$(dirname "$f")/$(basename "$f" | tr 'a-z' 'A-Z')"
done

# Bundle the licensed DOS/4GW extender alongside the executables. This is the
# whole point of the RELEASE tree: it runs as-is, with no toolchain to install.
cp "$DOS4GW" "$OUT/CASTALIA/BIN/DOS4GW.EXE"
chmod 0644 "$OUT/CASTALIA/BIN/DOS4GW.EXE"

# Stage the desktop icon packs (single source of truth: assets/icons/). The
# shipped INI defaults [Assets] Icons= to ICONS\TANGO. TANGO is the public-
# domain Tango set (see THIRD_PARTY_NOTICES.md); CASTALIA is the original MIT
# set. Cleared, the shell falls back to built-in procedural icons.
stage_icon_packs "$OUT/CASTALIA"

# Boot templates + docs at the tree root. THIRD_PARTY_NOTICES.md must travel
# with the tree: it carries the DOS/4GW redistribution basis that LEGAL.md and
# the release notes point to now that DOS4GW.EXE is bundled.
cp dist/CONFIG.SYS dist/AUTOEXEC.BAT "$OUT/"
cp LEGAL.md THIRD_PARTY_NOTICES.md "$OUT/" 2>/dev/null || true
cp docs/RECOVERY.md "$OUT/" 2>/dev/null || true

cat > "$OUT/README.md" <<'EOF'
# CastaliaOS 98 PE — RELEASE (fully compiled, ready to use)

This folder is the **compiled** CastaliaOS 98 PE, laid out exactly as it
installs to `C:\CASTALIA`. It is produced from source by
`tools/make_release_folder.sh` after the Open Watcom DOS build
(`wmake -f Makefile.dos`), and it ships the DOS binaries **and** the DOS/4GW
extender, so there is no toolchain to install and nothing extra to supply.

> CastaliaOS 98 PE is **not** Microsoft Windows and contains **no** Microsoft
> code, files, icons, sounds, fonts, or branding. It is an original work
> inspired by the late-1990s desktop idiom. See `LEGAL.md`.

## What's here

```
CASTALIA/                the C:\CASTALIA install tree
  BIN/CASTALIA.EXE       the graphical desktop      (DOS/4G, Open Watcom)
  BIN/CBOOT.EXE          the safe launcher
  BIN/INSTALL.EXE        the on-target installer
  BIN/DOS4GW.EXE         the DOS/4GW extender (bundled under license)
  APPS/                  first-party add-on packages (.CAPP)
  THEMES/CLASSIC/        the default theme
  SYS/CASTALIA.INI       main configuration (plain text, editable from DOS)
  FONTS/ ICONS/ HELP/    asset dirs (fonts + icons are built into the shell)
  DRV/ LOGS/ TEMP/ TRASH/ runtime dirs
CONFIG.SYS  AUTOEXEC.BAT boot templates
README.md  LEGAL.md  THIRD_PARTY_NOTICES.md  RECOVERY.md  INSTALL.txt
```

`THIRD_PARTY_NOTICES.md` records the DOS/4GW redistribution basis (the extender
is bundled under a license held by Tombatossals Softworks LLC).

## Use it in three steps

1. Copy `CASTALIA\` to the root of a FreeDOS (or compatible DOS) drive so it
   lives at `C:\CASTALIA`.
2. Run `C:\CASTALIA\BIN\INSTALL.EXE` (it backs up your CONFIG.SYS/AUTOEXEC.BAT
   and adds the Castalia boot block), or merge the provided templates by hand.
3. Reboot — or just run `C:\CASTALIA\BIN\CBOOT.EXE` to start the desktop now
   (`CBOOT.EXE /safe` for Safe Mode).

Target hardware: a Pentium-II / Intel 440BX-class machine with a VESA (VBE)
BIOS. The desktop walks a fallback ladder `800x600x16 → 640x480x16 →
640x480x8`. FreeDOS is a separate project and is **not** bundled — supply your
own. Full detail in `INSTALL.txt` and `RECOVERY.md`.
EOF

# Keep runtime dirs present but empty; drop transient cruft.
rm -f "$OUT"/CASTALIA/LOGS/* "$OUT"/CASTALIA/TEMP/* "$OUT"/CASTALIA/TRASH/* 2>/dev/null || true
# Dirs that now hold real files don't need a .gitkeep placeholder.
rm -f "$OUT/CASTALIA/BIN/.gitkeep" "$OUT/CASTALIA/APPS/.gitkeep"
# Re-seed .gitkeep so the empty runtime dirs survive in git.
for d in LOGS TEMP TRASH FONTS HELP DRV; do
    [ -d "$OUT/CASTALIA/$d" ] && touch "$OUT/CASTALIA/$d/.gitkeep"
done

cat > "$OUT/INSTALL.txt" <<'EOF'
CastaliaOS 98 PE -- install notes (RELEASE)
===========================================

This RELEASE tree is the fully compiled product, ready to run. Unlike a
source build, the DOS/4GW extender is already bundled -- there is nothing
extra to supply.

1. Copy the CASTALIA\ folder to the root of your DOS drive, so it lives at
   C:\CASTALIA (BIN, APPS, THEMES, SYS, ...). BIN already contains
   CASTALIA.EXE, CBOOT.EXE, INSTALL.EXE and DOS4GW.EXE.

2. Boot integration. Either run the bundled INSTALL.EXE (it backs up your
   CONFIG.SYS / AUTOEXEC.BAT and adds the Castalia boot block), or merge the
   provided CONFIG.SYS and AUTOEXEC.BAT templates by hand.

3. (Optional) Mouse. For an INT 33h pointer, load a DOS mouse driver (e.g.
   CuteMouse / CTMOUSE.EXE) before CBOOT -- uncomment the line in AUTOEXEC.BAT.
   The desktop is fully keyboard-navigable without one.

4. Start it. AUTOEXEC.BAT calls C:\CASTALIA\BIN\CBOOT.EXE, or run that
   yourself (add /safe for Safe Mode).

Recovery: press F5 at boot to skip CONFIG.SYS/AUTOEXEC.BAT, or F8 to step
through them, then edit C:\CASTALIA\SYS\CASTALIA.INI. See RECOVERY.md.
EOF

echo "assembled $OUT/"
find "$OUT" -type f | sort
