# Testing CastaliaOS 98 PE

This document describes how CastaliaOS 98 PE is tested, layer by layer, and —
just as important — states honestly what is **proven now** versus what still
**awaits emulator and real-hardware validation**.

See also: [BUILDING.md](BUILDING.md) for how to produce the artifacts under
test, [HARDWARE_TARGETS.md](HARDWARE_TARGETS.md) for the machine profile the
emulators stand in for, and [RECOVERY.md](RECOVERY.md) for failure handling.

---

## Test philosophy

This project prizes honesty over impressive claims. A screenshot is not a
proof of correctness, and "it should work" is not "it works." Testing is
therefore **layered**, from fast deterministic unit tests up to real hardware,
and each layer states what it actually demonstrates.

**What is proven today:**

- **Layer 1 — host unit tests pass.** `make test` reports `7609 checks,
  0 failures` / `ALL PASS`.
- **Memory safety, on the paths most likely to lose it.** `make memcheck` runs
  the unit tests, the file-format scenes (.CAR, .CZ, BMP, WAV), the tree walk,
  the network stack and four deliberately malformed worlds under valgrind, and
  requires zero invalid accesses and zero uninitialised reads. This is the one
  class every other gate here is blind to: a program can write one byte past a
  buffer and still produce exactly the right answer, so the unit tests, the
  demos and the abuse worlds all pass. `tools/check_memcheck_control.sh`
  plants a one-byte overrun and requires the memcheck run to catch it, because
  "no errors found" from a check that cannot fail is not a result.

  Read that report for exactly what it says: **invalid access and
  uninitialised reads**. These runs use `--leak-check=no` and have never
  checked leaks at all.
- **The same tests again under ASan, UBSan and LeakSanitizer.**
  `make sanitize` rebuilds the unit tests with `-fsanitize=undefined,address`
  into `build-san/`. This is not a second helping of the above — valgrind puts
  **no redzones around globals**, so a read one byte past a string literal is
  invisible to all thirteen of its runs. `calc_press` had precisely that,
  found the first time this ran; UBSan adds signed overflow, bad shifts and
  misaligned access, which valgrind is not looking for at all; and
  LeakSanitizer covers the leaks memcheck does not. It then runs **every demo
  scene** under the same instrumentation — the window manager, shell, renderer
  and apps, which the unit tests do not touch — reading the scene list out of
  `run_demos.sh` so a new scene is covered without anyone remembering to. It
  carries its own control — a deliberate out-of-bounds read it must report —
  so a clean result means the checker was awake.
- **Layer 2 — the host desktop renders.** `make run` composites a real desktop
  (wallpaper, taskbar, launcher menu, System Information window) into a
  back buffer and writes a 24-bit BMP.
