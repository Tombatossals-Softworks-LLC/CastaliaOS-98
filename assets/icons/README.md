# CastaliaOS 98 PE — Desktop Icon Packs

The shell draws its icons **procedurally** by default, so the base image is
asset-free. An **icon pack** is a directory of BMP files that overrides that art
by name — desktop icons, the File Manager toolbar, and the taskbar Quick Launch
strip all request icons from the active pack, falling back to their
procedural/text drawing for any icon the pack doesn't provide.

Two packs ship in-tree; select one with `[Assets] Icons=`:

```ini
[Assets]
Icons=C:\CASTALIA\ICONS\TANGO       ; default: the Public-Domain Tango set
;Icons=C:\CASTALIA\ICONS\CASTALIA   ; the original, all-first-party MIT set
;Icons=                             ; empty: built-in procedural icons
```

### Will the icon be visible where it is drawn?

An icon is authored against whatever background the author had open; the shell
draws each group somewhere specific. Two Tango glyphs turned out to be
near-white pages with no outline placed on near-white surfaces — `edit-copy` on
the File Manager's glossy button, `text-x-generic` on the Start menu's white
panel. Neither is broken art, both were invisible, and nothing reported it: an
icon that cannot be seen still loads, still blits, and still passes every check
that asks whether it exists.

```sh
python3 tools/icon_contrast.py                    # the default (Tango) pack
python3 tools/icon_contrast.py assets/icons/castalia
```

It counts, per icon, how many pixels stand clear of the surface behind it —
against backgrounds sampled from a real render, not assumed. Read the note at
the top of that file before trusting a number: it catches a **blank** reliably
and a **wash** not at all, so it narrows down where to look rather than
replacing looking. It is not part of `make lint` (every lint step is POSIX sh
with no interpreter dependency).

On the host build:

```sh
make run                # desktop with the default (Tango) pack -> build/castalia.bmp
make icons-demo         # File Manager (toolbar + Quick Launch icons) -> build/castalia-iconpack.bmp
make icons-demo ICONS=assets/icons/castalia    # same, with the original pack
./build/castalia --headless --icons assets/icons/tango --shot out.bmp
```

## Pack format

A pack is a flat directory of small **uncompressed BMPs**, one per named slot.

**The brand mark** — `logo-16` `logo-24` `logo-32` `logo-48` `logo-64`
`logo-128`. The Start button, the launcher header, the boot splash, the About
crest and the Welcome banner all draw the Castalia mark; when the pack supplies
these, that art is used and `sh_logo.c`'s procedural drawing becomes the
fallback for a bare install. The shell picks the smallest baked size that is at
least as large as it needs, so any scaling left is a reduction.

> These files are **original CastaliaOS artwork** in every pack, including
> `tango/`. They are the product's own identity, not part of the Tango set —
> the rest of `tango/` is the public-domain Tango Icon Library (see
> THIRD_PARTY_NOTICES.md). Bake them from a source PNG with:
>
> The source of truth is `presskit/castalia-98-logo.png`. Re-bake after any
> change to it — `--pad` letterboxes to square first, so a source that is not
> square (the current one is 256x269) is not stretched:
>
> ```sh
> for pack in tango castalia; do
>     for n in 16 24 32 48 64 128; do
>         python3 tools/png2bmp.py presskit/castalia-98-logo.png \
>             "assets/icons/$pack/logo-$n.bmp" --size $n --pad
>     done
> done
> make gen-presskit-logo      # ...and the press kit, from the same PNG
> ```
>
> The press kit's crest icons and favicon come from that same file. They used
> to be baked by `make gen-logo`, which renders `sh_logo.c`'s **procedural**
> drawing -- right while that drawing was the mark, and stale from the moment
> the shell started preferring `logo-*.bmp`. The desktop and the press kit then
> showed two different castles, and nothing reported it, because both were being
> generated correctly from different places. One source now, so they cannot
> drift again.

**Desktop** (drawn in a 40 px cell; **32–40 px** art recommended):

| File | Slot |
|------|------|
| `computer.bmp` | "This Machine" |
| `folder.bmp`   | "Documents" / folders |
| `settings.bmp` | "Control Center" (gear) |
| `document.bmp` | "Log Viewer" / document |
| `trash.bmp`    | "Recycle Bin" |
| `media.bmp`    | "Media Player" |
| `clock.bmp`    | "Clock" |

All seven are worth supplying. A missing one is not an error -- the desktop
falls back to its own procedural drawing -- but at 40 px that fallback is a
flat shape from primitives sitting in a row of detailed art, and it reads as a
broken icon rather than a simpler one. `media` and `clock` were absent from
this table and from the Tango pack for a long time, and the desktop showed two
black discs next to five Tango icons the whole time.
`tools/check_icon_slots.sh` (run by `make lint`) now requires every slot the
shell asks for to be both shipped in the default pack and listed here.

