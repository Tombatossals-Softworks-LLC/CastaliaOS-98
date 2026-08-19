# Features

## The desktop shell

- **Two‑column XP‑style Start panel** — a glossy blue header with the gold
  Castalia crest, a white **Programs** column and a blue‑tinted **Places &
  System** column, and a green footer band with **Log Off** and **Shut Down**
  buttons. Full keyboard navigation (open with Esc / Ctrl+Esc, arrow keys to
  move within and between columns, Enter to launch — no mouse required).
- **Taskbar** with a Start orb, Quick Launch strip, system tray (sound, network,
  clipboard indicators), and a live clock with a date tooltip that opens the
  Clock, Calendar & Agenda when clicked.
- **Four virtual desktops** — switch instantly with Ctrl+Left/Right; windows keep
  their state and geometry. Windows 98 SE never had this.
- **Window snapping / tiling** — drag a title bar to a screen edge to tile a
  window to that half, or to the top to maximize, with a **live Aero‑style
  preview outline** showing exactly where the window will land before you drop.
- **Draggable desktop icons** — pick up any desktop icon and drop it anywhere,
  with a floating ghost following the pointer and a snap back inside the screen.
  Positions **persist** to `CASTALIA.INI`, and "Line up Icons" tidies them back
  to a grid.
- **Right‑click context menus** on the desktop (Refresh, Line up Icons,
  **Properties** → the Control Center's Desktop/wallpaper page) and inside apps.
- **Themeable everything** — pick a preset (Classic, Storm, Forest, High Contrast,
  or the XP‑inspired **Aurora**) and the whole environment recolors live.
- **Desktop wallpapers** — six bundled originals with a live picker and an
  XP‑style monitor preview; centered, tiled, or stretched.
- **Software cursor, drop shadows, window open/close/minimize animations**, a
  boot splash, and a proper shutdown screen.
- **Four screensavers** — the drifting Castalia castle, a warp starfield, a
  demoscene plasma, and a Mystify‑style bouncing polyline — chosen in the Control
  Center with a live "Display Properties"‑style monitor preview.
- **A Welcome tour on first boot** — a glossy landing card whose rows are live
  links into the key apps, with a persisted "show at startup" checkbox.

## The window manager & UI toolkit

- A real window manager: title bars, caption buttons, resizable frames, focus and
  z‑order, Alt+Tab switching, Alt+F4 close, minimize/maximize/restore.
- An original UI toolkit: push buttons, checkboxes, radios, list boxes, menus,
  **scroll bars** (proportional thumb, arrows, and page-in-the-trough),
  single‑line text fields with a shared system clipboard, and modal dialogs.
- Win9x‑style bevels plus XP‑style glossy gradients and sheens, drawn by hand.

## The applications

- **File Manager** — a Windows XP‑style Explorer: menu bar, navigable address
  bar, a blue task pane (File and Folder Tasks / Other Places / Details),
  **three views** (Details, Tiles, Icons grid), sortable columns, right‑click
  context menus, **drag‑and‑drop** (move a file onto a folder or between panes,
  with a floating ghost), a Properties dialog, recursive search, copy/cut/paste,
  rename, safe delete‑to‑Trash, and a two‑pane commander mode. The desktop
  **Recycle Bin** opens straight to that Trash folder, shows an **empty vs
  full** icon that tracks the Trash live, and right‑clicks to **Open** or
  **Empty Recycle Bin** (with a Yes/No confirmation).
- **Media Player** — a Winamp‑style WAV player: a green LCD, a **live
  visualizer driven by the real decoded samples** with four modes you cycle by
  clicking it (bars, mirrored bars, dot‑matrix, oscilloscope),
  transport controls, a seek bar, volume/balance, a **three‑band equalizer**
  that really filters the decoded audio (its DSP is unit‑tested by measuring
  band energy, not by ear), shuffle & repeat, and a
  playlist that can add a single file or scan a whole folder for audio — with
  **drag‑to‑reorder** tracks (a floating ghost row and an insertion line show
  where the track will land).
- **CastaliaWrite** — a real **word processor** in the 2001 office idiom: a
  **menu bar with working drop‑downs**, a Standard and a Formatting toolbar
  whose buttons stay **flat until you hover them**, a **ruler** with margin
  markers, and a white page with a drop shadow floating on a gray workspace.
  Live **word‑wrap**; **bold, italic and underline held per character**;
  **left / centre / right** alignment per paragraph; drag‑to‑select; Insert
  Date/Time; **Find / Find Next / Replace** (case‑insensitive, wrapping, with
  replacements inheriting the formatting of the text they replace); a Word
  Count dialog; and documents that save to a plain,
  repairable markup you can still fix in any text editor (plain text opens
  too). The document model is pure logic with its own unit-test suite.
- **CastaliaSheet** — a real **spreadsheet** in the 2001 office idiom: a
  **workbook of three sheets** with tabs along the bottom, a menu bar with
  working drop‑downs, a flat‑until‑hovered toolbar with **AutoSum (Σ)** and
  **fx**, a formula bar with a **Name Box**, a grid with a select‑all corner
  and headers that highlight the cursor, **drag‑out range selection** (the
  block washes in, its row and column headers light up), and a status bar
  showing the **live Sum / Average / Count** of the selection — the whole
  column when nothing is selected, the block when it is. In‑cell editing over a formula engine with cell
  references,
  ranges, operator precedence, parentheses and **SUM / AVG / MIN / MAX / COUNT /
  ABS / INT / ROUND** — plus an **Auto‑Sum** button and honest error cells
  (`#DIV/0!`, `#REF!`, `#CYCLE!`). Edit one cell and every dependent formula
  reflows. Sheets **save and load as CSV** with the formulas preserved, so a
  file written here opens in any other spreadsheet. The engine is pure logic
  with its own unit-test suite.
- **Benchmark Suite** — runs seven real micro‑benchmarks (integer CPU, memory
  copy/fill, graphics fill‑rate, vector lines, **text throughput**, and a hash)
  and reports them as animated, tier‑colored bar meters with an overall
  **CastaliaMark** score.
- **About CastaliaOS** — a tabbed, information‑dense window with a glossy crest
  banner: identity, live **hardware** facts, live **source‑code statistics**
  (lines of code, files, functions), and credits.
- **Solitaire** — the classic Klondike patience game on a felt table, with full
  rules, click‑to‑move, double‑click‑to‑foundation, **drag‑and‑drop** of a
  single card or an entire ordered run (with a floating card ghost), and the
  beloved **bouncing‑cards win cascade** that fills the table when you win.
- **Clock, Calendar & Agenda** — a live analog clock with a sweeping second
  hand, a digital readout, and a navigable monthly calendar with an appointment
  panel: pick any day, add entries (timed or all‑day), delete them, and see a dot
  under every day that has something on it. Appointments persist as one plain,
  editable line per entry in `SYS\AGENDA.TXT`.
- **Character Map** — a grid of the system font's glyphs; pick characters into a
  sample string and copy them to the clipboard.
- **Console** — a DOS‑flavored command shell with real built‑ins (`dir`, `cd`,
  `type`, `del`, `mem`, `ver`, `date`, `time`, `echo`, `cls`), command history,
  scrollback, and a blinking cursor — all reading the actual filesystem.
- **Notepad** — a multiline editor with open/save, Find, word‑wrap, a scroll bar,
  and full text selection + cut/copy/paste across apps.
- **CastaliaPaint** — a raster editor in the 9x idiom: a two‑column tool box
  with eleven tools (pencil, brush, airbrush, eraser, fill, color picker, line,
  rectangle, ellipse, text and a rectangular selection), an options box for
  brush width and the three shape styles, the 28‑swatch palette with a
  foreground/background pair (right‑click sets the background), **cut, copy and
  paste** of any region with a dashed marquee, **Stretch** by a percentage,
  three levels of undo and redo, Flip Horizontal / Vertical, Invert Colors and
  Grayscale, and a 24‑bit BMP round trip.
- **Theme Editor** — the sixteen colors the shell draws with, an RGB mixer with
  channel‑gradient sliders and a quick palette, the title‑bar height and border
  width, and a **live miniature of the desktop** that repaints as you drag.
  Apply recolors the running session — windows, taskbar, Start orb and all —
  and themes save as plain INI files you can mail to somebody.
- **Capture Screen** — right-click the desktop (or the Start menu) and what is
  on screen is written to `PHOTOS\SHOTnnnn.BMP`, numbered so a capture never
  overwrites an earlier one. The shell saves its own composited back buffer, so
  it works identically on DOS and on the host build.
- **Help** — a real help browser: topics down the left, the text on the right,
  arrow keys to move and PgUp/PgDn to scroll. It documents what the system
  actually does — the shortcuts, the file types and which app owns each, and
  where every file lives on disk.
- **File associations** — a double‑click opens a bitmap in Paint, a CSV in
  CastaliaSheet, a document in CastaliaWrite, a sound in the Media Player, text
  in Notepad, and anything unclaimed in the Viewer, which can always show
  *something*. One table drives the double‑click, the Type column and the
  Properties dialog, so the three can never disagree. A DOS executable is named
  rather than launched behind your back.
- **Image thumbnails** in the file manager's Icons view — every bitmap shows
  itself, in proportion, decoded a few per frame into a bounded least‑recently‑
  used cache so a folder full of images never stalls a repaint and never grows
  without limit.
- **Open / Save As dialog** — one shared file browser behind every app that
  reads or writes: the folder listing with directories first, an extension
  filter, a scroll bar, and full keyboard navigation. Paint, Write, Sheet,
  Notepad, the Viewer, the Media Player and the Theme Editor all use it.
- **Network** — adapter and link status, the TCP/IP configuration, the live ARP
  table, and a **ping** tool: it resolves the address with ARP, matches every
  ICMP echo reply to its request, times each round trip, and answers ARP and
  echo requests addressed to this machine. The ARP/IPv4/ICMP/UDP layer under it
  is written by hand, one octet at a time, and host‑tested against deliberately
  corrupted frames.
- **Calculator**, **Task Manager** (live window list, memory gauge, end task),
  **Mines**, an image/hex **Viewer**, a color‑coded **Log Viewer**, a **Control
  Center**, and **System Information**.

## Under the hood

- **Original software renderer** — the entire UI composites into a 32‑bit XRGB
  back buffer in system RAM, then presents dirty rectangles to real hardware in
  8bpp (a search‑free 3‑3‑2 palette) or 16bpp (RGB565).
- **Optional, never required** sound (Sound Blaster DAC or PC speaker) and
  networking (a DOS packet‑driver client) — the desktop is identical without them.
- **`.CAPP` add‑on packages** — discovered add‑ons appear as first‑class launcher
  apps and run behind a stable ABI. Real extensibility for a retro OS.
- **Crash boundary & safe mode** — a fatal error writes a crash record and can
  boot into a reduced high‑contrast recovery profile.
- **Settings** persist to a plain `CASTALIA.INI` you can repair with any editor.
