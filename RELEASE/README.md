# CastaliaOS 98 PE — RELEASE (fully compiled, ready to use)

This folder is the **compiled** CastaliaOS 98 PE, laid out exactly as it
installs to `C:\CASTALIA`. It is produced from source by
`tools/make_release_folder.sh` after the Open Watcom DOS build
(`wmake -f Makefile.dos`), and it ships the DOS binaries **and** the DOS/4GW
extender, so there is no toolchain to install and nothing extra to supply.

> CastaliaOS 98 PE is **not** Microsoft Windows and contains **no** Microsoft
> code, files, icons, sounds, fonts, or branding. It is an original work
> inspired by the late-1990s desktop idiom. See `LEGAL.md`.

> **These binaries were built from commit `8ab8369` (2026-07-08), and the
> source has moved on since.** They boot and work, but they are a snapshot,
> not a mirror of `src/` — anything added after that date is missing from
> them. Building the DOS product needs Open Watcom, which is not available in
> every environment the source is worked on, so the folder is refreshed
> deliberately rather than on every change:
>
> ```sh
> wmake -f Makefile.dos && tools/make_release_folder.sh
> ```
>
> Check what you have with `git log -1 -- RELEASE/CASTALIA/BIN/CASTALIA.EXE`
> against `git log -1 -- src/`.

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