- **The DOS target builds under real Open Watcom.** `wmake -f Makefile.dos`
  compiles the whole DOS backend (VESA, INT 33h, INT 16h, DPMI, child launch)
  and links `CASTALIA.EXE` + `CBOOT.EXE` (DOS/4GW). This caught a real bug the
  host build could not (Open Watcom's `union REGS` has no `flags` member).
- **The DOS/VESA path boots end-to-end in QEMU.** A bootable FreeDOS floppy
  (built by `tools/make_dos_floppy.sh`) boots to the CastaliaOS desktop at
  **800x600x16** via VBE: DOS/4GW → DPMI real-mode INT 10h → VESA banked mode
  set (with the mode-list-cache fix) → 32bpp→RGB565 present → the full
  compositor, icons, taskbar, and a live **INT 21h wall clock**. Pressing **Esc**
  (injected over QMP; served by **INT 16h**, no driver) opens the launcher menu,
  proving the keyboard path.
- **INT 33h mouse works with a DOS mouse driver.** With CuteMouse
  (`CTMOUSE.EXE /P`) loaded, moving the pointer and clicking the "This Machine"
  icon (driven over QMP `mouse_move` / `mouse_button`) opens the System
  Information window — proving mouse move + click → desktop hit-test → window
  creation. The window self-reports `Platform: CastaliaOS DOS backend (Open
  Watcom / VESA / INT33 / FreeDOS)` and `Video: 800x600, 16 bpp (vesa-banked)`.
  See `docs/media/qemu-freedos-*.png` and the repro steps in "Layer 3" below.
- **A scene that asserts nothing fails the build.** `tools/run_demos.sh`
  used to accept a scene that ran, drew and logged no `(OK)`/`(MISMATCH)` at
  all, reporting it as "drawn only" -- and ten of them had drifted into that
  state, each driving a real gesture and reporting success whatever happened.
  One of them, the wallpaper scene, had never once loaded a wallpaper: it
  wrote its test bitmap to a path that does not exist in the throwaway home
  every scene runs from. Exactly one scene is exempt now, named in the script
  with its reason (it deals a FreeCell hand for the README screenshot; what
  the game DOES is checked elsewhere).
- **Every window agrees with a full repaint after being poked.**
  `--stale-demo` opens every window in the Start menu, hovers across it,
  turns the wheel over it, presses the arrow keys and clicks it, then forces
  a full repaint and compares. Zero differing pixels is the only pass. It is the
  counterweight to the narrowing work: a repaint region that missed part of
  what changed leaves last second's image on screen, no test of the drawing
  code sees it, and it looks like a rendering glitch rather than a bug in the
  invalidate. Windows that animate on their own are skipped and COUNTED
  (asked, by watching four frames of no input -- not looked up in a list that
  would go stale), and so are windows whose click opened a dialog or closed
  them. Only the Shut Down dialog is excluded from the click by name, because
  its OK ends the session. It found the cursor's shadow darkening itself on
  every repaint, in the shell, affecting every window at once.
- **Every window with a taskbar button can be minimized, and a lint keeps it
  that way.** `tools/check_minimize.sh` fails the build for a source that
  creates a titled, non-popup, non-modal window without `WM_STYLE_MINIMIZE`.
  The taskbar's rule for what gets a button is one line -- not a popup and not
  modal -- and nothing tied it to the flag, so six windows had drifted apart
  from it: Calculator, Mines, Reversi, Properties, the Welcome tour, and the
  generic text window behind System Information and the Log Viewer. Each had a
  button and no way to answer it, which reads as a DEAD CLICK rather than a
  missing feature: clicking the button of the window you are already in has put
  that window away since the taskbar was invented, and without the flag the
  click falls through to "focus and raise", which it already is. It also hid an
  animation -- minimize and restore fly the window down to its button and back,
  and both are started only for a window that can be minimized at all.
- **Every window that scrolls answers the wheel, and a lint keeps it that
  way.** `tools/check_wheel.sh` fails the build for a source that creates a
  window, keeps a scroll offset, and never handles `WM_MSG_MOUSEWHEEL` --
  because a wheel that works in every list except one reads as a broken
  window rather than a missing feature, and the exception is the one nobody
  reports. Its exemptions are listed by name in the script with the reason
  for each; deleting one window's handler fails it.
- **The mouse WHEEL is the one input path with no hardware evidence.** INT 33h
  has no wheel in its original service set; the extension every DOS wheel
  driver of the period implements is a handshake (`AX=0011h` answers `574Dh`,
  "WM") after which function 3 puts a signed detent count in `BH`. That
  handshake and that read are implemented in `src/platform/dos/mouse.c` and are
  **argued for, not observed** — QEMU's emulated mouse has no wheel and CI's is
  synthetic. Everything above the seam *is* observed: `--wheel-demo` drives
  `PLAT_EV_MOUSE_WHEEL` through the real manager and the real windows. If the
  handshake is wrong the failure is benign in one direction (`g_wheel` stays
  false and the wheel does nothing, which is exactly what happens today with a
  driver that has no wheel support) and visible in the other (`BH` misread as
  button bits would make ordinary clicks scroll) — which is why the handshake
  is checked before `BH` is read at all.
- **The built-in apps run on real DOS — keyboard-only.** The launcher is fully
  keyboard-navigable (Esc/Ctrl+Esc opens it, arrows move the highlight, Enter
  launches), so each app opens with **INT 16h keys alone**, no mouse. On FreeDOS
  in QEMU: the **File Manager** enumerates the real `A:\CASTALIA` floppy tree
  over INT 21h (`qemu-freedos-filemanager.png`); the **Notepad** accepts typed
  text and Enter across two lines (`qemu-freedos-notepad.png`); the
  **Calculator** computes `12 + 34 = 46` from the number row
  (`qemu-freedos-calculator.png`); the **Log Viewer** opens on the DOS log path
  (`qemu-freedos-logviewer.png`); and the **Control Center** renders its category
  listbox and theme radio buttons (`qemu-freedos-controlcenter.png`), exercising
  the new listbox/radio controls on real hardware-class VESA. This exercised —
  and fixed — a DOS-only keyboard bug: the enhanced BIOS call (INT 16h AH=10h)
  returns the grey cursor keys with `AL=0xE0`, not `AL=0`, so the arrows were
  being dropped until the poll handled both markers.

**What is NOT yet proven (and must not be claimed as working):**

- **DOS child-process launch, power-off/reboot, exit-to-DOS restore** — not yet
  driven in the emulator.
- **Alt+F4 window close on DOS** — the shortcut is wired (INT 16h scan `0x6B`
  → `PLAT_KEY_CLOSE` → close the focused window) and verified on the host
  backend, but QEMU's SeaBIOS does not surface Alt+F4 through the INT 16h buffer,
  so it cannot be exercised in the emulator. Real AT/PS-2 BIOSes do deliver scan
  `0x6B`; treat DOS Alt+F4 as spec-correct but hardware-unverified.
- **Real S3 Trio3D-class hardware** — QEMU's `-vga std` VBE BIOS is a stand-in.
  Banked window behavior across bank boundaries at 16 bpp and banked-present
  performance still need validation on the actual card (see HARDWARE_TARGETS.md).

---

## Layer 1 — Host unit tests

Pure-logic modules are tested with a tiny hermetic harness (`tests/ctest.h`,
`tests/test_main.c`) that links against only the logic modules — no platform
backend, no shell — so the tests are deterministic and portable.

```sh
make test        # builds build/run_tests and runs it
```

A healthy run ends with:

```
7609 checks, 0 failures
ALL PASS
```

The four suites:

| Suite | File | Covers |
|-------|------|--------|
| `test_str` | `tests/test_str.c` | Safe strings: `sys_strlcpy`/`sys_strlcat` truncation and NUL-termination, `sys_snprintf` clamping, `sys_strnlen` bounds, case-insensitive compare, `sys_strtrim`. |
| `test_rect` | `tests/test_rect.c` | Rectangle math: width/height, `contains` (exclusive edge), `intersect`, `overlaps`, `union`, `inset` (clamps to empty, never negative), `offset`. |
| `test_region` | `tests/test_region.c` | Dirty-region accumulator: merge, absorbing a fully-contained rect, separate distant rects, ignoring empty rects, `bounds`, and **overflow coalescing** — adding more than `CRGN_MAX` rects must coalesce to a bounding box and never drop a rectangle. |
| `test_ini` | `tests/test_ini.c` | INI config: in-memory set/get, typed getters (int/bool/color) with defaults, `#RRGGBB` color parse, save-then-reload round-trip, and a missing file loading as empty (not NULL) so defaults can proceed. |

These correspond to the Bible's Layer 1 list (INI parser, rectangle clipping,
dirty-region merging, string functions). Other Layer 1 items named in the
Bible — path normalization, manifest/resource-pack parsing, menu layout,
window hit-testing — are **not yet covered by automated tests**; add suites as
those modules stabilize.

