# CastaliaOS 98 PE — Task Backlog

Work grouped by phase, with an honest ledger of **what compiles and runs now**
versus what is staged. The guiding rule from the Project Bible: never pretend a
later-band feature is v1, and isolate unfinished work from the compiled critical
path.

Legend: ✅ done · 🟡 partial · ⬜ not started

---

## What compiles and runs today (Phase 1 MVP)

| Capability | Status | Where |
|-----------|:------:|-------|
| Portable C89 upper stack, two-backend platform seam | ✅ | `include/castalia/plat.h`, `src/platform/*` |
| Headless host backend + BMP screenshot | ✅ | `src/platform/host/plat_host.c` |
| DOS/VESA backend (banked VBE, INT16 kbd, INT33 mouse, DPMI) — **builds under Open Watcom + boots in QEMU** | ✅ | `src/platform/dos/*` |
| INT 33h mouse (with a DOS mouse driver, e.g. CuteMouse) — **click opens a window in QEMU** | ✅ | `src/platform/dos/mouse.c` |
| **Outline window drag and RESIZE** (`[Shell] DragOutline`, Control Center / Sound & Effects, forced in Safe Mode) — a rubber band instead of the window's contents: moving 11,464 px a frame against 118,201, resizing 8,824 against 136,856, measured | ✅ | `src/wm/wm_dispatch.c`, `src/shell/sh_core.c` |
| **Mouse wheel** — one shared rule (`ui_scroll_wheel`) in all 15 scrolling windows, kept complete by `tools/check_wheel.sh`, routed to the window under the pointer; the DOS `INT 33h AX=0011h` handshake is unverified on hardware | 🟡 | `src/platform/dos/mouse.c`, `src/ui/ui_scroll.c`, `src/wm/wm_dispatch.c`, `tools/check_wheel.sh` |
| Logging, safe strings, accounted memory, crash boundary | ✅ | `src/sys/*` |
| Repairable INI config (load/save/typed getters) | ✅ | `src/cfg/cfg_ini.c` |
| Software renderer: fills, lines, bevels, gradients, keyed blit | ✅ | `src/gfx/*` |
| Original 8×8 bitmap font (+ synthesized bold) | ✅ | `src/gfx/gfx_font_data.c` |
| 8bpp (3-3-2) / 16bpp (565) pack kernels for present | ✅ | `src/gfx/gfx_palette.c` |
| Window manager: pool, z-order, focus, drag, caption buttons | ✅ | `src/wm/*` |
| Common controls: button, pop-up menu, single-line text edit | ✅ | `src/ui/*` |
| Modal dialogs (message box + prompt) with input capture | ✅ | `src/ui/ui_dialog.c` |
| Shell: theme, desktop + icons + castle motif, taskbar, launcher, cursor | ✅ | `src/shell/*` |
| Dirty-rectangle layered compositor + present | ✅ | `src/shell/sh_core.c` |
| System Information app (live data) + generic text window | ✅ | `src/apps/*` |
| **File Manager** (browse, navigate, copy/cut/paste, rename, delete-to-Trash) | ✅ | `src/apps/app_fileman.c` |
| **Notepad** (multiline editor: edit, nav, scroll, open/save) | ✅ | `src/apps/app_notepad.c` |
| **Calculator** (arithmetic, button grid + keyboard) | ✅ | `src/apps/app_calc.c` |
| **Log Viewer** (color-coded, scrollable tail of the session log) | ✅ | `src/apps/app_logview.c` |
| Safe Mode (high-contrast theme, forced fallback video) | ✅ | `sh_theme.c`, `plat_dos.c` |
| CBOOT safe launcher (dirty-flag / fail-count / Safe Mode) | ✅ | `src/boot/cboot.c` |
| **INSTALL.EXE** (tree, boot-file backup, idempotent boot entry, uninstall) | ✅ | `src/install/*` |
| **Sound Blaster DAC path** (BLASTER detect + DMA PCM cues) behind `snd.h` | ✅ | `src/platform/dos/snd_blaster.c` |
| **VESA linear-framebuffer fast path** (banked fallback) | ✅ | `src/platform/dos/vesa.c` |
| **16/256-color theme pipeline** (EGA-16, nearest-match, quantize) | ✅ | `src/gfx/gfx_palette.c`, `sh_theme.c` |
| **`.CAPP` package format + validator + mkcapp + discovery** | ✅ | `include/castalia/capp.h`, `src/capp/*` |
| **Plugin ABI contract** (host API + plugin descriptor) | ✅ | `include/castalia/capp_abi.h` |
| **`.CAPP` plugin loader** (resolve → ABI check → init/open/frame/shutdown, builtin registry, launcher entries) | ✅ | `include/castalia/capp_loader.h`, `src/capp/capp_loader*.c`, `capp_host_wm.c`, `capp_sample.c` |
| **Networking seam** (packet-driver client, address/checksum helpers) | ✅ | `include/castalia/net.h`, `src/net/*`, `net_pkt.c` |
| **Keyboard modifiers** (Shift/Ctrl/Alt) on every event | ✅ | `plat.h`, `keyboard.c` (BIOS shift flags) |
| **System text clipboard** (cross-app cut/copy/paste) | ✅ | `include/castalia/clip.h`, `src/sys/clip.c` |
| **Notepad selection + cut/copy/paste** (Shift-select, mouse-drag, Ctrl+A/C/X/V) | ✅ | `src/apps/app_notepad.c` |
| **Window snapping** (drag to edge -> half / maximize) | ✅ | `src/wm/wm_snap.c`, `wm_dispatch.c` |
| **Task Manager** (window list, memory gauge, activate / end task, live memory + frame-time graphs) | ✅ | `src/apps/app_taskman.c`, `hist_core.c` |
| **Disk Usage** (squarified treemap: area is share, one level nested) | ✅ | `src/apps/app_diskuse.c`, `map_core.c` |
| **File Compare** (resynchronising line diff of two text files, and `fc` in the Console; CRLF and LF compare equal) | ✅ | `src/apps/diff_core.c`, `app_compare.c`, `tests/test_diff.c` |
| **Find inside files** (one Find matches a name OR the text in a file, case-insensitively, and reports the line) | ✅ | `src/apps/grep_core.c`, `app_fileman.c`, `tests/test_grep.c` |
| **Recycle Bin restore** (unique slots so a second delete cannot destroy the first, an index of where each item came from, and Restore on the bin's context menu) | ✅ | `src/apps/trash_core.c`, `app_fileman.c`, `tests/test_trash.c` |
| **Aurora theme** (XP-Luna glossy chrome: gradient title bars, red close box, gradient taskbar, and a shaded Start **orb** that overhangs the bar, bearing the Castalia mark) — default | ✅ | `src/shell/sh_theme.c`, `sh_taskbar.c`, `src/wm/wm_frame.c`, `gfx_vgradient3`/`gfx_tint` |
| **UI sound cues** (menu / window open / close, richer startup chime) | ✅ | `include/castalia/snd.h`, `src/sys/snd_common.c`, WM lifecycle hook |
| **UI animations** (window open/close zoom, launcher menu slide-up) — frame-budgeted, toggleable | ✅ | `src/shell/sh_anim.c`, `[Shell] Animations` |
| Host unit tests (7547 checks) | ✅ | `tests/*` |
| Host + DOS build systems, boot templates, emulator scripts, CI | ✅ | `Makefile`, `Makefile.dos`, `.github/workflows/ci.yml` |

Verified now: host build compiles clean (`-Wall -Wextra`), 7547 unit tests pass,
the desktop renders at 800×600 and 640×480; the **DOS target builds under Open
Watcom and boots end-to-end in QEMU** (FreeDOS → DOS/4GW → VESA 800×600×16
desktop, live clock, INT 16h keyboard, and INT 33h mouse — a click opens a
window — with a DOS mouse driver loaded). The **launcher is keyboard-navigable**
(Esc → arrows → Enter), and the **File Manager, Notepad, and Calculator have
each been driven on real FreeDOS with keys alone** (File Manager lists
`A:\CASTALIA`, Notepad takes typed text, Calculator computes `12+34=46`). **Not
yet verified:** DOS child-process launch, Alt+F4 close on DOS (QEMU's SeaBIOS
does not surface it via INT 16h), and real S3 Trio3D-class hardware — see
[TESTING.md](TESTING.md).

Roadmap extensions also verified on the host: a full **INSTALL.EXE →
uninstall round trip** (`build/install`), a **16-color desktop** render (96.6%
of pixels on the EGA-16 palette), a **`.CAPP` build → discover round trip**
(`build/mkcapp` authors a package the shell's startup scan validates and a
corrupt file it rejects), and the **kernel-lab reaching 32-bit protected mode**
(a two-stage image assembled with NASM and booted in QEMU, GDT/IDT loaded, PE
set, banner on screen). The Sound Blaster DAC, VESA LFB, and packet-driver
client are DOS-only and validated on emulator/hardware, not CI (their portable
parsers/helpers are host-tested).

Look-and-feel layer ("hyper-vitaminized 98 with XP-Luna aesthetics"),
host-verified: the default **Aurora** theme renders glossy chrome (azure
desktop gradient 77,138,215 at top; blue taskbar 48,91,183; a pale Start
orb tinted from the theme's own accent),
gated so the 16/256-color and safe-mode paths stay flat; **UI sound cues** fire
at menu/window events through a WM lifecycle hook (audible on the DOS
speaker/DAC, silent on the null host backend); and **UI animations** — a window
open/close zoom (outline grows 73x37 → 408x206 then shrinks, confirmed by a
mid-animation screenshot at the logged bounds) and a launcher menu slide-up
(rendered height grows monotonically across reveal frames) — are frame-budgeted,
dirty-rect friendly, and switch off in safe mode or via `[Shell] Animations`.
To keep the glossy gradient desktop cheap, the static background (gradient +
castle + watermark + a soft radial **vignette**, and an optional **BMP
wallpaper**) is cached in an offscreen surface and blitted per dirty rectangle
instead of recomputing it every time: a `--desk-bench` of 500 full-screen
repaints measures 111 ms cached vs 163 ms live-gradient on the host (~1.46x; the
gap is wider on a P2), with the cached output pixel-identical to the live
render. The vignette and wallpaper stretch/tile/center are therefore free at
steady state.

Second polish pass, host-verified: **taskbar hover highlights** (a hovered task
button lightens to (226,232,242) from the (216,224,238) baseline; the Start orb
brightens too); a **minimize/restore zoom** where the outline both travels and
scales between a window's frame and its taskbar button (logged stepping
100,110 600x380 <-> 99,574 129x22); a soft **vignette** (same-row center blue
178 vs edge 136); and **BMP wallpaper** support (`[Shell] Wallpaper` /
`WallpaperMode` center/tile/stretch -- an 80x60 test BMP stretched to fill,
sampled orange (220,126,31) after the vignette).

Third pass ("super espectacular"), host-verified: **drop shadows** under every
window, dialog, and menu (`gfx_drop_shadow`: an L-shaped darkening strip with
distance falloff, no alpha buffer; remnant-free by construction because
`wm_dirty_add` inflates every dirty rect by the shadow margin -- measured
falloff lum 229→268→300 beside a window, and a window snap leaves no dark
strips behind); the **Start ORB** -- the launcher button in glossy mode is a
procedurally shaded green sphere bearing the gold Castalia castle, with hover
and pressed states, rendered once into keyed sprites and blitted (895 sphere px,
98 castle px, 152 px of overhang rising above the taskbar edge, transparent
corners, pressed ~18% darker; 16-color/safe mode keep the classic flat button);
and **XP-style icon labels** (white with a 1px dark drop shadow). The glossy
gate now also excludes safe mode, keeping the recovery profile flat and free.

Fourth pass, host-verified: the **Vale de Castalia** factory wallpaper -- an
original painted landscape (three-stop sky, sun glow, puffy clouds, three
integer-sine hill layers, a sunlit stone castle with a gold pennant) rendered
once into the background cache (sky (28,70,147), ground (45,97,30), 584 bright
cloud px, 2642 stone px, 28 pennant px); the **XP Start-menu header** (glossy
blue banner with the gold castle mark and the product name -- 3329/592/84
blue/white/gold px measured above the first item row, hit-tests offset so mouse
and keyboard launching are unchanged); and the **boot splash**
(`sh_splash_draw`: navy stage, 4x castle crest, product name, gold progress
bar), shown as a short progress moment at interactive startup (skipped in safe
mode) and screenshot-verified headless via `--splash-demo`.

Fifth pass, host-verified: the **shutdown screen** (`sh_shutdown_screen`:
amber-on-black "It is now safe to turn off your computer" with the gold crest,
drawn before poweroff -- 533 amber px on a (8,6,2) ground); a **screensaver**
(`sh_saver.c`: a twinkling starfield with a DVD-logo-bouncing Castalia castle,
purely a function of the frame counter, kicking in after ~45s idle and dismissed
by any input -- 137 star px and a 253-px gold castle on a (2,3,8) ground); and a
**cursor drop shadow** in glossy mode (a feathered shadow down-right of the
arrow, save-under widened so the compositor repaints under it -- 23 darkened px
beside the arrow; 16-color/safe mode keep the crisp cursor). New `--saver-demo`
and `--shutdown-demo` verification flags.

Sixth pass ("Win98 SE feel"), host-verified: **hover tooltips**
(`sh_tooltip.c`, a shell overlay between the popups and the cursor) -- rest the
pointer 600 ms on a task button, the Start orb, a Quick Launch cell, a tray
icon, or the clock and an info-yellow card pops just above the bar (task
buttons show the full window title, the tray cells report sound/network/
clipboard state, and the clock shows the **full weekday date** via the new
`plat_wall_date` on both backends -- host `localtime`, DOS INT 21h AH=2Ah);
a **Quick Launch strip** beside the Start orb (`QUICK_N` original mini-icons:
Show Desktop, File Manager, Notepad, Mines; flat until hovered, then a thin
raised bevel -- the Win98 SE taskbar signature) with **Show Desktop**
(`SH_CMD_SHOWDESKTOP`) minimizing every ordinary window on the current desktop
to its taskbar button in one click; and **Mines** (`src/apps/app_mines.c` over
a pure, hermetically-tested board core `mines_core.c` -- 9x9/10 mines,
first-click-safe placement from a self-contained LCG, iterative flood reveal,
flags, LED-style counter, elapsed-time readout, fully keyboard-playable).
Verified by 180 new unit checks (`tests/test_mines.c`: adjacency, flood,
win/lose, determinism) and three new headless demos (`--tooltip-demo` caught
the date card above the tray, `--mines-demo` a revealed "2" cell,
`--ql-demo` two windows minimizing to buttons and Mines opening from cell 3).

---

## Phase 0 — Research & baseline
- ✅ Toolchain + platform-abstraction design; host-buildable proof.
- ✅ Emulator profiles (QEMU, Bochs) authored.
- ✅ Original design palette + procedural icon/castle identity.
- ⬜ Inventory the specific IBM target machine (BIOS, disk, video BIOS, audio
  ID). Still genuinely not started -- it needs the machine. **The tool for it
  now exists**, though: System Information reads the DOS version and OEM, the
  DPMI host's free memory, and walks the PCI bus naming what it finds, and F2
  writes the lot to `CASTINFO.TXT`. Boot it, press F2, take the floppy away.
- 🟡 **VESA mode selection** (`src/gfx/vbe_pick.c`): the choice of which mode
  to set was an exact match on width, height AND depth, taking the first entry
  that matched. Two costs on real cards, neither visible in an emulator with a
  textbook mode list: a card offering 800x600 in 15bpp and 8bpp but not 16bpp
  matched nothing at that size and the ladder fell to 640x480x8, losing
  resolution that was available at 8bpp; and a BANKED mode listed before the
  linear one won, which costs a bank switch every few scanlines. The policy is
  now a pure function with tests over the awkward lists real cards report.
  It also makes explicit something that was true only by luck: the presenter
  drives exactly 8bpp (3:3:2) and 16bpp (5:6:5), so a 15bpp mode would come out
  miscoloured and a 24bpp one skewed -- the old exact match excluded them by
  accident, and `vbe_depth_drivable` now excludes them on purpose. The DOS
  plumbing that fills the list cannot be run from this environment; the new
  `find_mode` was compile-checked in isolation under C89 and the policy it
  calls is covered by `tests/test_vbe.c`. Still to confirm on the target: PS/2
  behaviour, and the mode list the real card reports.

## Phase 1 — Bootable MVP
- ✅ Graphics init with fallback ladder (800×600×16 → 640×480×16 → 640×480×8).
- ✅ Desktop, taskbar, launcher menu, software cursor, Exit to DOS.
- ✅ CBOOT auto-start path + Safe Mode gate.
- ✅ DOS target builds under Open Watcom (`wmake -f Makefile.dos`).
- ✅ **First real boot to desktop in an emulator** — FreeDOS + DOS/4GW + VESA
  800×600×16 in QEMU, with a live clock and Esc-opens-launcher keyboard input
  (`docs/media/qemu-freedos-*.png`; repro in `tools/make_dos_floppy.sh` +
  `emulators/qemu_floppy.sh`).
- ✅ Emulator input: INT 16h keyboard (Esc opens launcher; arrows navigate it;
  Enter launches — the File Manager, Notepad, and Calculator have each been run
  on real FreeDOS by keyboard alone) and INT 33h mouse (with CuteMouse loaded, a
  click opens a window). DOS child-launch / exit-to-DOS restore still to exercise.
- ⬜ First real boot on the IBM target.

## Phase 2 — Window manager & file manager
- ✅ Top-level windows, message model, z-order, dirty rectangles, dialogs base.
- ✅ Buttons and pop-up menus.
- ✅ **Window resize** by dragging any border or corner (grip margin, minimum
  size clamp); apps re-layout from `wm_client_rect` on `WM_MSG_SIZE`.
- ✅ **Maximize reserves the taskbar** — the WM keeps a work area
  (`wm_set_work_area`, set by the shell to the surface minus the taskbar) and
  maximizes into it, so the taskbar stays visible and clickable.
- ✅ **Keyboard-navigable launcher** — Esc / Ctrl+Esc opens the menu, arrows move
  the highlight (skipping separators), Enter launches; every app is reachable
  without a mouse (the target hardware treats the mouse as optional).
- ✅ **Right-click context menu** (`src/shell/sh_context.c`) — right-clicking the
  desktop raises a Win98-style popup (Refresh, Line up Icons, System Information,
  Control Center, About) that reuses the `ui_menu` control and dispatches through
  the launcher command space; mouse + keyboard driven, mutually exclusive with
  the launcher. Window/taskbar context menus reuse the same machinery next.
- ✅ **Alt+F4 closes the focused window** (and dismisses a modal). Window
  teardown now repaints the vacated frame from `wm_destroy`, so keyboard,
  caption-button, and programmatic closes all leave a clean desktop.
- ✅ **Alt+Tab switches windows** (`wm_cycle_focus`) — rotates the window stack,
  raising and focusing the next window; the taskbar draws the focused window's
  button pressed. (Verified on host; under QEMU SeaBIOS, like Alt+F4, the combo
  is not delivered via INT 16h — spec-correct on real hardware.)
- ✅ **Window snapping** (`src/wm/wm_snap.c`) — dragging a title bar so the
  pointer reaches a work-area edge tiles the window: left/right edges → that
  half, top edge → maximize (the desktop-tiling gesture Win98 SE lacked). Pure,
  host-tested geometry; verified end to end (drag to the left edge → left half).
- ✅ **Keyboard modifiers** (`PlatEvent.mods`) — Shift/Ctrl/Alt on every event
  (DOS reads the BIOS shift-flags byte), enabling Shift-select and
  Ctrl-shortcuts across the UI.
- 🟡 Controls: push button, pop-up menu, **single-line text edit** (`ui_edit`),
  **checkbox, radio button, listbox**, **meter / progress** and the **menu-bar
  keyboard navigation** (`ui_controls.c`, `office_ui.c`) are shared.

  The rest of that list needs restating, because it read as "not written" and
  that is not the case. A multiline edit, a tab strip, a slider, a status bar
  and a titled group box all **exist and work** — as private implementations,
  one app each: Notepad and CastaliaWrite for multiline text, `app_about.c`
  for tabs, `app_media.c` for sliders, `app_net.c`'s `na_group` for group
  boxes, and a status bar in most of the office apps.

  They are deliberately *not* promoted yet. Every shared control in this tree
  earned its place by having a **second** caller — that is what turned four
  copies of the Ctrl-key macro and four copies of menu navigation into one
  each, and each promotion fixed a real inconsistency in the process.
  Promoting a widget with one caller invents an API from a single example and
  usually gets it wrong. Remaining, honestly stated: a **tree** control, which
  nothing has yet; and promotion of the five above when a second user appears.
- ✅ **Modal dialogs** (`src/ui/ui_dialog.c`): async message box (OK / OK-Cancel
  / Yes-No) and prompt (labeled text field), as WM_STYLE_MODAL windows that
  capture input and are excluded from the taskbar; Enter/Esc = default/cancel.
- ✅ **Shut Down dialog** (`src/shell/sh_shutdown.c`): a modal with a radio group
  (Shut down / Restart the shell / Exit to DOS) + OK/Cancel that sets the session
  exit reason, in the spirit of the classic shutdown dialog.
- 🟡 **File Manager** (`src/apps/app_fileman.c`): done — details listing,
  directories-first sort, navigation, keyboard selection + scroll, toolbar
  (Up / Refresh / New / Rename / Copy / Cut / Paste / Delete), named New Folder,
  **rename** (F2, prompt), **copy / cut / paste** (file clipboard, overwrite
  confirm, **recursive directory copy**), **delete-to-`TRASH` with confirm**,
  and **open a file** (Enter / double-click loads it in Notepad).
  **Two-pane (commander) view** and the **bitmap viewer** are done as well --
  see the entry below for how long the first one went unchecked. Remaining:
  drive selector on DOS.
- ✅ **Run dialog** — launcher "Run" opens a prompt that calls
  `plat_run_program`.
- ✅ Settings persistence wired through `cfg_` end to end — a typed
  `CastaliaSettings` model loads from `CASTALIA.INI` at startup, applies (theme,
  clock), and the Control Center saves it back.

## Phase 3 — Applications & installer
- ✅ **Notepad** (`src/apps/app_notepad.c`): multiline editing, caret nav
  (arrows/Home/End/PageUp/PageDown), scrolling, click-to-position, New/Open/Save
  via prompt, **Find** (F3, case-insensitive, highlighted match, wrap-around),
  **word-wrap toggle** (greedy word breaks, visual-row caret navigation), open a
  file handed over by the File Manager, and an Ln:Col status bar.
- ✅ **Notepad selection + clipboard** (`src/apps/app_notepad.c`): a real
  selection (Shift + navigation, mouse drag, Ctrl+A) with cut/copy/paste
  (Ctrl+X/C/V) through the system text clipboard (`clip.h`), so text moves
  BETWEEN apps; the single-line edit control gets Ctrl+C/X/V too. Verified end
  to end (--clip-demo).
- ✅ **Task Manager** (`src/apps/app_taskman.c`): a live list of open windows
  with state + focus marker, a memory gauge (live vs. the 8 MB budget), the
  allocation-block count, task count, and uptime; Activate / End Task / Refresh,
  keyboard-driven (arrows/Enter/Delete). Reached from the launcher and desktop
  context menu. Verified end to end (--taskman-demo: End Task drops the live
  window count).
- ✅ **Task Manager Performance panel** (`src/apps/app_taskman.c`,
  `src/apps/hist_core.c`): live memory and frame-time graphs over the last
  minute, drawn as filled areas with a grid on an LCD-green ground, newest
  sample against the right edge. The sample ring is pure and hermetically
  tested (`tests/test_hist.c`): wrap ordering, partial fill, max/avg, a bar
  height that clamps and cannot divide by zero, and a scale that rounds to
  1/2/5 x 10^n so the axis does not jitter. It samples -- and repaints -- at
  2 Hz rather than per frame, because a resource meter that costs a repaint
  per frame to display is mostly measuring itself; the frame figure is the
  mean over the elapsed interval.
- ✅ **Disk Usage** (`src/apps/app_diskuse.c`, `src/apps/map_core.c`): a
  squarified treemap of a folder tree beside a sorted list, so "where did the
  space go" is one glance rather than an arithmetic exercise. Every rectangle's
  AREA is its share, the rectangles tile the box exactly, and one level of
  nesting shows what is inside the biggest folders -- without it a treemap of
  eight folders is a wall of one colour that says nothing the list did not.
  Colours come from `assoc.c`, so a `.WAV` here matches its icon in the File
  Manager. The scan is bounded by depth and entry count and says "partial" in
  the status bar when it hits the bound, rather than reporting a total quietly
  missing half the disk; the tail past the row cap is grouped into one row for
  the same reason. The layout is pure integer math with no float and no
  overflow (aspect ratios are compared in pixels, weights pre-scaled against
  the rectangle being filled) and is hermetically tested
  (`tests/test_map.c`): exact tiling by pixel-stamped coverage, ordering,
  proportionality, no slivers, and the degenerate cases -- one item, all
  equal, zero weights, a box one pixel tall, and byte counts large enough to
  overflow naive weight arithmetic. Verified end to end (`--diskuse-demo`
  builds a folder with a known 3:1 ratio, then reads the answer back off the
  screen: the map covers its well with nothing showing through, the bitmap
  really occupies a quarter of it, descending into a subfolder repaints the
  map entirely in that folder's contents, and Backspace at the root is a
  no-op). Reachable from the launcher and from the File Manager's right-click
  menu, on the folder under the cursor.
- ✅ **Calculator** (`src/apps/app_calc.c`): standard arithmetic, 4x5 button
  grid, full keyboard support (digits, + - * /, Enter, Esc, Backspace).
- ✅ **Mines** (`src/apps/app_mines.c` + pure `mines_core.c`): the classic
  hidden-mines grid puzzle -- 9x9/10 mines, first-click-safe placement
  (self-contained LCG), iterative flood reveal, right-click/F flags, LED-style
  mines-left counter, elapsed time, New/N/F2 restart, fully keyboard-playable
  (arrows + Enter/Space). Board logic hermetically tested
  (`tests/test_mines.c`, 180 checks); `--mines-demo` screenshots a revealed
  board. In the launcher and on Quick Launch.
- ✅ **System Info** expansion: **processor identification** is done
  (`src/sys/cpu_core.c`, `plat_cpu_id`). Reading CPUID is four instructions and
  sits in the platform layer; naming what it read is a table with edge cases
  and sits in a pure module walked by `tests/test_cpu.c` -- the same
  family/model meaning a Pentium for Intel, a K5 for AMD and a 6x86 for Cyrix,
  a vendor field of control bytes reported as Unknown rather than as a new
  manufacturer (it means the read failed), and anything the table does not know
  answered as "family N model M" rather than with a confident wrong name.
  Family 6 in particular outlived this era by twenty years, so an unknown model
  there falls back to the numbers instead of claiming "Pentium Pro". Feature
  bits are listed in a fixed order so two machines can be compared by eye.
  Wired into the System Information window and checked end to end by
  `--cpu-demo`. **DOS version, memory inventory, PCI scan and report export**
  are done too (`src/sys/mach_core.c`, `plat_machine_info`,
  `tests/test_mach.c`, `--sysinfo-demo`) -- see the entry below for what the
  naming refuses to guess at and for the video-mode bug the export turned up.
  **Environment** too, as the variables that change what this system DOES
  (`BLASTER`, `PATH`, `TEMP`, `COMSPEC`, `CASTALIA_HOME`) rather than every
  variable DOS is holding -- thirty rows of `DIRCMD` and `PROMPT` for the sake
  of five, and no C89 way to enumerate them anyway. **The video mode list** is
  done too (`src/gfx/vbe_report.c`, `plat_video_modes`, `tests/test_vbe.c`) --
  see the entry below.
- ✅ **Log Viewer** (`src/apps/app_logview.c`): scrollable, color-coded tail of
  the session log (`sys_log_path()`), streamed through the platform file API
  into a bounded ring buffer; Refresh / Top / Bottom, keyboard scroll, and
  severity coloring (ERROR/FATAL red, WARN amber).
- 🟡 **Control Center** (`src/apps/app_control.c`): categorized settings window
  (Appearance / Clock / Startup / About) built on the listbox + radio + checkbox
  controls. Live theme presets (Classic / Storm / Forest / High Contrast), a
  "show seconds" clock toggle, and a "Safe Mode next boot" flag shared with
  CBOOT. Edits persist to `SYS\CASTALIA.INI` via a typed settings model
  (`settings.c`) that preserves hand-edits, and apply live (`sh_apply_settings`)
  and on next launch. **Date & Time, Mouse and Keyboard are done too** -- see
  the entries below. The category list is complete.
- ✅ **Help** window (`app_help_open`): desktop, launcher, windows, apps, and
  keyboard shortcuts.
- 🟡 **Run** and **DOS Program Launcher** — both launcher entries open a prompt
  that runs a program via `plat_run_program`. A full DOS profile UI (workdir,
  args, EMS/XMS/CD/sound flags, pre/post batch) is a later item.
- ✅ **INSTALL.EXE** (`src/install/`, `include/castalia/install.h`): creates the
  `C:\CASTALIA` tree, optionally copies program files from the media (`--src`),
  backs up CONFIG.SYS/AUTOEXEC.BAT to `*.CB_` (never overwriting the first
  pristine backup), and adds an idempotent, clearly-marked managed boot block
  (SET CASTALIA_HOME + CBOOT.EXE). **Uninstall** restores the pristine backups
  (or surgically strips just the managed block) and removes the tree, refusing
  dangerous roots. Portable core (only mkdir/enumerate differ per platform),
  built as INSTALL.EXE (DOS) and `build/install` (host), with a host round-trip
  test suite (`tests/test_install.c`). Remaining: first-run hardware report;
  upgrade/repair/portable modes.

## Phase 4 — Polish & hardware hardening
- ✅ **Linear-framebuffer VESA fast path** (`vesa.c`): when a mode advertises a
  VBE2 LFB (attribute bit 7 + PhysBasePtr), the backend sets the mode with the
  LFB bit and DPMI-maps the framebuffer (`dpmi_map_physical`), presenting with a
  straight per-scanline write and no bank switching; it falls back transparently
  to the banked window path (never removed). System Info shows
  `vesa-linear`/`vesa-banked`. DOS-only; validated on emulator/hardware.
- ✅ **16-color and 256-color theme pipelines** (`gfx_palette.c`, `sh_theme.c`):
  an EGA-16 palette builder, a generic nearest-color matcher, and
  `gfx_quantize_colors` (host-tested) drive a theme color target selectable via
  `[Theme] Colors` in `CASTALIA.INI`. A 16-color theme renders the desktop and
  window chrome within the EGA palette (verified: a 16-color desktop uses the
  EGA-16 colors for 96.6% of pixels). The realtime 8bpp present keeps the
  search-free 3-3-2 packer for the P2 frame budget. Remaining: low-color icon
  asset tiers (the icon-pack pipeline below can bake a 16/256-color tier via
  `gfx_quantize_colors`).
- ✅ Optional sound behind `include/castalia/snd.h`: a **null host driver**, a
  **DOS PC-speaker driver** (8253 timer 2 + port 61h), **and a Sound
  Blaster-class DAC path** (`snd_blaster.c`) — detected from the BLASTER
  variable (host-tested parser) + a DSP reset handshake, playing 8-bit PCM cues
  via 8237 single-cycle DMA, with transparent speaker fallback. System Info
  reports the active device. Remaining: Crystal/ESS, streaming audio.
- ✅ **Repaint cost pass** (`src/gfx/gfx_draw.c`, `src/gfx/gfx_blit.c`,
  `src/apps/app_clock.c`, `src/platform/host/plat_host.c`): profiled an
  ordinary desktop and cut it from 999M to 176M instructions over the same
  120 frames. Three findings, in increasing order of how much they mattered:
  `gfx_fill_rect` and `gfx_blit` re-read their loop bound from the stack on
  every pixel (CColor is an unsigned int, CRect is made of ints, so a pixel
  store may alias the bound) -- hoisting the bounds into locals fixed both and
  let the opaque blit become a per-row `memmove`. The Clock repainted its
  whole 484x356 window sixty times a second to move a second hand that only
  changes once a second; it now repaints when the second changes, and only
  the face and the readout, taking an idle desktop from 1.2M filled pixels
  per frame to zero. And `plat_ticks_ms` on the host was built on `clock()`,
  which is CPU time -- a loop that sleeps burns no CPU, so uptime ran slow,
  double-click timing measured the wrong thing, and elapsed-time samples read
  zero; it is wall time now, which is also what the DOS backend has always
  returned. Verified by `--clock-tick-demo`: real seconds pass with the
  partial path doing the work, then a forced full repaint is diffed against
  it pixel by pixel and must differ by zero -- a check confirmed sensitive by
  shrinking the invalidated face rect and watching it report ~100 differing
  pixels.
- ✅ **Animated-window repaint budgets** (`src/apps/app_media.c`,
  `src/apps/app_console.c`): the same finding as the Clock, in the other two
  windows that animate. The Media Player repainted its whole window every
  frame -- 620,579 filled pixels, playing or not -- to move a strip of
  visualizer bars; it now repaints the display strip (LCD through seek bar)
  and nothing else, 95,858 pixels while playing and **zero** when stopped
  with the bars settled and a title short enough to sit still. The Console
  repainted a 600x400 window to blink a six-pixel caret: 600,462 pixels a
  blink, now 633. `--repaint-demo` pins the media half down by taking two
  FULL repaints one tick apart and requiring every moved pixel to lie inside
  the strip -- both images come from full repaints, so the tick between them
  cannot make the comparison lie, and claiming a strip half the real height
  makes it fail. The console half is deliberately NOT asserted: the repaint
  region merges nearby rectangles, so a caret-sized error in the invalidated
  rect is absorbed before it reaches the screen, and a check that cannot fail
  is not a check.
- ✅ **Wallpaper build cost** (`src/shell/sh_desktop.c`): the factory landscape
  is drawn once into the background cache, but "once" means at every boot and
  every theme change, over every pixel of the screen -- the part of startup the
  target machine feels. Three per-pixel costs removed, with the result verified
  pixel-for-pixel identical against the previous build across the whole 800x600
  screen. The hill shading mixed a fresh colour per pixel when the shade only
  takes 201 distinct values, so the ramp is built once per layer and the inner
  loop is a table read stepping down a column. The cloud/sun glow did two
  integer DIVIDES per pixel for a falloff term that separates by axis, so it is
  precomputed one value per column and one per row. And the vignette divided
  once per pixel -- nearly half a million divides -- for a shade factor that
  takes 101 values and is monotone in the squared distance; it now walks the
  101 step boundaries and uses each distance for the pixel on both sides of the
  centre column, exactly and with no approximation. Instruction count for the
  landscape fell from ~45M to ~23.6M; startup measured 14 ms -> 11-12 ms on the
  host. (Instruction count understates the divides: a div is one instruction
  and about forty cycles, so the vignette work is worth more on a 386 than its
  Ir delta suggests.)
- ✅ **Two algorithmic fixes in the primitives** (`src/gfx/gfx_draw.c`):
  `gfx_fill_circle` found its half-width by scanning up from zero on every
  row, which is O(r) multiplies a row and O(r^2) for the circle -- and the
  clock face draws four discs a second. The half-width only shrinks as the
  row leaves the centre and the halves mirror, so carrying it down makes the
  circle O(r): measured 135 ms -> 3 ms for twenty thousand r=85 discs, with
  the two algorithms compared row by row over radii 1..2000 (4,004,000 rows,
  zero mismatches). `gfx_drop_shadow` divided once per shadow pixel for a
  falloff that depends only on the distance band, so the divide moved into a
  table of at most GFX_SHADOW_MAX entries; every glossy window carries a
  shadow and every drag frame redraws it. Both verified pixel-identical on
  four full screens.
- ✅ **Compression** (`src/apps/lzss_core.c`, `src/apps/cz_file.c`): LZSS with a
  4 KB window and 3..18 byte matches -- the era's own shape, chosen because
  DECODING is what has to be fast on the target and this decodes with no
  tables, no allocation and one branch per token. Match search is a hash chain
  with a bounded walk, so repetitive input cannot make it quadratic. Offsets
  point backwards into the output, so decoding needs no window buffer of its
  own; the match copy is byte-at-a-time on purpose, because an overlapping
  match is how a repeated run is encoded. Hermetically tested
  (`tests/test_lzss.c`, 449 checks): round trips over runs, ordinary text,
  incompressible data, sizes 1..30000, matches at both ends, the longest
  encodable match; and -- as much the point -- a decoder that REFUSES corrupt
  input rather than following it, including 400 trials of random single-byte
  damage that must never overrun the buffer. The `.CZ` container carries the
  original size and name (8.3 means `NOTES.TXT` compresses to `NOTES.CZ`, so
  the name has to travel inside) and its header is rejected if it claims more
  data than the file holds. `cz_file.c` adds the two decisions that only exist
  once a filesystem does: a size cap, since both buffers are resident at once
  on a 4 MB machine, and storing verbatim when compression would grow a file.
  Wired to the File Manager's right-click menu, and to opening one: a
  double-click on a `.CZ` gives the file back rather than a hex dump of the
  container. Verified end to end
  (`--cz-demo`): compress a real file, delete the original, restore it, compare
  the bytes, refuse a damaged archive -- and then do the compression again
  through the actual context-menu item, checking both that the archive appeared
  and that the original survived, since a mis-aimed click would land on Delete;
  then open the archive from the list and check the file came back.
- ✅ **Last-known-good configuration** (`src/cfg/lastgood.c`, wired in
  `src/main.c`): `make abuse` proves a corrupt `CASTALIA.INI` cannot stop the
  shell coming up; without this, what it comes up ON is defaults, so every
  setting the user chose is gone because one file got a bad write. A session
  that boots and runs cleanly now keeps its config aside as `CASTALIA.BAK`, and
  a boot that finds the live file unusable restores that instead of falling
  back to defaults. The judgement -- when to restore, when to promote -- is a
  pure pair of functions with every one of the sixteen boot cases and eight
  promote cases walked in `tests/test_lastgood.c`, asserting two invariants
  over the whole table: a valid live config is ALWAYS used (a crash is not
  evidence the config caused it), and an unclean session NEVER promotes.
  Verified end to end by three checks in `make abuse`, each with a negative
  control. The validity probe is stricter than it looks and had to be: an
  earlier version accepted any file with a `[section]` in it, which three
  thousand random bytes reliably contain -- so the corrupt config was used AND
  promoted over the known-good copy, the exact outcome the mechanism exists to
  prevent. It now requires a section this program actually writes, holding at
  least one key.
- ✅ **The desktop icons are reachable with the keyboard** (`icon_nav.c`,
  `sh_desktop.c`, `tests/test_iconnav.c`): the last place the keyboard could
  not go. This Machine, Documents, the Control Center, the Log Viewer, the
  Media Player, the Clock and the Recycle Bin could be reached by clicking and
  in no other way -- `g_sel_icon` was only ever set by a mouse press -- on a
  system whose own hardware verification was carried out with a keyboard alone.
  Arrows now walk them and Enter opens one. Where an arrow GOES is a pure
  module, because the icons are draggable and their positions persist: after
  somebody rearranges their desktop, "the next icon" has to mean the next one
  on the screen, and the order they sit in the array says nothing about that.
  Candidates are ranked by distance along the direction of travel plus a
  heavily weighted perpendicular offset, so a far icon on the same row beats a
  nearer one below it -- which is what an arrow key means, and what a plain
  nearest-centre metric gets wrong. Edges stop rather than wrapping. Tested
  against desktops laid out by hand, including a walk that must return to where
  it started, since nothing in the single-step checks would catch an asymmetric
  metric. The routing check is the one that stops this being a regression: with
  a window focused the arrows must belong to the WINDOW, or every list and text
  field in the system would lose its arrow keys the moment the desktop had a
  selection. Three mutations, all caught.
- ✅ **Byte order was decided in five places** (`src/sys/sys_le.c`,
  `tests/test_le.c`): after tripping over the same duplication three times in
  one session -- menu stepping, the calendar, the double-click window -- the
  fourth was found by looking for it rather than by hitting it. Normalising
  every static function body in `src/` and grouping the identical ones turned
  up **fifteen bodies written twice or more**, and the largest cluster was
  little-endian byte access: `rd32`/`wr32`/`rd16`/`wr16` in `capp.c`,
  `put_le32`/`put_le16` in `gfx_bmp.c`, `rd32`/`rd16` in `wav.c`,
  `car_get32`/`car_put32` in `car_core.c`, `cz_get32`/`cz_put32` in
  `lzss_core.c`. Same code, five names, character for character.

  Duplication is the small problem. The large one is what this code decides:
  **every on-disk format this system owns is little-endian on purpose**, so a
  file written on the DOS target opens on a modern machine and the reverse.
  Add-on packages, bitmaps, WAV audio, .CAR archives and .CZ files each went
  through their own copy of that invariant.

  One copy now, and tested against **literal bytes rather than a round trip**:
  a big-endian implementation round-trips perfectly and writes files nothing
  else can read, so only "byte 0 is the low one" can tell them apart. Both
  big-endian mutations fail 30 checks; a write that spills a fifth byte fails
  22. A fourth mutation -- sign-extending the top byte -- turned out not to be
  a defect at all, since the cast to `cu32` before a 24-bit shift discards it,
  which is worth recording as a non-finding rather than as a coverage gap.

  Checked first that none of this is on a hot path: every caller uses these
  for headers and directory records, never per pixel or per sample, so being
  real functions rather than inlined statics costs nothing measurable.

  **Still duplicated, and worse than it looks:** `app_fileman.c` and
  `app_console.c` each carry their own `path_join` and `path_parent` while
  `ui_path_join`/`ui_path_up` already exist AND are tested by
  `tests/test_path.c`, and `base_name` exists four times over (five, counting
  the copy the Hex Viewer added this session). Next.
- ✅ **Paths were decided in seven places** (`src/ui/ui_path.c`,
  `tests/test_path.c`, `--nav-demo`): the rest of the sweep above.

  `base_name` was the easy half -- five identical copies (`cz_base`,
  `base_name` twice, `pr_base`, and an inline loop in the Hex Viewer) collapse
  into `ui_path_base` with nothing to decide.

  The other half looked identical and was not, which is the interesting part.
  The File Manager and the Console each carried a `path_join`/`path_parent`
  pair, character for character the same as each other, while
  `ui_path_join`/`ui_path_up` sat one directory away already tested. The
  obvious move -- delete both copies, call the tested pair -- **would have
  regressed the address bar**. Both shells fall back to `"."` when
  `CASTALIA_HOME` is unset, and there the two behaviours genuinely differ:
  `ui_path_up` treats `"."` as a root and REFUSES, which is right for a file
  dialog (a picker must not wander above where it was opened) and wrong for a
  shell, where `cd ..` is an ordinary thing to type. So this is two named
  functions, `ui_path_up` and `ui_path_parent_rel`, rather than one with a
  flag: they are two policies that a duplicate-finder cannot tell apart.
  `ui_path_join` did absorb the `"."` collapse, since a leading `"./"` that
  means nothing is wrong for every caller including the dialog.

  Six mutations on the shared code, all caught: `"."` no longer climbing to
  `".."` (1 failure), a trailing separator kept (1), the root guard removed
  (2), backslash no longer a separator (1), the `"."` collapse removed (3),
  and the NULL guard removed -- which is checked by **exit code 139 rather
  than by a failure count**, because a segfault reports zero failures and
  reads as a pass.

  Then the finding that mattered. Breaking `ui_path_parent_rel` outright --
  making it return `"ZZZ"` -- **failed nothing**. Thirty-one demo scenes and
  every unit check in the tree, and not one walked either browser up a level;
  the gap predated the de-duplication, since the copies were equally
  uncovered. A screenshot cannot close it either: two folders holding the
  same names draw identically, so a navigation that quietly went nowhere
  looks exactly like one that worked. `--nav-demo` checks the path each app
  ends up ON, through both of the File Manager's routes (the `".."` row and
  Backspace) and the Console's `cd ..`, and checks that the two agree
  string-for-string -- which is the invariant the shared helper buys. The
  same mutation now fails three of its nine checks, and removing the `"."`
  collapse fails a fourth.
