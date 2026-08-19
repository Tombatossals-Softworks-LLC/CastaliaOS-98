# CastaliaOS 98 PE — Architecture

This document describes how CastaliaOS 98 PE is built, layer by layer, and the
engineering decisions behind it. It reflects the code as it exists in this
repository, and marks clearly what is implemented now versus staged for a later
phase.

CastaliaOS 98 PE is an original, Win9x-*inspired* desktop environment. It is not
Microsoft Windows, contains no Microsoft code or assets, and implements a small,
Castalia-native API rather than emulating Win32. See [../LEGAL.md](../LEGAL.md).

---

## 1. Design goals and non-goals

**Goals**
- Boot to a graphical desktop on a Pentium II / 440BX-class machine over FreeDOS.
- Fast dirty-rectangle repaint; no full-screen redraws in steady state.
- Idle shell under 8 MB (the host build measures ~3.7 MB, checked by
  `--mem-check` against a 4 MB idle ceiling).
- Repairable from DOS with a text editor (INI configuration).
- Always recoverable: crash logging, boot-dirty flag, Safe Mode.
- Every external dependency behind a Castalia-owned interface.

**Non-goals for v1 (explicitly out of scope, not hidden)**
- Win32 binary compatibility, VxD drivers, a real registry, Plug and Play.
- Preemptive multitasking or a protected kernel (that is the isolated
  `/kernel-lab` research track).
- Required sound, networking, or USB.

## 2. The central idea: one platform seam, two backends

The whole value of the architecture is a single, narrow abstraction boundary —
**`include/castalia/plat.h`** — between the portable upper stack and the machine.

```
        portable C89 upper stack (identical in both builds)
        sys / cfg / gfx / wm / ui / shell / apps / main
                              │
                     ┌────────┴────────┐   plat.h  (the only seam)
                     │                 │
        src/platform/host/     src/platform/dos/
        headless framebuffer   VESA + PS/2 + DOS (Open Watcom)
        + BMP screenshot       CASTALIA_DOS only
        CASTALIA_HOST only
```

Consequences:
- **The shell is buildable and runnable on a normal machine and in CI** through
  the host backend, which renders into memory and can dump a screenshot. This is
  how the desktop is verified today without DOS, VESA, SDL, or an emulator.
- **DOS-specific code cannot break portability**, because the DOS backend is
  compiled only by `Makefile.dos` (with `-DCASTALIA_DOS`) and never by the host
  `Makefile`.
- Swapping graphics/input implementations (e.g. adding a linear-framebuffer fast
  path, or an Allegro-4 backend) is a localized change under `src/platform/`.

Nothing above Layer 1 calls DOS, VESA, BIOS, SDL, or the OS directly.

## 3. Layer map

| Layer | Prefix | Modules | Role |
|------|--------|---------|------|
| 0 | — | BIOS, FreeDOS, `AUTOEXEC.BAT`, `CBOOT.EXE` | Boot substrate |
| 1 | `plat_` | `platform/host`, `platform/dos` | Host abstraction |
| 2 | `sys_`, `cfg_` | log, safe strings, memory, time, crash, INI | Runtime services |
| 3 | `gfx_`, `wm_`, `ui_` | surfaces, blits, font, bevels, windows, controls | Graphics + window mgr |
| 4 | `sh_` | desktop, taskbar, launcher, cursor, theme | Shell |
| 5 | `app_` | System Info, generic text window | Applications |
| 6 | `capp_`, `net_`, `install_`, `snd_` | `.CAPP` format + plugin ABI, packet-driver net, installer, Sound Blaster DAC, LFB, 16/256-color pipeline, Win98 companion, kernel lab | Extensions (partly implemented; see BACKLOG) |

## 4. Data representation choices

### 4.1 Fixed-width types (`ctypes.h`)
C89 has no `<stdint.h>`, and 16-bit DOS makes `int` 16 bits, so no code assumes
the width of `int`. `cu32`/`cs32` resolve to **exactly 32 bits** on our real
targets (gcc/clang on x86-64 and Open Watcom in 32-bit flat mode, where `int`
is 32 bits), falling back to `long` only on a genuine 16-bit build. This makes
`CColor` a true 32-bit XRGB pixel — important because an early version used
`unsigned long`, which is 64 bits on LP64 hosts and silently doubled the back
buffer to 3.7 MB.

