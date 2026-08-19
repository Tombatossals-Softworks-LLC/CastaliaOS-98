# Third-Party Notices

CastaliaOS 98 PE aims to be legally clean. This file records every third-party
component and its terms. The policy: **all CastaliaOS code and artwork is
original and MIT-licensed; the only third-party binary shipped is the DOS/4GW
extender (`DOS4GW.EXE`), bundled under license and recorded below.** Everything
under `src/`, `include/castalia/`, `assets/`, and `dist/` is original to this
project (MIT-licensed) unless listed below.

## Currently bundled third-party code or assets

| Component | Version | Copyright | Where it lives | Redistribution basis |
|-----------|---------|-----------|----------------|----------------------|
| **DOS/4GW DOS extender** (`DOS4GW.EXE`) | The DOS/4GW extender distributed with Open Watcom (DOS/4G © Rational Systems, Inc. 1987–1993) | Rational Systems, Inc. / Tenberry Software | `include/vendor/DOS4GW.EXE` (source), and `.../CASTALIA/BIN/DOS4GW.EXE` in the `RELEASE/` tree and the release ZIP | Redistributed under a DOS/4GW distribution license **held by Tombatossals Softworks LLC**. It is not modified, patched, or wrapped — it is the stock extender the DOS/4G runtime loads at startup. |
| **Tango Icon Library** (default desktop icon pack) | tango-icon-theme 0.8.90 (rasterized PNG mirror) | Tango Desktop Project — **released into the Public Domain** | `assets/icons/tango/*.bmp` (source), and `.../CASTALIA/ICONS/TANGO/` in the `RELEASE/` tree | Public Domain (see [`licenses/tango-COPYING.txt`](licenses/tango-COPYING.txt)). Downsized to the CastaliaOS keyed-BMP convention with `tools/png2bmp.py`; artwork unmodified in content. No attribution legally required; the Tango Desktop Project is credited as a courtesy. **Four files in this directory are NOT Tango** and are original CastaliaOS art (MIT, baked by `tools/gen_iconpack.c`): `tb-rename.bmp` and `tb-dual.bmp`, because Tango has no equivalent glyph for either (its nearest "rename" icon is the text-editor one the pack already uses for Notepad, and it has no split-view icon at all); and `tb-copy.bmp` / `m-logview.bmp`, because Tango's `actions-edit-copy` and `text-x-generic` are near-white pages with no outline, illegible on the toolbar's near-white button face and on the Start menu's white panel respectively (measure with `tools/icon_contrast.py`). |
| **Spleen** (default system text face) | Spleen 5x8, 2.2.0 | © 2018–2026 Frederic Cambus — **BSD-2-Clause** | `assets/fonts/spleen-5x8.bdf` (source) → generated `src/gfx/gfx_font_spleen.c` (compiled in) | BSD-2-Clause (see [`licenses/spleen-LICENSE.txt`](licenses/spleen-LICENSE.txt)). The BDF glyphs for ASCII 32–126 are baked into the engine's 8-row cell by `tools/bdf2font.py`; unmodified in shape. Attribution retained per the license. |

`DOS4GW.EXE` is the 32-bit DOS extender that the Open-Watcom-built executables
(`CASTALIA.EXE`, `CBOOT.EXE`, `INSTALL.EXE`) load at runtime. It is the only
third-party binary shipped. Everything else in the tree — all source under
`src/` and `include/castalia/`, the procedural icons, the Castalia
castle/shield identity, the "Castalia Classic" palette, and the original 8×8
bitmap font — is original CastaliaOS work under the [MIT License](LICENSE). No
Microsoft code or artwork is included (see [LEGAL.md](LEGAL.md)).

## External dependencies the user supplies (not bundled)

These are required to build or run the product but are obtained and licensed by
the user directly from their sources. They are **not** part of this repository.