- ✅ **Where this system keeps its files was decided forty-one times**
  (`src/sys/sys_home.c`, `tests/test_home.c`, `ui_path_default_dir`,
  `office_docs_dir`/`office_doc_path`): the sweep kept going, and this was the
  largest cluster in the tree -- larger than byte order, larger than paths.

  Every app, the shell, the file dialog and every demo scene did its own
  `getenv("CASTALIA_HOME")` followed by its own
  `if (home == NULL || home[0] == '\0') { home = "."; }`. Forty-one copies of
  one sentence. They agreed only because nobody had changed one.

  The fallback is not an obvious default, which is why it was worth naming.
  An unset `CASTALIA_HOME` meaning "the current directory" is exactly what
  lets `tools/run_demos.sh` and the abuse harness point the whole desktop at a
  throwaway folder; an absolute default would send those runs into a real
  installation while still reporting green. That is now one line with a test
  on it, and the mutation that makes it absolute fails three checks.

  **Two callers deliberately do NOT use it.** CBOOT and the installer run
  before the desktop exists and must find the INSTALLED tree rather than
  whatever folder they were started from, so they fall back to `C:\CASTALIA`.
  Neither is linked against `sys_*` at all, which is what keeps the two
  policies from being mistaken for one. Checked before touching them rather
  than after.

  Three more near-copies fell out on the way. The Spreadsheet and the Writer
  had a byte-identical `docs_dir`/`doc_path` pair each, and both `save`
  functions carried a *third* inline copy of "create DOCS first" -- five
  copies of one folder. The Theme Editor had the same `doc_path` rule again
  with `THEMES` substituted. The shared rule is `ui_path_default_dir`: place a
  name in a default folder unless it already says where it goes. The direction
  is what the checks are about -- reversing it would take a file the user
  picked from elsewhere and write the app's copy into DOCS instead, leaving
  the original untouched and the edit apparently lost.

  Six mutations, all caught: an absolute fallback (3 failures), a cached
  first answer (8), the doubled-separator guard removed (2), the no-subpath
  guard removed (2), the default-folder direction reversed (2), and only
  absolute names treated as located (2).

  Two honest limits, recorded rather than smoothed over. First, reversing the
  direction only fails on the "name carries a folder" case, because
  `ui_path_join` already handles absolute and drive-qualified names itself --
  that one case IS the whole value `ui_path_default_dir` adds over a plain
  join. Second, the bare-name branch is not reached by the three apps' own
  menus at all: every save in them routes through `ui_file_dialog`, which
  returns a complete path. It is reachable only when a dialog is opened with a
  NULL directory and `CASTALIA_HOME` is unset, so the unit tests are its only
  coverage and no demo scene was invented to pretend otherwise.
- ✅ **The video mode list, and why the desktop is the size it is**
  (`src/gfx/vbe_report.c`, `plat_video_modes`, `vesa_enum_modes`,
  `tests/test_vbe.c`, `--sysinfo-demo`): the last open piece of the System
  Information roadmap line.

  "Video: 640x480, 8 bpp" means two completely different things depending on
  what the card could have done, and the report had no way to say which. On a
  machine whose BIOS offers 1024x768 that line is a bug report; on a card that
  offers nothing better it is just the machine. Same eight words either way.

  So the report now lists what the hardware says it can set. Three decisions,
  all of them the opposite of what the mode PICKER does:

  - Depths this system cannot drive are **kept, and marked**. The picker
    discards them; the report must not. A card offering 800x600 only at 15bpp
    is precisely why a desktop fell back to 640x480, and a list filtered down
    to what worked would show 640x480 and never mention 800x600 was there.
  - Duplicates on (w, h, bpp) collapse, keeping the linear-framebuffer entry,
    because that is the one `vbe_pick` would take. Showing both would offer a
    choice the user does not have.
  - When the list does not fit, the **smallest** modes are dropped. That is the
    end that explains nothing; truncating by arrival order instead would make
    the report depend on the sequence the BIOS happened to store its list in.

  The DOS walk that reads the list is now `vesa_enum_modes`, called by both the
  chooser and the report -- one walk, two policies, rather than two walks that
  drift. `VESA_MAX_MODES` moved into `vesa.h` because the array size is part of
  that contract now.

  Four mutations on the tidier, all caught: sorting that ignores depth (9
  failures), de-duplication that keeps the banked entry (1), a failed BIOS read
  printed as a 0x0 mode (2), and truncation by arrival order (3).

  Two things came out of looking at the output rather than at the checks. The
  first draft reported "... and 2 smaller mode(s) not shown" when nothing was
  truncated at all -- it was counting collapsed duplicates as hidden modes, so
  the count had to come from the tidied total rather than the raw one. And it
  marked TWO rows as the current mode, because it matched on width and height
  but not depth.

  The third came from a mutation. Breaking the sort entirely failed no demo,
  because the host backend was handing over an already-sorted list -- the check
  passed for the wrong reason. The host now reports its modes deliberately
  jumbled, and repeats the current size so the de-duplication is exercised too;
  the same mutation now fails the scene.

  One honest limit: the current-mode marker never fires on the host, because
  the host presenter runs 32bpp and that is not a VESA mode at all. That is
  correct rather than a gap -- there is no mode to mark -- and the marker
  itself is covered by unit test.

  Still to confirm on the target: the mode list a real card reports, which is
  the one thing this environment cannot produce.
- ✅ **Nineteen demo scenes had never run** (`tools/check_demos_listed.sh`,
  `--dual-demo`): the worst kind of gap, because it looks exactly like
  coverage.

  Going to close out the File Manager's roadmap line -- two-pane commander
  view, listed as not started -- turned up that it had been fully implemented
  for some time: toolbar button, Tab between panes, drag-and-drop across them,
  its own paint path. So the roadmap was stale. The habit of breaking a thing
  before trusting it is what turned up the real problem: **two-pane mode could
  be disabled outright and every check in the repository still passed.**

  There was a `--dual-demo` scene. It asserted nothing at all, and it was not
  listed in `tools/run_demos.sh`, so it had never been executed. Comparing the
  flags `src/main.c` parses against the ones the harness drives found
  **nineteen** scenes in that state -- and three of them (`--capp-demo`,
  `--clip-demo`, `--vdesk-demo`) had working assertions that had simply never
  been run. `--capp-demo` is the one that proves package code executes behind
  the add-on ABI.

  `make demos` can only ever be as complete as its list, and a missing entry
  is invisible by construction: the harness reports "no mismatches" across the
  scenes it knows about and says nothing about the ones it does not. So the
  list is now checked. `check_demos_listed.sh` fails if a scene exists that no
  harness runs, with two named exception groups rather than a pattern -- the
  `-keep` screenshot variants (each of which must have its asserting sibling
  in the gate, or it is not an exception but two holes), and the small
  hand-driving helpers. Named, so that a real scene whose name happens not to
  contain "demo" fails the check instead of slipping past it.

  All seventeen runnable orphans are now in the gate: **49 scenes, up from
  32**. Fifteen new real checks, and thirteen scenes that at least prove they
  still run. `--dual-demo` was rewritten from zero assertions to ten, around
  the property that actually matters and that a screenshot cannot see: the two
  panes are two INDEPENDENT places. Navigating the right pane must move the
  right pane and leave the left one exactly where it was -- two panes showing
  the same folder and two panes that are the same pane drawn twice are the
  same picture. It also asks for the 2-Pane button by name rather than
  clicking the hard-coded pixel offset it used to carry, which would have
  started pressing a different command the moment the toolbar gained an entry.

  Two mutations, both caught now and neither caught before: the 2-Pane button
  doing nothing (4 failures), and input always acting on the left pane (2 --
  and it shows the panes swapping roles, which is the property itself).

  Also removed a ternary in the single-pane paint path whose two arms were the
  same value.