---

## Layer 2 — Host visual smoke

The host backend is headless: it renders into memory and writes a screenshot
instead of opening a window. This makes a full desktop composite reproducible
on any machine and in CI.

```sh
make run                     # -> build/castalia.bmp (800x600, 24-bit)
tools/run_host.sh 640 480    # -> build/castalia_640x480.bmp (fallback mode)
```

Open the BMP in any viewer. What to look for:

- The stone-gradient wallpaper fills the desktop.
- The **taskbar** sits at the bottom with a launcher button, task area, and clock.
- The **launcher menu** is open (from `--open-launcher`) with a highlighted item.
- A **System Information** window is open (from `--open-sysinfo`) with a title
  bar, caption buttons, and content.
- Window bevels, borders, and text render crisply with no obvious clipping or
  garbage.

This is a **smoke test**, not a golden-image comparison. The Bible's Layer 3
"visual golden tests" (pixel comparison against reference images with
tolerance, across 16-color / 256-color / 16-bit exports) are a **roadmap item**;
today the BMP is inspected by eye.

---

## Layer 3 — Emulator integration (DOS backend)

> **VERIFIED in QEMU.** The rendering + keyboard path has been booted end-to-end
> on real FreeDOS; the exact, reproducible recipe is below. Mouse (INT 33h),
> child-process launch, and real S3 hardware remain to be exercised.