### 4.2 Canonical color and the present boundary
The entire UI composites into **one 32-bit XRGB back buffer (`0x00RRGGBB`) held
in system RAM**. All drawing logic is therefore format-independent and dither-
free. The platform `plat_present()` step is the **only** place that converts the
dirty region down to the hardware depth:
- 8bpp: a 3-3-2 palette; index packing is a bit shuffle, no nearest-color search
  (`gfx_pack_index_332`).
- 16bpp: RGB565 (`gfx_pack_565`).

This deliberately keeps the 32bpp buffer out of the 4 MB VRAM. Only the visible,
converted 8/16bpp framebuffer lives in VRAM; the back buffer competes with the
128 MB of system RAM instead. (See [HARDWARE_TARGETS.md](HARDWARE_TARGETS.md).)

### 4.3 Rectangles and regions (`rect.h`)
Half-open rectangles (`x0<=x<x1`). A bounded `CRegion` accumulates dirty
rectangles, cheaply coalescing overlaps and, on overflow, collapsing to one
bounding box — it **never drops a rectangle** (correctness over optimality).
This is the geometric core behind clipping and repaint; it is unit-tested.

## 5. Layer 2 — runtime services

- **`sys_log`** — bounded, timestamped records to a file (rotated at 512 KB with
  one `.old` backup) and/or stderr. Uses stdio directly so it is available
  before `plat_init` and captures `__FILE__`/`__LINE__` via the `SYS_LOG*`
  macros.
- **`sys_str`** — `strlcpy`/`strlcat`/`snprintf`-style helpers that always
  NUL-terminate and report truncation; the core never uses `strcpy`/`sprintf`.
- **`sys_mem`** — accounted `malloc`/`free` wrappers tracking live/peak bytes and
  block count (surfaced in System Info and used to check the memory budget). The
  caller passes the size on free, so there is zero per-block overhead.
- **`sys_crash`** — the fatal boundary. `sys_fatal()` writes a crash record,
  keeps the persistent **boot-dirty flag** set for the session, invokes the
  installed handler (the shell paints a recoverable notice and drops to DOS),
  then exits nonzero. `CBOOT` reads the flag on the next boot to offer Safe Mode.
- **`cfg_ini`** — a forgiving INI reader/writer. Unparseable lines are skipped
  (repairability), a missing file loads as empty so defaults win, saves are
  written to a temp file then renamed. Typed getters (`str/int/bool/color`) and
  enumeration for the Control Center.

Every subsystem has `_init`/`_shutdown` symmetry.

## 6. Layer 3 — graphics

`src/gfx/` is a self-contained software renderer over a `GfxSurface` (32-bit
pixels + pitch + active clip rect). All primitives clip to the surface's clip
rectangle:
- Fills, H/V lines, frames, Bresenham lines, vertical gradients.
- **Two-tone bevels** (`gfx_bevel`) — raised/sunken/etched — the visual grammar
  of every button, panel, and window frame.
- **Color-keyed blits** (magenta `GFX_COLORKEY`) instead of per-pixel alpha, per
  the "avoid heavy alpha blending" rule; plus rectangle capture/restore for the
  software cursor's save-under.
- An **original 8×8 bitmap font** (`gfx_font_data.c`, hand-authored, legally
  clean) rendered at a 6px advance; the bold face is synthesized by smearing each
  row one pixel, so only one glyph table is stored.

## 7. Layer 3 — window manager

`src/wm/` owns a **fixed pool** of top-level windows (no dynamic window
explosion), a z-order list (back to front), focus, and per-window invalidation.

- **Frames** (`wm_frame.c`): a raised 3D border, a gradient title bar (active
  vs. inactive), caption buttons (close/max/min), and a client fill. Client
  content is produced by the window's `WmProc` on `WM_MSG_PAINT`.
- **Dispatch** (`wm_dispatch.c`): raw `PlatEvent`s are resolved into focus
  changes, title-bar dragging, caption-button actions, and client-area
  mouse/keyboard messages delivered in client coordinates.