- ✅ **The memory gauge read empty when memory was nearly gone**
  (`ui_meter_fill`, `ui_draw_meter`, `tests/test_ctrl.c`, `--taskman-demo`):
  a real defect, found by going after a fifth duplicate rather than by anything
  failing.

  Four windows drew "a proportional fill in a sunken well" and each did its own
  arithmetic: the About window's source statistics, the Task Manager's memory
  gauge, the Benchmark score bars, the boot splash. The Task Manager's was
  wrong, in the worst possible direction. It measures BYTES live against an
  8 MB budget across a 386-pixel bar and computed `(value * width) / max`. On a
  32-bit long that product passes 2^31 at **5.31 MB**, wraps negative, and the
  clamp underneath turns a negative fill into zero. The bar read EMPTY for the
  top third of its own range -- it said "nothing is using memory" at precisely
  the moment memory was nearly exhausted, which is the one reading a person
  opens that window to get.

  The fix is one shared control. What makes it more than a tidy-up is the
  argument type: **`cs32`, not `long`, and that is load-bearing.** `long` is 32
  bits on the DOS target and 64 on the host, so written with `long` the bug
  existed only on hardware nobody here has and no host test could ever reach
  it. The first version of this check passed against the broken formula for
  exactly that reason, which is how it was noticed. Fixing the width makes both
  platforms compute the same thing and makes the wrap reproducible here: the
  original formula now fails six checks, including a monotonicity sweep across
  the wrap point -- more memory must never draw less.

  The Benchmark's `if (sc > 200000L) { sc = 200000L; }` is gone too. That was a
  pre-clamp whose only job was keeping its own multiply bounded; a helper that
  cannot overflow retires it.

  The splash and the Benchmark keep their own gradients and take only the
  arithmetic -- a shared control should not flatten a deliberate look.

  `--taskman-demo` gained a check that what is PAINTED matches what the shared
  arithmetic says, by counting accent pixels along the bar. It catches a
  painter that goes back to computing its own fill six pixels off; the overflow
  itself is the unit test's job, since reaching 5.31 MB in a scene is not
  something a demo can arrange.

  **Then the same bug turned up three more times**, once the shape was known
  to look for: a byte count multiplied by a small constant. Disk Usage showed
  "N% of the total" as `(bytes * 100) / total`, and the Archive viewer showed
  "N% saved" twice -- once per member and once for the archive -- as
  `100 - (stored * 100) / original`. All three wrap at **20.5 MB**, and a
  photo folder, an install tree or a single video inside an archive passes
  that without trying. Against the original formula a half-full 200 MB tree
  reported **1%**, a quarter reported **0%**, and an archive that saved half
  its bytes claimed **99% saved**. Scaled to 1000 through the same helper and
  rounded as before, all three are right at any size; the original formula
  fails eleven checks in total.

  Worth naming as a class rather than three bugs: `long` is 64 bits on the
  host and 32 on the target, so every one of these was correct in every test
  that has ever run here and wrong on the machine the system is actually for.
  Nothing in the gate could have found them. What found them was reading the
  first one carefully enough to know what shape to grep for.

  A fourth turned up on the sweep after that: the **About window has its own
  memory gauge**, separate from both `ab_bar` and the Task Manager's, with the
  identical `live * width / budget` in bytes and the identical 5.31 MB wrap.
  Three copies of one widget and one of them was wrong twice over. It is the
  shared control now too.

  The sweep across the whole portable tree turned up 64 candidate sites, which
  is far too many for a lint. Almost all are pixel geometry -- `pixels + y *
  pitch` and circle radii -- bounded by the screen. The ones checked and left
  alone, so the next reader does not re-check them: `hist_bar` is fed KILOBYTES
  and milliseconds by both its callers (a few thousand against a height of a
  few dozen), and now says in a comment where its own limit is; the Media
  Player's seek math is bounded by track length; the sound driver's
  `rate * ms` is clamped immediately after and would need a 195-second beep.
  Attempting `-m32` to make the whole class reproducible was the first idea
  and does not work here -- no 32-bit multilib -- so the discipline is the
  `cs32` type on the shared helper rather than a second build.
- ✅ **The desktop background baked 27% faster** (`land_hills` in
  `sh_desktop.c`): a divide per pixel, and an instruction-count profile that
  said the opposite of the truth.

  Profiling the idle frame found nothing left to take -- it is 3,417
  instructions, where earlier work had left it. But the same run showed
  **startup is 34.6M instructions and 71% of that is baking the desktop
  background**, which is not a one-time cost at all: it re-runs on every theme
  change, wallpaper change and resolution change.

  Inside it, `land_hills` computed `t = ((y - crest) * 200) / span` once per
  pixel, and three hill layers cover most of the screen -- about **six hundred
  thousand divides** per bake. `y - crest` is never negative there (the start
  row is the larger of the crest and zero), so C's truncation is a floor and
  the value can be walked with an add and a conditional subtract instead. The
  lit crest colour, mixed identically for all eight hundred columns, is now
  mixed once.

  The measurement is the interesting part. **Instruction count went UP 1.5%**,
  because a divide is one instruction and the replacement is four. Wall clock
  went **down 27%** (655 ms to 480 ms for 500 full re-renders, three runs each,
  almost no variance), because a divide is one instruction and twenty to forty
  CYCLES on the machines this targets. Ir was not merely insensitive here, it
  pointed the wrong way; the change would have been rejected on that number.

  Verified by byte-for-byte comparison of the rendered screenshot, which is the
  only honest check for a pure-performance change: same MD5 before and after.

  `apply_vignette` was read at the same time and left alone -- it already
  steps `i*i` instead of multiplying, uses a threshold table instead of
  dividing, and halves the work by symmetry.
- ✅ **A stretched wallpaper redraws 74% faster** (`gfx_scale_map`,
  `tests/test_blit.c`): the divide-per-pixel above turned out to be a shape
  worth grepping for, not a one-off.

  Four nearest-neighbour scalers each computed the source column inside their
  inner loop -- the wallpaper stretch, the image viewer, the Control Center's
  wallpaper preview, and `pc_scale` (the thumbnail resampler). The column only
  depends on the column, so it is the same on every row: the wallpaper stretch
  did **480,000 divides where 800 would do**. One tested function fills the
  table now, and the four callers each keep their own.

  Measured on the path where the number is large: a stretched wallpaper
  re-renders in **140 ms instead of 532** for 500 full redraws, three runs each
  with almost no variance. The thumbnail path is honestly no faster in any way
  this host can measure -- a 30x30 thumbnail is 900 divides against 30, which
  is lost in the noise -- and that is worth saying rather than quoting the
  ratio and implying it mattered everywhere.

  Two process notes, both about not fooling oneself:

  The first byte-comparison said `--thumbs-demo` DIFFERED, across 93,000
  pixels. It was the experiment that was wrong, not the code: all six runs
  shared one CASTALIA_HOME, and `tools/run_demos.sh` rebuilds its world before
  EVERY scene precisely because scenes write into it. With a fresh world per
  run, every scene is identical. Establishing that two runs of the SAME build
  match, before attributing any difference to the change, is what separated
  those.

  The second: the first attempt to measure the stretch path showed no win at
  all, because `WallpaperMode=1` is CENTER, not STRETCH -- the enum runs
  CENTER=1, TILE=2, STRETCH=3. The wrong path was being measured. All three
  modes are byte-identical before and after.
- ✅ **The banked VESA presenter divided once per pixel** (`src/gfx/vbe_bank.c`,
  `tests/test_vbe.c`): the last site the sweep turned up, and the one in the
  hottest place.

  A card without a linear framebuffer shows the screen through one small
  window that must be re-pointed as the write position moves past it. Turning
  an absolute byte offset into a bank and a window offset is a divide and a
  multiply, and `vesa_present_rect` did **both, per pixel** -- 480,000 of each
  for one 800x600 present, EVERY FRAME, on the fallback path taken by exactly
  the cards least able to afford it. A divide is twenty to forty cycles on a
  486.

  It never needed computing after the first pixel of a row: the write position
  advances by a fixed number of bytes, so the bank and offset carry forward
  with an add and a compare. `set_bank` still returns immediately when the
  bank has not moved, so the BIOS is called only on a real boundary crossing,
  exactly as before.

  Split the same way `vbe_pick.c` is: the BIOS call that re-points the window
  stays in `src/platform/dos`, and the arithmetic that has to be RIGHT lives
  where a test can reach it. The check is not that stepping is fast but that
  it lands in **exactly** the same place as dividing -- verified over a full
  frame's worth of offsets at five granularities real cards report (64K, 32K,
  16K, 4K, 1K), for one-byte and two-byte steps, for whole-scanline jumps that
  cross several banks at once, and backwards.

  **Not measured, and cannot be from here.** This is DOS-only code on a path
  that needs a card without a linear framebuffer; nothing in this environment
  executes it. The claim is that the arithmetic is identical -- which IS
  tested -- and that an add beats a divide, which is arithmetic rather than
  measurement. It goes on the list of things to confirm on the target, beside
  the VESA mode list.
- ✅ **Solitaire's spades had diamonds in the corner** (`SOL_SMALL_PIP` in
  `app_solitaire.c`): found by opening the apps one at a time and looking at
  them, which is the only way this class of thing is ever found.

  The suit pips are drawn from primitives -- a triangle, two circles and a
  stem -- and they read beautifully at the size of the CENTRAL pip. At corner
  size they do not. The lobes swallow the triangle, the stem lands inside
  them, and the spade comes out a featureless blob that on screen is
  indistinguishable from the diamond. So the eight of spades had a spade in
  the middle and what looked like a diamond in both corners, which is worse
  than having no corner pip at all: the corner is what a person reads when the
  cards are overlapped, and in Klondike they always are.

  Raising the radius does not fix it -- checked at 4, 5 and 6. At 4 it is
  still a blob, at 5 a mushroom, and only by 6 does it read, by which point it
  crowds the rank. A vector shape built for one size does not scale down; at
  seven pixels you draw the pixels. Each suit now keeps the one feature that
  identifies it: the spade a point on top, the club a flat cap, the heart a
  notch, the diamond neither.

  Verified by rendering all four side by side, because "these four glyphs are
  telling apart" is a visual property and a test asserting the four byte
  arrays differ would not have caught the original defect anyway -- the
  original data was fine, it was the RASTERISATION that collapsed.