**File Manager toolbar** and **Quick Launch** (**16 px** recommended). A missing
toolbar icon keeps the button's text label; a missing Quick Launch icon keeps
the procedural glyph:

| File | Where |
|------|-------|
| `tb-up` `tb-refresh` `tb-newfolder` `tb-rename` `tb-copy` `tb-cut` `tb-paste` `tb-delete` `tb-find` `tb-dual` | File Manager toolbar buttons (one per button -- a button with no icon is drawn as a wide text button, which in a row of square glyphs reads as a mistake) |
| `ql-showdesktop` `ql-fileman` `ql-notepad` `ql-mines` | Quick Launch cells |
| `m-sysinfo` `m-logview` `m-fileman` `m-control` `m-taskman` `m-notepad` `m-app` `m-viewer` `m-calc` `m-mines` `m-terminal` `m-help` `m-restart` `m-shutdown` | Launcher (Start) menu + desktop context menu, drawn in the menu's left gutter |
| `tray-sound-on` `tray-sound-off` `tray-net-on` `tray-net-off` `tray-clip-on` `tray-clip-off` | System tray cells (state-selected: sound on/muted, network up/down, clipboard full/empty) |

Windows also show an app icon in their **title bar** and **taskbar button**; the
shell picks it from the desktop-slot names above by window title (no extra
files). Desktop icons scale best at 32–40 px; menu/toolbar/tray glyphs at 12–16 px.

Rules:
- **Size:** any square size; it is centered in its cell (not scaled at runtime),
  so author at the target size. Integer downscales (e.g. 64→32→16) look best.
- **Format:** BMP that `src/gfx/gfx_bmp.c` decodes — uncompressed `BI_RGB`,
  24-bit (or 8/32-bit). No RLE, no color profiles.
- **Transparency:** the magenta key **`#FF00FF` (255,0,255)** = transparent.
  There is no alpha channel; the shell composites with a 1-bit keyed blit.

## The default "Tango" pack (`tango/`) — Public Domain

`tango/*.bmp` is converted from the **Tango Icon Library** (Public Domain; see
[../../THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) and
[../../licenses/tango-COPYING.txt](../../licenses/tango-COPYING.txt)). Rebuild it
reproducibly (fetches the source PNGs, needs `curl` + `python3`):

```sh
sh tools/build_tango_pack.sh        # -> assets/icons/tango/*.bmp
```

## The original "Castalia" pack (`castalia/`) — MIT, all first-party

`castalia/*.bmp` is **original, MIT-licensed** art baked by
`tools/gen_iconpack.c` with the engine's own renderer — the zero-third-party
option. Rebuild / iterate:

```sh
make gen-iconpack       # -> assets/icons/castalia/*.bmp
```

## Using a free third-party pack (CC0 / MIT recommended)

Any freely-redistributable pixel-art set can become a pack. **Prefer CC0 /
public-domain / MIT** so the shipped build stays cleanly redistributable; CC-BY
is fine but adds an attribution line; **avoid CC-BY-SA / GPL art in the shipped
tree**, and never use icons extracted from Windows or any Microsoft product (see
[../../LEGAL.md](../../LEGAL.md)).

Vetted, aesthetically-fitting sources:

| Set | License | Notes |
|-----|---------|-------|
| [Kenney](https://kenney.nl/assets/pixel-ui-pack) (Pixel UI Pack, UI Pack) | **CC0** | No attribution. Cleanest fit. |
| [OpenGameArt CC0 icon sets](https://opengameart.org/content/cc0-public-domain) | **CC0** | 16/24/32/64 px tool & document icons. |
| [Pixelarticons](https://github.com/halfmage/pixelarticons) | **MIT** | 24×24 monochrome; rasterize the SVGs first, then tint. |
| [Tango Icon Library](http://tango.freedesktop.org/Tango_Desktop_Project) | **Public Domain** | Classic desktop look; credit is polite, not required. |

Convert PNG exports to the pack convention with the bundled, dependency-free
pipeline tool (`tools/png2bmp.py`, Python stdlib only — no Pillow/ImageMagick):

```sh
# one icon, resized to 40px, alpha -> magenta key, letterboxed to square:
python3 tools/png2bmp.py kenney/computer.png assets/icons/mypack/computer.bmp --size 40 --pad

# flat-background icons (no alpha): key a solid background color instead
python3 tools/png2bmp.py tango/folder.png assets/icons/mypack/folder.bmp --size 40 --bg 255,255,255
```

Then set `[Assets] Icons=` to `assets/icons/mypack` (or the installed path).

**Before shipping a third-party pack, record it** in
[../../THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md): name, version,
source URL, SPDX license, a copy of the license text under `licenses/`, and the
attribution string if the license requires one. If a license can't be confirmed,
draw an original equivalent instead — *when in doubt, leave it out*.