### Verified end-to-end (reproducible)

The turnkey path (used to produce `docs/media/qemu-freedos-desktop.png` and
`...-launcher.png`):

```sh
# 1. Build the DOS product with Open Watcom (see BUILDING.md for owsetenv).
wmake -f Makefile.dos                 # -> dist/cdroot/CASTALIA/BIN/*.exe

# 2. Assemble a bootable FreeDOS floppy (needs a FreeDOS 1.3 boot floppy such
#    as x86BOOT.img, your $WATCOM/binw/dos4gw.exe, and mtools).
WATCOM=/opt/watcom tools/make_dos_floppy.sh x86BOOT.img build/castalia_floppy.img

# 3a. Boot interactively:
emulators/qemu_floppy.sh build/castalia_floppy.img
# 3b. Or headless: capture the desktop, and the launcher after pressing Esc:
emulators/qemu_floppy.sh build/castalia_floppy.img --shot desktop.png
emulators/qemu_floppy.sh build/castalia_floppy.img --shot launcher.png --key esc

# For the mouse: bundle a DOS mouse driver (CuteMouse) and it loads on boot:
CTMOUSE=/path/to/CTMOUSE.EXE WATCOM=/opt/watcom \
    tools/make_dos_floppy.sh x86BOOT.img build/castalia_floppy.img
# Then drive the pointer over QMP with `mouse_move`/`mouse_button`
# (human-monitor-command); a click on a desktop icon opens its window.

# 4. Open an app with the keyboard only (no mouse): press Esc to raise the
#    launcher, then Down to walk to the entry and Enter to launch it -- over QMP:
#      send-key esc ; send-key down (xN) ; send-key ret
#    File Manager = 3 Downs, Notepad = 5, Calculator = 6. This produced
#    qemu-freedos-{filemanager,notepad,calculator}.png; the digits/letters that
#    populate Notepad and Calculator are ordinary INT 16h keystrokes.
```

Observed result: FreeDOS boots, `FDAUTO.BAT` launches `CASTALIA.EXE`, and the
desktop appears at **800x600x16** with the wallpaper, castle motif, all desktop
icons, the taskbar, and a **ticking clock** (the wall clock advanced across
runs). Pressing **Esc** opens the launcher menu, confirming INT 16h keyboard
input works with no driver. This exercises DOS/4GW, the DPMI transfer-buffer +
real-mode-interrupt code (`dos_dpmi.c`), VESA mode selection and the banked
present, and the whole upper stack under the real toolchain.

### Building a persistent HDD install instead of a floppy

For a full `C:\CASTALIA` install (rather than the single demo floppy): install
FreeDOS onto a FAT16/FAT32 hard-disk image, copy `dist/cdroot/CASTALIA` to
`C:\CASTALIA`, add your `DOS4GW.EXE` to `C:\CASTALIA\BIN`, and append the
`dist/AUTOEXEC.BAT` block (which calls `CBOOT.EXE`) plus the `dist/CONFIG.SYS`
lines. Boot with `emulators/qemu_run.sh castalia.img`.

### Boot in QEMU

The `emulators/qemu_run.sh` (Linux/macOS) and `emulators/qemu_run.bat`
(Windows) scripts model the IBM Pentium II / 440BX target and boot the image:

```sh
./emulators/qemu_run.sh castalia.img
```

which runs, in effect:

```sh
qemu-system-i386 -M pc -cpu pentium2 -m 128 -hda castalia.img \
    -boot c -vga std -display gtk -serial stdio
```

Notes baked into the script: `-vga std` provides a standard VBE BIOS; try
`-vga cirrus` as a second data point closer to period S3-class behavior. Sound
is optional and off by default; add `-device sb16` to exercise it. The desktop
must never require it.

### Boot in Bochs

`emulators/bochs_castalia.bxrc` is a hardware-debug profile (slower than QEMU,
but with detailed device logging useful for chasing VESA/VBE and PS/2 issues):