- ✅ **The whole test harness was reading its evidence from a broken fallback**
  (`main.c` first-boot folders, `tools/run_demos.sh`, `tools/run_abuse.sh`):
  the largest finding of the sweep, and it came out of a one-line bug fix.

  The visual pass turned up something small: the Log Viewer said "could not
  open the log file". The log lives in `LOGS\`, and `sys_log_init` only
  `fopen`s its path -- it does not create the folder. On a first boot, or an
  install that stopped half way, there is no LOGS folder and **the session log
  was silently never written.** Silently is what makes it matter: the crash
  screen tells the reader to go and read that log, and the Log Viewer opens on
  it. Advice pointing at a file that never existed is worse than no advice.
  Two `plat_mkdir` calls before `sys_log_init` fix it; both are plain mkdir
  underneath and need nothing initialised.

  **That one-line fix turned the entire gate red: 49 of 49 demo scenes, and 12
  of 12 hostile worlds, all reporting "produced no output".**

  A scene reports through `SYS_LOGI`. Looking at `sys_log.c` shows why: log
  lines reach stderr ONLY when the log file cannot be opened -- a deliberate
  "nothing is silently lost" fallback, whose own comment names the case ("the
  configured path could not be opened, e.g. its directory does not exist").
  The throwaway worlds both harnesses build have no LOGS folder and nothing
  created one. So **every `(OK)` either harness has ever counted arrived
  through a fallback that existed only because logging was broken.** Fix the
  logging and the evidence vanishes.

  Nothing was actually wrong with the scenes, and nothing had ever been. But
  the verification apparatus was resting on an accident, and would have
  collapsed the first time anyone fixed the log -- which is precisely what
  happened. Both harnesses now read the captured output AND the log file, so
  it no longer matters which one carries the line.

  The guard that caught it was already there: `run_demos.sh` refuses a scene
  that prints nothing at all, on the stated grounds that "no output is exactly
  how a broken harness looks like a green one". It was written against this
  failure mode and it worked. Verified after the repair that a real MISMATCH
  still fails the run, because a harness reading two sources could just as
  easily pass on neither.

  Also corrected the abuse summary, which counted `cases + 3` where the 3 was
  a bare literal and the real number was 5 -- it had been under-reporting its
  own check count.
- ✅ **An unknown driver flag was accepted in silence** (`main.c`): found by
  making the mistake it protects against.

  A screenshot pass was run with `--open-paint`, `--open-mines` and
  `--open-control`. None of those flags exists. The binary ran three plain
  desktops, said nothing about any of them, and exited 0 -- so three panels of
  a contact sheet showed an empty desktop and looked, for a moment, like three
  applications that had stopped opening.

  That is the same shape as the demo-list gap and the harness-fallback one:
  something quietly not happening reads exactly like something that worked.
  This driver runs every scene in the gate, so a mistyped flag means the scene
  does not happen, and a scene that does not happen reports no mismatch. An
  unrecognised argument now prints what it was and exits 2.

  Checked first that nothing already depends on the old silence: every flag
  the scripts pass to this binary is real. The twenty that a naive grep calls
  unknown all belong to other programs -- `gh`, chromium, ImageMagick -- or
  are comment rules. The full gate then passed unchanged, which is a second
  proof of the same thing across some fifty flag combinations.

  Two things checked here and found NOT to be defects, recorded so they are
  not re-investigated: the `--font` handler reads `argv[i]` after an `i++`, so
  it does compare the value rather than the flag name; and Solitaire's opening
  deal is a correct Klondike, which merely looks like a single row of cards at
  small scale because the face-down piles overlap tightly.
- ✅ **Nothing here could see a memory error** (`tools/run_memcheck.sh`,
  `tools/check_memcheck_control.sh`, `make memcheck`).

  Every gate in this tree proves BEHAVIOUR. The unit tests prove the logic,
  the demos prove the wiring, the abuse worlds prove nothing hostile brings
  the shell down. None of them can see an invalid read, a write one byte past
  a buffer, or a branch on an uninitialised value -- a program does all three
  and still produces exactly the right answer, right up until the day it does
  not. This is C89 with hand-rolled parsers for four file formats and its own
  allocator, which is precisely the shape where those live, and it had no such
  check at all.

  Thirteen runs under valgrind: the unit tests (every pure module), the scenes
  that read a format this project wrote itself (.CAR, .CZ, BMP, WAV), the
  directory walk, the network stack, and four deliberately malformed worlds.
  Sixteen seconds, which takes the gate from thirteen to twenty-nine.

  **It reports zero, and the zero is real.** That was checked rather than
  assumed: the first attempt to plant a defect edited the wrong function
  signature, so the "0 errors" that came back proved nothing at all --
  exactly the kind of empty result this is meant to prevent.
  `check_memcheck_control.sh` now plants a one-byte overrun in the frame loop,
  requires the memcheck run to fail, and restores the tree on any exit. It
  fails 12 of the 13 runs; the thirteenth is the unit tests, which never call
  the mutated function, and that is the right answer rather than a gap.

  The result: no invalid access and no uninitialised read anywhere in those
  thirteen. A genuine negative, worth recording as one.
- ✅ **Nothing checked that a window gives its memory back** (`--mem-check`):
  the other half of the same blind spot.

  The idle ceiling was measured on a desktop with nothing open, which says
  nothing about what a SESSION costs. On an 8 MB budget a window that keeps a
  few kilobytes per open is fatal after enough use, and it was invisible in
  every check here: the app works, its scene passes, and the idle figure is
  taken before anything is opened. Valgrind does not see it either -- memory
  that is still reachable and never freed is not an error to memcheck, it is
  just memory.

  Five windows are now opened and closed three times over, and the figure
  after each cycle is required to settle. The invariant is deliberately not
  "back to the original baseline": the icon pack, the desktop bake and the
  thumbnail cache are allocated once on purpose, so the first cycle
  legitimately ends higher. What must hold is that the SECOND and THIRD agree
  exactly -- once the one-time costs are paid, an open and a close are free.

  It runs **every application with a no-argument opener -- all twenty-four**,
  not a sample. A leak lives in exactly one app, and checking five of
  twenty-four is a four-in-five chance of looking in the wrong place.

  It reports **3767, 3767, 3767 KB**, which is also the idle figure to the
  kilobyte: every app in the system currently opens and closes at zero net
  cost. Planting a 2 KB leak in one app's open makes it read 3769, 3771, 3773
  and fail, so the three matching numbers mean what they appear to.

  One more thing had to be added before they did. Three equal numbers look
  exactly the same when nothing opened at all -- a cycle that opens no windows
  leaks nothing and would have passed for the worst possible reason. The
  windows are counted and their cost reported while they are up: **24 windows,
  4564 KB**, which is 797 KB above the settled figure. That the check nearly
  shipped without this is the same trap found three times elsewhere today.
- ✅ **Six more compiler warnings, and the one real thing they found**
  (`WARN` in the Makefile): the build ran on `-Wall -Wextra` and nothing else.

  The tree turned out to be almost entirely clean under a stricter set, which
  is what makes the set affordable: `-Wstrict-prototypes` (an empty `()` in
  C89 is not "no arguments", it is "unchecked"), `-Wmissing-prototypes` (a
  non-static function nobody declared is either dead or wanted to be static),
  `-Wredundant-decls`, `-Wnested-externs`, `-Wold-style-definition` and
  `-Wpointer-arith` (a GNU extension Watcom will not take). `-Wconversion` is
  already clean too, which incidentally confirms today's overflow findings were
  never a class the compiler could have caught.

  Four duplicate declarations went: `sh_glossy` was in both the public header
  and the internal one, and three apps re-declared their own entry points on
  top of `apps.h`. `sh_switch_draw` is static now -- it was exported and used
  only in its own file.

  The one that mattered was in `plat_host.c`, which **did not include its own
  header** and instead copied two of its three prototypes by hand. So the
  definitions were never checked against the declarations `main.c` calls
  through; a signature could have diverged silently, and the third hook had no
  local copy at all. Including the header is the fix, and is exactly the same
  "same decision in two places" that the de-duplication work has been removing
  all day -- here between a header and the file implementing it.

  `-Wshadow` is deliberately NOT in the set. Its only hits are inner blocks
  declaring their own loop index, which is ordinary C89 style; adding it would
  mean renaming seven variables to silence a warning about nothing.
- ✅ **The gate used one compiler** (`tools/check_second_compiler.sh`): gcc and
  clang do not diagnose the same things, and the difference is not cosmetic
  here -- several of clang's are about integer width, which is the distinction
  that matters most in a product targeting 32-bit from a 64-bit desk.

  A syntax-only pass with the other compiler costs three seconds rather than a
  second full build. It carries the clang-specific warnings this tree is
  already clean under: `-Wshorten-64-to-32`, `-Wconditional-uninitialized`,
  `-Wunreachable-code`, `-Wloop-analysis`, `-Wtautological-compare`. `-Wcomma`
  is left out; its only hit is `for (k = 0, x = 1; ...)`, which is what a
  for-initialiser is for.

  Worth stating plainly, because the temptation is to imply otherwise:
  **`-Wshorten-64-to-32` would NOT have caught today's overflow bugs.** Those
  were `long * int` products, correct at 64 bits and wrapping only at 32, with
  no narrowing conversion anywhere for a compiler to see. This guards the
  adjacent class, not that one.

  Establishing that took three attempts, and the first two were worthless in
  the same way: `-w` suppresses even a `-W` flag that follows it, so a scan
  written as `clang -w -Wshorten-64-to-32` reports zero warnings about
  anything. The fix was to compile a file with a deliberate narrowing in it
  and confirm the scan could see THAT before believing a zero from the real
  tree. Third time today that a clean result turned out to be measuring
  nothing.
- ✅ **The Calculator grew a memory and lost its blind spot** (`calc_core.c`,
  `tests/test_calc.c`, `--open-calc`): four-function, and the only app of
  twenty-four with no unit tests at all.

  The arithmetic was never where a calculator goes wrong. What goes wrong is
  the SEQUENCE -- an operator pressed twice, equals with nothing pending, a
  second decimal point, backspacing a number down to nothing, dividing by zero
  and then carrying on typing. Every one of those is a key a person really
  presses and not one of them was checked. The state machine is `calc_core.c`
  now, and `test_calc.c` presses all of them: `5 + - 3 =` is 2 and not 8,
  `2 + 3 = =` does not re-apply, `0.` survives as a string because no double
  can hold a trailing point, and an error state clears itself the moment you
  type rather than needing the window closed.

  Then the keys it was missing: **memory (MC / MR / MS / M+), square root,
  reciprocal and percent.** Square root is Newton's method rather than
  `<math.h>`, because the DOS build links no floating-point library beyond
  what the compiler emits and pulling one in for one button is a poor trade.
  Percent is percentage OF the pending left-hand side -- `200 + 10 %` is
  `200 + 20` -- which is what the key has meant on desk calculators since long
  before it was on a screen. `C` leaves memory alone, as every calculator ever
  made does.

  **The first layout shipped without an equals button.** Twenty-six keys were
  put into a 5x5 grid, which is twenty-five cells, and the one that did not
  fit was `=`; only the keyboard could finish a sum. It was caught by
  rendering the window and looking at it, not by any check -- the same lesson
  as the Solitaire pips. The grid is 5x6 now, with `=`, `+` and `0` each given
  adjacent cells so they read as the wide keys a calculator has, and
  `calc_press` returns CFALSE for a label it does not know so a key added to
  the table and never wired up fails loudly instead of looking dead.

  The unit tests cannot show that a keystroke reaches the machine at all, so
  `--open-calc` -- which typed `12+34=` and asserted nothing -- now requires
  the display to read 46, and is in the gate.

  **Correction to the entry above:** the Calculator was described there as
  "the only app of twenty-four with no unit tests". That was wrong, and was
  found out by actually counting rather than assuming. Sixteen apps keep their
  logic inside the window with no pure core: Solitaire (1,125 lines), Notepad,
  the Control Center, the Benchmark, About, the Theme Editor, Network, the
  Console, Help, the Log Viewer, Character Map, the Viewer, Welcome,
  Properties, the window helper and System Information. The Calculator was one
  of many, not the exception.
- ✅ **The rules of Klondike were unreachable** (`src/apps/sol_core.c`,
  `tests/test_sol.c`): the largest of those sixteen, and the one where being
  unreachable matters most.

  Which card may go on which IS the game, and it was decided inside an
  1,100-line window file. A solitaire that accepts an illegal move, or refuses
  a legal one, is broken in a way no screenshot shows: the board looks exactly
  the same either way, and the only symptom is a player quietly finding the
  game unwinnable.

  The predicates are pure now and pinned: a tableau takes a King on empty and
  otherwise the next rank DOWN in the opposite colour; a foundation takes an
  Ace on empty and otherwise the next rank UP in the SAME suit. Those two are
  opposite on both counts, which is exactly the pair a copy-paste gets wrong,
  so both directions of both are checked. A run moves as one only if every
  card is face up and they descend in strictly alternating colour -- including
  the case where the face-down card is the FIRST one, which is the one an
  off-by-one in the loop bound would miss.

  The deal is checked over **two hundred shuffles** for the property a shuffle
  is supposed to have and a broken one silently loses: every card present
  exactly once, none face up, and consecutive deals different -- a shuffle
  that returns the same order every time passes all the other checks. The same
  seed still deals the same game, which is what makes a bug in one
  reproducible.

  Deliberately NOT restructured: the selection, click routing, drawing and win
  cascade stay in the window. Moving those too would have meant rewriting a
  working game whose only end-to-end coverage is "drawn only", and the rules
  are where the value was. Verified the refactor changed nothing by comparing
  the opening deal byte for byte before and after.
- ✅ **Where the lines break** (`src/apps/wrap_core.c`, `tests/test_wrap.c`):
  the next of the sixteen, and the one where a bug is hardest to see.

  Word wrap is easy to write, easy to get subtly wrong, and impossible to
  notice when it is -- the text is all still there, it just breaks in the
  wrong place, or a row is skipped, or the caret lands one row off. It lived
  in the Notepad window.

  Pinned now: the break goes AFTER the space so the space stays on the row it
  ended; the LAST space that fits is chosen, not the first; a word longer than
  the row is cut hard rather than running off the edge; an EMPTY logical line
  is one visual row and not zero, because a blank line in a document is a
  blank line on the screen and swallowing it puts every following line a row
  too high; a trailing newline leaves an empty last row, which is where the
  caret goes when you press Enter at the end.

  Six mutations, and the interesting part is the three that did NOT count.
  Breaking before the space fails 7 checks and swallowing empty lines fails 5.
  Three others survived, and the first instinct -- that each was a coverage
  gap -- was wrong twice: searching for the space from `rs` rather than
  `rs + 1` is equivalent because the `sp > rs` guard rejects it either way,
  and `le > len` in the outer loop bound is equivalent because the enclosing
  `while (i <= len)` already ends the loop. Both were checked by compiling
  them and comparing output rather than by reasoning alone -- one of the two
  had been reasoned about incorrectly first.

  The third was real. Changing the wrap gate from `remain > cols` to
  `remain >= cols` makes a line that fits EXACTLY wrap anyway -- `"ab cd"` at
  width five becomes two rows. Every exact-fit case in the suite happened to
  contain no space, and the bug only bites when there is a space to break at,
  so all of them passed. That case is in the suite now and kills the mutation
  with 4 failures.

  Worth noting how close that came to being missed: the FIRST attempt at this
  mutation edited the ternary a line above the gate, which computes the same
  value when the two are equal and is therefore a no-op. It was recorded as
  "survived, therefore equivalent" and would have stayed that way if the two
  outputs had not been compiled and compared side by side.
- ✅ **The parsers, fed things no encoder would write** (`tests/test_fuzz.c`):
  this system reads four formats it wrote itself — `.CAR` archives, `.CZ`
  compressed files, `.CAPP` packages, its own BMPs — and every one is a
  hand-rolled parser pulling lengths and offsets out of a byte stream, which
  is the exact shape where a truncated file walks off the end of a buffer.
  The abuse harness covered one such case per format, by hand. This covers
  thousands: take something valid, damage it the ways files are really
  damaged (truncated anywhere, a bit flipped anywhere, a length field
  replaced with `0xFFFFFFFF`, the whole thing replaced with noise) and
  require a refusal rather than a crash, an over-read, or a success the
  parser cannot back up. The generator is seeded, so a failure is
  reproducible rather than a story about a run that once went wrong. It lives
  in the unit-test binary on purpose: `make memcheck` runs that binary under
  valgrind, so the fuzzing gets memory checking for free — and a read one
  byte past a buffer that lands on readable memory is invisible without it.

  **The finding is not the suite. It is that three of its four loops tested
  nothing, and each was found by counting rather than by reading the code.**

  The LZ loop passed a capacity larger than any output it could produce, so
  the decompressor never came within eighteen bytes of the limit and the
  overrun guard was never approached — deleting `if (out + len > cap)`
  entirely failed nothing at all. Measured both ways: at capacity 255 the
  mutation causes 0 overruns, at 64 it causes 142.

  The CAR loop passed `total = 32` where `car_size_for(3, 0) = 104` was
  needed, because `car_write_entry` returns the size of **one** entry and the
  largest of three of those is not the archive. Every archive was refused at
  the header before it was even damaged. Counted: 0 of 400 reached the entry
  parser; after the fix, 216.

  Having been caught twice, the third loop was counted before being believed —
  and it was the same story a third time. `.CAPP` is CRC-32'd over the whole
  image, so *every* damaged package is refused and the assertions inside the
  `== CE_OK` arm are unreachable: 0 of 400. That loop is kept, with the count
  written down beside it, because the CRC is checked **last** — every
  structural test in `capp_parse` really does run on damaged bytes, which is
  worth having under valgrind — but it proves only refusal.

  So a fourth loop earns the acceptance: damage, repair `total_size` if the
  damage truncated, then **recompute the CRC exactly as `capp_build` does**.
  A checksum proves an image is the one that was written, not that what was
  written made sense, and anyone who can damage a package can re-checksum it;
  past that point the structural checks are the only thing between a lying
  header and a read past the end. Now 211 of 400 are accepted, and an
  accepted image owes a promise — every section it reports lies inside the
  bytes it was given — which is checked by fetching each section through
  `capp_find_section` and touching its first and last byte, turning "the
  parser says it is in bounds" into "it is in bounds".

  Controlled, because a check never seen to fail is not yet a check: removing
  `s->size > len - s->offset` from `capp_parse` fails it with 9 bad, and
  removing the `s->offset > len` half of the offset bound fails it with 13.

  The acceptance count is itself a check now (`capp_ok > 50`), which is the
  only lasting fix for the mistake this entry is mostly about.
- ✅ **The hard-coded 126 the font layer warned about** (`src/apps/cmap_core.c`,
  `tests/test_cmap.c`, `app_charmap.c`): the Character Map's grid, extracted
  from the window — and a latent bug that the codebase had already predicted
  in writing.

  `gfx_font_range()` exists so callers follow the font instead of assuming it,
  and its own comment says why:

  > *"It is still asked per font, because the day a face arrives with more in
  > it is the day a hard-coded 126 somewhere else becomes a bug nobody is
  > looking for."*

  That hard-coded 126 was **one file away**, in the Character Map — the app
  that calls `gfx_font_range()` — whose key handler jumped the highlight with
  `if (ch >= 32 && ch <= 126) { cm->sel = ch; }`. The map's range comes from
  the font; the typing shortcut came from a constant. They agree today by
  coincidence, both being 32..126, so nothing misbehaves and **no test built
  on the real font could ever have caught it**: the only way it shows is to
  drive a range that is not the one the constant happens to name.

  Widen the font and every character above 126 becomes unreachable by typing
  while sitting visibly in the grid. Narrow it and typing selects a cell that
  is not on the map — which then indexes the highlight rectangle with a
  negative row and draws it outside the grid.

  The grid is now `cmap_core.c`: rows from a range, code ↔ cell both ways,
  movement, hit-testing, and the typed-character jump, with the range as a
  parameter everywhere and no constant anywhere. `tests/test_cmap.c` runs it
  at 32..126, at 32..255, at 32..90, at 48..57, at a single character, at a
  reversed range and at zero columns.

  Four mutations, all caught. Restoring the original `ch >= 32 && ch <= 126`
  fails 5 checks. Dropping the partial-last-row round-up — which is how the
  last fifteen characters of the font become unreachable — fails 20. Making
  movement clamp instead of refuse fails 1, and it is the interesting one:
  code 111 sits in the second-to-last row at a column where the last row has
  already run out, so pressing Down must not move at all. Removing the upper
  bound in `cmap_code_at` fails 6.

  Verified the refactor itself changed nothing: the Character Map rendered
  before and after is **byte-for-byte identical**. Rendering it was worth
  doing for another reason — the first screenshot showed the grid overflowing
  the window, a stray blue rectangle, and the status line outside the frame,
  all of which looked like three fresh bugs and were the window-open zoom
  animation caught at frame 6. At frame 40 the window is correct. The
  screenshot was nearly the evidence for a bug report about nothing.

  Also checked and clean, so it is written down rather than re-investigated:
  the Benchmark's `score = value * 1000L / ref` is the same shape as the four
  32-bit overflows found earlier in the memory gauges and percentages, but it
  does not overflow. Worked at the pathological limit — every `dt` clamped to
  1 ms — the largest `value * 1000L` is the text row's 376,000,000, comfortably
  inside a 32-bit `long`.
- ✅ **A folder over two gigabytes showed a negative size** (`fsize_core.c`,
  `tests/test_fsize.c`, `app_fileman.c`): the fifth bug of this exact shape,
  and the first one found by going looking for it rather than by tripping
  over it.

  The File Manager's status line summed the files in a folder into a `long`.
  That is 64 bits on every machine this is developed on and **32 bits on the
  machine it ships to**, so a folder holding more than 2 GB wrapped to a
  negative total on the target and on the target only. The display then made
  it worse instead of catching it: the "small enough to state exactly" test is
  `bytes < 10240`, which a negative number passes, so the status line printed
  a negative byte count.

  Measured, not reasoned — restoring the original addition makes the suite
  print what the shipped binary would have:

  ```
  "-1778384896 bytes in 3 files" == "more than 2047.9 MB in 3 files"
  "-2 bytes in 2 files"          == "more than 2047.9 MB in 2 files"
  ```

  Two gigabytes in one folder is not exotic on this hardware. FAT32 allows a
  single file of up to 4 GB, and a CD image beside a couple of archives
  reaches it easily.

  The total is a **`cs32`** now, which is exactly 32 bits on the host as well
  as on DOS, and that is the only reason a host test can see this at all —
  written with `long`, the arithmetic is correct on every machine a test can
  run on and wrong only where no test runs. The sum saturates rather than
  wraps and records that it did, so the label reads "more than 2047.9 MB",
  which is true, instead of a number that is false in either direction. It
  also stays saturated: a second wrap would come back **positive** and
  plausible, which is the version of this bug that would never be reported.

  Eleven checks fail on the original addition, including the two status
  strings above.

  Also checked while looking, and clean: `mach_kb_label` (the shared KB/MB
  formatter) does not overflow — `(kb % 1024) * 10` cannot exceed 10240 — and
  the Benchmark's `score = value * 1000L / ref` is the same shape but safe,
  since at the pathological limit of every `dt` clamped to 1 ms the largest
  product is the text row's 376,000,000.
- ✅ **Two of three is how a class of bug survives being found** (`ui_percent`,
  `app_fileman.c`): a percentage that wrapped negative, in the one place the
  previous sweep for exactly this bug did not look.

  `tests/test_ctrl.c` already carried a section headed *"the same primitive
  used as a PERCENTAGE"*, written when this overflow was found in **Disk
  Usage** and in the **Archive viewer**: both computed `(bytes * 100) / total`
  by hand, both wrapped at 21,474,836 bytes — 20.5 MB — and both were fixed by
  routing them through `ui_meter_fill`.

  It missed the third. The File Manager's *"compressed — N% smaller"* still
  read `(saved * 100L) / orig`, which wraps at the same 20.5 MB. Restoring
  that form makes the suite print what the target would have:

  ```
  ui_percent(75 MB, 100 MB)  ->   -6   (should be 75)
  ui_percent(21 MB,  42 MB)  ->  -47   (should be 50)
  ui_percent(30 MB,  60 MB)  ->  -18   (should be 50)
  ui_percent(100 MB, 200 MB) ->    9   (should be 50)
  ```

  A 100 MB file compressed to 25 MB would have reported **“−6% smaller”**.
  It is reachable on this target: `cz_compress_file` slurps the whole file, so
  it needs a well-specced machine, but 440BX boards commonly carried 64–256 MB
  and a ~25 MB log compresses well past the threshold.

  The lesson is not the arithmetic, which was already understood and already
  written down. It is that finding a bug in two places and fixing it in two
  places leaves the third, and nothing about the fix made the third easier to
  spot. So the percentage has a **name** now — `ui_percent(part, whole)`,
  which is `ui_meter_fill` with a width of 100 and not a second copy of the
  arithmetic — and a hand-rolled `* 100 /` is the thing that reads as wrong at
  a glance.

  Found by scanning the whole tree for the shape rather than by reading one
  file. The same scan cleared `hist_bar` (already guarded, with a comment
  naming this exact class), `app_props.c` and `app_taskman.c` (both take a
  remainder before multiplying, so bounded by construction), and
  `mach_kb_label`.
- ✅ **What the compiler could already see** (`ui_dialog.c`, `assoc.c`,
  `--dlg-exhaust`): two bugs found by turning on warnings this tree had never
  enabled, rather than by reading code.

  The sweep itself needed a control first, and it is worth recording why: the
  first run reported **zero hits for every flag**, because it was written
  `gcc -w -Wshadow ...` and `-w` suppresses a `-W` that follows it. That is
  the *third* time that exact mistake has been made here. It was caught by
  compiling a file with a deliberately shadowed variable and confirming the
  scan could see **that** — a scan that has never been shown to fire is not a
  scan. Run correctly: `-Wshadow` 18, `-Wfloat-equal` 5,
  `-Wduplicated-branches` 2, and zero for `-Wformat=2`, `-Wcast-align`,
  `-Wduplicated-cond`, `-Wlogical-op`, `-Wnull-dereference`,
  `-Wjump-misses-init`, `-Wswitch-default`.

  Both `-Wduplicated-branches` hits were real.

  **A leaked dialog, worst exactly where it is most likely.** `ui_msgbox`
  allocated its `Dlg`, then read
  `i = dlg_create(...) ? 0 : 0; CASTALIA_UNUSED(i);` — a shape elaborate
  enough to look deliberate, which discards the answer. A `Dlg` is freed by
  its window on `WM_MSG_DESTROY`; if `wm_create` fails there is no window,
  that message never arrives, and the allocation is lost. `ui_prompt` had the
  same hole. The failure is correlated with its own cause: what makes
  `wm_create` fail is exhausted window slots or memory, which is also when
  something wants a message box on screen to say so.

  A new scene, `--dlg-exhaust`, fills the 64-window pool and then asks for 200
  more dialogs. Measured: **97,600 bytes leaked** without the fix (488 bytes a
  dialog, one `Dlg` each) and **zero** with it. The scene checks its own
  precondition too — it reports how many windows are actually up, because if
  the pool were not really full the dialogs would have been created, nothing
  refused, and zero growth would have proved nothing.

  Two details of the harness were load-bearing. The failure marker had to be
  the literal word `MISMATCH`, since that is what `run_demos.sh` greps for —
  a scene reporting a leak in its own vocabulary reports it to nobody. And the
  whole path was verified end to end by restoring the leak and watching
  `DEMOS FAILED: 1 of 51`, not merely by watching the scene print a number.

  **A Type column that said nothing.** `assoc_type_name` ended with
  `return (a_ext(name) != NULL) ? "File" : "File";` — both arms identical, so
  every unknown extension read "File". The shape of the ternary is what shows
  a distinction was meant to be there. It now gives "XYZ File", which is what
  the table already does for the rows it has ("Log File", "Batch File"), so a
  folder of `.DAT` and `.ZIP` files stops repeating a word the reader can
  already see in the name.
- ✅ **The sweep made permanent** (`tools/check_warning_sweep.sh`, `make lint`):
  the two bugs above were found by flags nobody had enabled. A one-time sweep
  finds them once; this is what stops the next one being written.

  Ten flags the tree is already clean under, now checked on every `make lint`:
  `-Wduplicated-branches -Wduplicated-cond -Wlogical-op -Wnull-dereference`
  `-Wjump-misses-init -Wformat=2 -Wcast-align -Wswitch-default`
  `-Wwrite-strings -Wundef`.

  **The script carries its own control, and that is not decoration.** Written
  `cc -w -Wfoo`, this scan reports zero for everything, because `-w` silences
  a `-W` that follows it — a mistake made three separate times in this
  project, each producing a confident "no hits" that meant nothing. So the
  script first compiles a file containing a deliberate duplicated branch and
  **fails if it cannot see it**. Both failure paths were verified by exit
  code, not by reading output: a duplicated branch reintroduced into
  `assoc.c` exits 1, and an injected `-w` exits 1 with "WARNING SWEEP IS
  BLIND".

  Getting that verification right took two corrections worth recording, since
  both are ways to believe a check works when it does not:

  - The first attempt read the exit status through a pipe
    (`sh ... | head; echo $?`), which reports `head`'s status and printed a
    reassuring `0` for a run that had just failed.
  - `git checkout tools/check_warning_sweep.sh` silently did **nothing**,
    because the file was untracked — so a `-w` planted for the experiment was
    still sitting in the script afterwards. It was caught by grepping for it
    rather than by assuming the restore had worked.

  Three flags are deliberately excluded, with the reasons written into the
  script so an omission is not mistaken for an oversight: `-Wshadow` (18 hits,
  all checked — `main.c`'s headless loop has an `int f` frame counter and
  scene blocks declare their own `PlatFile *f` or `CRect f`; different types,
  and mixing them would not compile), `-Wfloat-equal` (5 hits, all correct —
  four exact-zero divide guards and Newton's-method's exact convergence stop
  in `calc_core.c`), and `-Wconversion` (thousands, almost all noise).
- ✅ **A read past the end that thirteen valgrind runs could not see**
  (`calc_core.c`, `tools/run_sanitize.sh`, `make sanitize`): the escalation
  from compiler warnings to runtime sanitizers, and it found a real
  out-of-bounds read on its first run.

  ```c
  if (label[1] == '\0' &&                      /* read BEFORE the test below */
      (label[0] == '+' || label[0] == '-' ||
       label[0] == '*' || label[0] == '/')) {
  ```

  `calc_press` tested `label[1]` before `label[0]`, so an **empty** label —
  which its own suite passes deliberately, `CHECK(!calc_press(&c, ""))` —
  read one byte past the end of a one-byte string literal. The test was
  right; the code was wrong; and nothing in the gate could tell.

  Valgrind missed it in thirteen runs and **could not have found it**: `""`
  is a literal in `.rodata`, and valgrind puts no redzones around globals.
  AddressSanitizer does. The two tools overlap on the heap and barely at all
  on globals and the stack, so running one is not running the other. The
  digit test one line above is safe only by having been written in the right
  order, not by anyone choosing it — so the fix reorders the operator test and
  says why in a comment.

  Two more gaps the same run exposed:

  - `tools/run_memcheck.sh` runs with **`--leak-check=no`**. Its report — "13
    runs, no invalid access and no uninitialised reads" — is exactly true and
    says nothing whatever about leaks, which is easy to read as more than it
    claims. That is now stated in the script itself.
  - LeakSanitizer immediately found a **12 KB canvas** abandoned by
    `tests/test_paint.c`, which called `pc_undo_init` a second time over an
    undo ring that still held snapshots. A test bug, not a product one — the
    product pairs `pc_undo_free` with `pc_undo_init` on a resize, which was
    checked rather than assumed.

  `make sanitize` is now part of `make check`, building into `build-san/` so
  it never disturbs the ordinary build. It carries a control for the same
  reason the warning sweep does: it compiles a deliberate out-of-bounds read
  and fails if the sanitizer stays quiet. Both failure paths verified by exit
  code — restoring the real `calc_press` ordering exits 1, and stripping
  `-fsanitize` exits 1 with "SANITIZER IS BLIND".
- ✅ **The sanitizer extended to the shell, and a sweep that nearly proved
  nothing** (`tools/run_sanitize.sh`): the unit tests are pure logic; the
  demo scenes are where the window manager, the shell, the renderer and the
  twenty-four apps actually run. That is most of the tree by volume and none
  of it was covered.

  Result: **51 scenes, zero findings.** The shell is clean under ASan + UBSan
  + LeakSanitizer.

  That number is only worth writing down because of what happened while
  establishing it. The first sweep also reported zero — and it was not yet
  entitled to. Verifying it took two attempts:

  - The first control planted a one-byte overrun in `sh_run_frame` and the
    sweep said nothing. The reason was that the rebuild had been run as
    `make ... >/dev/null 2>&1`, so a failed build was invisible and the sweep
    re-ran the **previous, unmodified** binary. Hiding the output of the step
    that makes the evidence is the same mistake as `-w` on a warning scan,
    wearing a different hat.
  - Re-run with the build output visible, the control fired — and then the
    full sweep with it still planted reported **51 of 51**, which is the
    thing that actually licenses the zero. Every scene reaches `sh_run_frame`,
    and the detection pattern matches what UBSan really prints.

  The scenes cost almost nothing once built: 30–90 ms each, the whole gate
  step under ten seconds for 3,449 checks plus 51 scenes. So all of them run,
  not a sample.

  The scene list is **read out of `run_demos.sh`** rather than copied, so a
  scene added there is sanitized without anyone remembering to. A copy would
  drift, and the copy that drifts is always the one nobody is watching.
- ✅ **The Play button pointed backwards** (`ui_draw_tri`, `app_media.c`):
  found by rendering the Media Player and looking at it, which is the one
  method that finds this class at all.

  The deck's Play triangle pointed **left**, and Next was drawn identically to
  Prev — both pointing left, so the two buttons either side of the transport
  row said the same thing. Both were plainly visible on screen for as long as
  the player has existed, and no check anywhere could have noticed: the
  windows composite, the demos pass, the pixels are all inside their buttons.

  The cause is worth more than the fix. The direction lives in one character
  of a height formula:

  ```c
  int h = 1 + 2 * i;      /* grows with x -> apex on the LEFT  */
  ```

  written out **three times** in `mp_glyph`, and two of the three wanted the
  other direction. Meanwhile `ui_scroll.c`'s `sb_arrow` — which has taken the
  direction as an **argument** since it was written — has all four of its
  arrows correct. Same author, same idiom, same file tree; the difference is
  entirely whether the direction was a parameter or retyped.

  So there is now one `ui_draw_tri(s, rect, dir, col)` in `ui_controls.c`,
  used by the deck, and it is tested the only way a glyph's direction can be:
  draw it and count where the ink actually landed. A right-pointing triangle
  must have its full-height base in the left column and a single pixel in the
  right; inverting the formula flips that comparison and fails 6 checks. All
  four directions are pinned, plus containment (a triangle that paints outside
  its box smears into the button beside it) and degenerate 1×1, 9×1 and 1×9
  boxes.

  Two other hand-drawn glyphs were checked while there rather than assumed:
  all four scroll-bar arrows are correct, and the File Manager's task-pane
  bullet is a **diamond** whose comment claimed it was a right-pointing arrow.
  Rendered and looked at before deciding which to change — at four pixels the
  diamond reads cleanly against the pane blue, so the art stayed and the
  comment was corrected. Changing artwork that looks right to match a stale
  comment is the wrong way round.

  One self-inflicted detour: the new pixel checks were first placed after
  `gfx_surface_free(g_s)` in `test_ctrl.c`, giving a use-after-free that
  segfaulted the whole runner with an empty stdout, because the buffer is lost
  on a crash. `stdbuf -o0` showed the last suite to print and named the spot.
- ✅ **Every three-row group in the Network window overflowed its own frame**
  (`app_net.c`, `app_net_ping_button`): found by rendering all eighteen
  openable apps and looking at each one, which is the sweep the Media Player's
  backwards Play button earned.

  Two defects in the Network window, the second only visible once the first
  was fixed:

  - **A ragged column.** The Adapter group's Device field was
    `crect_w(&r) - 104` while Address and Status were a fixed `140` — one
    full-width box above two short ones, in a single column. The TCP/IP group
    below already did the right thing (three fields, all 130). One shared
    width now.
  - **The group boxes were four pixels too short.** `3 * NA_ROW + 16` is 58,
    but the rows start at +16, step by `NA_ROW + 2`, and the last box is
    `NA_ROW` tall, so the content ends at 62. The bottom field sat across its
    own group's border — in **both** groups, because they share the formula.
    The height is now derived from the same numbers the rows are placed with.

  Fixing the height broke `net-ping-demo`, which is the harness working: the
  scene clicked at `o.y + 8 + 58 + 8 + 58 + 8 + 27`, with a comment naming
  app_net.c's "two 58 px group boxes". Ten pixels of growth put the Ping
  button out from under the click and it reported `0 frames out (MISMATCH)`.

  Patching the constant would only postpone this, so the scene **asks the
  window** now — `app_net_ping_button()` returns the rect from the same
  `na_layout` the painting uses. A harness that copies a layout constant is a
  copy that drifts, which is the same reason the sanitizer reads its scene
  list out of `run_demos.sh` instead of repeating it.

  Also worth recording from the sweep: **two apparent bugs were misreads of an
  8-pixel font at 1:1** and evaporated on zooming — the Console's `3792 KB`
  read as `MB`, and the Theme Editor's `#4E8BD8` read as `$4E8BD8` because the
  `#` and `4` glyphs touch. Neither was real. Any claim about small on-screen
  text is now made from a magnified crop, not from the full screenshot.
- ✅ **A path that ran off the edge, and the "..." that was too narrow**
  (`ui_path_fit`, `app_logview.c`, `app_diskuse.c`): finishing the app sweep,
  and a defect found by testing the extracted code rather than by looking at
  it.

  Three windows show a long path three different ways. **Disk Usage** cut it
  from the LEFT and marked the cut `...`, which is right — the tail of a path
  is the part that says where you are, so a deep path should lose its root and
  not its folder. The **Log Viewer**'s path bar just ran off the right edge
  with nothing to say anything had been dropped. (The **Console** is left
  alone deliberately: a command prompt is not a display field, and DOS shows
  the whole path and lets the line wrap.)

  So Disk Usage's logic is now `ui_path_fit()` and both windows share it — and
  writing a test for it immediately found something neither window's author
  would have seen:

  ```
  FAIL  CHECK(gfx_text_width(GFX_FONT_SYSTEM, out) <= 120)
  ```

  The original reserved a hard-coded **12 pixels** for the ellipsis, and
  `"..."` measures **18** in the system font — so the finished string could
  come out about six pixels wider than the space it had just been fitted to.
  (This entry first said 24, from assuming three 8-pixel cells rather than
  measuring; the period is a narrow glyph. The number was wrong, the
  conclusion was not.) It would never read as a bug — only as a path sitting a
  little close to the edge. The reservation is measured in the font now, and
  putting the 12 back fails the check.

  Worth stating the general form: a function whose entire job is *"make this
  fit"* should be handed something too big and then **measured**, because
  "it fits" is exactly the claim such code is least likely to be checked on.

  The remaining apps in the sweep — Disk Usage, CastaliaSheet, Log Viewer,
  Benchmark, Write, Help, Console, Theme Editor, File Manager, Solitaire,
  Character Map — render correctly. Two things that looked wrong were checked
  and are not: CastaliaSheet clips `"Sound card"` to `"Sound car"` because the
  neighbouring cell holds a value, which is what a spreadsheet does; and the
  Benchmark's saturated bars are correct on a host that far exceeds the
  period reference constants.
- ✅ **Icon labels ran into each other, and out of the window**
  (`ui_text_fit`, `app_fileman.c`): the File Manager's Icons view did no
  fitting whatsoever.

  It copied the raw name and centred it with `(FM_CELL_W - 4 - tw) / 2`, which
  goes **negative** the moment a name is wider than the 82-pixel cell.
  Rendered with four long filenames, the result is unambiguous: one label
  started to the left of the list area entirely, one was clipped by the window
  frame, and three ran together into a single unreadable line.

  `ui_text_fit()` is the sibling of `ui_path_fit()` and cuts from the opposite
  end for the opposite reason — a file name is identified by how it *starts*,
  a path by where it *ends*. `A_VERY_LONG_FILENAME_INDEED.TXT` now reads
  `A_VERY_LO...`, centred inside its own cell.

  Two mutations. Removing the ellipsis reservation fails 30+ checks. The
  second is the interesting one: stopping the shortening loop one character
  early (`n > 1u`) **survived the whole suite**, and looked equivalent — both
  produce something that fits in the buffer, and both overflow a budget too
  small to satisfy. Compiling the two and comparing their output settled it:

  ```
  original   w=8 -> "..."   (18 px)
  mutant     w=8 -> "A..."  (24 px)
  ```

  Not equivalent — strictly worse in a budget that is already impossible. The
  contract that distinguishes them is now pinned: when not even one character
  fits beside the mark, the answer is the mark alone. That is the second time
  in this project that "survived, therefore equivalent" was wrong, and the
  second time compiling both and diffing the output was what settled it.
- ✅ **Every button in the system could paint outside itself**
  (`ui_draw_button`): the general form of the icon-label bug, and the test
  gap that hid it.

  `gfx_draw_text_rect` does **not** clip. `GFX_ALIGN_HCENTER` is
  `x0 + (rw - tw) / 2`, which goes negative for a label wider than its box —
  so the text starts left of the button and runs out of **both** ends.
  Measured on a 60-pixel button with a 24-character label: **168 pixels
  painted outside it**, across whatever happened to be next to it.

  It survived because of how it was checked. `tests/test_ctrl.c` has had
  containment checks from the beginning — fill with a sentinel, draw, require
  every pixel outside the rectangle to be untouched — and **every one of them
  passed an empty label**: `ui_draw_button(g_s, &btn, "", ...)`. The one thing
  that can actually escape a button was the one thing never drawn into one.

  Fixed with **both** a fit and a clip, because they answer different
  questions. Fitting is what makes a long label read `A VERY LO...` instead of
  being sliced through the middle; clipping is what guarantees containment when
  even the mark cannot fit — a ten-pixel button has no honest label, and `"..."`
  is eighteen pixels. Fitting alone still leaked six pixels there.

  That distinction came out of the mutations rather than from planning it.
  Removing the clip fails the narrow-button check. Removing the **fitting**
  first survived the entire suite, because containment is the clip's doing and
  the checks could not see the difference between a shortened label and a
  sliced one. So a second check renders the long label and the same label
  already fitted, and requires the two to be **pixel-identical** — that
  mutation now fails with 181 differing pixels.

  Verified it costs nothing where nothing was wrong: the Theme Editor's
  `Next Preset`, `Apply`, `Save As...` and `Open...` all render in full, and a
  label ending in a real ellipsis is not confused with a truncated one.
- ✅ **The checkbox, the radio, the taskbar and the title bar had it too**
  (`gfx_text_fit`, `ui_controls.c`, `sh_taskbar.c`, `wm_frame.c`): asking
  whether anything *else* drew a caller's string into a fixed box, instead of
  assuming the button was special.

  All four did.

  - **Checkbox and radio**: 240 pixels outside a 70-pixel row, each — worse
    than the button's 168, from the same `gfx_draw_text` that clips to
    nothing. Both go through one `label_in_row()` now.
  - **Taskbar buttons**: width is `avail / n` with a maximum but **no
    minimum**, so they keep shrinking as windows open. At eighteen windows
    they are near 25 px and every title ran straight across its neighbours.
    The taskbar's own clip keeps the text on the taskbar; it never kept a
    label inside its own button.
  - **Window titles**: the frame clips to the window, so the title could not
    escape it — but nothing stopped it running *under* the minimise, maximise
    and close boxes, where it simply vanished beneath them. The caption
    buttons' rectangles are now computed first so the title can be fitted to
    the space before them, which is what Win9x does.

  Rendered at both ends of the range: at eight windows the taskbar reads
  `File...` `Syst...` `Cloc...` `Book...` `Docu...` `Notepad` `Network` — the
  short titles whole — and at eighteen the buttons are too narrow for anything
  but the mark, which is honest rather than a smear of overlapping titles.

  **A layering note.** `wm_frame.c` needed the fitting, and `src/wm` has never
  included `castalia/ui.h` — the window manager and the controls library are
  peers, not a stack. The implementation therefore moved to `gfx_text_fit()`
  in `gfx_font.c`, which is where it belonged anyway since it needs nothing
  but `gfx_text_width`; `ui_text_fit` forwards to it so every existing caller
  and test stands unchanged. The warning that caught this was an implicit
  declaration, not a design review.
- ✅ **The listbox as well — and the one place it should NOT be fixed**
  (`ui_list_draw`, `sh_desktop.c`): finishing the sweep of every control that
  draws a caller's string, having claimed the previous commit closed it.

  It had not. The **listbox** leaked 194 pixels outside an 80-pixel control
  with one long entry — it has a scrollbar for its *height* and nothing at all
  for its width. Fitted and clipped now, the same pairing as the button: a
  14-pixel list still leaked 167 with the fit alone.

  The interesting case is the one deliberately left alone. **Desktop icon
  labels** exceed their cells by a wide margin — the hit rectangle is 76 px
  and "Control Center" is about 112 — so on the face of it this is the same
  bug again. It is not worth fixing, and the reason is worth recording so the
  next reader does not "fix" it:

  - the desktop lays its seven icons in a **single column** (`y += step`, no
    x advance), so there is no horizontal neighbour to collide with;
  - fitting them to 76 px would truncate "Control Center" to "Control..." on
    every boot — a visible regression to correct a defect nobody can reach.

  What is true is that the label is wider than the clickable rectangle, so the
  outer few pixels of a label do not select its icon. That is a real if minor
  inconsistency and it is written down rather than acted on, because widening
  the hit rectangle touches `icon_nav.c`'s arrow navigation, which has its own
  suite and no bug.

  Verified no regression where labels already fitted: the Control Center's
  category list draws all ten names in full, and its theme radio buttons still
  read "Castalia Classic", "Forest Green" and "High Contrast" complete.
- ✅ **Four rings became one, and the refactor broke the Console**
  (`ring_core.c`, `tests/test_ring.c`, `--console-demo`): the duplication
  noted three times in this file and never acted on.

  Four places kept "the last N of these", each spelling the wrap differently:
  `hist_core.c`'s `at = head - count + i; while (at < 0) at += MAX;`, the
  Console's `(line_head - line_count + i + CON_LINES * 2) % CON_LINES`, the
  Console history's `(base + pos) % CON_HIST` over a count that never capped,
  and the Log Viewer's `(start + i) % LV_MAX_LINES`. **All four were correct**
  — that was checked before any of this — so this is housekeeping, not a bug
  fix. The point is that they were four chances to be wrong about one
  question, and the arithmetic only diverges *after the ring wraps*, which for
  a 240-line scrollback is long after anyone is watching.

  All four go through `ring_core.c` now. What is worth recording is what the
  refactor cost on the way:

  **It broke the Console, and only rendering caught it.** A `Ring` in a
  `sys_calloc`'d struct has capacity **zero**, and `ring_push` quietly rounded
  that up to one. The Console became a *one-line* console — banner and
  directory listing scrolling off the instant they were printed. Every suite
  stayed green. It was found by opening the window and looking at it.

  So `ring_push` no longer repairs itself: an uninitialised ring stays empty
  and the window shows **nothing**. A blank console is a bug report; a
  one-line console is a puzzle. And `--console-demo` now counts the
  scrollback and the history, which fails three ways if the init is dropped.

  Two more things the work turned up:

  - `ring_full()` answered **true** for an uninitialised ring, since
    `count >= cap` is `0 >= 0`. A ring that can hold nothing reporting itself
    full would tell a caller to evict. Its own test caught that.
  - A careless string replace put the history ring's `ring_init` inside the
    **`cls` handler** instead of the constructor — so `cls` would have wiped
    the command history (a shell does not do that) while the constructor
    left it uninitialised. Caught by checking where the edit actually landed
    rather than trusting that it matched.

  **And a false alarm worth writing down.** `make check` reported
  `index 528 out of bounds for long int [120]` in `hist_core.c` — alarming,
  and not reproducible from a clean build. The cause was stale objects in
  `build-san/`, which had been built by hand with varying flags during the
  sanitizer work: some compiled against the old `Hist` layout, some the new.
  A mixed build can invent a fault, and can equally hide a real one, so
  `run_sanitize.sh` now stamps the flags it built with and wipes the directory
  when they differ. A green run means the tree and not the directory.
- ✅ **Every ring accounted for, and a gauge that hid its own graphs**
  (`app_taskman.c`, `--taskman-demo`): the obligation the ring extraction
  created — a `sys_calloc`'d struct holding a ring is only correct if
  something initialises it — checked across **all six** holders rather than
  the three that happened to be touched.

  All six are initialised. Two were worth changing anyway:

  **The Task Manager initialised its histories after `wm_create`.** That call
  dispatches `WM_MSG_CREATE` into the window proc immediately, so anything the
  window did on creation would have run against a zeroed `Hist` — a ring of
  capacity zero. Nothing pushes that early today (the graphs are fed from
  `WM_MSG_TIMER`, started further down), so this was ordering that *happens*
  to be safe rather than ordering that *is* safe. Initialising first makes it
  the latter.

  **And the scene could not have caught it.** `--taskman-demo` checked the
  memory **gauge** — which reads `sys_mem_live_bytes` directly — and never the
  **graphs**, which are the thing fed by the ring. The control shows the gap
  exactly: with the histories left uninitialised,

  ```
  TASKMAN-DEMO: and it is neither empty nor full (176 of 382) (OK)
  TASKMAN-DEMO: 0 samples in the performance history (MISMATCH)
  ```

  Two empty Performance boxes, and every existing check green. The sample
  count is checked now.

  The general shape, since it has come up twice in two commits: a check that
  reads the *source* a widget is fed from will pass while the widget draws
  nothing. It has to read what the widget actually holds.
- ✅ **FreeCell** (`fc_core.c`, `app_freecell.c`, `card_draw.c`,
  `tests/test_fc.c`, `--freecell-demo`): a new game, and the first thing added
  to this desktop in a while that is simply *more of it* rather than a repair.

  The other patience game the era shipped, and a better one to have than a
  second Klondike: every card is face up from the deal, so nothing is hidden
  and nothing is luck. Whether you win is whether you saw the move.

  **The rule worth having a core for** is how many cards may move at once. A
  player can only lift one card by hand; a "supermove" is shorthand for
  shuffling them through the free cells and empty columns and back, so the
  limit is `(free + 1) * 2^empty` — with the caveat that a column you are
  moving *into* cannot also be scratch space, which is the part
  implementations get wrong. `fc_max_move` takes the destination for exactly
  that reason, and the window prints the number on its status bar, because it
  is the rule most players never learn. Both are checked over every
  combination that exists; putting the Klondike King-on-empty rule back fails
  2 checks and forgetting to charge for the destination fails 14.

  **The card artwork moved to `card_draw.c`** rather than being copied. The
  pips, faces and back were statics inside `app_solitaire.c`, and a second
  copy is precisely how the Media Player ended up with a Play button pointing
  the wrong way. The move is provably behaviour-preserving: the extracted
  file diffs **identical** to the original block once the four names are
  changed. (A screenshot comparison was tried first and was useless here —
  Solitaire seeds its shuffle from `sys_now_ms()`, so two runs deal different
  hands and every card differs. Diffing the code was both stronger and
  cheaper.)

  Three defects of my own on the way, all found by rendering:

  - the felt was drawn with `crect_offset(&client_rect, origin)`, but
    `wm_client_rect` already returns **screen** coordinates — so the table was
    shifted down-right by the window's own position, leaving the desktop
    showing through the top-left corner;
  - the status line's `char buf[64]` truncated the last number, so the board
    reported "…, move" with no value;
  - the four free cells and four foundations were eight identical holes.
    Empty foundations carry a faint suit pip now, which is what tells the two
    halves apart.

  `--freecell-demo` plays a real move through the window's own hit-testing and
  then counts the cards, because **conservation** is the one property a card
  game cannot show you: leaving the source card behind still reports
  "1 moves (OK)" and draws a perfectly ordinary board — only the count catches
  it, at 53.

  **New Game and Send Home** followed, because a game whose only way to deal
  again is an undocumented F2 is not finished. Send Home walks the board
  sending every card that can go to a foundation, repeatedly until a pass
  finds none — bounded by the deck rather than by "until nothing moves" alone,
  since a rule bug that let a card be accepted twice would otherwise spin with
  the window frozen, and a hang is a worse way to learn about it than a
  stopped board. The scene drives both buttons and checks the deck is still
  whole after each: Send Home put 2 on the foundations with 52 still
  accounted for, and New Game returns to 52 out, 0 moves, 0 home.
- ✅ **The screen saver scene wrote nothing and said it had** (`--saver-demo`,
  `splash-demo`): found by trying to look at the four saver modes and
  discovering the files were not there.

  `plat_screenshot` returns a `CResult`. Both scenes **discarded it** and then
  logged `"wrote build/saver.bmp"` unconditionally — which was a lie whenever
  the scene ran in a world without a `build/` directory, i.e. every run of the
  demos harness. The file never appeared and the log said it had. Both write
  into their own world now and report what actually happened.

  Then the coverage: `sh_saver_draw_mode` has **four** branches and the scene
  only ever drew the one in settings, so three of the four were rendered by
  nothing in the gate — a saver that had stopped painting would have been
  found by a user switching to it. All four run now.

  The first version of that check asked "is this pixel not black", and every
  mode answered **30000 of 30000** — because these savers lay down a very dark
  ground rather than leaving the buffer at zero, so the question was satisfied
  by the fill alone and a mode painting nothing on top would have sailed
  through. Measured against each mode's **own** ground instead, the numbers
  finally say something and match what the modes look like:

  ```
  mode 0 castle     284 of 30000     sparse stars and a small castle
  mode 1 starfield  105 of 30000     thin warp streaks
  mode 2 plasma   29802 of 30000     fills the screen
  mode 3 mystify    522 of 30000     bouncing polylines
  ```

  Control: making Mystify return before painting gives `0 of 30000
  (MISMATCH)`.
- ✅ **Undo, at last** (`undo_core.c`, `tests/test_undo.c`, `app_notepad.c`,
  `--undo-demo`): Notepad, CastaliaWrite and CastaliaSheet have had **no undo
  at all**. Paint had one; the editors, where Ctrl+Z is pure muscle memory,
  did not. Typing over a selection you meant to keep is the most-regretted
  keystroke in any text editor and this system had no answer to it.

  Bounded by design, since there is no allocator to lean on: a fixed ring of
  **edit records**, not snapshots. A snapshot ring of Notepad's 16 KB buffer
  would cost 16 KB a level; a record costs the characters that actually
  changed, so sixty-four levels come to about 8 KB.

  Every edit is one of two things and each is the other's inverse — insert *n*
  at *p* is undone by deleting *n* at *p*, and vice versa — **which is why the
  deleted text is kept in the record**. That is the part that is easy to get
  wrong and impossible to see: a stack that keeps only positions and lengths
  restores the right *number* of characters and the wrong characters, and the
  document still looks like a document. The mutation says it plainly:

  ```
  FAIL  "The       brown fox" == "The quick brown fox"
  ```

  The second mutation is more interesting because it **survived**. Removing
  the timeline fork — the line that discards the abandoned redo branch when a
  new edit arrives — passed the whole suite, because the fork test undid only
  *once* and the stale record sits one step further back. Undoing twice
  reaches it, and there it is an insert of `" and dog"` against a
  three-character buffer: the undo fails and the document simply stops moving,
  with every earlier check still green. That pair is checked now and the
  mutation fails.

  Wired into Notepad at the three points that actually change the buffer, with
  Ctrl+Z and Ctrl+Y. `--undo-demo` drives it through the real key path and
  compares the **exact text**, never the length: typed `hello world`, five
  undos leave `hello `, five redos restore `hello world`.
- ✅ **Undo in the spreadsheet too** (`cundo_*`, `app_sheet.c`): the second of
  the two gaps left by the Notepad work.

  A spreadsheet edit is not an insert or a delete in a stream of characters —
  it is *"cell (sheet, row, col) held X and now holds Y"*. Forcing that into
  the text stack would mean inventing an offset for a thing that has no
  offset, so it gets its own small stack with the same two properties, because
  those are the two that are invisible when wrong: **the old text is kept**,
  and a new edit discards the abandoned branch.

  A sheet is the worst place for a silent undo bug. Every formula
  recalculates, so a wrong restore spreads through the book and the result is
  a page of entirely plausible numbers. The control makes it concrete —
  returning the location without the text gives:

  ```
  UNDO-DEMO: sheet undo put '' back where '=D8+D9' had been (MISMATCH)
  ```

  Every user edit now goes through one `sh_set()` — typing, cut, paste, clear
  and Delete — while loading a file and laying out the sample book still call
  `sheet_set` directly, deliberately: neither is something the user did, and
  putting them on the stack would have Ctrl+Z quietly dismantling a document
  nobody edited.

  **The scene's first version checked the wrong thing and passed anyway.** It
  assumed the cursor opens on A1 holding the sample's `"Item"` — it actually
  opens on **D10**, and Enter then moves it down, so the check was reading a
  cell the edit never touched. It now captures whatever the cell really held
  *before* editing and requires exactly that back, which is both correct and
  independent of where the cursor happens to start.
- ✅ **Undo that keeps the emphasis** (`wundo_*`, `app_write.c`): the third and
  last gap. CastaliaWrite carries a **parallel attribute byte per character** —
  bold, italic, underline — so the flat stack would have restored the words
  and dropped the formatting. That is a partial undo wearing the costume of a
  complete one: the sentence is right, the emphasis is gone, and the document
  still reads as a document.

  The record therefore carries the attribute bytes beside the text, and it is
  a separate stack rather than a wider `UndoRec` because Notepad has no
  attributes and would pay 8 KB for a payload it never fills. It also hands
  the record **back** instead of applying it — CastaliaWrite already has
  `wr_insert` and `wr_delete_range`, and `undo_core` has no business including
  `write_core.h` to reach them.

  **The scene's first version passed for the wrong reason** — the fourth time
  that has happened in this tree, and worth writing down because the shape
  keeps recurring. It typed a plain character, so `before_attr` was `0`, and
  "the attribute came back" reduced to `0 == 0`: the check would have been
  just as cheerful if undo dropped attributes entirely, which is the single
  bug the whole stack exists to prevent. It now presses **Ctrl+B first**, so
  there is an attribute worth losing, and zeroing the stored bytes gives:

  ```
  UNDO-DEMO: write redo restored 'Q' attr 0, was 'Q' attr 1 (MISMATCH)
  ```

  A check that has never been seen to fail is not yet a check.
- ✅ **The keystrokes Help had been promising** (`app_write.c`, `app_help.c`):
  writing the Ctrl+B above needed a way to arm bold from the keyboard, and
  there wasn't one — the menu was the only way into *any* of CastaliaWrite's
  commands. Help, meanwhile, had listed `Ctrl+B / Ctrl+I / Ctrl+U, Ctrl+S to
  save, Ctrl+O to open` since the word processor was written. **Not one of
  them existed.** A manual that lists a keystroke the program ignores is worse
  than no manual, because the reader concludes the keyboard is broken.

  Now wired, through the same `wa_command` the menus call, so there is no
  second implementation to drift: **Ctrl+B, Ctrl+U, Ctrl+Z, Ctrl+Y, Ctrl+A,
  Ctrl+F, Ctrl+S, Ctrl+O**, with the accelerators shown in the menus. Ctrl+I
  is deliberately *absent*: it arrives as ASCII 9, indistinguishable from Tab,
  so mapping it would turn every Tab in the document into italics. Help says
  so now rather than promising it.

  Detection uses the same `WA_CTRL` shape as Notepad and Paint, because the
  two backends disagree: DOS delivers Ctrl+letter as an ASCII control code in
  `key`, while a host backend may send the letter with a CTRL modifier — and
  Write's window proc was throwing the modifier word away entirely.

  Both new checks were confirmed by deleting the handlers:

  ```
  UNDO-DEMO: Ctrl+U over Ctrl+B gives attr 1, want 3 (MISMATCH)
  UNDO-DEMO: Ctrl+O opened 0 window(s) (MISMATCH)
  ```

  Ctrl+U is checked *on top of* the bold rather than alone, because alone it
  would pass against a handler that assigned the attribute instead of toggling
  one bit into it.
- ✅ **The shortcut that worked only on DOS** (`tools/check_ctrl_keys.sh`,
  `app_sheet.c`, `app_theme.c`): auditing Help's claims turned up the
  *structural* cause of the CastaliaWrite bug above, and one more instance of
  it — **committed two commits earlier, by me**.

  The backends disagree about what Ctrl+letter looks like:

  ```
  DOS  (INT 16h)   key = the ASCII control code   -- Ctrl+Z arrives as 26
  host (SDL-ish)   ch  = the letter, mods = CTRL  -- Ctrl+Z arrives as 'z'
  ```

  A handler written `if (key == 26)` answers only the first, so it works on
  the target and ignores every host-encoded press. Notepad and Paint got this
  right with an `NP_CTRL` / `PT_CTRL` macro; Write and Sheet were both written
  testing the control code alone, so **CastaliaSheet's Ctrl+Z had been dead to
  host-encoded input since the day it was added** — and the demo scene drove
  it as a control code, so the scene passed either way.

  To be exact about the blast radius, since the first version of this entry
  overstated it: the host backend is **headless**, so no user was pressing
  Ctrl+Z into it. What was broken was half of a documented platform contract
  — the half the scenes drive, and the half any future interactive host
  front-end would use. Real, and worth a lint rule; not a user-visible outage
  on DOS.

  The scene now drives Ctrl+Y the way a host backend encodes it. Restoring
  the old handler gives:

  ```
  UNDO-DEMO: host-encoded Ctrl+Y redid to '=D8+D9', want '42' (MISMATCH)
  ```

  And because the shape recurred three times, it is now mechanical: `make
  lint` refuses any comparison of `key` or `ch` against a bare control-code
  constant. Backspace, Tab, LF, Enter and Esc are exempt — a real key sends
  those, and testing for them is not a shortcut. The check carries a control
  line *and* was confirmed against the genuine historical bug, which is the
  stronger test: a scanner that only ever catches its own bait proves only
  that it can read its own bait.

  Its one finding outside the editors was a false positive worth keeping
  fixed: `th_set_channel(ThemeApp *, int ch, int v)` used `ch` for a colour
  **channel** in a tree where `ch` is a character everywhere else. Renamed to
  `chan` rather than exempted — the collision was a readability trap
  independent of the scanner.

  Everything else Help promises was checked by hand and is real: Esc /
  Ctrl+Esc, Ctrl+Left/Right, Alt+Tab / Space / F4, Ctrl+C/X/V in text fields,
  F2 and F10 in the File Manager, Tab between panes, Paint's clipboard and
  three-deep undo, Enter / Del in the Agenda, `#CYCLE!`, CSV with formulas.
  CastaliaWrite was the only liar, and it was the only app whose window proc
  threw the modifier word away.

  Sheet also gained the rest of the standard set — Ctrl+X/C/V, Ctrl+S,
  Ctrl+O — routed through the same `sh_command` the menus call, and only
  while *not* mid-edit: during a cell edit those are characters, and a Ctrl+C
  that cut the cell out from under a half-typed formula is the kind of
  surprise that costs an afternoon.
- ✅ **Notepad gets a menu bar** (`app_notepad.c`, `--menuaccel-demo`): the
  program people actually type into was the only app in the suite with **no
  menu at all** — five toolbar buttons and nothing else. Its ten keyboard
  shortcuts were therefore undiscoverable: nothing anywhere in the program
  said Ctrl+Z would undo. Menus are where people learn shortcuts, which is
  half of why the accelerator column above was worth building.

  File / Edit / Search / Help, over the same commands the toolbar runs. That
  needed the commands factored out of the click handler into `np_command`
  first: the toolbar and the key handler had each grown their own copy of
  "new document", and two copies of a command is how one of them quietly
  stops matching the other. (Notepad's Ctrl+N also now clears the undo
  history, which the toolbar's New never did — a new document that could be
  Ctrl+Z'd back into the old one.)

  The Edit menu greys what cannot be done — Undo when there is nothing to
  take back, Cut and Copy with no selection, Paste with an empty clipboard —
  so it reports the state of the document rather than listing verbs.

  Two shortcuts were nearly shipped as lies and caught before commit. The
  menu drafted `Time/Date\tF5`, and **`PLAT_KEY_F5` does not exist**: the
  label went in before the key was checked, which is exactly the defect the
  previous two entries are about. F5 is gone from the label and Time/Date
  stays a menu command. `Find Next\tF3` survived because F3 *is* mapped
  (`keyboard.c`, scan `0x3D`).

  The scene drives the menu the way somebody who does not know the shortcuts
  must — click "Edit", click "Select All" — because a menu that draws
  perfectly and routes nothing looks identical in a screenshot:

  ```
  MENUACCEL-DEMO: Edit > Select All selected 0 of 5 chars (MISMATCH)
  ```
- ✅ **The menus needed a mouse** (`office_menu_key`, all four office-style
  apps): building Notepad's menu bar made the pattern visible — every menu in
  the suite was opened from a **click handler and nowhere else**.

  On the target that is not a small thing. The DOS mouse is a loadable driver
  (`CTMOUSE.EXE`), and on a machine booted without one there was **no way to
  reach** Paint's Image menu, CastaliaWrite's alignment, CastaliaSheet's
  Recalculate or Notepad's Word Wrap — none of them has a Ctrl shortcut, and
  the menu was the only door. The launcher and the File Manager had been
  keyboard-complete for months, and Help said so, which made the gap easy to
  miss: the parts anyone would think to check were fine.

  **F10** opens the bar, arrows walk it (wrapping at both ends), Home/End
  jump to the first and last live row, Enter chooses and Esc closes. That is
  the idiom the File Manager already used for its context menu.

  One implementation in `office_ui.c`, not four. "Right at the last menu
  wraps to the first" is exactly the rule four copies would drift apart on,
  and this tree has paid for that lesson more than once. Enter with nothing
  highlighted deliberately runs **nothing** — treating it as "the first item"
  means a stray Enter silently invokes File > New.

  Two mistakes worth recording, both caught before commit:

  - The three office apps read `L.menubar` for the bar rect **before**
    `wa_layout`/`sh_layout` had filled `L` in. It compiled — `L` was declared,
    just not initialised — and would have opened the drop-down at a garbage
    offset. Each now lays out its own `ML` first.
  - The scene's first route counted Downs from the top of the Edit menu on the
    assumption Undo would be greyed. It types before it navigates, so Undo was
    live, the Down landed on it, and Enter undid a character: `selected 0 of 4
    chars (MISMATCH)` for a reason with nothing to do with keyboard menus. It
    now anchors at End and steps up, which is independent of what the document
    happens to allow.

  Deleting the F10 handler gives the same MISMATCH, so the check is watching
  the thing it names.
- ✅ **The settings app needed a mouse too** (`app_control.c`, `gfx_focus_rect`):
  a rough count of keys handled against click targets put the Control Center
  at **one key and fifteen click targets** — the worst ratio in the suite, in
  the worst app to have it. You could browse the categories from the keyboard
  and change *nothing*; and since the **Mouse and Keyboard panels live in
  there**, the place you would go to sort out a mouse was behind the mouse.

  The panel was a pile of per-category click tests with no notion of "row 3",
  so there was nothing for a key to move to. It is now a row model —
  `cc_panel_rows`, `cc_panel_row_rect`, `cc_panel_activate` — that the mouse
  and the keyboard both drive. Tab moves into the panel, arrows walk it,
  Enter or Space chooses, Left/Right step the Date & Time spinners, Esc
  returns to the category list.

  Keyboard focus needs to be *visible*, so `gfx_focus_rect` draws the era's
  dotted ring. Dotted rather than tinted for a practical reason: any fill
  colour that reads on the default theme disappears on a high-contrast one,
  and safe mode is exactly when somebody is driving this window without a
  mouse.

  **Three findings, none of them the thing I set out to do:**

  1. Making the click path share the row model, I had the click also take
     keyboard focus — "so the two ways don't fight". That quietly changed
     what Enter means: with the panel focused, Enter activates the
     highlighted row instead of pressing OK. The existing `--mousekey-demo`
     caught it within a minute — it clicks a row, presses Enter to apply, and
     found the double-click window still at its default because the setting
     had been chosen and never applied. Clicking now moves the keyboard's
     *place* without taking focus.
  2. My first keyboard check asserted the delay became **500 ms**, which is
     the built-in default — so it passed with the entire Tab handler removed.
     The arrows had merely walked the category list and Enter had pressed
     Apply. It now picks 250 ms, and the control fires.
  3. Rendering the panel to look at the focus ring showed **"Then how fast:"
     printed through the tail of "Shortest (250 ms)"**, the T clipping into
     its radio button. The heading was positioned by hand at four pixels
     above the bottom of the last row, and had presumably always been wrong;
     no check in this tree looks at a panel's pixels. Both headings are now
     anchored to the row grid.
- ✅ **A caption was painting over the window frame** (`cc_caption`,
  `--mousekey-demo`): the heading collision above was found by luck — that
  panel was only rendered to check the focus ring. So all **ten** categories
  were rendered and looked at together, which is the cheap version of the
  same luck.

  Two findings, one of them a genuine containment bug:

  - The Screen Saver caption is 45 characters in a panel that fits 42. It ran
    straight through the panel's etched border and out over the window frame.
    `gfx_draw_text` neither fits nor clips — **the same hole that once let
    every button in the system paint outside itself** — and it had presumably
    always done this: the overflow is at the right-hand edge, where the eye
    does not go, and nothing here looks at a panel's pixels.
  - Four of the five captions sat flush against the bottom of the last option
    row with no gap, while the Mouse panel's had a full row of air. Nothing
    overlapped; it just looked wrong the moment two panels were seen side by
    side.

  All five now go through `cc_caption`, which fits to the panel width and
  sits on the row grid. **Both** halves are kept, for the reason the button
  work established: fitting is what keeps the text readable, clipping is what
  keeps it inside the panel, and they answer different questions. The two
  long captions were also **shortened** so they fit at the default size —
  ellipsizing is the safety net for a resized window or a wider face, not the
  plan, and `"...zoom + slide effe..."` has lost the sentence it existed to
  say.

  And the class is now mechanical rather than a matter of my having looked:
  the scene renders all ten panels and requires the four columns just outside
  each one to be untouched. Restoring the original caption gives:

  ```
  MOUSEKEY-DEMO: 9 pixel(s) painted outside a settings panel (worst category 2) (MISMATCH)
  ```
- ✅ **Every app window rendered and looked at** (`app_media.c`,
  `--repaint-demo`): the settings sweep paid, so the same was done for
  eighteen app windows at once — one screenshot each, tiled into contact
  sheets.

  **One real defect, and two of my own misreadings worth recording.**

  The defect: the Media Player draws its equalizer captions **LO / MID / HI**
  eight pixels tall, placed *ten* pixels above their sliders. The transport buttons occupy rows 70–91 and the
  sliders start at 100, so there are exactly eight rows of clearance and the
  captions were using ten: **two rows of green inside the SHUF and REP
  buttons.** Nobody would report that — it reads as the deck looking slightly
  soft.

  It is also the first bug this session that **survived being looked at
  directly**. After moving the labels I compared the before and after
  renders by eye and concluded nothing had changed; only counting the
  label-coloured pixel rows showed 198–203 becoming 200–205, with the buttons
  ending at 199. A two-row shift is invisible in a side-by-side. So the check
  counts pixels rather than trusting an eye:

  ```
  REPAINT-DEMO: 16 label pixel(s) inside the transport buttons (MISMATCH)
  ```

  The two misreadings, both from judging 8-pixel text on a tiled sheet at 1×:
  the Task Manager's gauge appeared to read `Memory 3769 MB` against a status
  line of `3.68 / 8 MB` — it says **KB**, and 3769 KB *is* 3.68 MB, so the two
  agree. And the Theme Editor's preset caption looked collided; it is
  correctly spaced. That is now the fourth and fifth time in this tree that
  low-zoom bitmap text has produced a false report. **Zoom first, then
  claim.**
- ✅ **Reversi** (`rev_core.c`, `app_reversi.c`, `tests/test_rev.c`,
  `--reversi-demo`): a fourth game, and the one that most rewards having a
  pure rules core, because **Reversi is uniquely hostile to being checked by
  looking at it.** Every position is a grid of black and white discs: a board
  produced by a broken rule is indistinguishable from a correct one. Nothing
  crashes, nothing is out of place, the game simply becomes a different game
  and the player assumes they misunderstood it.

  So the rules are driven directly, and the three that carry the risk each
  got a test — then each test was checked against a mutation of the rule it
  covers:

  | Rule broken | What the tests said |
  |---|---|
  | a run reaching the edge closes the flip | 5 failures, incl. `rev_move_count 10 != 4` |
  | one direction's sign transposed | `rev_would_flip 3 != 4`, the wrong arm left unturned |
  | game over only when 64 stones are down | `rev_game_over` on a dead position |
  | the computer plays the most stones now | takes (4,1) for five discs instead of the corner |

  That last one is worth keeping: **greedy is a losing Reversi strategy**,
  because discs flip back and corners never do. The computer plays positionally — corners
  weighted heavily, the squares diagonally inside them penalised — with the
  flip count only as a tie-break, and the test offers it a corner worth one
  disc against a middle move worth five.

  The **passing** rule is the one implementations get wrong: a position where
  neither side can move is over *with empty squares left*. Waiting for a full
  board leaves the program sitting on a finished game, which the player reads
  as a hang. `rev_game_over` therefore asks the board, not a counter.

  Two things the window does that the rules cannot: it **marks the legal
  moves**, because a Reversi board that does not is one most people give up
  on; and it **announces a pass**, because a turn silently coming back looks
  exactly like the program ignoring your move.

  One design point came out of the scene rather than the plan. White replied
  in the *same frame* as the click — the shell handles input and then ticks
  animations in one pass — so the board jumped two moves at once and the
  player never saw their own disc land. White now counts down three frames,
  which is also what gives "White is thinking..." a moment on screen. The
  window asks for ticks only while it owes a move and gives them back after,
  so an idle board is off the per-frame list entirely.
- ✅ **Where the renderer's time actually goes** (profiling; **no code
  change**): "make it faster" deserves a measurement rather than a guess, so
  the host build was profiled under gprof against an animated scene (4000
  frames of the Benchmark window).

  ```
  49.4%  gfx_fill_rect     440,591 calls
  21.7%  bench_layout        2,702 calls   (0.07 ms each -- the app's own)
  13.3%  gfx_draw_text      67,443 calls
   4.8%  gfx_vgradient      21,555 calls
   3.6%  shadow_px      11,128,120 calls
  ```

  `gfx_fill_rect` dominates, and its callers are the interesting part: about
  48% of those calls come from **`gfx_bevel`** — which is to say from
  `gfx_hline` / `gfx_vline` inlined into it. Every button, panel, well, frame
  and separator draws a bevel, and each was paying `crect_make`,
  `crect_intersect` and `crect_empty` four to eight times to write runs one
  pixel thick. That is the obvious optimisation, so it was written: direct
  clipping for the two line primitives, no rectangle machinery.

  **It made no difference, and the change was reverted.**

  Getting to that answer honestly took three attempts, and the first two were
  wrong:

  1. Compared against a baseline taken minutes earlier: *slower by 3%*.
  2. Re-measured the reverted code and got 1105–1263 ms where the same code
     had just measured 993–1007. **The machine drifts by more than 20%
     between sessions** — larger than the effect being measured, so neither
     number meant anything.
  3. Built both binaries, kept them, and **interleaved** the runs: old median
     1016.5 ms, new median 1015.5 ms, distributions almost entirely
     overlapping. No effect.

  The reason is worth writing down: `gfx_fill_rect`'s inner loop is a plain
  indexed `for`, which the compiler vectorises. The per-call setup that looked
  wasteful is a rounding error next to the pixel writes themselves, and the
  hand-written clipper writes exactly as many pixels. The renderer is already
  at the limit of what this class of micro-optimisation can give it.

  What was kept is the method: **any performance claim here needs interleaved
  A/B against a saved binary**, because a before-and-after taken minutes apart
  measures the machine's mood. And the profile is recorded above so the next
  attempt starts from data rather than from the same guess.

  Also visible in that profile and *not* pursued: `bench_layout` recomputing
  its layout on every paint at 0.07 ms a time. Every app in this tree lays out
  inside its paint handler; it is cheap for the rest and that one is a test
  tool, so it is noted rather than changed.
- ✅ **Reversi: a Hint key, and a whole game played by the harness**
  (`app_reversi.c`, `--reversi-demo`): **H** offers the move the program
  would play in your place — the same positional judgement White uses, rather
  than keeping it for the opponent. Reversi is hard to read even with the
  legal squares marked; knowing *which* of six is worth having is the skill.

  The scene then uses that same call to play a **complete game** through the
  window, both sides, clicking real cells. Two moves cannot catch the failure
  `rev_core.h` warns about: a position where neither side can move is over
  with empty squares left, and a program that waits for sixty-four stones
  sits on a finished game forever. That is a hang, and only playing to the
  end can see it. The loop is bounded so a game that will not finish reports
  it instead of hanging the harness too. Conservation is checked on **every
  move**: the disc count must rise by exactly one and never fall.

  **The check's first version was hollow, and the control proved it.**
  Deleting the turn hand-off makes the pass counter reach two immediately, so
  the game was declared over after ONE turn with six discs — and the check
  said `OK`, because it asked only "did it end?". A game that ends instantly
  is precisely what it was written to catch. It now also requires the game to
  have been *played*: at least twenty turns and thirty discs, floors far below
  the real 58 turns / 64 discs and far above a broken one.

  ```
  REVERSI-DEMO: full game ended after 1 turns with 6 discs, over=1 (MISMATCH)
  REVERSI-DEMO: full game ended after 58 turns with 64 discs, 0 bad step(s) (OK)
  ```

  One honest note on coverage, since the passing rule is what this was aimed
  at: with both AIs deterministic the game ends by **filling the board**, so
  the scene does not actually exercise a mutual-pass ending. That path is
  covered in `test_rev.c` against a hand-built dead position instead.

  **And the Hint key itself went one commit unverified.** The scene exercised
  `app_reversi_hint` — whether the function has an answer — which is a
  different question from whether pressing **H** does anything, while Help had
  already been updated to tell readers it does. That is the CastaliaWrite
  defect exactly, in code written the same hour, by the person who removed it.
  H is now driven as a reader will drive it: park the cursor in the far
  corner, press the key, and require the cursor to land on the hinted square
  with the status line saying so. Unhandling H gives:

  ```
  REVERSI-DEMO: H moved the cursor to 0,0 (hint 3,2) and said 'Your move -- black' (MISMATCH)
  ```
- ✅ **The Quick Launch strip was wearing two different art styles**
  (`sh_taskbar.c`): rendering the launcher turned up something in the
  **shipped default**. The Tango pack has `ql-fileman` and `ql-notepad` but
  **no `ql-showdesktop` or `ql-mines`**, so those two fell through to the
  procedural glyphs — which were ~10 px and flat, sitting between two 16 px
  shaded colour icons. They read as icons that had failed to load.

  Two ways to fix it, and the tempting one is wrong: adding original 16 px
  BMPs to `assets/icons/tango/` would put my artwork inside a directory
  documented as the public-domain Tango set, which is exactly the kind of
  provenance smudge this project spends effort avoiding. Improving the
  **procedural** art instead is original code-drawn work by definition, and it
  fixes every configuration at once — including the no-pack case.

  So the monitor got a bezel, a graded screen and a stand, and the mine got a
  round shaded body, spikes and a red detonator cap.

  That immediately created the opposite mismatch: with no pack at all, the
  *folder* and *page* glyphs were now the flat undersized ones sitting next to
  two properly drawn neighbours. All four are redrawn together for that
  reason — raising two of them and stopping would only have moved the seam
  from one configuration to the other.

  Worth recording as a near-miss: the first reading of this was "the launcher
  has no icons at all", from a bare test home with no pack configured. The
  product ships the packs and defaults `[Assets] Icons=ICONS\TANGO`, so the
  Start menu is fully iconned on a real install. Rendering with the pack
  actually installed is what turned a phantom bug into the real and much
  smaller one next to it.
- ✅ **The brand mark can come from an image** (`sh_logo.c`, `sh_core.c`,
  `assets/icons/*/logo-*.bmp`): the logo looked bad, and there were two real
  reasons.

  **The castle was drawn at 9x9 pixels no matter how big the mark was.** The
  sprite scale is `k = ((r_in * 165) / 100) / aw` — integer division, `aw` = 9
  — which floors to **1 for every size from 20 to 43 px**, exactly the range
  the Start orb and the launcher header live in. A 9-pixel castle inside a
  34-pixel mark is a speck floating in a white hole, which is how it read.

  **And the Start orb never saw the icon pack.** `sh_taskbar_layout()` bakes
  the orb into cached sprites, and `sh_desktop_init()` — which tells the pack
  where it lives — runs on the *next line*. That never mattered while the mark
  was procedural; the moment a pack could supply artwork, the button would
  keep the fallback for the whole session while every other surface showed the
  real logo. The orb is rebuilt after the pack is known rather than swapping
  the two calls, because `taskbar_layout` also sets the WM work area the
  desktop wants in place.

  `sh_logo_draw` now prefers `logo-16/24/32/48/64/128` from the active pack,
  baked by `tools/png2bmp.py`, choosing the smallest baked size at least as
  large as the request so any remaining scaling is a reduction. The procedural
  drawing stays as the bare-install fallback. `sh_logo_draw_plated` skips its
  pale backing disc for an image — that disc is tuned to the procedural
  geometry and pokes out from behind a finished mark as a blob on one side.

  The `logo-*.bmp` files are **original CastaliaOS artwork in every pack,
  including `tango/`** — the product's own identity, not part of the
  public-domain Tango set. Recorded in `assets/icons/README.md`.
- ✅ **A check that failed at random** (`app_freecell_set_seed`,
  `--freecell-demo`): running the suite for the logo work turned up a red
  FreeCell scene, unrelated to it. **The deal is seeded from the clock**, and
  "Send Home puts an ace up" depends on the deal — six runs failed twice. A
  board that exposes no ace looks exactly like a broken Send Home.

  A check that fails at random is worse than no check: it teaches the reader
  to ignore a red result, which is the one habit this harness cannot afford.
  The scene now forces a deal (seed 23, found by *searching* for one that
  exposes two aces rather than guessing) and restores the clock shuffle
  immediately, so players still get a random game. With the board fixed the
  bound was tightened from "at least one ace" to **exactly two** — the loose
  form would have hidden a Send Home that found one of the two.
- ✅ **The real logo is in** (`presskit/castalia-98-logo.png`,
  `assets/icons/*/logo-*.bmp`): Dave's mark, baked to the six sizes with
  `--pad` so the 256x269 source is letterboxed to square rather than
  stretched, and dropped into both packs. It now appears on the Start orb, the
  launcher header, the About crest, the Welcome banner and the boot splash.

  The procedural drawing was also repaired rather than left to rot, because it
  is still what a pack-less install gets:

  - **the scale floor** described above is fixed by rounding, and the middle
    range (30-55 px) now uses the *detailed* 15-cell cut at 1:1 instead of the
    bold 9-cell one. Whole-pixel scaling means a 9-cell sprite can be 9 px or
    18 px and nothing between: in a 34 px mark, 9 is a speck and 18 swamps the
    ring the castle is meant to sit inside. Fifteen was the size that was
    missing, and it is the density that art was drawn at.
  - **the 2x2 dither is suppressed below 64 px.** A checkerboard over a
    twenty-pixel hill is not a highlight, it is noise: half the pixels
    alternate and the two greens average into one muddy tone. It earns its
    place on the 80 px splash and nowhere smaller.

  Both were found by rendering the mark at every size the shell actually asks
  for, rather than by reading the code — the arithmetic looks reasonable until
  you notice which integers it lands on.
- ✅ **Three windows had no icon, and a dozen had no entry** (`sh_icon_for_title`,
  `--winicon-demo`): adding Reversi to the launcher raised the question of the
  *other* icon — the one in the title bar and on the taskbar button, which is
  assigned separately by matching the window's title against a table. Reversi
  had no entry. Neither did fourteen other apps. And **two existing entries
  were quietly dead**:

  - the table said `"Paint"`; the window is called **`"CastaliaPaint"`**. The
    match was a *prefix*, so it never fired and Paint had no icon at all — an
    entry that looks alive in the source and does nothing on screen.
  - the office apps put the document first — `"Book1 - CastaliaSheet"`,
    `"Document1 - CastaliaWrite"` — which no prefix can ever reach.

  Matching is now a substring, ordered most-specific-first so `"Hex Viewer"`
  and `"Log Viewer"` are tested before the bare `"Viewer"`, and every app has
  an entry. The scene opens twenty windows and asks the WM what each got;
  restoring the old prefix match and the `"Paint"` key gives:

  ```
  WINICON-DEMO: 3 of 20 app windows have no icon, first: CastaliaPaint (MISMATCH)
  ```

  Three, and exactly the three a prefix could not reach — which is the check
  agreeing with the diagnosis rather than merely going red.

  One wrinkle worth recording: window icons come from the **pack**, and there
  is no procedural fallback for them, so in the bare world each scene runs in
  the honest answer is *twenty of twenty missing*. The scene therefore points
  at the in-tree pack with `--icons`, which needed the per-scene extra-args
  field to learn an `@ROOT@` placeholder — the table is single-quoted, so a
  shell variable written there would have arrived verbatim.
- ✅ **`tree` in the Console** (`app_console.c`, `--console-demo`): the shell
  had thirty commands and not the one that shows you where you are. DOS TREE
  with `/A` -- ASCII elbows, because the shipped font is 32..126 and a
  box-drawing character comes out as the missing-glyph box. The prefix is
  carried down as a string so a deep child lines up under its parent's rail.

  Bounded on two axes, and both bounds are load-bearing rather than tidiness:
  **depth**, because a real disk goes deeper than this console's scrollback
  and the useful part is the top; and **lines**, because the walk recurses and
  a pathological tree would push everything else out of a ring somebody is
  reading. Truncation is *announced* — a tree that silently stops looks
  exactly like a tree that ended, which would have the reader believe a folder
  is empty.

  The check reads the scrollback back rather than counting it, because a line
  count cannot tell a folder tree from an error message. It requires elbows
  *and* a folder that is really there. Making the walk return immediately
  gives:

  ```
  CONSOLE-DEMO: tree drew 0 elbow(s), SYS named 0 time(s) (MISMATCH)
  ```

  Adding it also broke a neighbouring check on the first try: the new command
  went in *before* "three commands remembered", which then counted four. Moved
  after, so the existing checks still mean what they said.
- ✅ **Control Center: Mouse and Keyboard** (`app_control.c`, `settings.c`,
  `plat_set_key_repeat`, `--mousekey-demo`): the last two categories, which
  completes the Control Center line on the roadmap.

  Writing them turned up **the same duplication for a third time this
  session**. The double-click window was `#define ICON_DBLCLICK_MS 400` in
  `sh_desktop.c` and, independently, `#define WM_DBLCLICK_MS 400` in
  `wm_dispatch.c`: the desktop icons and the title bars each had their own
  idea of what a double-click is, agreeing only because nobody had touched
  either -- and a Mouse panel needed a third. It is one setting now, read by
  both, and clamped on load because a hand-edited INI is a supported way to
  configure this system and a `DoubleClickMs=0` would make every double-click
  impossible with nothing on screen to say why.

  A settings panel is easy to write and easy to leave decorative, so the scene
  is about whether anything downstream CHANGED. The double-click one is
  measurable: set the fastest speed, then click a desktop icon twice **300 ms
  apart**. Under the old hard-coded 400 that was a double-click; under a
  setting the desktop really reads it is two single clicks and nothing opens
  -- and then back-to-back clicks must still open it, so the check is not
  merely "clicking an icon does nothing".

  The keyboard rate has no such handle: nothing above the platform layer is in
  the key-repeat loop, so on a host there is no behaviour to observe. **The
  first version of that check was worthless** -- it read back the settings
  struct, which passes with `plat_set_key_repeat` deleted entirely. The second
  tried to grep the session log and failed for a duller reason: the run had no
  log file. What works is a host-side record of the last rate the platform was
  ASKED for, which is the whole path -- panel, settings, apply, platform --
  and does fail when the call is removed.

  Applying at startup as well as on Apply matters for the same reason: a rate
  saved last session that only takes effect once somebody re-opens the panel
  looks exactly like the panel not working.
- ✅ **Control Center: Date & Time** (`app_control.c`, `plat_set_wall_clock`,
  `plat_set_wall_date`, `--datetime-demo`): the roadmap's remaining Control
  Center category, and the one the target hardware actually needs -- a 1999
  machine out of a cupboard comes up in 1980 with a flat coin cell, and until
  the clock is right every file it saves is stamped wrong and the Agenda is
  useless.

  Six stepped fields rather than typed ones: no text-entry control to build,
  nothing that can be left holding half a number, and no way to express a date
  that does not exist -- the day clamps into the month through
  `cal_clamp_day()` the moment the year or month moves, which is what the
  shared calendar was extracted for. It has its own Set button rather than
  riding on OK/Apply, because setting a clock is an action against hardware
  and not a setting to be staged.

  The platform grew `plat_set_wall_clock` and `plat_set_wall_date` (INT 21h
  AH=2Dh and AH=2Bh). The host implements them by **refusing**: it returns
  CE_UNSUPPORTED and the panel says "This platform will not let a program set
  its clock." A silent no-op returning CE_OK would look identical in every
  screenshot and be a lie on the only machine that matters, so the scene
  checks the refusal is reported rather than dressed up.

  **Three mutations were written before one of them was a real bug.** Shifting
  the button geometry proved nothing twice over: painter and hit test share
  `cc_dt_minus()`, so moving it moves both and the click still lands -- the
  design forecloses the draw/hit disagreement class rather than needing a test
  for it. What survives is a painter that diverges on its own, so the scene
  now samples the pixels where it is about to click and requires a button to
  actually be there. That catches "the minus buttons are never drawn" and "one
  row's minus is drawn 60 px off"; the uniform relocations it does not catch
  are not defects.

  The scene also had to learn to click a MINUS button: it only ever clicked +,
  and a whole row of rectangles was going untested.
  (`src/sys/cal_core.c`, `tests/test_cal.c`): groundwork for the Control
  Center's Date and Time panel, and the same shape as the `ui_menu_step` find
  earlier -- leap years and month lengths existed in `agenda_core.c` and again
  in `app_clock.c`, and a third caller was about to need them. **The two did
  not agree**: asked how long month 13 is, one answered 0 and the other 30.
  Thirty is the more dangerous answer, because it looks like a measurement.

  One copy now, and tested for the first time. The leap rule is checked at the
  two years that separate a correct implementation from the common one --
  **1900 is not a leap year and 2000 is** -- because anything testing only
  "divisible by four" passes on both counts and is wrong for eight decades
  either side. The weekday is checked against dates verifiable outside this
  repository, and structurally: every weekday appears exactly once across a
  week, and the sequence runs continuously across a leap day and a year end.
  Five mutations, all caught, including the naive leap rule and the other
  copy's answer for an impossible month.

  `cal_valid` fixes the range at **1980..2099**, which is DOS's and not an
  opinion: INT 21h will not set a date before 1980, so a panel that accepted
  1979 would take the value, fail silently at the platform layer, and show the
  old date back with no explanation.

  One behaviour deliberately kept: `app_clock` wraps `cal_days_in_month` and
  turns the honest 0 back into 30, because its month-browsing state clamps
  against that number and a zero would collapse the view. The difference is
  now at the one place that cares rather than baked into the shared answer.
- ✅ **Over half the Character Map was the missing-glyph box**
  (`app_charmap.c`, `gfx_font.c`, `tests/test_font.c`): the clearest thing the
  render-and-look pass turned up. The grid ran codes 32 to 255 -- 224 cells --
  and both bundled faces carry exactly 95 glyphs, printable ASCII and nothing
  else. So **129 cells drew the same hollow rectangle**: the shape
  `gfx_font.c` renders for a code it has no art for, which is right for text,
  where a missing glyph should be visible rather than silent, and exactly
  wrong for the one window whose whole job is to show what is available. Eight
  rows of identical boxes, offered as though they were characters.

  The map now asks the font. `gfx_font_range()` reports what a face actually
  holds, the grid is built from it -- six rows instead of fourteen, and the
  window is shorter to match -- and the status line says "Codes 32 to 126 --
  every glyph this face has", so the codes that are absent read as a fact
  about the font rather than as the window having lost them. Asking rather
  than hard-coding 126 in a second place is the point: a face with wider
  coverage widens the map on its own.

  The range is a claim, so it is checked against the glyph data rather than
  against the constant next to it: every code inside the range must draw
  something OTHER than the box, and the codes just past each end must draw
  the box. Three mutations -- claiming 255, claiming 100, starting at 16 --
  all caught. The box pattern itself is checked to be non-empty first, or
  "differs from the box" would be trivially true.
- ✅ **The Delete button was a paper shredder** (`tools/build_tango_pack.sh`,
  `app_fileman.c`): same method as the entry below -- render it, look at it,
  then zoom in on anything that reads oddly. The File Manager toolbar's Delete
  came from Tango's `actions-edit-delete`, which at 64 px is unmistakably a
  paper shredder and at 16 px is a grey-blue box that scans as a printer. It
  is now `places-user-trash`, the same green bin the desktop's Recycle Bin
  uses -- which is also where the file actually goes, so the button now shows
  its own destination.

  Two more icons were made and then thrown away. `actions-document-properties`
  for Rename and `actions-view-fullscreen` for 2-Pane both came out muddy at
  16 px -- one unreadable, the other saying "fullscreen" -- and those two
  buttons draw their label as text when they have no icon. **A word beats a
  bad picture**, so the files were deleted and the text buttons kept.

  The status bar said the same thing twice: "3 object(s)" in the left cell and
  "3 items" in the right. The right cell is the File Manager's only line for
  telling somebody anything -- it carries "Renamed to X", "Compressed, saved
  N bytes", "Cannot name a compressed copy of that" -- and its resting state
  was spending that line on a fact already on screen. It now says how much is
  in the folder ("42 bytes in 2 files"), which is the number that answers
  "will this fit on a floppy". Checked in `--hex-demo`, and the check needed
  two attempts: the first mutation left the `else if` chain to overwrite the
  status again, so the control passed and proved nothing.
- ✅ **Ten Start menu entries shared one icon** (`tools/build_tango_pack.sh`,
  `assets/icons/tango/`, `sh_launcher.c`): found by rendering the desktop and
  LOOKING at it, which is not something any check in this repository does.
  Paint, CastaliaSheet, Media Player, Benchmark Suite, Clock, Character Map,
  Network, Capture Screen, Run and every add-on all fell back to `m-app`, the
  generic diamond, because the pack had no art for them -- so a third of the
  left column was the same orange shape repeated down the page. Solitaire and
  Mines shared one too, and CastaliaWrite borrowed Notepad's.

  Thirteen new icons, from the same Public-Domain Tango source and the same
  reproducible script, so provenance stays documented and the pack can still
  be rebuilt from nothing. Two were chosen by comparing candidates at the size
  they are actually drawn rather than by name: the play triangle intended for
  Media Player is a pale outline that vanishes on a white menu, so it is a
  musical note instead. And the first attempt at Solitaire pointed `m-cards`
  at the same Tango source `m-mines` already used, which produced two
  identical icons and a new file for nothing -- Mines now takes a red octagon.
  Tango has no bomb; that is a stand-in, and saying so is better than
  pretending the match is good.

  Nothing here is checkable by a test -- "these icons are distinct" is not a
  property the gate can hold, and inventing a check that counts BMP files
  would not have caught any of it. The screenshot is the evidence.
- ✅ **The File Manager's menu had no keyboard** (`app_fileman.c`,
  `src/ui/ui_menu.c`, `tests/test_ctrl.c`): found while wiring View Bytes into
  it. With the context menu open, **every key except Esc was swallowed** --
  the `fm->ctx_open` branch returned -- and nothing opened it but the right
  mouse button. So Properties, Compress, Back Up, Restore, Disk Usage and the
  new View Bytes were mouse-only, on a system whose own hardware verification
  is carried out with a keyboard alone and no mouse driver loaded.

  The reason it was missing is worth recording: stepping a highlight past
  separators and disabled rows existed **twice** -- once in the desktop menu,
  once in the launcher, character for character apart from which rectangle
  each marked dirty -- and a third caller needing it simply went without.
  There is one copy now, `ui_menu_step()` with `ui_menu_selectable()`, which
  also makes it worth testing properly for the first time: wrapping at both
  ends, skipping both kinds of unselectable row, a menu with nothing choosable
  in it coming back instead of spinning, and moving reported as CTRUE only
  when the highlight actually changed, so a caller does not repaint on
  every key.

  **F10 opens it, not F3.** F3 was the obvious free function key and the wrong
  one: it already means Find Next in Notepad, and `--cz-demo` was pressing it
  expecting nothing to happen -- which is exactly how that scene caught the
  collision, by failing the moment F3 started opening a menu. F10 is the
  convention and needed a new key code, so the DOS scan table learned 0x44.

  The scene walks it the way somebody without a mouse must: File Manager, End
  to the file, F10, arrows to View Bytes, Enter. It finds the item **by name
  rather than by counting Down presses**, because a count starts choosing the
  wrong command the moment the menu gains an entry -- which this very commit
  did to it. Four negative controls, including both halves of the original
  bug.