- **Compositing** is split so the shell can sequence layers correctly:
  `wm_collect_dirty()` moves window invalidations into the shell's frame region
  (so the desktop repaints under them first), then `wm_paint()` draws every
  visible window back-to-front, clipped to that region. Back-to-front painting
  gives correct occlusion; clipping to the dirty region keeps frames cheap.

The theme is pushed *down* into the manager (`wm_set_theme`) so Layer 3 never
depends on the Layer 4 theme representation.

## 8. Layer 3 — controls (`ui_`)

v1 ships the controls the shell actually needs: **push buttons** and **pop-up
menus** (used by the launcher). The shared **`UiPalette`** is set at theme apply,
so a theme switch recolors every control. The remaining Bible controls (checkbox,
radio, textbox, listbox, tree, tabs, slider, progress, status bar, group box) are
staged for later phases and are explicitly marked TODO in `ui.h` so they stay out
of the compiled path until implemented.

## 9. Layer 4 — the shell

`src/shell/` is what a user experiences as "CastaliaOS":

- **Theme** (`sh_theme`): the original "Castalia Classic" palette (Mediterranean
  stone desktop, royal-blue active titles, antique-gold accent, graphite grays)
  with a high-contrast Safe Mode variant. Loadable from INI; applies down to
  `ui_` and `wm_`.
- **Desktop** (`sh_desktop`): stone gradient, a quiet castle silhouette, and
  procedurally-drawn original icons (monitor, folder, gear, document, trash) with
  labels — no copied iconography.
- **Taskbar** (`sh_taskbar`): the "Castalia" shield launcher button, running-task
  buttons enumerated from the window manager, and a live clock in a sunken well.
- **Launcher** (`sh_launcher`): a pop-up menu built from `ui_menu`, opening above
  the launcher button; routes commands.
- **Cursor** (`sh_cursor`): an original arrow with **save-under** — the pixels it
  covers are saved and restored so it never smears.
- **Core** (`sh_core`): the session loop and the layered, dirty-rectangle frame:

```
sh_run_frame():
  drain input  -> route to launcher / taskbar / window mgr / desktop
  erase cursor (restore save-under)
  build region R = window invalidations + shell invalidations + old/new cursor
  for r in R: repaint desktop under r
  wm_paint(R)                    # windows, back to front
  if taskbar ∩ R: repaint taskbar
  if launcher open: paint launcher overlay
  draw cursor (save-under at new position)
  plat_present(R)                # push exactly the touched rectangles
```

The taskbar is repainted only when the clock, task count, or focus changes, so
idle frames touch almost nothing.

## 10. Layer 5 — applications

The **System Information** window is a fully real app: it gathers live data
(version, platform identity, actual video mode, live/peak memory, wall clock) and
renders it through a generic multi-line text window that owns a heap payload and
frees it on `WM_MSG_DESTROY` (init/shutdown symmetry). It is the flagship proof
that the `platform → gfx → wm → app` pipeline works end to end.

### 10.1 Closing a changed document asks first

Type into any editor, press the close box, and the work was gone. No dialog,
no warning, nothing to undo it with — the most ordinary way a desktop loses
somebody's work, and this system did it in four places.

The window manager had always supported the fix. `wm.h` says an app receives
`WM_MSG_CLOSE` and "may handle it (prompt, veto), and only if it does not does
the manager destroy it." Nothing had ever used it. `wm_destroy()` still does
**not** ask, which is what the shutdown path and the scene teardown want.

**How each editor knows it changed matters more than the dialog.** Notepad had
a `modified` flag already. The other three had nothing, and a flag is the wrong
answer for them: it is set by every command that edits, so it is wrong the
first time somebody adds an edit path and forgets — wrong in the direction that
loses work, silently. They take a **checksum of the document** instead, stamped
when it is saved or loaded and compared when the window closes. There is
nowhere for an edit to hide from it.

**The close box is not the only way out.** Ending the session does not close a
single window, so every one of those guards is bypassed and the work goes
anyway. There were **four** ways out — Shut Down's dialog, Restart Shell, Exit
to DOS, and the host's quit event — and the first version of this fix guarded
only the dialog. Three doors, and the guard was on the fourth.