```sh
bochs -f emulators/bochs_castalia.bxrc
```

Its profile: `cpu: model=pentium_mmx`, `megs: 128`, `vga: extension=vbe`,
PS/2 keyboard + mouse, an IDE hard disk (`ata0-master ... path="castalia.img"`),
and verbose logging to `bochsout.txt`. Optional SB16 and NE2000 lines are
present but commented out. It needs a VGABIOS with VBE support (e.g. the
LGPL `vgabios`/Bochs VBE BIOS) — adjust the ROM paths to your install.

### Manual integration checklist

Walk this by hand on each emulator (mirrors Bible Layer 2). None of these are
verified yet:

- [ ] Machine boots FreeDOS and `AUTOEXEC.BAT` runs `CBOOT.EXE`.
- [ ] `CBOOT` launches `CASTALIA.EXE`; a graphics mode is set (watch the serial
      log for the `VESA: mode ... set` line and the chosen `WxHxD`).
- [ ] The **desktop appears** (wallpaper, taskbar, clock).
- [ ] The **launcher opens** from the taskbar button.
- [ ] The **System Info window** opens and shows plausible values.
- [ ] A window can be **dragged** and refocused; z-order and caption buttons work.
- [ ] Launching a DOS command suspends the desktop, runs, and returns to the shell.
- [ ] **Exit to DOS** cleanly restores text mode and returns control to
      `AUTOEXEC.BAT`.
- [ ] Fallback ladder: force a mode failure and confirm
      `800x600x16 -> 640x480x16 -> 640x480x8`, and that `CBOOT /safe` forces
      640x480x8.
- [ ] **Mode selection** (changed since the last FreeDOS run — see below). On a
      card that offers the requested size but not the requested depth, the log
      should say `VESA: no WxHxD; using N bpp instead` and the desktop should
      come up at the requested SIZE rather than dropping down the ladder. On a
      card offering the same mode both banked and linear, the driver name in
      System Information should read `vesa-linear`.

### Changed since the last verified FreeDOS run

The end-to-end run recorded above exercised the DOS backend as it stood then.
These parts have changed since and are **not** re-verified on DOS; they are the
first things to re-check on the target:

| Change | What to watch |
| --- | --- |
| `find_mode` now defers to `src/gfx/vbe_pick.c` instead of matching width, height and depth exactly | The mode actually set, and the two checklist items above. The policy is unit-tested (`tests/test_vbe.c`); the BIOS walk that feeds it is not runnable off-target. |
| `plat_screenshot` implemented on DOS | That it writes a readable BMP. This is the one change on this list that makes the others easier to check, so try it first. |
| Alt+Tab walks most-recently-used order and draws a panel | That Alt+Tab still switches at all (it depends on the BIOS reporting scan `0xA5`), and whether Shift+Alt+Tab does anything — see the note in `src/platform/dos/keyboard.c`. |
| Last-known-good config restore (`src/cfg/lastgood.c`) | That `SYS\CASTALIA.BAK` appears after a clean exit, and that deleting or corrupting `CASTALIA.INI` restores it on the next boot. |
| `plat_ticks_ms` on the HOST moved from CPU time to wall time | Nothing on DOS: that backend already returned the BIOS tick. Listed so the difference is not mistaken for a regression. |

`plat_screenshot()` now works on the DOS backend too — it writes a 24-bit BMP
of the back buffer through the same portable writer the shell's Capture Screen
command uses. Layer-2-style automated screenshots are therefore available under
DOS, which is the cheapest way to check the items in the table above without
watching a screen. (It has not itself been run on DOS from this environment;
the function is four lines over a writer the DOS product already links.)

---

## Layer 4 — Real-hardware QA matrix

Run on the actual IBM Pentium II / 440BX target (see
[HARDWARE_TARGETS.md](HARDWARE_TARGETS.md)). Record the machine's BIOS version,
disk model, video BIOS/VESA modes, and audio chip ID as you go (Bible §22 real
hardware profile). Keep a known-good DOS boot floppy/CD nearby.

