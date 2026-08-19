#!/bin/sh
# =====================================================================
#  package_release.sh - Assemble a downloadable CastaliaOS 98 PE release.
#
#  Takes the DOS product just built by `wmake -f Makefile.dos` (the .exe in
#  dist/cdroot/CASTALIA/BIN) plus the shipped install tree, boot templates, and
#  top-level docs, and packs a clean, ready-to-copy-to-C: ZIP:
#
#      castalia-98-pe-<version>/
#        CASTALIA/            the install tree (BIN with the .EXE, APPS, THEMES,
#                             SYS\CASTALIA.INI, ICONS, HELP, ...)
#        CONFIG.SYS           boot templates the installer merges
#        AUTOEXEC.BAT
#        README.md  LEGAL.md  THIRD_PARTY_NOTICES.md  RECOVERY.md  INSTALL.txt
#
#  DOS4GW.EXE IS bundled -- from include/vendor/DOS4GW.EXE if a copy is
#  committed, otherwise from the Open Watcom installation that just built the
#  executables ($WATCOM/binw/dos4gw.exe) -- under the DOS/4GW distribution
#  license held by Tombatossals Softworks LLC; see LEGAL.md and
#  THIRD_PARTY_NOTICES.md. The packaged tree therefore runs as-is.
#
#  Usage:  tools/package_release.sh <version>     (e.g. v0.1.0)
#  Output: dist/castalia-98-pe-<version>.zip  +  dist/RELEASE_NOTES.md
# =====================================================================
set -e
# Every path below is repo-root-relative, as the header promises; say so once
# instead of depending on where the caller happened to be standing.
cd "$(dirname "$0")/.."
VER="${1:?usage: package_release.sh <version>}"
NAME="castalia-98-pe-$VER"
STAGE="dist/pkg/$NAME"
BIN="dist/cdroot/CASTALIA/BIN"
. tools/release_common.sh

# The DOS product must be present -- never publish a release without the exes.
if ! ls "$BIN"/*.exe >/dev/null 2>&1; then
    echo "ERROR: no .exe in $BIN -- run 'wmake -f Makefile.dos' first." >&2
    exit 1
fi
# The extender must be present -- the shipped tree runs as-is.
DOS4GW="$(find_dos4gw)" || exit 1
echo "bundling DOS/4GW from $DOS4GW"

rm -rf "dist/pkg"
mkdir -p "$STAGE"

# The install tree, with the .exe uppercased to match README/AUTOEXEC.
cp -r dist/cdroot/CASTALIA "$STAGE/CASTALIA"
for f in "$STAGE"/CASTALIA/BIN/*.exe; do
    [ -e "$f" ] || continue
    mv "$f" "$(dirname "$f")/$(basename "$f" | tr 'a-z' 'A-Z')"
done

# Bundle the licensed DOS/4GW extender so the tree runs with no toolchain.
cp "$DOS4GW" "$STAGE/CASTALIA/BIN/DOS4GW.EXE"
chmod 0644 "$STAGE/CASTALIA/BIN/DOS4GW.EXE"

# The icon packs the shipped CASTALIA.INI points at.
stage_icon_packs "$STAGE/CASTALIA"

# Boot templates + docs at the package root. THIRD_PARTY_NOTICES.md ships too:
# it carries the DOS/4GW redistribution basis the bundled extender relies on.
cp dist/CONFIG.SYS dist/AUTOEXEC.BAT "$STAGE/"
cp README.md LEGAL.md THIRD_PARTY_NOTICES.md "$STAGE/" 2>/dev/null || true
cp docs/RECOVERY.md "$STAGE/" 2>/dev/null || true

# Strip transient/runtime cruft and VCS placeholders so the tree is pristine.
find "$STAGE" -name '.gitkeep' -delete
rm -f "$STAGE"/CASTALIA/LOGS/* "$STAGE"/CASTALIA/TEMP/* "$STAGE"/CASTALIA/TRASH/* 2>/dev/null || true

cat > "$STAGE/INSTALL.txt" <<EOF
CastaliaOS 98 PE $VER -- install notes
=======================================

1. Copy the CASTALIA\\ folder to the root of your DOS drive, so it lives at
   C:\\CASTALIA (BIN, APPS, THEMES, SYS, ...). BIN already contains
   CASTALIA.EXE, CBOOT.EXE, INSTALL.EXE and DOS4GW.EXE -- nothing to supply.

2. Boot integration. Either run the bundled INSTALL.EXE (it backs up your
   CONFIG.SYS / AUTOEXEC.BAT and adds the Castalia boot block), or merge the
   provided CONFIG.SYS and AUTOEXEC.BAT templates by hand.

3. Start it. AUTOEXEC.BAT calls C:\\CASTALIA\\BIN\\CBOOT.EXE, or run that
   yourself (add /safe for Safe Mode).

Recovery: press F5 at boot to skip CONFIG.SYS/AUTOEXEC.BAT, or F8 to step
through them, then edit C:\\CASTALIA\\SYS\\CASTALIA.INI. See RECOVERY.md.
EOF

# Zip with the versioned dir as the archive root.
( cd dist/pkg && zip -r -q "../$NAME.zip" "$NAME" )
echo "packaged dist/$NAME.zip"
ls -la "dist/$NAME.zip"

# Release notes body (used by the workflow's `gh release create --notes-file`).
cat > dist/RELEASE_NOTES.md <<EOF
# CastaliaOS 98 PE $VER

An original, Windows-98-SE-inspired desktop with a Windows-XP look and feel,
built for DOS-class hardware (Pentium II, 384 MB). No Microsoft code or
artwork -- original assets only (see LEGAL.md).

## Download

\`$NAME.zip\` contains the ready-to-install \`CASTALIA\\\` tree (the DOS product
built with Open Watcom: **CASTALIA.EXE**, **CBOOT.EXE**, **INSTALL.EXE**), the
bundled **DOS4GW.EXE** extender, the boot templates, and install/recovery docs.
See \`INSTALL.txt\` inside. It runs as-is -- no toolchain to install.

**DOS4GW.EXE is bundled** under the DOS/4GW distribution license held by
Tombatossals Softworks LLC (see LEGAL.md and THIRD_PARTY_NOTICES.md).

## Verified

- DOS product compiled + linked by CI with Open Watcom 2.0 (this release's exes).
- Portable stack: 7609 unit tests pass on gcc + clang; strict-C89 (Watcom) lint clean.
- kernel-lab image boots in QEMU (PIT IRQ0 + PS/2 IRQ1) under CI.
- The DOS/VESA desktop has booted end-to-end in QEMU on FreeDOS (see TESTING.md).
EOF
echo "wrote dist/RELEASE_NOTES.md"