`sh_end_session()` is the one place that stops the loop now, and it asks
`wm_unsaved_count()` first, which puts the question to
every open window (`WM_MSG_QUERY_UNSAVED`) rather than consulting a list the
shell keeps: a list is a second place to forget an app, and the forgotten one
is the one whose work is lost. A window that does not answer has nothing to
lose, which is the right default.

It asks once rather than closing each window in turn. Closing them one at a
time means a chain of modal dialogs with a cancel in the middle, and a
half-shut-down desktop is not a state anybody asked for.

The stamp has to land at the right moment, and that is the part worth testing:
stamped before CastaliaSheet fills in its sample workbook, or before Paint
clears its canvas, every fresh window looks edited and the first thing anybody
notices is being nagged for closing a window they never touched. `--unsaved-demo`
opens each of the four editors twice — once to close untouched, once to change
and close — so both directions are held.

### 10.1 An editor may only open what it can write back

Every editor here holds its document in a fixed structure: Notepad 16 383
characters, CastaliaSheet a 48 × 16 grid of 31-character cells, CastaliaWrite
4 095 characters across 256 paragraphs, Paint a 420 × 260 canvas. That is the
memory budget doing its job, and it is fine — what is not fine is discovering
the ceiling by losing data at it.

All four used to read as much of a file as fit, report **"Opened"**, and then
write the part they kept back over the whole file. The message was the problem
more than the truncation: `"Opened FILE.TXT (16383 bytes)"` is the same
sentence a successful open gives, and for every file that fits, 16 383 really
would be the file's size. Nothing distinguished the two. Paint was worse still
— it quoted the picture's *real* size, `"Opened SHOT.BMP (800x600)"`, while
holding a 420 × 260 corner of it, and the system's own screen capture writes
800 × 600 BMPs.

The rule now:

- **Measure before reading.** `plat_file_size()` first, not `plat_fread()`
  into a buffer and hope. The size is available on both backends.
- **Refuse or admit, never both silently.** Notepad refuses outright and says
  the two numbers. The Sheet and Write *show* what fits — the first 48 rows
  are worth seeing — but do **not adopt the path**, so Ctrl+S offers
  `BOOK1.CSV` / a new name instead of overwriting the file it came from.
- **The parse reports what it could not take.** `sheet_from_csv()` and
  `wr_parse()` return CTRUE only when the model holds the *whole* source, and
  hand back a count of the rows, columns, cells or characters left behind. A
  caller that ignores both is asserting the file fits.

The read-only readers already did this and are the precedent: the console's
`fc` and the File Compare window decline a file over their limit and quote its
size, and the Viewer, which cannot write at all, still prints `(truncated)`.
The programs that *write* were the ones saying nothing.

`--bigfile-demo` drives all four end to end against the filesystem: an
oversize file, an open, Ctrl+S, and then the file's size read back off the
disk rather than off a status line.

**Why the canvas does not simply grow.** The obvious fix for Paint is to size
the canvas to the picture, and it is not affordable. A canvas is 4 bytes a
pixel and the undo ring holds `PC_UNDO_LEVELS` snapshots *at canvas size*, so
a picture costs five times its own pixels — and a session has more than one
Paint window. A 500 × 400 ceiling works out to 3 906 KB on paper against a
3 767 KB idle desktop and an 8 192 KB budget; measured with three windows
open it peaked at **9 040 KB**. Growing the canvas needs a cheaper undo first
(tiles, or a command log rather than whole-surface snapshots).

The same measurement found a bug that predates all of this: three Paint
windows with full undo rings already peaked at **8 523 KB**, over the hard
budget, and nothing caught it — `--mem-check` opens each app once and closes
it, so it never held three canvases at once. Shrinking the canvas *down* to a
smaller picture (rather than padding every picture out to 420 × 260) brought
it to 8 045 KB. The margin is thin, and `--bigfile-demo` now measures it on
every run.

## 11. Boot flow (Layer 0)

