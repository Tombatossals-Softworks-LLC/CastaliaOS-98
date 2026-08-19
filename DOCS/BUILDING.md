# Building CastaliaOS 98 PE

This document describes how to build CastaliaOS 98 PE from source. There are
two independent build tracks, and understanding why they are separate is the
key to building the project correctly.

See also: [ARCHITECTURE.md](ARCHITECTURE.md) for the layer design,
[TESTING.md](TESTING.md) for how to validate a build, and
[../LEGAL.md](../LEGAL.md) before redistributing any binaries.

---

## 1. Overview: two build tracks

Everything above the platform layer (`include/castalia/*.h` and most of
`src/`) is portable C89. That upper stack talks to the machine only through
one Castalia-owned interface, `include/castalia/plat.h`. Two backends
implement that interface, and each has its own build:

| Track | Makefile | Compiler | Backend | Output | Purpose |
|-------|----------|----------|---------|--------|---------|
| **Host** (portable) | `Makefile` / `build.sh` | any C89 compiler (`cc`/`gcc`/`clang`) | `src/platform/host` | `build/castalia`, `build/run_tests`, `build/castalia.bmp` | Development, unit tests, CI, golden screenshot |
| **DOS** (the product) | `Makefile.dos` | Open Watcom `wcc386` + `wlink` | `src/platform/dos` | `CASTALIA.EXE`, `CBOOT.EXE` | The real desktop that boots on FreeDOS |

**Why two tracks.** The DOS backend under `src/platform/dos` is compiled *only*
by `Makefile.dos` (guarded by `#ifdef CASTALIA_DOS`), and the host backend
under `src/platform/host` is compiled *only* by the host `Makefile`. Because
the two never compile together, DOS-specific code (VESA, INT 33h, DPMI) can
never break host portability, and the entire shell stays buildable, runnable,
and testable on an ordinary workstation. The host build needs **no** SDL,
Allegro, QEMU, or DOS toolchain.

The `CASTALIA_HOST` / `CASTALIA_DOS` preprocessor defines are what select the
backend; `src/main.c` is the one file that legitimately differs per platform,
because it is the integration seam (headless screenshot driver on host,
interactive session loop on DOS).

---

## 2. Host build (portable)

### Prerequisites

- A C89-capable C compiler. The `Makefile` defaults to `cc`; `gcc` or `clang`
  work. The build uses `-std=gnu89` (strict C89 *language*, Watcom-compatible,
  but with the standard-library declarations the platform layer needs).
- `make`.

No graphics library, emulator, or DOS toolchain is required. The host backend
is **headless**: it renders into a memory framebuffer and writes a screenshot
rather than opening a window.

### One-command build

```sh
./build.sh          # build host shell + run unit tests + render demo BMP
```

`build.sh` runs `make all`, `make test`, and `make run` in sequence and prints
where the outputs landed. Use `./build.sh clean` to remove `build/`.

### Step by step

```sh
make            # build the host shell -> build/castalia
make test       # build + run the unit tests -> build/run_tests
make demos      # run every end-to-end scene and fail on any MISMATCH
make lint       # strict-C89 guard + the DOS-build registration guard
make run        # render a demo desktop  -> build/castalia.bmp
make clean      # remove build/
```

`make test` proves the pure logic; `make demos` proves the WIRING -- each scene
drives a real window through synthetic input and then checks what it can
actually observe (the pixels in a saved bitmap, the bytes in a saved file, the
frames that came back off the simulated wire). A scene that produces no output
at all is reported as a failure, not a pass, because that is what a broken
harness looks like from the outside.

`make run` invokes the shell headless with a representative scene:

```sh
CASTALIA_HOME=build build/castalia --headless --open-launcher \
    --open-sysinfo --frames 10 --shot build/castalia.bmp
```

### Command-line flags of `build/castalia`

These are parsed in `src/main.c`. Defaults: 800x600, 16 bpp requested,
interactive (non-headless), 3 frames.

