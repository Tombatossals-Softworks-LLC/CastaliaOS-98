# CastaliaOS 98 PE — Recovery Guide

CastaliaOS is designed to be **repairable from DOS with a text editor**. This
guide covers getting back to a working state after a bad boot, a broken
configuration, a failed video mode, or a crash. Keep a DOS boot floppy or CD
nearby before experimenting on real hardware.

The golden rule: **if a setting breaks the desktop, you can always delete or
edit one plain-text file and recover.** No binary registry is required to boot.

---

## 0. Fastest escape hatches

| Situation | Do this |
|-----------|---------|
| Desktop won't start / graphics garbled | Reboot; the desktop returns to DOS on video failure. Then run `CBOOT /safe`. |
| Machine hangs before the desktop | At boot, press **F5** to skip `CONFIG.SYS`/`AUTOEXEC.BAT` entirely, or **F8** to step line by line. |
| You just want DOS | From the desktop: launcher → **Exit to DOS**. |
| Repeated crashes | CBOOT forces **Safe Mode** automatically after two unclean boots. |

## 1. How auto-start works (so you can undo it)

`AUTOEXEC.BAT` sets `CASTALIA_HOME`/`PATH` and runs
`C:\CASTALIA\BIN\CBOOT.EXE`, which launches `CASTALIA.EXE`. To stop the desktop
from starting automatically, boot to DOS and put `REM ` in front of the
`CBOOT.EXE` line in `AUTOEXEC.BAT`. The installer keeps a backup named
`AUTOEXEC.CB_` (and `CONFIG.CB_`) — you can restore it verbatim.

## 2. Safe Mode

Safe Mode uses a high-contrast theme and forces the most compatible video mode
(640×480×8). Enter it any of these ways:

- Run `C:\CASTALIA\BIN\CBOOT /safe`.
- Edit `C:\CASTALIA\SYS\CASTALIA.INI` and set, under `[Boot]`, `SafeMode=1`.
- Let CBOOT trigger it automatically after two consecutive unclean boots.

To leave Safe Mode, set `SafeMode=0` again (or launch without `/safe`).

## 3. Video mode problems

CastaliaOS tries modes in this order and falls back automatically:

```
800x600x16  ->  640x480x16  ->  640x480x8  ->  (fail) return to DOS with a message
```

If the screen is unreadable or the card rejects a mode:

1. Boot to DOS (F5 at boot, or Exit to DOS).
2. Edit `C:\CASTALIA\SYS\CASTALIA.INI`:
   ```ini
   [Video]
   Width=640
   Height=480
   Depth=8
   ```
3. Restart with `CBOOT`.

If even 640×480×8 fails, the shell logs the failure and returns to DOS; your
VESA BIOS may not expose a usable mode — see the video BIOS notes in
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md).

## 4. Broken or corrupt configuration

All configuration is plain text under `C:\CASTALIA\SYS\` and
`C:\CASTALIA\THEMES\`. The INI reader is deliberately forgiving: it skips
unparseable lines and treats a missing file as "use defaults."

- **Reset all settings:** delete `C:\CASTALIA\SYS\CASTALIA.INI`. The shell
  recreates missing keys with defaults on next run.
- **Reset the theme:** delete or fix
  `C:\CASTALIA\THEMES\CLASSIC\THEME.INI`. Missing colors fall back to the
  built-in "Castalia Classic" palette, so even an empty file is safe.
- **Bad value only:** open the file in `EDIT` (FreeDOS) and correct the one line;
  you do not need to understand the whole file.

## 5. Reading crash information

When the shell hits a fatal error it:

1. Paints a recoverable notice and returns to DOS.
2. Appends a record to `C:\CASTALIA\SYS\crash.log` (subsystem, code,
   file:line, message, live/peak memory).
3. Leaves the boot-dirty flag set so CBOOT can offer Safe Mode next boot.

Session logs are in `C:\CASTALIA\LOGS\castalia.log` (rotated at 512 KB to
`castalia.log.old`). Errors are also echoed to the console. Include both files
when reporting a bug.

## 6. The boot-dirty flag and fail counter

- `C:\CASTALIA\SYS\bootdirty.flg` exists **only while a session is running**. A
  clean exit removes it. If it is present at boot, the previous session did not
  end cleanly.
- `bootfail.cnt` counts consecutive unclean boots. At **2** it forces Safe Mode.
  A clean run resets it to 0.
- If you ever want to clear the "previous session was dirty" state by hand,
  delete `bootdirty.flg` and `bootfail.cnt` from DOS. They are harmless to
  remove.

## 7. Missing or damaged program files

If `CBOOT` reports that `CASTALIA.EXE` is missing, or the desktop won't load
resources:

1. Verify `C:\CASTALIA\BIN\` contains `CASTALIA.EXE`, `CBOOT.EXE`, and the
   DOS extender `DOS4GW.EXE` (required to run the 32-bit executables).
2. Re-copy the `CASTALIA` tree from your install media / `dist/cdroot/CASTALIA`,
   or run the installer's **Repair** mode (Phase 3).
3. Confirm free disk space and that the files are not read-only.

## 8. Last-resort clean recovery

1. Boot from a DOS floppy/CD.
2. Restore `AUTOEXEC.CB_` → `AUTOEXEC.BAT` and `CONFIG.CB_` → `CONFIG.SYS` to
   remove CastaliaOS from the boot path.
3. Optionally delete or rename `C:\CASTALIA` to disable it entirely without
   touching the rest of your DOS install.

CastaliaOS never overwrites `CONFIG.SYS` or `AUTOEXEC.BAT` without leaving a
timestamped backup, and it lives entirely inside `C:\CASTALIA`, so removing it is
always reversible.

## 9. Prevention checklist for real hardware

- Clone the old IDE disk to a CompactFlash/SD-to-IDE adapter and experiment on
  the clone (old drives are fragile).
- Keep a known-good DOS boot medium.
- Record your machine's BIOS version, disk model, and VESA mode list before
  testing (see [TESTING.md](TESTING.md), real-hardware profile).