- ✅ **Hex Viewer** (`src/apps/app_hex.c`, `src/apps/hex_core.c`,
  `tests/test_hex.c`, `--hex-demo`): the tool for "this file will not open".
  Is it really a bitmap, did the download stop half way, what is actually in
  the first sector -- questions with no other answer on a machine that has
  nothing else installed. In the File Manager's context menu as **View Bytes**,
  offered for every file rather than only the ones nothing else can open,
  because a file that DOES open in Notepad can still be the wrong file.

  It reads only the rows on screen: a seek to the row the view starts at and
  one read for a screenful, so a forty-megabyte file costs what a forty-byte
  one does. That needed a new platform primitive -- **`plat_fseek`** -- because
  the file contract had `fread`, `fwrite` and `size` and no way to start
  anywhere but the beginning. Absolute offsets only: "from the end" is
  `plat_file_size()` minus what you want, and one form is one thing to get
  right on two backends.

  The layout is where hex dumps actually go wrong, and always in the same
  place: the last row, which is nearly never sixteen bytes long. Pad it wrong
  and the text column of the final line slides left -- which looks like a font
  problem and is the one line somebody is squinting at. So `hex_core.c` is
  pure, and `tests/test_hex.c` walks every short-row length from 1 to 15
  requiring the text column to start at the same character every time. High
  bytes are dots rather than passed through: on a DOS codepage they are
  box-drawing characters, and binary rubbish would render as a table with
  borders. Five mutations against the core, all caught -- the padding one
  fails 154 checks.

  The scene's job is the claim a window cannot make on its own. The test file
  is built so the byte at any offset is a function of that offset, so scrolling
  anywhere produces bytes that say where they came from: **a viewer that
  loaded the first screenful and then only moved its address column would pass
  every check about offsets and fail every one of these.** Five mutations, and
  one had to be tightened -- "it costs one screenful" first asked only for less
  than the file, which a viewer reading twice what it draws satisfies happily.
  It now asks for exactly the rows on screen.

  Found while wiring it, and fixed in the entry below: the File Manager's
  context menu could not be operated from the keyboard at all.
