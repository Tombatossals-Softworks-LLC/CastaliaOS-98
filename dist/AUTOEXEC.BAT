@ECHO OFF
REM ====================================================================
REM  AUTOEXEC.BAT (template) - starts CastaliaOS 98 PE automatically.
REM
REM  The installer appends the Castalia block to your existing
REM  AUTOEXEC.BAT after backing it up (AUTOEXEC.CB_) -- it never discards
REM  your content. To disable auto-start, remark the CBOOT line.
REM
REM  Recovery: boot with a DOS floppy/CD, or press F8 at boot and skip
REM  this file, then edit C:\CASTALIA\SYS\CASTALIA.INI. See docs/RECOVERY.md.
REM ====================================================================

REM --- (your existing AUTOEXEC content stays above this block) ---

SET CASTALIA_HOME=C:\CASTALIA
SET PATH=C:\CASTALIA\BIN;%PATH%
SET DOS4GVM=1

CD \CASTALIA\BIN

REM Load a DOS mouse driver for INT 33h mouse support (verified with CuteMouse).
REM Remove or edit this line if your driver differs; the desktop works without
REM a mouse (keyboard-only) but the pointer will not move without it.
REM CTMOUSE.EXE /P

REM CBOOT checks Safe Mode / crash flags and then launches the desktop.
CBOOT.EXE

REM On exit (Exit to DOS / Shutdown), control returns here.
CD \
