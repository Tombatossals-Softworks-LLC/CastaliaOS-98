CastaliaOS 98 PE  -  Powerful Edition
=====================================

This is the installed layout of CastaliaOS 98 PE, an original, legally clean,
Win9x-inspired retro desktop environment. It is NOT Microsoft Windows and
contains no Microsoft code, files, artwork, or branding. See LEGAL.md in the
source tree.

Directory layout (C:\CASTALIA)
------------------------------
  BIN\      CASTALIA.EXE   graphical desktop
            CBOOT.EXE      safe launcher (called from AUTOEXEC.BAT)
            INSTALL.EXE    on-target installer (boot-file backup + boot block)
            DOS4GW.EXE     32-bit DOS extender (bundled in the RELEASE tree and
                           release ZIP under license; see LEGAL.md. A raw
                           source build supplies its own -- docs/BUILDING.md)
  SYS\      CASTALIA.INI   main configuration (plain text, editable)
            crash.log      crash records, if any
            bootdirty.flg  present only while a session is running
  THEMES\   CLASSIC\THEME.INI   the default theme
  APPS\     first-party application data (grows in later phases)
  FONTS\    original bitmap fonts (built into the shell for v1)
  ICONS\    original icon packs (procedural in v1)
  HELP\     help content
  LOGS\     rotated session logs (castalia.log)
  TEMP\     scratch space
  TRASH\    safe-delete holding area

Starting the desktop
--------------------
AUTOEXEC.BAT runs C:\CASTALIA\BIN\CBOOT.EXE, which starts the desktop. To
start it by hand:  C:\CASTALIA\BIN\CBOOT.EXE  (add /safe for Safe Mode).

Recovery
--------
If the desktop will not start, boot to DOS and edit SYS\CASTALIA.INI (set
[Boot] SafeMode=1), or run CBOOT /safe. Full instructions are in
docs/RECOVERY.md in the source distribution.