- ✅ **An idle frame is eleven times cheaper** (`sh_core.c`, `sh_cursor.c`,
  `--cursor-demo`): the shadow tables took a still desktop from 37,380
  instructions a frame to 18,717, and then the profile said the pointer was
  *still* 70% of what was left. It was being redrawn every frame whether or
  not anything had happened -- and worse, its old rectangle was being added to
  the repaint region every frame, so the desktop underneath was re-composited
  from scratch sixty times a second to make room for an arrow that had not
  moved.

  Now the rectangles go in only when the pointer actually moved, and the arrow
  is redrawn when it moved or when SOMETHING was repainted -- anything
  composited this frame may have painted over it. When nothing was repainted
  at all, the arrow is still sitting on the back buffer from last frame,
  correct and untouched. **3,408 instructions a frame**, from 37,380: eleven
  times cheaper, byte-for-byte identical, all 29 scenes green. The arrow also
  became one `GFX_BLIT_KEYED` blit of a sprite baked once, instead of 204
  `gfx_put_pixel` calls -- which is what that primitive is for, and gets the
  arrow covered by `tests/test_blit.c` for free.

  **This uncovered a trap that had been harmless only by accident.**
  `plat_present(rects, 0)` does not mean "present nothing", it means "present
  the whole surface" -- on the DOS backend, the entire 800x600 framebuffer
  pushed over the bus. Nothing could reach it before, because the cursor added
  its rectangle unconditionally and the region was never empty. Letting an
  idle frame have an empty region walks straight into it, so the call is now
  guarded. That guard is load-bearing and cannot be tested from a host, where
  `plat_present` is a no-op.

  Four negative controls, and the last two are the interesting ones. Removing
  the redraw-on-repaint branch passed EVERY check the scene had, because
  nothing in it ever repainted under a still pointer -- the scene now parks the
  arrow on a window, invalidates the window and takes a frame with no input.
  And a pointer that holds still has to SURVIVE the frames that no longer draw
  it, which is its own check: eight frames of nobody touching the mouse.