| Area | Check | Status |
|------|-------|--------|
| PS/2 keyboard | Typing, arrows, function keys, Delete reach the shell | Unverified |
| PS/2 mouse | Motion tracks 1:1, left/right/middle buttons register | Unverified |
| VESA modes | 800x600x16 sets; fallbacks to 640x480x16 and 640x480x8 work | Unverified |
| IDE file ops | Read/write/rename/delete under `C:\CASTALIA` | Unverified |
| CD read | Read files from the CD-ROM drive | Unverified |
| Audio (optional) | If a driver exists, sound plays; **absence never blocks boot** | Not implemented |
| Expansion | PCI/ISA cards where available do not destabilize boot | Unverified |
| Exit/reboot/power | Exit to DOS, reboot (KBC pulse), power-off (APM) behave | Unverified |

Everything in this matrix is currently **unverified**; it is the QA plan, not a
results table.

---

## Layer 5 — Abuse tests

Deliberately break things and confirm the shell degrades gracefully rather than
crashing or corrupting state (Bible §21 Layer 5). Recoverability is a product
goal — see [RECOVERY.md](RECOVERY.md).

| Abuse | Expected behavior | Status |
|-------|-------------------|--------|
| Corrupt / malformed `CASTALIA.INI` | Forgiving parse; bad keys fall back to defaults | Partially covered by `test_ini` (parse tolerance); end-to-end unverified |
| Missing theme file | Fall back to built-in default colors/metrics | Unverified |
| Failed video mode | Walk the fallback ladder; if all fail, log and return to DOS with a message | Code path exists in `plat_dos.c`; unverified on hardware |
| Deleted `CASTALIA.INI` | Recreate missing keys / restore defaults | Unverified |
| Full disk / interrupted copy | Report the error; do not corrupt existing files | Unverified |
| Read-only files | Handle gracefully | Unverified |
| Directory with ~1,000 files | Enumerate without excessive delay (`_dos_findfirst`/`findnext`) | Unverified |
| DOS program crash under launcher | Recover video and return to the shell | Unverified |
| Repeated unclean boots | After two, `CBOOT` forces Safe Mode | Logic present in `cboot.c`; end-to-end unverified |
| Repeated shell restart | `main.c` restart loop handles `SH_EXIT_RESTART_SHELL` | Unverified |

---

## Summary: proven vs. pending

| Layer | Mechanism | State |
|-------|-----------|-------|
| 0 DOS build | `wmake -f Makefile.dos` (Open Watcom) | **Proven in CI on every push** — compiles + links `CASTALIA.EXE` / `CBOOT.EXE` / `INSTALL.EXE`, uploaded as the `castalia-dos-exe` artifact |
| 0b C89 portability | `make lint` (`tools/c89_lint.sh`) | **Proven in CI** — every portable source is strict-C89 clean, so a Watcom-incompatible change fails on host runners first |
| 0c Ctrl-key encodings | `make lint` (`tools/check_ctrl_keys.sh`) | **Proven** — no shortcut may test the bare ASCII control code, which works on DOS and silently does nothing on the host backend; carries a control line |
| 1 Host unit tests | `make test` | **Proven in CI (gcc + clang)** — 7609 checks pass |
| 1b End-to-end scenes | `make demos` | 74 scripted scenes drive real windows and check what they changed |
| 1b kernel-lab boot | `kernel-lab/smoke_test.sh` (NASM + QEMU) | **Proven in CI** — image boots, PIT IRQ0 tick advances, PS/2 IRQ1 scancode read |
| 2 Host visual smoke | `make run` / `tools/run_host.sh` | **Proven** — desktop renders to BMP |
| 3 Emulator integration | `tools/make_dos_floppy.sh` + `emulators/qemu_floppy.sh` | **Proven (rendering + keyboard + mouse + apps)** — boots to the VESA desktop; keyboard opens the launcher and runs File Manager / Notepad / Calculator; mouse click opens a window; child-launch pending |
| 4 Real-hardware QA | IBM 440BX target matrix | **Pending** |
| 5 Abuse tests | fault injection | **Pending** (INI parse tolerance partially covered) |

The honest one-line status: the host stack is unit-tested and renders; the DOS
product **builds under Open Watcom and boots end-to-end in QEMU to a live VESA
desktop with working keyboard (INT 16h) and mouse (INT 33h + a DOS mouse
driver) input, and runs the File Manager, Notepad, and Calculator from a
keyboard-navigable launcher**; DOS child-launch and real S3 hardware remain to
be exercised.
