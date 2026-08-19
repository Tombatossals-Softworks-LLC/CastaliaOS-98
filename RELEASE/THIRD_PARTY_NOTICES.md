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

## Optional, design-only (NOT used by current code)

| Component | Status |
|-----------|--------|
| Allegro 4 | Mentioned in the architecture as an *optional* backend behind the Castalia platform interface. The current code does **not** use, link, or bundle Allegro. If ever adopted, it must sit behind `plat.h`, be recorded here with its license (giftware/zlib-style), and honor its terms. |

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
