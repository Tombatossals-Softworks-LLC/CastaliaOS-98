# Fact Sheet

**Developer / Publisher**
Tombatossals Softworks — independent, developer‑led.

**Creators**
Dave Abellan and Claudio di Castello.

**Release date**
In development. Public preview available now (v0.1.0, MVP / Phase 1).

**Platforms**
- Real DOS on 386‑class hardware and up (VESA/VBE video, PS/2 mouse, INT 16h keyboard)
- Emulators: DOSBox / DOSBox‑X, 86Box, PCem, QEMU
- Headless "host" build (any C89 compiler) used for development, CI, and golden‑image screenshot tests

**Website**
tombatossalssoftworks.com

**Regular price**
Free. Open source under the MIT license.

**Languages**
User interface in English. Source and documentation in English.

**Age rating**
Everyone. No objectionable content.

---

## The one‑liner

> CastaliaOS 98 PE is an original, Win9x/XP‑inspired desktop operating system
> that boots on a 386 and fits in a few megabytes — hand‑written in C, with no
> Microsoft code, art, or branding anywhere in it.

## The pitch

Remember the era of beveled buttons, a Start‑style launcher, glossy title bars,
and a file explorer with a blue task pane? CastaliaOS recreates that feeling as
a **brand‑new, legally‑clean** system you can actually run on period hardware.
It's a love letter to late‑90s / early‑2000s desktop computing — a complete
shell with a window manager, a themeable UI toolkit, virtual desktops, window
snapping, and a growing suite of real applications — all composited by an
original software renderer and small enough to feel instant.

## By the numbers

- **~69,400** lines of C across **218** files
- **7,609** unit checks, all passing, plus **74** scenes run end to end
  (`make demos`); strict C89 / Open Watcom‑clean lint
- **6‑layer** architecture: Platform → System → Graphics → Window Manager → UI → Shell → Apps
- **4** virtual desktops, window snapping/tiling, and a `.CAPP` add‑on format — features Windows 98 SE never had
- **20+** built‑in applications and system components
- **32‑bit** internal compositing, presented to **8bpp (3‑3‑2)** or **16bpp (RGB565)** hardware

## Included applications

File Manager (Windows XP‑style Explorer), Notepad, Calculator, **CastaliaWrite**
(word processor), **CastaliaSheet**
(spreadsheet with formulas), Task Manager,
**Media Player** (Winamp‑style WAV player with a live visualizer), **Benchmark
Suite** (CPU / memory / graphics with a CastaliaMark score), Clock & Calendar,
Character Map, **Solitaire** (Klondike), Mines, Paint‑lite, image/hex Viewer,
Log Viewer, Control Center, System Information, and a spectacular tabbed About.

## What makes it notable

- 100% original code and artwork — a real, runnable retro OS, not a theme or a mod.
- Runs on genuine DOS hardware yet develops and tests on a modern host build.
- A software renderer with XP‑style glossy chrome, drop shadows, and dirty‑rect
  compositing that stays inside a tiny memory budget.
- Extensible: discoverable `.CAPP` packages appear as first‑class launcher apps.

## Contact

Press & general: **hello@tombatossalssoftworks.com**
Web: **tombatossalssoftworks.com**