| Flag | Effect |
|------|--------|
| `--safe` | Force the Safe Mode profile (the 640x480x8 fallback path). |
| `--headless` | Render frames and (optionally) write a screenshot, then exit. **Host only** — the headless driver is compiled under `CASTALIA_HOST`; on the DOS build these flags are parsed but the interactive session loop runs instead. |
| `--open-launcher` | Open the launcher menu in the rendered scene. |
| `--open-sysinfo` | Open the System Information window in the rendered scene. |
| `--open-fileman` | Open the File Manager window in the rendered scene. |
| `--open-about` | Open the About CastaliaOS window (identity / hardware / statistics / credits). |
| `--open-bench` | Open the Benchmark Suite and run it (Enter triggers Run All). |
| `--open-media` | Open the Media Player; it scans `CASTALIA_HOME/MEDIA` for WAVs and starts playback. |
| `--open-clock` | Open the Clock, Calendar & Agenda window. |
| `--open-charmap` | Open the Character Map window. |
| `--open-solitaire` | Open the Solitaire (Klondike) game. |
| `--open-sheet` | Open CastaliaSheet (the spreadsheet) on its sample budget. |
| `--open-write` | Open CastaliaWrite (the word processor) on its sample document. |
| `--open-logview` | Open the Log Viewer on `$CASTALIA_HOME/LOGS/castalia.log`. |
| `--open-notepad` | Open Notepad on `docs/BUILDING.md` (a long file, for scroll checks). |
| `--open-console` | Open the Console and run a few sample commands. |
| `--open-welcome` | Open the Welcome first-run tour. |
| `--open-net` | Open the Network window (adapter, TCP/IP, ARP table, ping). |
| `--open-theme` | Open the Theme Editor. |
| `--open-help` | Open the Help browser. |
| `--open-diskuse` | Open Disk Usage on `CASTALIA_HOME`. |
| `--clock-tick-demo` | Let real seconds pass with the Clock's partial repaint doing the work, then force a full repaint and diff the window pixel by pixel. Zero difference means the partial path is not losing an update. (Verified sensitive: shrinking the invalidated face rect makes it report ~100 differing pixels.) |
| `--screen-check` | Assert the shell reached a real desktop: sample the frame and require genuine colour variety plus a video mode inside the supported range. Used by `make abuse`. |
| `--crash-demo` | Draw the crash screen (without crashing — `sys_fatal` exits, so painting is a separate function) and check it covers the screen and carries a screenful of advice, in both the with- and without-known-good-config branches. |
| `--car-demo` | Pack a folder into a `.CAR`, delete the originals, restore them and compare bytes; then feed the extractor a member named `../ESCAPE.TXT` and require it to refuse and to leave nothing outside the folder. |
| `--mem-check` | Report the idle desktop's live and peak bytes and fail if idle passes the 4 MB ceiling or peak passes the 8 MB budget (`castalia.h`). |
| `--switch-demo` | Open three windows and Alt+Tab twice: the first press must leave the window it started on and the second must return to it (z-order cycling fails this), and the switcher panel's selected row must be drawn. |
| `--sysmenu-demo` | Drive the window menu with no mouse at all: Alt+Space, arrows, Enter. Move the window with the arrow keys and cancel with Esc (it must land back on the exact rectangle it started from) then repeat and keep it with Enter; resize it, checking the far edges move and the top-left corner does not, and that holding Left stops at the minimum size rather than walking down to nothing; maximize, then Restore back to the exact same rectangle; send the window to another virtual desktop and follow it there; open the same menu from the title bar and from the taskbar button; bring a minimized window back straight to full screen (which a plain click on its button would not do); Close. The number of Down presses in each pick is itself an assertion about which items are disabled — with Restore wrongly live on a normal window, four Downs stop on Minimize and the maximize check fails. Finally, a window is closed out from under an open menu: the menu must go with it, or the slot it named would later belong to somebody else. |
| `--capture-demo` | Capture the screen through the shell command the menus use, load the file back, and check a second capture does not overwrite the first — then measure what saving *cost*: the accounted peak must grow by less than 64 KB, because `gfx_bmp_save` streams a row at a time. Restoring whole-image buffering grows it by 1,440,062 bytes and takes the peak to 5.3 MB, past what the target machine has. |
| `--deskkeys-demo` | Walk the desktop icons with the arrow keys and open one with Enter — they were reachable by mouse and in no other way. Includes the routing checks that stop this being a regression: with a window focused the arrows must belong to the WINDOW (or every list and text field in the system loses its arrow keys), and closing it must give them back. |
| `--archive-demo` | Pack forty known files into a .CAR, then open it through the File Manager the way a person would (End, Enter) and check what the Archive window managed to say having read only the front of the file: the right member count, the exact total original size, a stored size genuinely smaller, and that none of them exist beside the archive yet. Also that opening it wrote NOTHING — a .CAR used to unpack itself on a double-click, replacing whatever was already there. Then extraction and the question that comes with it: with nothing in the way it must extract without asking, and with files in the way it must stop and ask. A sentinel file of a different length is written over one member first, so answering can be told apart — "the file still exists" is true whether you cancelled or replaced it, and that is what the first version of this check tested. |
| `--deskcache-demo` | The desktop background is baked into an offscreen surface once and blitted from there; baking it costs ~25M instructions and was being paid again on every theme apply, including the ones that cannot change it (the factory landscape is drawn from hardcoded colours and depends only on the screen size). Checks both directions, because they pull against each other: an unchanged rebuild must do no work, a theme change that cannot alter the landscape must not rebake it, a change that *can* (the 16-colour target, which takes the shell off the glossy path) still must — and the landscape pixels must be identical across the skip while the icon-column pixels must NOT be, since the icons are drawn live from the theme palette and prove the theme actually applied. |
| `--menulook-demo` | Check the glossy menu treatment by looking at the pixels it makes: the selected row is exactly one item tall, the gutter is a visibly different colour from the body, and the selection is a gradient rather than a slab. Then redraw the same menu with the treatment switched off and require every one of those to invert — that flat path is what the 16-colour pipelines and safe mode get, and it is the half that would otherwise rot unnoticed. |
| `--cpu-demo` | Run the real CPUID read through the real decoder and check the result is fit to display. Cannot verify the read itself is correct — a misread vendor is indistinguishable from an unknown one. |
| `--cz-demo` | Compress a real file, delete the original, restore it and compare the bytes; refuse a deliberately damaged archive; then run the compression again through the File Manager's actual right-click menu item. |
| `--repaint-demo` | Play a track and take two *full* repaints one tick apart: every pixel that moved must lie inside the media deck's display strip. Both images come from full repaints, so the tick between them cannot make the comparison lie — the trap the obvious version of this test falls into. |
| `--help-demo` | ...and step through every topic, so one that overflows shows up here. |
| `--assoc-demo` | Open one file of each type from the File Manager and check the window that answered. |
| (see `make demos`) | `tools/run_demos.sh` runs every scene below against a throwaway `CASTALIA_HOME` and fails on any `MISMATCH`. |
| `--thumbs-demo` | Open the File Manager, switch it to the Icons view and let the thumbnail cache fill in. |
| `--filedlg-demo` | Open the Viewer, raise its Open dialog, browse the listing with the keyboard and load the picked file. |
| `--theme-demo` | Drive the Theme Editor: load a preset, repaint the accent, apply it live, save the theme and read the file back. |
| `--net-loopback` | **Host only.** Replace the null network backend with a *simulated* wire: two make-believe stations (10.0.2.2 and 10.0.2.3) that answer ARP, ICMP echo and a UDP echo port. It is reported as "Loopback (simulated)" everywhere, and it exists so the real protocol code can be exercised on a machine with no NIC. |
| `--net-demo` | Implies `--net-loopback`; runs ARP resolution, four pings, an unanswered address, and a UDP echo across that wire, logging each result. |
| `--net-ping-demo` | Implies `--net-loopback`; opens the Network window and drives a ping through its UI. |
| `--taskman-demo` | Open a few apps + the Task Manager, End Task the first one, then let real time pass so the Performance graphs collect real samples. |
| `--diskuse-demo` | Build a folder with a known 3:1 size ratio, open Disk Usage on it, and read the answer back off the screen: the treemap must cover its well with no background showing, the bitmap must really occupy a quarter of it, descending into a subfolder must repaint the map in that folder's contents, and Backspace at the root must be a no-op. |
| `--width N` | Requested framebuffer width (default 800). |
| `--height N` | Requested framebuffer height (default 600). |
| `--bpp N` | Requested hardware depth (default 16). The host backend always renders and presents in 32-bit XRGB regardless of this value; it matters on the DOS backend. |
| `--frames N` | Number of frames to run before the screenshot (default 3). |
| `--shot PATH` | Write a 24-bit BMP of the back buffer to `PATH`. Implemented by the host backend; the DOS backend returns `CE_UNSUPPORTED` for now. |
| `--record DIR` | Marketing recorder: drive a scripted scene (`--scene N`) and capture one BMP per shell frame to `DIR`. Host only. |
| `--scene N` | Which recorder scene to run (1 desktop tour + snap, 2 file drag‑and‑drop, 3 benchmark, 4 media player, 5 Solitaire win cascade, 6 desktop icons, 7 CastaliaSheet formula entry, 8 CastaliaWrite typing + formatting). Assembled into GIF + WebM by `tools/make_clips.sh`. |