- ✅ **Three quarters of an idle frame was the mouse pointer's shadow**
  (`sh_cursor.c`, `--cursor-demo`): profiling the frame after the save-under
  came out gave an answer worth stopping over. Differencing a 360-frame run
  against a 120-frame one -- which is the only way to separate the one-time
  wallpaper bake from the per-frame cost -- put a steady idle frame at 37,380
  instructions, and **28,535 of them were the cursor's drop shadow**. 76% of
  everything an idle desktop does, to darken a dozen pixels behind an arrow.
  Confirmed by suppressing the shadow: the frame fell to 8,845.

  The arithmetic was never the problem. Three darkening steps per inked cell
  of the arrow, each a `gfx_get_pixel`, a `gfx_tint` and a `gfx_put_pixel`,
  is 300 read-modify-writes a frame through functions that bounds-check and
  recompute a row pointer every time. Neighbouring cells overlap, so a pixel
  could take up to three steps in sequence -- which is where the falloff comes
  from, so it had to be preserved exactly, not tidied away.

  It is now worked out once. Every pixel of the shadow box gets a slot number,
  every slot a 256-entry table from channel value to final darkened value, and
  drawing is one read, three lookups and one write per pixel -- roughly 150
  pixels instead of 300 read-modify-writes. **The idle frame halves: 37,380 to
  18,717**, and every screenshot is byte-for-byte identical.

  That identity is the point, and it decided the design. The obvious version
  folds the three steps into one multiplier; measured against all 256 channel
  values and all five step sequences, that loses up to 2 per channel to
  truncation. Nobody could see it in a drop shadow -- and it would have thrown
  away the byte-for-byte comparison, which is the only real evidence a change
  made purely for speed did not change anything. So the tables replay the
  original steps in the original order instead.

  The first table build cost 962,254 instructions, because slots were matched
  by comparing whole tables. Nothing on a host; forty milliseconds of startup
  on a 25 MHz 486. Matching on the three-byte step sequence instead brought it
  to **30,461**, at the cost of occasionally giving two orderings a slot each
  when they would have agreed -- there is room for nine in sixteen.

  Four mutations, all caught, but only after two of them were rewritten: "all
  pixels get the same slot" turned out to still produce a falloff as first
  written, and the "transposed" one only transposed the read. The scene needed
  a falloff check to catch the real version -- the shadow's core must be
  darker than its edge, and the edge must still be shadow.
- ✅ **The cursor's save-under had never erased anything** (`sh_cursor.c`,
  `--cursor-demo`): closing the last blind spot on the mutation map turned up
  something better than a missing test. `sh_cursor_draw()` was the one function
  the whole gate could not see; the scene that covers it moves the pointer onto
  clear desktop, reads the back buffer, and checks that the outline and the
  fill are inked in different colours, that the transparent cells of the 12x17
  arrow are left alone -- a solid block would satisfy "the pixel changed"
  perfectly and would not be an arrow -- and that moving away restores every
  pixel exactly.

  Then two of the five mutations written against it did not fail. Removing the
  save-under entirely, and capturing the wrong rectangle, both went unnoticed.
  The scene was not weak: **the save-under had never done anything.** `sh_core.c`
  adds the cursor's previous rectangle to the repaint region, and it has no
  choice, because that region is also the list handed to `plat_present()` --
  a rectangle left out of it is one the physical framebuffer never hears
  about, so the old arrow would stay on the glass whatever the back buffer
  said. Being in the region means being re-composited from the desktop up. The
  restore was overwritten in the same frame, every frame, since the file was
  written.

  Deleted, along with `gfx_capture_rect`/`gfx_restore_rect`, which existed for
  it and had no other caller. **504,221 instructions saved over a 40-frame
  run** -- about 12,600 a frame against a steady-state frame of roughly 50,100
  -- and every screenshot byte-for-byte identical, back to back, which is what
  makes that a measurement rather than an argument. (An earlier comparison
  showed a 39-byte difference and was a false alarm: the two runs were minutes
  apart and the taskbar clock had advanced.)

  The erase is now `cregion_add(&R, &oldc)` and nothing else, said at both
  sites, because two mechanisms each guaranteeing the same thing is exactly
  how both come to be removed -- each looks redundant on its own.
  `--cursor-demo` fails if that line goes.

  **What no host check can catch:** whether the cursor's NEW rectangle reaches
  the framebuffer. That is `plat_present()`, and on a headless host the back
  buffer is the framebuffer -- dropping `newc` changes nothing here and
  everything on real hardware. Verified: that mutation passes all four checks.
- ✅ **The DOS backend had never been compiled by anything** (`tools/wcshim/`,
  `tools/check_dos_syntax.sh`): the natural follow-on from the link bug below,
  and a bigger hole than that one. `src/platform/dos` is ~2,100 lines -- every
  interrupt call, the VESA driver, the mouse, the keyboard, the packet driver,
  the Sound Blaster, and now CPUID and the machine inventory. The host build
  excludes it by design, `c89_lint.sh` reads the portable source list, no
  runner has Open Watcom, and CI does not build it either. **Nothing in this
  repository's history had ever parsed those files.** They got one attempt to
  be right, on hardware, with no way back -- and the last two commits added a
  hundred lines to them.

  `tools/wcshim/` declares enough of Watcom's surface (`<i86.h>`, `<dos.h>`,
  `<conio.h>`, `<direct.h>` -- `union REGS`, `struct find_t`, `int386`,
  `segread`, `FP_SEG`, `inp`/`outp`, `_dos_findfirst`) for gcc to parse them
  with `-fsyntax-only`. All nine sources do, which is a fact worth having
  rather than a fact worth assuming: the five negative controls are a typo'd
  `REGS` field, a call to an undeclared function, a declaration after a
  statement, a wrong argument count, and a missing semicolon. All five caught.

  **What a pass does not mean, since a green check invites the opposite
  conclusion:** it is not evidence Open Watcom accepts the file, the `#pragma
  aux` inline assembly is skipped entirely by gcc so nothing anywhere says
  whether those opcodes assemble, and the shim's struct layouts are
  approximate -- which does not matter for syntax and would matter enormously
  for a build. The script says all of that where somebody reading a green line
  will see it, and says that an error from a stub header must be checked
  against Watcom's documentation before the source is changed, because editing
  correct code to satisfy a stub is worse than not having the stub.

  It found one real defect immediately: `dos_dpmi.h` had a `/*` inside a
  comment, which is a warning on gcc and undefined behaviour in C89.
- ✅ **System Information knows what machine it is on** (`src/sys/mach_core.c`,
  `tests/test_mach.c`, `src/apps/app_sysinfo.c`, `--sysinfo-demo`): the window
  reported the video mode and the heap and said nothing about the computer.
  It now asks DOS its version, asks the DPMI host what memory it will part
  with, and walks the PCI bus, with the naming split off into a pure module
  for the same reason `cpu_core.c` exists -- reading a configuration dword is
  four registers, deciding that 1013h:00B8h is a Cirrus GD 5446 is a table
  with edge cases. 64 checks, five mutations, all caught.

  The rule the tests pin is that the module never GUESSES. An unknown vendor
  comes back as `PCI 0BAD:F00D`, a known vendor with an unknown part as
  `Intel device 1F30`, and an empty slot (0FFFFh:0FFFFh) is not dressed up as
  a device -- because the reason to open this window on unfamiliar hardware is
  to find out what is actually in it, and a wrong name is worse than a number.
  FreeDOS gets the same treatment from the other side: it reports whichever
  DOS version it is configured to impersonate, so the line reads "FreeDOS
  (reports DOS 7.10)" rather than naming a release that has never existed.

  The memory figure is the DPMI host's largest free block, not the XMS
  driver's total. Under a 32-bit extender those are different numbers and only
  the first one predicts whether this system will run.

  **F2 writes the whole report to CASTINFO.TXT**, and that is the part with a
  real bug behind it. Fitting the window to `APP_MAX_LINES` was always wrong:
  forty lines is 584 pixels, which runs off the bottom of a 640x480 mode --
  the mode the DOS backend falls back to on exactly the machine whose System
  Information somebody needs. The window drew all forty and the screen simply
  ended. Now the drawing stops where the screen does, the count of what is
  below is shown, and the SAVE still writes every line the window holds. The
  scene proves the gap rather than the file: it runs at 240 pixels tall, and
  checks that 13 lines are drawn, 20 are held, the file has 20, and it
  contains a line the window never drew. Four negative controls, including a
  save that writes only the visible lines -- which produces a file, and would
  have satisfied any check that only asked whether one appeared.

  The environment rows carry the same rule one level down. A DOS `PATH` runs
  to 127 characters and the row holds 76, so a value that did not fit ends in
  `...` -- on the line somebody reads to work out why a program will not
  start, a silently shortened PATH looks like a short PATH, which is the worst
  available way to be wrong. Truncation is MEASURED rather than inferred from
  the row coming back full, because a value that fills the row exactly is not
  truncated and marking it would destroy three characters of a correct answer
  in order to say something untrue about it. Three more mutations, all caught,
  including that one.
- ✅ **The DOS product could not have linked** (`src/platform/dos/plat_dos.c`,
  `tools/check_dos_build.sh`): the worst find so far, and it was sitting in
  plain sight. `plat_cpu_id()` is declared in `plat.h`, called unconditionally
  by System Information and by `--cpu-check`, and **implemented only in
  `src/platform/host/`** -- which is host-only by definition. Both callers are
  in the DOS product's link list. `wlink` would have stopped with an undefined
  symbol; every host build, every host CI job and every one of the 2750 checks
  passed, because the host has an implementation and no runner here has Open
  Watcom. The processor line that `cpu_core.c` and `tests/test_cpu.c` exist to
  produce was reachable on the one platform that does not need it and on no
  DOS machine at all.

  `check_dos_build.sh` was written to catch precisely this failure -- its own
  header says it stops the "undefined symbol that names the symbol rather than
  the missing file" -- and it walked past it, because it matches **files** and
  what was missing was a **function**. It now also requires every function
  declared in `include/castalia/*.h` to be DEFINED somewhere in the DOS link
  set (the portable sources minus the host-only exceptions, plus `main.c` and
  `src/platform/dos`). All 350 resolve, now that CPUID does; `plat_cpu_id` was
  the only one that did not.

  Two drafts of that check were wrong in ways worth recording, because both
  produced a confident green. Looking for definitions **only** in
  `src/platform/dos` reported nine sound functions and four network functions
  as missing, all of which link perfectly -- the platform contracts are split
  on purpose, and `snd_click()` and `net_checksum()` are portable. Requiring a
  declaration to end in `);` to be recognised silently skipped the **55
  prototypes that wrap onto a second line**, which is the same shape of hole
  the check exists to find. Five negative controls now: the historical bug,
  a newly declared function nobody implements, a link-set file that only
  *prototypes* one -- which must not count -- a real definition, which must,
  and a multi-line declaration, which the second draft could not see.

  The implementation reads CPUID through `#pragma aux`, guarded by the EFLAGS
  bit-21 toggle so it never executes the instruction on the 386 and early 486
  this system still targets. The three opcodes are written as `db` bytes: no
  host can compile this file, so it gets exactly one chance to be right on the
  target, and a byte cannot be rejected by an assembler that has never heard
  of the instruction. Decoding stays in `cpu_core.c`, which is tested; this
  side only reads registers. **Still unverified by anything here:** whether
  the DOS backend compiles at all. The symbol check is a proxy for a link, not
  a link, and that gap closes only when Open Watcom runs.
- ✅ **The flood fill could be made to spin forever** (`src/apps/paint_core.c`):
  the sweep's last find, and it did not fail a check -- it HUNG the suite.
  Making `gfx_fill_rect()` a no-op should have failed a pile of paint checks
  and stopped; instead `run_tests` printed five failures and never returned.
  `gfx_hline()` is written as a one-pixel-tall `gfx_fill_rect()`, so breaking
  the fill broke the line, and `pc_flood()` walks outwards until the pixels it
  painted stop reading as the target colour. Its termination depended on its
  own drawing landing. `gfx_fill_rect` honours the surface clip, so the same
  thing happens with the code perfectly intact the moment somebody narrows the
  canvas clip past the fill area -- and "fill inside the selection" is an
  obvious next feature. In a cooperative single-threaded shell that is not a
  slow frame, it is a dead machine with no way back. Bounded: no honest fill
  covers a pixel twice, so once more than `w * h` pixels have been painted the
  drawing is going nowhere and the loop stops. Every current caller is safe --
  `app_paint.c` only ever RESETS the canvas clip, never narrows it -- so this
  was latent rather than live, the same category as the `sys_calloc` overflow
  above. The test clips the fill away and requires it to return; note that
  removing the bound makes that test **hang rather than fail**, which is worse
  than a clean failure and is said here rather than hidden, because it is the
  unavoidable shape of testing a termination guarantee. Confirmed by building
  the unbounded version and watching it die on a timeout.
- ✅ **The blit primitive had no test either** (`tests/test_blit.c`): third
  find from the same sweep. Making `gfx_blit()` copy nothing broke no unit
  check, no demo scene and no abuse world -- and that is every desktop icon,
  every File Manager thumbnail, the cursor sprites, the Start orb, the
  wallpaper, and the per-frame blit of the cached desktop background onto the
  screen. The desktop could have gone blank with the whole gate green. The new
  suite is about placement and about what is NOT written: source values are a
  function of position, so a blit landing one pixel off shows up as a wrong
  VALUE rather than a wrong count, and every case fills the destination with a
  sentinel and requires everything outside the expected rectangle to survive
  (a one-pixel row overrun is caught by that alone). The keyed path is checked
  in both directions, since a keyed blit that copied nothing would satisfy
  "the key was skipped" perfectly. Four mutations, all caught.

  **`sh_cursor_draw()` was the last uncovered one** -- it could be made a
  no-op with nothing noticing. Now covered by `--cursor-demo`; see the entry
  above, which also records what that scene found when it was written.
- ✅ **The memory budget was reading an unchecked instrument**
  (`tests/test_mem.c`, `src/sys/sys_mem.c`): the same mutation sweep found a
  worse one. Making `sys_mem_peak_bytes()` return a constant **zero** broke
  nothing at all -- no unit check, no demo scene, no abuse world -- and yet
  `--mem-check` is the guard on this project's central constraint, the README
  quotes its reading as a fact, and the capture scene added days earlier
  asserts that saving a screenshot does not cost a megabyte. Three claims, one
  instrument, never checked. The accounted allocator now has a suite, and what
  it checks is the instrument rather than any allocation: live moves by exactly
  what was asked for, **peak is a high-water mark that does not fall when live
  does** (the property `--mem-check` rests on entirely), and a wrong size on
  free clamps instead of underflowing an unsigned counter to four billion,
  which would make the budget report an impossible number and pass. Writing it
  turned up a real defect: `sys_calloc(count, size)` computed the product in 32
  bits with no overflow check, so `sys_calloc(65536, 65536)` came to zero,
  became a one-byte allocation, and was handed back as a non-NULL pointer the
  caller believed addressed four gigabytes. Every caller in the tree passes a
  literal 1, so it was latent rather than live -- but a memory primitive should
  not be safe only because of who happens to call it. Guarded, and the guard's
  removal is confirmed to fail (it accounts 536,883,248 bytes from a wrapped
  product). Four mutations, all caught.
