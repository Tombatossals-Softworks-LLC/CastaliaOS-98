# =====================================================================
#  release_common.sh - the parts of a release tree both assemblers need.
#
#  There are two: tools/package_release.sh zips a versioned download for a
#  GitHub Release, tools/make_release_folder.sh populates the in-repo
#  RELEASE/ tree. They are supposed to lay out the SAME C:\CASTALIA, and
#  they had already drifted -- make_release_folder.sh staged the icon packs
#  and package_release.sh did not, so every ZIP shipped a CASTALIA.INI
#  pointing [Assets] Icons= at a C:\CASTALIA\ICONS\TANGO that was not in the
#  archive. Nothing failed; the desktop just quietly fell back to procedural
#  icons and the default pack the notices describe never arrived.
#
#  So the shared steps live here once, and both scripts call them.
#
#  Usage:  . tools/release_common.sh    (sourced, from the repo root)
# =====================================================================

# Print the path to the DOS/4GW extender to bundle, or fail saying where it
# looked.
#
# Two sources, in order of preference:
#   1. include/vendor/DOS4GW.EXE -- a copy committed to the repository, which
#      pins one exact extender build to the product.
#   2. $WATCOM/binw/dos4gw.exe -- the extender that ships inside the Open
#      Watcom installation that just compiled the executables. This is the
#      same file THIRD_PARTY_NOTICES.md already describes ("the DOS/4GW
#      extender distributed with Open Watcom"), and it means CI can cut a
#      complete release without a binary being committed to a public tree.
#
#  Either way it is redistributed under the DOS/4GW distribution license
#  recorded in LEGAL.md and THIRD_PARTY_NOTICES.md, and shipped unmodified.
find_dos4gw() {
    if [ -f "include/vendor/DOS4GW.EXE" ]; then
        printf '%s\n' "include/vendor/DOS4GW.EXE"
        return 0
    fi
    if [ -n "$WATCOM" ] && [ -f "$WATCOM/binw/dos4gw.exe" ]; then
        printf '%s\n' "$WATCOM/binw/dos4gw.exe"
        return 0
    fi
    echo "ERROR: no DOS/4GW extender to bundle. Looked for:" >&2
    echo "         include/vendor/DOS4GW.EXE   (a committed copy)" >&2
    echo "         \$WATCOM/binw/dos4gw.exe     (WATCOM=${WATCOM:-unset})" >&2
    echo "       The shipped tree must run as-is; see LEGAL.md." >&2
    return 1
}

# Stage the desktop icon packs into an install tree's ICONS\ directory.
#
# The shipped CASTALIA.INI defaults [Assets] Icons= to C:\CASTALIA\ICONS\TANGO,
# so a tree without these is a tree whose own configuration points at nothing.
# TANGO is the public-domain Tango set, CASTALIA the original MIT one; both are
# recorded in THIRD_PARTY_NOTICES.md. Uppercased to match the INI, the notices,
# and what DOS shows -- it matters the moment somebody unpacks the ZIP on a
# case-sensitive filesystem to look inside.
stage_icon_packs() {
    tree="$1"
    mkdir -p "$tree/ICONS"
    cp -r assets/icons/tango    "$tree/ICONS/TANGO"
    cp -r assets/icons/castalia "$tree/ICONS/CASTALIA"
    cp assets/icons/README.md   "$tree/ICONS/README.TXT"
}