`CASTALIA_HOME` is the install root; the shell writes the rotating log to
`$CASTALIA_HOME/LOGS/castalia.log` and the crash log + boot-dirty flag to
`$CASTALIA_HOME/SYS/`. The host targets set `CASTALIA_HOME=build` (so
`build/LOGS/` and `build/SYS/`), and `make run` / `tools/run_host.sh` create
those directories for you.

### Rendering other resolutions

`tools/run_host.sh` builds and renders at a chosen resolution:

```sh
tools/run_host.sh            # 800x600 (preferred mode)
tools/run_host.sh 640 480    # 640x480 fallback mode -> build/castalia_640x480.bmp
```

### Viewing the output

`build/castalia.bmp` (and the `build/castalia_WxH.bmp` variants) is a plain
24-bit Windows BMP. Open it in any image viewer. The demo scene shows the
desktop, taskbar, open launcher menu, and the System Information window. A
run also writes `build/LOGS/castalia.log`.

---

## 3. DOS build (the real product, Open Watcom)

The DOS product is built with Open Watcom via `Makefile.dos`. This produces
the 32-bit DOS/4GW executables that boot on FreeDOS.

> The DOS build is **compiled and linked by CI on every push** (the `dos-build`
> job installs Open Watcom 2.0 and runs `wmake -f Makefile.dos`, uploading
> `CASTALIA.EXE` / `CBOOT.EXE` / `INSTALL.EXE` as the `castalia-dos-exe`
> artifact), and has **booted end-to-end in QEMU** (see [TESTING.md](TESTING.md)
> for the reproducible recipe). A host-side `make lint` (`tools/c89_lint.sh`)
> guards the strict-C89 rules Open Watcom enforces, so a C99-ism that gcc's
> `-std=gnu89` would accept fails CI on the host runners before it reaches
> Watcom. The commands below are the actual working procedure. Real S3-class
> hardware is still pending.

