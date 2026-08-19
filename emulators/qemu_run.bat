@echo off
REM ====================================================================
REM  qemu_run.bat - Boot a FreeDOS + CastaliaOS disk image in QEMU on
REM                 Windows for a quick smoke test.
REM
REM  See docs/TESTING.md for building castalia.img (FreeDOS + C:\CASTALIA
REM  + AUTOEXEC.BAT calling CBOOT). Models the IBM Pentium II / 440BX
REM  target: i440FX PC, 128 MB, PS/2, IDE, VBE VGA. Sound is optional.
REM ====================================================================
setlocal
set IMG=%1
if "%IMG%"=="" set IMG=castalia.img

if not exist "%IMG%" (
  echo Disk image "%IMG%" not found.
  echo Build one per docs\TESTING.md.
  exit /b 1
)

qemu-system-i386 ^
  -M pc ^
  -cpu pentium2 ^
  -m 128 ^
  -hda "%IMG%" ^
  -boot c ^
  -vga std ^
  -serial stdio
REM Add '-device sb16' for optional Sound Blaster testing.
endlocal