| Component | Used for | Bundled? | License / notes |
|-----------|----------|:--------:|-----------------|
| FreeDOS | Host DOS for the flagship product | No | Independent open-source project; obtain and use under FreeDOS's own license. |
| Open Watcom C/C++ | Building the DOS product (`Makefile.dos`) | No | Sybase Open Watcom Public License; obey its terms. Toolchain + runtime obtained by the user. |
| DOS/4GW extender (`DOS4GW.EXE`) | Running the 32-bit DOS executables | **Yes** — see "Currently bundled" above | Now shipped in `RELEASE/` and the release ZIP under a distribution license held by Tombatossals Softworks LLC. (A from-source build still loads whatever `DOS4GW.EXE` you place in `C:\CASTALIA\BIN`.) |
| A host C compiler (gcc/clang/cc) + make | Building the portable host shell/tests | No | The user's own system toolchain. |
| QEMU / Bochs / 86Box / PCem | Emulator smoke testing | No | Obtained by the user under their respective licenses. |

## Desktop icon packs

The shell loads a **desktop/toolbar/Quick Launch icon pack** — a directory of
keyed BMPs pointed to by `[Assets] Icons=` (`src/shell/sh_iconpack.c`; see
[assets/icons/README.md](assets/icons/README.md)). Two packs ship in-tree, and a
missing/empty path falls back to the built-in **procedural** icons (no assets):

- **`assets/icons/tango/`** — the **default**, converted from the **Tango Icon
  Library** (**Public Domain**; recorded in the bundled-components table above
  and [`licenses/tango-COPYING.txt`](licenses/tango-COPYING.txt)). This is the
  one third-party icon set bundled.
- **`assets/icons/castalia/`** — **original, first-party, MIT-licensed** art
  baked by `tools/gen_iconpack.c` (the all-original alternative; select it with
  `[Assets] Icons=C:\CASTALIA\ICONS\CASTALIA`).

Any other freely-redistributable set (CC0 / MIT / public-domain recommended) can
be added with `tools/png2bmp.py` (Python standard library only; bundles no
assets itself). If a CastaliaOS build ships one, it **must** be recorded above
with its version, source URL, SPDX license, a license-text copy under
`licenses/`, and any required attribution. Avoid CC-BY-SA/GPL art in the shipped
tree, and never use Microsoft-derived icons (see [LEGAL.md](LEGAL.md)).

## System text face

The engine renders text from a compiled-in 8-row bitmap glyph table
(`src/gfx/gfx_font.c`), and the physical face is selectable via `[Assets] Font=`
with no layout change (all faces share the 8-row cell and 6px advance):

- **`spleen`** — the **default**, the **Spleen 5x8** font (**BSD-2-Clause**;
  recorded in the table above and [`licenses/spleen-LICENSE.txt`](licenses/spleen-LICENSE.txt)),
  baked into `src/gfx/gfx_font_spleen.c` from `assets/fonts/spleen-5x8.bdf` by
  `tools/bdf2font.py` (`make gen-font`).
- **`system`** — the **original 8x8 face authored for this project**
  (`src/gfx/gfx_font_data.c`), MIT, always available.

`tools/bdf2font.py` accepts any ≤8-row BDF, so another permissively-licensed
(MIT/OFL/BSD/PD) bitmap face can be baked in the same way — record it above
before shipping. Avoid CC-BY-SA/GPL fonts in the shipped tree.

## Optional, design-only (NOT used by current code)

| Component | Status |
|-----------|--------|
| Allegro 4 | Mentioned in the architecture as an *optional* backend behind the Castalia platform interface. The current code does **not** use, link, or bundle Allegro. If ever adopted, it must sit behind `plat.h`, be recorded here with its license (giftware/zlib-style), and honor its terms. |
| Additional icon packs | Beyond the bundled Tango (default) and Castalia (original) packs, other free sources plug into `sh_iconpack.c` via `tools/png2bmp.py` (record each here before shipping): Kenney (CC0), OpenGameArt CC0 sets, Pixelarticons (MIT). |

## If you add a third-party component

Before bundling anything third-party (a font, icon set, library, sound pack,
etc.), add a row here with:

- Component name and version.
- Exact source (URL / repository).
- License (SPDX identifier if possible) and a copy of the license text under a
  `licenses/` directory.
- Redistribution notes (may it be shipped in binary? attribution required?).
- What in the tree uses it.

If a suitable license cannot be confirmed, generate an original equivalent
instead. When in doubt, leave it out.