```
BIOS -> FreeDOS -> CONFIG.SYS (HIMEM, DOS=HIGH,UMB, FILES/BUFFERS)
      -> AUTOEXEC.BAT sets CASTALIA_HOME + PATH, runs CBOOT.EXE
      -> CBOOT checks the boot-dirty flag / fail counter / CASTALIA.INI [Boot]
         SafeMode; after two unclean boots it forces Safe Mode
      -> CBOOT launches CASTALIA.EXE (optionally --safe)
      -> CASTALIA.EXE: plat_init (VESA fallback ladder) -> wm_init -> sh_init
         -> session loop
```

`main.c` is the one file allowed to differ per platform (it is the integration
seam): under `CASTALIA_HOST` it runs a headless driver that opens a representative
desktop and writes a screenshot; under `CASTALIA_DOS` it runs the real
interactive loop. It also handles the exit reasons: **Restart Shell** re-enters
the session loop; **Exit to DOS**, **Shut Down** (APM power-off), and **Reboot**
clean up and mark the boot flag clean.

## 12. The DOS/VESA backend in detail

`src/platform/dos/` implements `plat.h` for the real product (Open Watcom, DOS/4GW
flat model):
- **`vesa.c`** — VBE 2.0 detection, walking the BIOS mode list for an exact
  width/height/bpp, and a **banked (windowed)** framebuffer (`AX=4F02` without the
  LFB bit; bank switching via `AX=4F05`) for maximum compatibility with
  S3 Trio3D-class BIOSes. A linear-framebuffer fast path is a documented TODO.
- **`dos_dpmi.c`** — DPMI transfer buffer + real-mode interrupt simulation, needed
  for the pointer-based VBE info calls under a 32-bit extender.
- **`mouse.c`** (INT 33h), **`keyboard.c`** (INT 16h), **`dos_proc.c`** (child
  launch via the C runtime, keyboard-controller reboot, APM power-off).
- **`plat_dos.c`** — ties them together and synthesizes the same `PlatEvent`
  stream the host backend produces, so `wm`/`shell` are platform-agnostic.

> The DOS backend is complete and compiles under Open Watcom, but it **has not
> been executed** from this repository's host-only environment. It must be
> validated on QEMU/Bochs/real hardware — see [TESTING.md](TESTING.md). This is
> stated plainly rather than claimed as working.

## 13. Memory and performance

- Idle host shell: ~3.7 MB, under half the 8 MB budget. Two 800×600×32 surfaces
  at 1875 KB each account for nearly all of it — the back buffer, and the
  desktop cache that turns a steady-state repaint into a row copy. System Info
  reports live/peak bytes, and `--mem-check` fails the demo run if idle passes
  4 MB or peak passes 8 MB. It read ~1.9 MB here until the cache was added,
  which is why the ceiling is now enforced rather than described.
- Repaint is dirty-rectangle only; the taskbar repaints on clock/task/focus
  change; the cursor uses save-under. Steady-state frames touch a few small rects.
- The 4 MB VRAM holds only the visible 8/16bpp framebuffer; the 32bpp compositor
  buffer is in system RAM (see §4.2).

## 14. Coding standards in force

- C89 core (one deliberate C99-ism: variadic logging macros, supported by all
  target toolchains and documented in `sys.h`).
- Subsystem prefixes: `sys_ cfg_ gfx_ wm_ ui_ sh_ app_ plat_ res_`.
- No unbounded string ops; fixed buffers carry their size.
- Explicit error codes (`CResult`); no silent failure; init/shutdown symmetry.
- Public APIs are commented in the headers under `include/castalia/`.

## 15. What is deliberately deferred

Everything not yet implemented is isolated from the compiled critical path and
listed in [BACKLOG.md](BACKLOG.md). Several Layer-6 extensions have since landed
behind their own seams — the **installer** (`install.h`), the **Sound Blaster
DAC** path (`snd.h`), the **VESA linear-framebuffer** fast path, the
**16/256-color theme pipeline**, the **`.CAPP` format + plugin ABI**
(`capp.h`/`capp_abi.h`), and the **packet-driver networking** seam (`net.h`) —
each with host-tested portable logic and DOS-only hardware code compiled only
under `CASTALIA_DOS`. Still deferred: Paint-lite, the DOS profile launcher UI,
the `.CAPP` code loader, a TCP/IP stack, the Win98 shell companion, and kernel
lab milestones past protected mode.
