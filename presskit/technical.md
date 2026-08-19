# Technical Details — how it's built and what we used

CastaliaOS is written **from scratch in portable C** with a deliberately small,
layered design. Nothing above the platform layer talks to DOS, VESA, or the
hardware directly, which is what lets the same upper stack build and run on a
modern host for development and CI.

## Languages & toolchains

- **C — strict C89** for the portable core (Watcom‑compatible), gnu89 on the host.
  The single deliberate convenience is variadic logging macros.
- **DOS target:** built with **Open Watcom** (`wmake -f Makefile.dos`), producing a
  DOS/4GW‑hosted 32‑bit protected‑mode executable.
- **Host target:** any C89 compiler (gcc/clang). A headless framebuffer backend
  renders frames and writes screenshots, so the whole stack is verifiable in CI.
- Small Python helpers author fonts, icon packs, and assets offline.
- **Zero third‑party runtime dependencies.** Optional bundled freely‑licensed
  assets: the **Spleen** bitmap font (BSD) and, optionally, **Tango** icons.

## Architecture (six layers)

1. **Platform (Layer 1)** — the only seam to the machine: video (VESA/VBE with
   graceful mode fallback), input (PS/2 mouse via INT 33h, keyboard via INT 16h/09h),
   timing, file I/O, child‑process launch, optional sound and networking. Two
   backends implement it: `dos/` and a headless `host/`.
2. **System (Layer 2)** — logging, safe bounded strings, an accounted allocator,
   time helpers, a text clipboard, and a crash/error boundary.
3. **Graphics (Layer 3)** — an original software renderer: clipped primitives,
   Win9x bevels, gradients, drop shadows, blitting, a BMP codec, bitmap fonts, and
   the hardware pixel packers (3‑3‑2 and RGB565).
4. **Window Manager (Layer 3)** — a fixed pool of top‑level windows, z‑order,
   focus, non‑client frames, message dispatch, virtual desktops, and snapping.
5. **UI toolkit (Layer 3)** — buttons, checkboxes, radios, list boxes, menus, a
   text edit control, and modal dialogs, drawn as lightweight helpers.
6. **Shell (Layer 4)** — desktop, taskbar, launcher, context menus, tooltips,
   theme engine, cursor, animations, splash, screensaver, and command dispatch.
7. **Apps (Layer 5)** — the built‑in applications, plus the `.CAPP` add‑on loader.

## The rendering model

The entire UI is composited into **one 32‑bit XRGB back buffer in system RAM**.
Drawing is therefore format‑independent and dither‑free. Only the platform's
`present()` step converts the **dirty rectangles** down to the real hardware
depth — a search‑free 3‑3‑2 palette at 8bpp or RGB565 at 16bpp. There is no
per‑pixel alpha by design: icons and cursors use a 1‑bit magenta color key, and
shadows are a cheap read‑modify‑write on the edge strips. The desktop background
is cached and blitted rather than recomputed per frame. The result is XP‑style
chrome that still fits comfortably in a period memory budget.

## Extensibility — `.CAPP` add‑ons

Add‑ons are packaged files discovered at boot; valid ones are indexed and appear
as first‑class entries in the launcher. Their code runs behind a stable ABI
(`CappHostApi`) so a package can open and draw its own windows without touching
the shell or the hardware — real third‑party extensibility for a retro OS.

## Quality & verification

- **1,211 host unit‑test checks**, all passing (strings, rectangles/regions, INI,
  color packing, the BMP codec, window snapping, the clipboard, Mines logic,
  the spreadsheet formula engine, the word‑processor document model, the
  appointment store, the three‑band equalizer, Paint's raster core, the
  ARP/IPv4/ICMP/UDP stack — including deliberately corrupted frames — the theme
  round trip, path handling, the thumbnail cache, the file‑association table,
  and the `.CAPP` loader).
- **Nine scripted end‑to‑end scenes** (`make demos`) drive real windows through
  synthetic input and then check what they changed: the pixels in a saved
  bitmap, the bytes in a saved file, the frames that came back off the
  simulated wire. A scene that produces no output is reported as a failure, not
  a pass.
- A **strict‑C89 lint** compiles every portable source with Watcom‑style rules to
  guarantee the DOS build never breaks.
- A **golden‑screenshot** pipeline renders representative scenes headlessly so UI
  regressions are caught in CI.

## Footprint

- ~37,800 lines of C across 145 files.
- Composited at up to 800×600; internal buffer is 32‑bit, hardware output is
  8‑ or 16‑bit.
- Designed to idle quietly (cooperative single‑threaded loop, ~60 Hz cap).

## License

MIT. Original code and artwork throughout; see `LEGAL.md` and
`THIRD_PARTY_NOTICES.md` in the source tree for the (short) list of bundled,
freely‑licensed assets.