- ✅ **The gate was blind to text rendering** (`tests/test_font.c`): found by
  mutation rather than by reading. Breaking one subsystem at a time and asking
  the whole gate whether it noticed gave a coverage map:

  | deliberately broken | unit checks | demo scenes | abuse |
  |---|---|---|---|
  | the clipboard never stores anything | **7 fail** | none | none |
  | every file write silently no-ops | — | 3 of 25 | — |
  | no window is ever composited | — | 3 of 25 | — |
  | the desktop layer is never painted | — | 1 of 25 | — |
  | **`gfx_draw_text` draws nothing** | **0** | **1 of 25** | **0** |

  The clipboard result is fine -- the unit suite covers it, which is where that
  belongs. The last row was not: every menu, list, header, title bar and status
  line in the system could render blank and the whole gate would go green apart
  from the crash screen, which counts rows of advice and noticed by accident.
  The most-called drawing function in the shell had no test.
  `tests/test_font.c` now covers it, and the check that matters is not "ink
  appeared" but that **`gfx_text_width()` agrees with where the ink stops** --
  every right-aligned column and centred label in the shell measures with one
  and draws with the other, and a drift between them breaks every layout
  quietly. Ink is not evidence either: a glyph table indexed one byte off draws
  something for every character, so different characters must produce different
  pixels (that mutation is caught by a space no longer being empty). Four of
  five font mutations are caught; the fifth -- dropping the glyph blitter's
  horizontal clip clamp -- changes no pixel, because **no glyph in either
  bundled face inks past cell column 4 of 8**, measured rather than assumed.
  That is recorded in the suite instead of covered by a check that would always
  pass.
- ✅ **Archive viewer** (`src/apps/app_archive.c`): backing a folder up into a
  `.CAR` worked and restoring one worked, but you could not LOOK. Opening a
  `.CAR` from the File Manager unpacked every member into whatever folder you
  were standing in -- and members are written with `"wb"`, so anything already
  there under the same name was gone -- before telling you what the archive had
  contained. The window now lists the members (name, original, stored, saved)
  and says **how many of them already exist where they would land**, which is
  the number the window exists for. It reads the DIRECTORY and nothing else:
  `car_core.c` puts fixed 32-byte records at the front of the file precisely so
  a reader can list an archive without decompressing a byte, and this is the
  code that finally takes that offer -- a 700 KB archive is listed by reading
  its first eight kilobytes. The context menu's "Restore from Archive" is
  unchanged; somebody who asked for a restore asked for it. Five checks in
  `--archive-demo`, driven through the real File Manager open path with the
  keyboard, each watched to fail. One of them could not fail at first: it
  probed for a filename left over from an earlier draft of the scene, so
  "opening it wrote nothing" was true whatever happened. The negative control
  is what said so.
- ✅ **Restoring an archive asks before replacing anything** (`cz_file.c`,
  `app_fileman.c`, `app_archive.c`): the viewer reported the clash count but
  extraction still went ahead regardless, and the File Manager's "Restore from
  Archive" gave no warning at all -- while the same app has always asked before
  overwriting a single pasted file. Restoring an archive over forty files is
  that act performed forty times and should not be quieter for being bigger.
  Both routes now count clashes with one shared reader (`car_list`, which is
  also what the viewer lists with, so the window and the thing that acts on it
  cannot disagree about an archive's contents) and ask **only when there is
  something to lose** -- a backup restored into an empty folder needs no
  decision. Four checks, each watched to fail against never-ask, always-ask and
  answering-No-extracts-anyway. The cancel check could not fail as first
  written: "the file is still there" is true whether you cancelled or replaced
  it. It writes a sentinel of a different LENGTH over a member first, so which
  of the two is on disk is a fact.
- ✅ **The desktop cache is rebaked only when the picture would differ**
  (`src/shell/sh_desktop.c`): with the screenshot cost fixed, profiling showed
  **84% of what remains of startup** is baking the desktop background -- the
  sky gradient, five soft glows, three hill layers and a vignette, about 25M
  instructions. That is fine once. It was being paid again on **every theme
  apply and every settings apply**, and the factory landscape is drawn from
  hardcoded colours: it depends on the screen size and nothing else, so
  switching from Aurora to Forest Green repainted window frames over a
  pixel-identical desktop. On the target that is a visible stall every time
  somebody clicks a theme in the Control Center, for no change at all. The
  cache now remembers what it was baked from -- **per branch, naming only what
  that branch actually reads**, which is the whole trick: a key that mentioned
  the theme colours in the mode that ignores them would rebuild on every theme
  change and the optimisation would be worth nothing (confirmed -- adding them
  makes the demo fail). Measured at **25.6M instructions saved per avoided
  rebake**, roughly a tenth of a second of stall per click on a P2. Five checks
  in `--deskcache-demo`, pulling in both directions so that "never rebuild"
  fails as loudly as "always rebuild". Its pixel check first compared the whole
  screen and reported 722 changed pixels -- every one a correctly recoloured
  desktop icon, drawn live from the theme palette on top of the cache. It now
  reads two zones and asserts opposite things about them: the landscape must
  not move, and the icon column MUST, since otherwise the first half would pass
  just as well if the theme had never been applied.
- ✅ **Saving a bitmap costs a row, not an image** (`src/gfx/gfx_bmp.c`,
  `gfx_bmp_io.c`, `src/platform/host/plat_host.c`): profiling an idle desktop
  showed **46% of the whole run inside `fwrite`** — not the logger (15 lines),
  not drawing. It was the host backend's `plat_screenshot()`, a SECOND BMP
  writer with its own header and row loop, no test covering it, calling
  `fwrite()` once per PIXEL: 480,000 calls for one 800x600 screenshot. Saving
  one picture cost **74.5M instructions, more than drawing 120 frames of the
  desktop**. It is gone; the host now calls the shared `gfx_bmp_save()` exactly
  as the DOS backend already did, and that function streams a row at a time
  through two new codec entry points (`gfx_bmp_write_header` /
  `gfx_bmp_write_row`) which `gfx_bmp_encode` is also built on, so one place
  knows the format. **74.5M → 4.7M instructions, a 15.9x cut**, and the file
  that comes out is byte-identical (checked against the previous build on four
  scenes). The memory result matters more than the speed: `gfx_bmp_save` used
  to allocate the whole encoded image, 1.4 MB for an 800x600 screen, on a
  machine with 4 MB total and 3.7 already spoken for. `--capture-demo` now
  measures it -- saving grows the accounted peak by **2408 bytes**, and with
  the old whole-image version restored it grows by **1,440,062**, taking the
  peak to 5.3 MB on a 4 MB target. Pressing Capture Screen on real hardware
  would have failed. The first version of the format test could not fail --
  it compared the streamed bytes against `gfx_bmp_encode`, which calls the same
  two functions, so breaking a header field broke both sides identically and
  the comparison still matched. It reads the fields back against literal
  numbers now, and all three breaks (a wrong header field, dropped row padding,
  rows written top-down) were watched to fail.
- ✅ **Glossy buttons, and therefore glossy scrollbars** (`src/ui/ui_button.c`):
  a button face is now a vertical gradient lit from above, and a PRESSED one is
  lit from below -- which is what makes it read as pushed in beyond the sunken
  bevel alone. Disabled buttons stay flat on purpose: a gradient is what "lit"
  means here, and a control that cannot be used should not look lit. Because
  `ui_scroll.c` builds its arrows and its thumb out of buttons, every scrolling
  surface in the shell followed without knowing anything about it. Checked as
  pixels in `tests/test_ctrl.c` -- the first suite here that looks at what was
  drawn rather than at a computed value, because a visual contract has nowhere
  else to live. It also fills the surface with a sentinel and requires every
  pixel outside the control to survive: a one-pixel overdraw is invisible in a
  screenshot and is exactly what an inset written the wrong way round produces
  (confirmed -- `crect_inset(r, -1)` fails eight checks). The scrollbar check
  originally sampled an arrow button and passed on the strength of the dark
  arrow head sitting in the lower sample; it measures the thumb now, which has
  nothing drawn on it.
- ✅ **The docs' screenshots are reproducible** (`tools/bmp2png.py`,
  `docs/media/README.md`): nothing recorded how any picture in `docs/media`
  had been made, so when the drawing changed nobody could remake them and
  several went quietly stale. Two now carry commands anyone can run, and the
  file says plainly which ones do not. Taking them also exposed why the
  showcase image had a stray rectangle across it: `make run` rendered three
  frames, and the window-open zoom and launcher slide-up both take about seven
  and draw over everything while they run. It renders ten now.
- ✅ **XP-styled pop-up menus** (`src/ui/ui_menu.c`): every pop-up in the system
  -- the desktop menu, the window menu, and the File Manager, Paint,
  CastaliaSheet and CastaliaWrite menus -- was a flat panel with a flat block
  of accent on the selected row. They now get a tinted icon gutter, a
  near-white body, a three-stop gradient selection with a darker rim, and
  disabled entries engraved rather than merely pale (grey text on a pale menu
  is the first thing to go on a 1999 CRT). Every colour is derived from the
  active theme's own face and accent, so a dark theme gets a dark body and a
  lighter gutter rather than a white slab. The treatment is gated on a new
  `ui_set_glossy()` switch -- a switch of its own rather than a field in
  `UiPalette`, because a palette is a set of colours that gets written to a
  theme file, and this is a statement about the pipeline underneath, which no
  theme should be able to claim. The first attempt tinted the gutter a few
  percent off a panel that was already almost white, and was invisible; the
  fix was to move the BODY, not the gutter. Menus draw only while one is open,
  so the idle desktop pays nothing. Five checks in `--menulook-demo` measure
  the pixels, and the second half redraws the same menu with the switch off
  and requires every property to invert -- that flat path is what the
  16-colour pipelines and safe mode get, and it is the half that would
  otherwise rot unnoticed. All five were watched to fail against a break.
  Finding it also turned up a screenshot detail in `src/main.c` that ran for
  EVERY scene: a mouse-move nudge meant to highlight the launcher landed in
  whatever pop-up happened to be open, missed it, and cleared its highlight --
  so no other menu's selection had ever appeared in a screenshot.
- ✅ **The window menu** (`src/shell/sh_context.c`): Alt+F4 was the only thing
  the keyboard could tell a window to do. A keyboard-only user -- which is how
  this system's own hardware verification was carried out -- could open and
  close windows and nothing else: no minimize, no maximize, no restore. And
  `wm_move_window_to_desktop()` had been written, tested and wired to nothing,
  so the four virtual desktops could be switched between but a window could not
  be moved to one. Alt+Space (or a right-click on a title bar, or on a taskbar
  button) now raises a menu carrying Restore, Move, Size, Minimize, Maximize,
  Send to Desktop 1-4 and Close. **Move and Size** borrow the arrow keys for a
  moment -- Enter keeps the result, Esc puts the window back exactly where it
  started -- which is the only way to place a window on a machine where the
  mouse is optional. Where a window is ALLOWED to end up moved out to
  `src/wm/wm_move.c` on the way (pure, `tests/test_move.c`), so the keyboard
  and the mouse drag now answer that question with the same code instead of
  two inline blocks of magic numbers. That refactor also turned up a
  `wm_move()` that set a window's frame, sent WM_MSG_MOVE and invalidated
  nothing -- correct only for a caller that knew to repaint the hole itself,
  and it had no callers at all, so nobody had ever found out. It is now
  `wm_set_frame()`, which does the whole job, and the snap path uses it too.
  Items that do not apply are shown DISABLED rather than
  dropped, so the menu keeps its shape and items do not move under the pointer
  between one press and the next -- and so the keyboard walk in
  `--sysmenu-demo` doubles as an assertion about which items are dead. The
  taskbar-button route is not a duplicate: a minimized window has no title bar
  to right-click and does not hold the focus, so it is the only way to reach
  one. The menu is torn down when its window closes, because window ids are
  slot numbers and a menu outliving its subject would eventually be pointing at
  whoever took the slot. Twenty checks in `--sysmenu-demo` plus 75 in
  `tests/test_move.c`, and every demo check was watched to fail against a
  deliberate break.
- ✅ **Alt+Tab switcher** (`src/shell/sw_core.c`, `sh_core.c`): Alt+Tab rotated
  the z-order, so pressing it twice left you two windows from where you
  started. It now walks most-recently-used order -- one press goes to the
  window you were just in, a second brings you back, which is what the key is
  mostly used for -- and puts up a panel of the open windows with their icons
  and the selection highlighted. The order is a pure structure
  (`tests/test_sw.c`) driven through the sequences that only turn up in use:
  focusing something already held moves it rather than duplicating it, closing
  the window you are standing on leaves a sane place to stand, and running past
  the bound drops the LEAST recent entry rather than the one in front of you.
  The panel lingers on a timer rather than while a modifier is held, because
  the DOS keyboard path delivers Alt+Tab as one event and never reports the
  release; focus therefore follows every press immediately and the panel only
  shows where you have got to. Verified by `--switch-demo`, whose two checks
  each have a negative control -- z-order cycling fails the return check, and
  disabling the panel fails the drawing check.
- ✅ **Documented counts are checked** (`tools/check_docs.sh`, run by
  `make lint`): the README claimed "1211 checks across 25 suites" long after
  both had stopped being true -- nobody lied, the numbers simply drifted, and
  nothing was watching. Now a stale count is a lint failure. Only counts a
  script can derive are checked, and telling a whole-run claim from a per-suite
  one written into prose is the entire difficulty: an early version missed
  "(2750 checks)" because it only matched a comma or "across", and passed while
  the README was wrong. A line counts as a total if it talks about the run as a
  whole and does not name a specific suite file. Confirmed against three cases:
  a wrong total in parentheses is caught, a wrong total mid-sentence is caught,
  and a per-suite count nearby is left alone.
- ✅ **Memory budget enforced, not described** (`--mem-check`, run by
  `make demos`): the docs said the idle shell was ~1.9 MB in three places. It
  is 3.7 MB, and has been since the desktop cache was added -- 1.9 MB was one
  800x600x32 back buffer, and the cache is a second one at the same size. Still
  under half the 8 MB budget, but the figure had drifted by half with nothing
  watching, exactly like the test counts. There is now a 4 MB idle ceiling and
  an 8 MB peak ceiling in `castalia.h`, checked on every demo run; the ceiling
  deliberately leaves no room for a THIRD full-screen surface, and adding one
  makes it report 5642 KB and fail.
- ✅ **Generated source statistics are checked** (`tools/gen_buildstats.sh`,
  `make lint`): `buildstats.h` feeds the About window's Statistics tab, and it
  was ~4,400 lines and 18 files behind the source. Its own header says the
  point is "to report the size and shape of the codebase honestly", which it
  had stopped doing. `make lint` now regenerates it to a temp file and fails if
  it differs. The TODO count was also counting its own machinery -- it reported
  7 when there were 3, the other four being the generated line that holds the
  number, the About window's label that prints it, and an `SD_TODOS` enum that
  merely contains the letters. Word boundaries and two file exclusions fix it,
  with the reason written where the exclusion is.
- ✅ **`.CAR` archives** (`src/apps/car_core.c`, `cz_file.c`): a `.CZ` holds one
  file, which is the wrong shape for the thing people want on a machine this
  size -- putting a folder somewhere safe before changing it. `.CAR` is a
  fixed-record directory plus the same LZSS members, readable with two seeks
  and no allocation. Flat by design: nested members would need paths inside the
  archive, and a path inside an archive is what the reader spends most of its
  effort refusing. `tests/test_car.c` is mostly refusals -- names carrying a
  separator, a drive letter, `.`, `..`, control characters or no terminator;
  directories larger than the file holding them; members starting past the end,
  running off the end, overlapping the directory, or with a length that wraps a
  32-bit sum. Wired to the File Manager (Back Up on a folder, Restore on a
  `.CAR`, and opening one). Verified by `--car-demo`: pack, delete, restore,
  compare bytes, and hand it an archive whose member is `../ESCAPE.TXT` --
  removing the name check makes the escape succeed and both checks fail, which
  is how the defence is known to be load-bearing.
- ✅ **Crash recovery screen** (`sh_crash_screen`): the old one named the fault,
  said "reboot into Safe Mode to repair", slept 2.5 seconds and dropped to DOS.
  All true, almost none of it actionable -- it did not say WHICH log, it went
  away before it could be read, and it predated the known-good config copy. It
  now names the crash log by absolute path, gives the exact command
  (`CBOOT /safe`), says whether a known-good configuration exists (and only
  mentions one when it does), and waits for a key with a 30-second cap so an
  unattended machine still finishes returning to DOS. Painting is separated
  from handling because `sys_fatal` exits: a screen drawn inside the handler
  could never be inspected. `--crash-demo` draws both branches and requires a
  screenful of advice; stripping the advice takes it from 56 rows of text to 21
  and fails.
- ⬜ Real-hardware QA matrix (PS/2, VESA modes, IDE, CD, audio).
- ✅ **Abuse tests automated** (`tools/run_abuse.sh`, `make abuse`): twelve
  hostile worlds the shell must RECOVER from, not merely survive -- a
  CASTALIA.INI of random bytes, one cut off mid-key, one whose every number is
  impossible, a zero-byte one, a config file that is a directory, no SYS folder
  at all, a wallpaper that is a bitmap in name only, a theme of nonsense
  colours, hundreds of malformed appointments, a folder nested sixty deep, a
  filename longer than any buffer, and a `.CZ` claiming two gigabytes. None of
  these are exotic: a machine losing power mid-write produces most of them by
  itself. Each case asserts a clean exit, a completed lifecycle, and
  `--screen-check` confirming a real desktop was painted rather than a blank
  screen that failed quietly. All three assertions were confirmed able to fail
  (disabling `sh_desktop_paint` fails all twelve; a binary that exits 1 fails
  all twelve; one that succeeds silently fails all twelve) -- the first draft
  passed every one of those, because it opened the File Manager and was
  measuring a window's colours instead of the desktop's.

## Phase 5 — Package ecosystem
- ✅ **`.CAPP` package format** (`include/castalia/capp.h`, `src/capp/`): a
  self-describing container (magic, versions, CRC-32, manifest, typed section
  table) with a defensive validator (`capp_parse` bounds-checks every offset and
  verifies the CRC), a builder (`capp_build`), an authoring tool
  (`build/mkcapp`), and startup discovery (`capp_scan_dir` finds, validates, and
  logs packages in `APPS\` without executing code). Host-tested
  (`tests/test_capp.c`).
- ✅ **Plugin ABI** (`include/castalia/capp_abi.h`): an append-only `CappHostApi`
  service table (log, windowing, config) and a `CappPlugin` descriptor with
  init/open/frame/shutdown lifecycle callbacks.
- ✅ **Plugin loader** (`include/castalia/capp_loader.h`, `src/capp/capp_loader.c`):
  resolves a package's `CAPP_SEC_CODE` section to a `CappEntryFn`, checks the
  plugin's ABI against the host, hands over a live `CappHostApi`, and drives the
  lifecycle — package code **actually executes behind the ABI**. Two resolution
  paths: a portable **builtin registry** (a code section is a `CBLT`-tagged
  reference to a compiled-in plugin, reached only through the ABI — how the host
  and the base image ship add-ons) and a **native resolver** seam
  (`capp_loader_set_native_resolver`) for a future DOS map/relocate/resolve
  loader. Windowing is injected (`capp_host_wm.c` gives each plugin a retained
  client surface the compositor blits), so the loader stays free of wm/gfx code
  and is host-tested (`tests/test_capp_loader.c`: registry, ABI rejection, the
  full lifecycle, and `config_get`). Runnable packages in `APPS\` are indexed
  (`capp_index_dir`) and appear as first-class launcher entries; two sample
  add-ons ship (`APPS\HELLO.CAPP`, `APPS\CLOCK.CAPP`, authored by
  `mkcapp --builtin`). Verified end-to-end on the host (`--capp-demo`, and the
  keyboard launcher path opening an add-on whose plugin code draws its window).
- ✅ Theme editor; SDK headers; example app; help viewer + content. **This
  line said "not started" long after all four shipped**, which is the sort of
  thing this repository has tooling to prevent and did not catch here: the
  Theme Editor is `src/apps/app_theme.c` with a `--theme-demo` scene that
  edits, applies, saves and reloads; the SDK is `include/castalia/capp.h`
  alongside `capp_abi.h` and `capp_loader.h`; the example apps are
  `APPS\HELLO.CAPP` and `APPS\CLOCK.CAPP` from `mkcapp --builtin`, described
  in the entry directly above this one; and the Help viewer is
  `src/apps/app_help.c` with `--help-demo` drawing every topic. Each was
  checked to exist before this mark was changed.

  `check_docs.sh` could not have caught it and no honest version of it could:
  the line names no file and no scene, it is a phase summary restating a
  status that the detailed entries around it already hold. The fix is not
  another check -- a check that cannot fail on the case in front of it is
  theatre, which this project has learned twice over -- it is to stop keeping
  the same status in two places. Phase summaries now point at the entry that
  owns the work rather than re-asserting how far along it is.
- 🟡 Asset build tools — **icon-pack pipeline done**: a named, cached BMP icon
  loader (`src/shell/sh_iconpack.c`) behind `[Assets] Icons=` supplies the
  **desktop icons, the File Manager toolbar buttons, and the taskbar Quick Launch
  strip** (magenta-keyed, per-icon procedural/text fallback). Two packs ship: the
  **default Public-Domain Tango set** (`assets/icons/tango/`, rebuilt by
  `tools/build_tango_pack.sh`) and the original MIT **Castalia** set
  (`assets/icons/castalia/`, baked by `tools/gen_iconpack.c` / `make gen-iconpack`).
  `tools/png2bmp.py` (stdlib-only, area-average downscale, alpha→key) converts any
  CC0/MIT/PD PNG set into the convention. INSTALL/release stage the packs to
  `C:\CASTALIA\ICONS\` and the shipped INI defaults to Tango. Verified host
  end-to-end (`make run`, `make icons-demo`; PNG→BMP round trip through
  `gfx_bmp_decode`; 457 tests, C89/Watcom lint clean). A **font pipeline** is done
  too: `tools/bdf2font.py` bakes any ≤8-row BDF into the engine's glyph cell, and
  a **selectable system face** (`[Assets] Font=`) ships the BSD **Spleen 5x8** as
  default with the original 8x8 one line away — identical metrics, no layout
  change (`make gen-font`). The **launcher (Start) menu and desktop context menu**
  are iconized (`ui_menu` gained a per-item icon gutter; `m-*` slots), and every
  **window title bar + taskbar button** shows its app icon (`wm_set_icon`, set by
  title in the shell's WM lifecycle hook). The **system tray** (sound / network /
  clipboard) uses state-selected pack icons too (`tray-*`, procedural fallback).
  **Low-color tier done**: `sh_iconpack` maps each icon onto the active theme
  palette at load (EGA-16 / 3-3-2) preserving the magenta key, so a 16-color
  theme's icons read as one EGA set instead of full-color at present time.
  Remaining: `palette_tools`.

## Phase 6 — Experimental branches
- ⬜ Win98 shell companion (`CASTSHL.EXE`, reversible, no MS file replacement).
- 🟡 **Packet-driver / mTCP-style networking** (`include/castalia/net.h`,
  `src/net/`, `net_pkt.c`): the seam is defined with host-tested address/checksum
  helpers and a null host backend; the DOS packet-driver client detects an
  installed driver (vector scan for "PKT DRVR"), reads its class/MAC, and sends
  raw Ethernet frames. Remaining: the receive callback path, then ARP/IP/UDP/TCP
  and the file-transfer / crash-log-upload apps.
- 🟡 **`/kernel-lab` native kernel research** (boot sector → PM → GDT/IDT → PIC/
  PIT → PS/2 → memory map → FAT reader → cooperative scheduler): milestones 1-4
  done — a two-stage loader reaches 32-bit protected mode with a GDT and IDT,
  **remaps the 8259 PICs, and takes real hardware interrupts** (PIT IRQ0 drives a
  live on-screen tick counter; PS/2 keyboard IRQ1 reads scancodes), all
  **verified in QEMU** via a headless VGA-memory dump. Milestones 5+ (physical
  memory map onward) remain. Must not block v1; nothing here is referenced by the
  shipping product.

---

## Known technical debt / follow-ups
- DOS backend needs emulator + hardware validation (biggest open risk).
- The launcher's columns SCROLL when they do not fit the work area; they used
  to drop the overflow, which was neither drawn nor clickable and therefore
  indistinguishable from never having been added. Each column carries a
  scroll bar when it overflows, the wheel over the panel scrolls the column
  under the pointer (the popups now take the wheel before the windows do --
  it used to fall through to the window BEHIND the open menu), and the arrow
  keys scroll the highlight into view. `--launch-demo` still opens the menu
  at 640x480 (the smallest supported mode) and FAILS if the clamp trips, so
  adding one item too many to the shipped lists breaks the build rather than
  the menu; measured headroom is two more rows. `--lscroll-demo` covers the
  case no build-time check can: `.CAPP` entries are discovered at runtime,
  one row each, so it runs at 640x320 and asserts that the LAST entry, off
  the bottom at rest, becomes clickable after the wheel and after arrowing.
- `plat_screenshot` on DOS is implemented: it writes a 24-bit BMP of the back
  buffer through `gfx_bmp_save`, the same portable writer the shell's Capture
  Screen command already used. It was a TODO from when that writer was
  host-only, which it has not been for some time.
- File Manager operations: this note used to say New Folder and delete ran with
  no confirm dialog and that rename and copy/move "await a text-input control".
  All of it shipped -- Delete asks first, New Folder and F2 rename prompt, and
  cut/copy/paste work. The claim that delete "is safe because it moves to
  `TRASH` (recoverable)" was the one worth catching: it was not true. Deleting
  two files of the same name destroyed the first, and nothing recorded where
  anything came from, so nothing was recoverable. Both are fixed (unique slots,
  `TRASH\TRASH.IDX`, and Restore on the bin's context menu).