### Prerequisites

- **Open Watcom C/C++** (the project's chosen period-appropriate toolchain).
  Install it, then activate its environment so `wcc386` and `wlink` are on the
  path. Open Watcom ships a setup script — for example `owsetenv.sh` on
  Unix-like hosts, or `owsetenv.bat` — that sets `WATCOM`, `PATH`, `INCLUDE`,
  and related variables. Run it in the shell you build from.
- `wmake` (Open Watcom's make, ships with the toolchain).

### Build

From the repository root, in an activated Open Watcom environment:

```sh
wmake -f Makefile.dos
```

Outputs are copied into `dist/cdroot/CASTALIA/BIN`:

- `CASTALIA.EXE` — the graphical desktop.
- `CBOOT.EXE` — the safe launcher that `AUTOEXEC.BAT` calls.

`wmake -f Makefile.dos clean` deletes the intermediate `*.obj` files and the
two executables.

### Compiler / target flags

From `Makefile.dos`:

```
CC     = wcc386
LD     = wlink
CFLAGS = -bt=dos -mf -5r -oaxt -w3 -dCASTALIA_DOS -Iinclude -Isrc/apps -Isrc/platform/dos
```

- `-bt=dos` — build target is DOS.
- `-mf` — **flat** memory model (32-bit protected mode).
- `-5r` — generate Pentium (586) instructions, register-based calling.
- `-oaxt` — speed optimizations.
- `-w3` — warning level 3.
- `-dCASTALIA_DOS` — selects the DOS backend and excludes the host backend.

Linking uses the DOS/4GW target:

```
wlink system dos4g name castalia.exe file { ... }
```

### DOS4GW.EXE requirement and licensing

`CASTALIA.EXE`, `CBOOT.EXE`, and `INSTALL.EXE` are DOS/4GW-extended executables.
They require the **DOS/4GW extender, `DOS4GW.EXE`**, to run. The `wmake` build
does **not** produce this file — but the repository now vendors it at
`include/vendor/DOS4GW.EXE`, and both the committed `RELEASE/` tree and the
release ZIP bundle it in `C:\CASTALIA\BIN`, so a shipped build runs as-is.

> **Licensing.** `DOS4GW.EXE` (DOS/4G © Rational Systems, Inc. / Tenberry
> Software) is a third-party binary. CastaliaOS bundles it under a DOS/4GW
> distribution license **held by Tombatossals Softworks LLC**; it is shipped
> unmodified and recorded in
> [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). If you redistribute a
> CastaliaOS build outside that license, supply your own `DOS4GW.EXE` under your
> own rights instead. `AUTOEXEC.BAT` sets `DOS4GVM=1` to enable the extender's
> virtual-memory mode.

---

## 4. Releases (downloadable builds)

### The committed `RELEASE/` folder

`RELEASE/` at the repo root is the **fully compiled, ready-to-use** product,
checked in so it ships with the source. It is the `C:\CASTALIA` tree with the
DOS binaries **and** the bundled `DOS4GW.EXE` — copy `RELEASE/CASTALIA` to a
DOS drive and run it, no toolchain required. Regenerate it after a DOS build:

```sh
wmake -f Makefile.dos            # build CASTALIA.EXE / CBOOT.EXE / INSTALL.EXE
tools/make_release_folder.sh     # assemble RELEASE/ (bundles DOS4GW.EXE)
```

### CI artifacts and tagged GitHub Releases

Every push builds the DOS product in CI and uploads the three `.exe` as the
`castalia-dos-exe` workflow artifact (grab it from the run's **Artifacts**).
For a stable, named download, push a version tag:

```sh
git tag v0.1.0-preview1
git push origin v0.1.0-preview1
```

The `Release` workflow (`.github/workflows/release.yml`) then rebuilds the DOS
product with Open Watcom, runs `tools/package_release.sh` to assemble the
ready-to-copy install tree, and publishes a **GitHub Release** with
`castalia-98-pe-<tag>.zip` attached. The ZIP contains:

```
castalia-98-pe-<tag>/
  CASTALIA/          the C:\CASTALIA tree: BIN\ (CASTALIA.EXE, CBOOT.EXE,
                     INSTALL.EXE, DOS4GW.EXE), APPS\, THEMES\, SYS\CASTALIA.INI, ...
  CONFIG.SYS  AUTOEXEC.BAT     boot templates
  README.md  LEGAL.md  RECOVERY.md  INSTALL.txt
```

`DOS4GW.EXE` **is** bundled (under the license held by Tombatossals Softworks
LLC — see the licensing note above and
[../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)). Tags below `v1.0.0`, or
with a hyphenated suffix (`-rc1`, `-preview`), are published as GitHub
**pre-releases**.

---

## 4. Assembling a bootable install

The `dist/` tree is the template for a bootable CastaliaOS install. After a
successful DOS build, `CASTALIA.EXE` and `CBOOT.EXE` sit in
`dist/cdroot/CASTALIA/BIN`. To make a bootable machine or disk image:

1. Install FreeDOS on the target disk/image (a real IDE/CF disk, or a QEMU/Bochs
   image — see [TESTING.md](TESTING.md)).
2. Copy the whole `dist/cdroot/CASTALIA` tree to `C:\CASTALIA`. (Or just copy
   `RELEASE/CASTALIA`, which already includes `DOS4GW.EXE` — then skip step 3.)
3. Place a `DOS4GW.EXE` in `C:\CASTALIA\BIN` (the `RELEASE/` tree bundles one).
4. Use `dist/AUTOEXEC.BAT` and `dist/CONFIG.SYS` as templates. The intended
   installer *appends* the Castalia block to the user's existing files after
   backing them up (`AUTOEXEC.CB_`, `CONFIG.CB_`); it never discards user
   content.

The `dist/AUTOEXEC.BAT` template sets the environment and launches the shell:

```bat
SET CASTALIA_HOME=C:\CASTALIA
SET PATH=C:\CASTALIA\BIN;%PATH%
SET DOS4GVM=1
CD \CASTALIA\BIN
CBOOT.EXE
```

`CBOOT.EXE` is a small text-mode gatekeeper: it reads the boot-dirty flag,
forces Safe Mode after two unclean boots, honors `CBOOT /safe` and a
`[Boot] SafeMode=1` line in `SYS\CASTALIA.INI`, checks that `CASTALIA.EXE`
exists, then launches the desktop and returns its exit code to DOS.

The `dist/CONFIG.SYS` template is intentionally minimal (a HIMEM-class driver,
`DOS=HIGH,UMB`, `FILES=40`, `BUFFERS=20`, `LASTDRIVE=Z`). No sound, network, or
USB driver is required to reach the desktop. The shell's DPMI host is the
DOS/4GW extender itself, so `CONFIG.SYS` does not need a separate DPMI server.

The installed layout and its directories (`BIN`, `SYS`, `THEMES`, `APPS`,
`FONTS`, `ICONS`, `HELP`, `LOGS`, `TEMP`, `TRASH`) are documented in
`dist/cdroot/CASTALIA/README.TXT`. Configuration lives in
`SYS\CASTALIA.INI` (plain text, editable from DOS; delete it to restore
defaults). See [RECOVERY.md](RECOVERY.md) for repair procedures.

---

## 5. Build outputs

| Output | Track | Produced by | What it is |
|--------|-------|-------------|------------|
| `build/castalia` | Host | `make` / `make all` | The shell driven by the headless host backend. |
| `build/run_tests` | Host | `make test` | The unit-test runner (7547 checks across 58 suites). |
| `build/castalia.bmp` | Host | `make run` | 24-bit BMP of the demo desktop (800x600 by default). |
| `build/castalia_WxH.bmp` | Host | `tools/run_host.sh W H` | BMP at a chosen resolution. |
| `build/LOGS/castalia.log` | Host | any run | Rotating session log (rotates at 512 KB). |
| `dist/cdroot/CASTALIA/BIN/CASTALIA.EXE` | DOS | `wmake -f Makefile.dos` | The graphical desktop (DOS/4GW). |
| `dist/cdroot/CASTALIA/BIN/CBOOT.EXE` | DOS | `wmake -f Makefile.dos` | The safe launcher. |

---

## 6. Troubleshooting

**Host build**

- *`make: cc: command not found`* — set a compiler explicitly, e.g.
  `make CC=gcc` or `make CC=clang`.
- *Warnings you did not expect* — the host build uses `-Wall -Wextra`
  (with `-Wno-unused-parameter`). Warnings are not fatal, but the tree is
  intended to build clean.
- *`make run` renders nothing / no BMP* — the shell writes the BMP to the path
  given by `--shot`; `make run` uses `build/castalia.bmp` with
  `CASTALIA_HOME=build`. Ensure `build/` is writable.
- *Tests fail* — `build/run_tests` prints `N checks, M failures`; a healthy
  tree reports `7547 checks, 0 failures` and `ALL PASS`.

**DOS build**

- *`wcc386` / `wlink` not found* — the Open Watcom environment is not
  activated; run the `owsetenv` script for your platform first.
- *`DOS4GW.EXE` missing at runtime* — copy it into `C:\CASTALIA\BIN`. The
  `RELEASE/` tree and the release ZIP already bundle one; from a raw `wmake`
  build, use `include/vendor/DOS4GW.EXE` or your own Open Watcom copy (see the
  licensing note above).
- *No graphics mode / drops back to DOS* — the DOS backend requires a VBE
  (VESA) BIOS; it walks the fallback ladder
  `800x600x16 -> 640x480x16 -> 640x480x8` and returns to DOS with a message if
  none is available. Try Safe Mode (`CBOOT /safe`) and see
  [RECOVERY.md](RECOVERY.md).

---

## 7. A note on assets

For v1, the shell's **fonts are built in** (`src/gfx/gfx_font_data.c`) and its
**icons are procedural** — there are no external font or icon files to build,
and `dist/cdroot/CASTALIA/FONTS` and `ICONS` are placeholders for later phases.

The **asset pipeline** described in the Project Bible (Python tools for image
packing, palette reduction, font building, and disk-image generation) is a
**roadmap item** and is not present in this repository yet; the only tool
shipped today is `tools/run_host.sh`. Track its status in
[BACKLOG.md](BACKLOG.md).
