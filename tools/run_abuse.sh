#!/bin/sh
# run_abuse.sh - boot the shell against hostile input and require it to recover.
#
# `make test` proves the pure logic and `make demos` proves the wiring. Both
# work with input the code expects. This one supplies input it does not: a
# CASTALIA.INI full of random bytes, one truncated mid-key, one whose numbers
# are absurd, a config directory that is not a directory, a wallpaper that is
# a bitmap only in name, a folder nested sixty deep, an agenda with hundreds
# of malformed lines, a filename longer than any buffer, and an archive whose
# header claims two gigabytes.
#
# (An unwritable home is deliberately NOT among them: these runs may happen as
# root, where chmod does not actually stop a write, and a case that cannot fail
# is not a case.)
#
# These are the failure modes that make an operating system feel unreliable,
# and none of them are exotic -- a machine that loses power mid-write produces
# most of them by itself.
#
# What each case must do is RECOVER, which is a higher bar than not crashing.
# Three assertions per case: the process exits cleanly, the log shows it
# reached the end of its own lifecycle rather than stopping somewhere in the
# middle, and --screen-check confirms a real desktop was painted rather than a
# blank screen that failed quietly.
#
# All three were confirmed able to fail, because a harness nobody has seen fail
# is a harness nobody should trust: disabling sh_desktop_paint fails all twelve
# on the screen check, a BIN that exits 1 fails all twelve on the exit code, and
# a BIN that succeeds silently fails all twelve on the lifecycle.
#
# Usage:  sh tools/run_abuse.sh        (or: make abuse)
# Env:    BIN=/path/to/castalia
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build/castalia}"
case "$BIN" in
    /*) ;;
    *)  BIN="$ROOT/$BIN" ;;
esac
WORK="$ROOT/build/abuse"

if [ ! -x "$BIN" ]; then
    echo "run_abuse: $BIN is missing -- run make first" >&2
    exit 1
fi

rm -rf "$WORK"
rm -f "$ROOT/build/.abuse-fails"
mkdir -p "$WORK"

# --- the hostile worlds ------------------------------------------------
mk() { mkdir -p "$WORK/$1/SYS"; }

mk garbage_ini
head -c 4000 /dev/urandom > "$WORK/garbage_ini/SYS/CASTALIA.INI" 2>/dev/null || \
    printf '\001\002\003\377\376' > "$WORK/garbage_ini/SYS/CASTALIA.INI"

mk truncated_ini
printf '[Shell]\nAnimations' > "$WORK/truncated_ini/SYS/CASTALIA.INI"

mk absurd_ini
printf '[Display]\nWidth=999999\nHeight=-5\nBpp=77\n[Shell]\nAnimations=maybe\nTaskbarHeight=-100000\nIconPack=\n' \
    > "$WORK/absurd_ini/SYS/CASTALIA.INI"

mk empty_ini
: > "$WORK/empty_ini/SYS/CASTALIA.INI"

# A config file that is a directory: every open of it fails, not just parses.
mk ini_is_a_dir
rm -f "$WORK/ini_is_a_dir/SYS/CASTALIA.INI"
mkdir -p "$WORK/ini_is_a_dir/SYS/CASTALIA.INI"

# No SYS folder at all -- a first boot, or a half-finished install.
mkdir -p "$WORK/no_sys"

# A wallpaper that is a bitmap only by its name, referenced from the config.
mk bad_wallpaper
printf 'BM\377\377\377\377\000\000\000\000' > "$WORK/bad_wallpaper/BROKEN.BMP"
printf '[Desktop]\nWallpaper=BROKEN.BMP\nWallpaperMode=2\n' \
    > "$WORK/bad_wallpaper/SYS/CASTALIA.INI"

# A theme whose every colour is nonsense.
mk absurd_theme
printf '[Theme]\nFace=notacolour\nText=-1\nAccent=999999999\nDesktopTop=\nTitleActiveL=#ZZZZZZ\n' \
    > "$WORK/absurd_theme/SYS/CASTALIA.INI"

# Thousands of malformed appointment lines.
mk huge_agenda
i=0
: > "$WORK/huge_agenda/SYS/AGENDA.TXT"
while [ $i -lt 400 ]; do
    printf '9999-99-99 99:99 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n' \
        >> "$WORK/huge_agenda/SYS/AGENDA.TXT"
    i=$((i + 1))
done

# A folder nested far past anything a walker should follow.
mk deep_tree
d="$WORK/deep_tree"
i=0
while [ $i -lt 60 ]; do d="$d/L$i"; i=$((i + 1)); done
mkdir -p "$d"
printf 'bottom\n' > "$d/DEEP.TXT"

# Names longer than the buffers that will hold them.
mk long_names
long=NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN
printf 'x\n' > "$WORK/long_names/$long$long$long.TXT"

# A .CZ that lies about its contents, sitting where the shell will list it.
mk lying_archive
printf 'CZ1\000\377\377\377\177\377\377\377\177\001\000LIAR.TXT\000\000\000\000\000\000' \
    > "$WORK/lying_archive/LIAR.CZ"

CASES='garbage_ini:a CASTALIA.INI of random bytes
truncated_ini:a CASTALIA.INI cut off mid-key
absurd_ini:a config whose every number is impossible
empty_ini:a zero-byte config
ini_is_a_dir:a config file that is a directory
no_sys:no SYS folder at all
bad_wallpaper:a wallpaper that is a bitmap in name only
absurd_theme:a theme whose colours are all nonsense
huge_agenda:hundreds of malformed appointments
deep_tree:a folder nested sixty levels down
long_names:a filename longer than any buffer
lying_archive:a .CZ claiming two gigabytes'

echo "$CASES" | while IFS= read -r row; do
    [ -n "$row" ] || continue
    name="${row%%:*}"
    what="${row#*:}"
    home="$WORK/$name"
    code=0
    # No window is opened on purpose. An earlier version passed --open-fileman
    # here, and a File Manager filling the screen supplies plenty of colour on
    # its own -- the screen check then passed with desktop painting disabled
    # entirely, which is to say it was measuring the wrong thing.
    out="$(cd "$home" 2>/dev/null && CASTALIA_HOME="$home" "$BIN" --headless \
           --screen-check --frames 6 --shot /dev/null 2>&1)" || code=$?
    # The scene reports through SYS_LOGI, which reaches stderr only when the
    # log file cannot be opened. That used to be the case in every world here
    # -- none of them has a LOGS folder -- so this harness was reading its
    # evidence from a fallback that existed only because logging was broken.
    # Read both, and it no longer matters which one carries the line.
    out="$out
$(cat "$home/LOGS/castalia.log" 2>/dev/null || true)"
    code=${code:-0}
    bad=$(printf '%s\n' "$out" | grep -c 'MISMATCH' || true)
    ok=$(printf '%s\n' "$out" | grep -c 'SCREEN-CHECK.*(OK)' || true)
    ended=$(printf '%s\n' "$out" | grep -c 'exited to DOS' || true)
    if [ "$code" -ne 0 ]; then
        printf '  FAIL  %-14s %s -- exit %d\n' "$name" "$what" "$code"
        echo failed >> "$ROOT/build/.abuse-fails"
    elif [ "$bad" -gt 0 ]; then
        printf '  FAIL  %-14s %s\n' "$name" "$what"
        printf '%s\n' "$out" | grep 'MISMATCH' | sed 's/^/        /'
        echo failed >> "$ROOT/build/.abuse-fails"
    elif [ "$ended" -eq 0 ]; then
        printf '  FAIL  %-14s %s -- never finished its lifecycle\n' "$name" "$what"
        echo failed >> "$ROOT/build/.abuse-fails"
    elif [ "$ok" -eq 0 ]; then
        printf '  FAIL  %-14s %s -- no screen check ran\n' "$name" "$what"
        echo failed >> "$ROOT/build/.abuse-fails"
    else
        printf '  ok    %-14s %s\n' "$name" "$what"
    fi
done

# --- the session log on a machine that has never booted -----------------
#
# The log lives in LOGS\ and sys_log_init only opens it -- it does not create
# the folder. On a first boot, or an install that stopped half way, there is
# no LOGS folder yet, and the log was then silently never written. Silently is
# what makes it worth a check: the crash screen tells the reader to go and
# read that log, and the Log Viewer opens on it. Advice pointing at a file
# that was never created is worse than no advice.
nl="$WORK/no_folders"
mkdir -p "$nl"
CASTALIA_HOME="$nl" "$BIN" --headless --frames 4 --shot /dev/null >/dev/null 2>&1 || true
if [ -s "$nl/LOGS/castalia.log" ]; then
    printf '  ok    %-14s %s\n' firstboot "a home with no folders still gets a log"
else
    printf '  FAIL  %-14s %s\n' firstboot "a home with no folders wrote no log"
    echo failed >> "$ROOT/build/.abuse-fails"
fi
# ...and the crash log's folder too, since the crash handler writes there at
# the worst possible moment and cannot go making directories then.
if [ -d "$nl/SYS" ]; then
    printf '  ok    %-14s %s\n' firstboot "and the folder the crash log needs"
else
    printf '  FAIL  %-14s %s\n' firstboot "SYS was never created"
    echo failed >> "$ROOT/build/.abuse-fails"
fi

# --- last-known-good configuration, which needs more than one boot ------
#
# The twelve cases above prove a corrupt config cannot stop the shell coming
# up. This proves it does not cost the user their settings either: boot once
# cleanly so the config is kept aside, corrupt it, boot again, and require the
# original to be back byte for byte.
#
# The second half is the one that matters more. A corrupt config with no
# known-good copy must fall back to defaults and must NOT be promoted -- if it
# were, the broken file would become the file every later boot restores, which
# is worse than having no mechanism at all.
lg="$WORK/lastgood"
mkdir -p "$lg/SYS"
printf '[Shell]\nAnimations=0\n[Theme]\nPreset=2\n' > "$lg/SYS/CASTALIA.INI"
cp "$lg/SYS/CASTALIA.INI" "$WORK/lastgood.want"

CASTALIA_HOME="$lg" "$BIN" --headless --frames 4 --shot /dev/null >/dev/null 2>&1 || true
if [ -s "$lg/SYS/CASTALIA.BAK" ]; then
    printf '  ok    %-14s %s\n' lastgood "a clean boot keeps its config as known-good"
else
    printf '  FAIL  %-14s %s\n' lastgood "a clean boot did not keep its config"
    echo failed >> "$ROOT/build/.abuse-fails"
fi

head -c 3000 /dev/urandom > "$lg/SYS/CASTALIA.INI" 2>/dev/null || \
    printf '\001\002\003\377' > "$lg/SYS/CASTALIA.INI"
CASTALIA_HOME="$lg" "$BIN" --headless --frames 4 --shot /dev/null >/dev/null 2>&1 || true
if cmp -s "$lg/SYS/CASTALIA.INI" "$WORK/lastgood.want"; then
    printf '  ok    %-14s %s\n' lastgood "a corrupt config is replaced by the last good one"
else
    printf '  FAIL  %-14s %s\n' lastgood "a corrupt config was NOT restored"
    echo failed >> "$ROOT/build/.abuse-fails"
fi

# No backup anywhere: defaults, and the garbage must not become the backup.
lg2="$WORK/lastgood_nobak"
mkdir -p "$lg2/SYS"
head -c 3000 /dev/urandom > "$lg2/SYS/CASTALIA.INI" 2>/dev/null || \
    printf '\001\002\003\377' > "$lg2/SYS/CASTALIA.INI"
CASTALIA_HOME="$lg2" "$BIN" --headless --frames 4 --shot /dev/null >/dev/null 2>&1 || true
if [ -s "$lg2/SYS/CASTALIA.BAK" ]; then
    printf '  FAIL  %-14s %s\n' lastgood "a corrupt config was promoted to known-good"
    echo failed >> "$ROOT/build/.abuse-fails"
else
    printf '  ok    %-14s %s\n' lastgood "a corrupt config is never kept as known-good"
fi

# The twelve hostile worlds, plus the two first-boot checks and the three
# last-known-good ones. Counted from the list plus a named constant rather
# than a bare "+ 3", because the number in the summary line is the only thing
# a reader has to tell a full run from a truncated one.
ABUSE_EXTRA=5      # firstboot x2, lastgood x3
runs=$(echo "$CASES" | grep -c ':')
runs=$((runs + ABUSE_EXTRA))
fails=0
[ -f "$ROOT/build/.abuse-fails" ] && fails=$(wc -l < "$ROOT/build/.abuse-fails")

chmod -R u+w "$WORK" 2>/dev/null || true
rm -rf "$WORK"
rm -f "$ROOT/build/.abuse-fails"
if [ "$fails" -gt 0 ]; then
    echo "ABUSE FAILED: $fails of $runs"
    exit 1
fi
echo "Abuse OK: $runs checks, every hostile world recovered."
