# CastaliaOS 98 PE — Legal Notice

## Summary

CastaliaOS 98 PE ("Powerful Edition") is an **original retro computing
project**. It is **not** Microsoft Windows, Windows 98, Windows 98 Second
Edition, MS-DOS, or any Microsoft product, and it is **not affiliated with,
endorsed by, or sponsored by Microsoft**. "Windows", "Windows 98", and related
marks are the property of Microsoft Corporation and are used here only nominally,
to describe the era and interaction style CastaliaOS is *inspired by*.

CastaliaOS is Win9x-**inspired**. It is not a Windows 98 clone, and that phrase
should not be used to describe it in public material.

## What this project does NOT contain

This project does not include, redistribute, or depend on any of the following:

- Microsoft operating system files, system DLLs, or binaries.
- Microsoft product keys, activation data, setup media, or boot disks.
- Microsoft registry hives.
- Microsoft icons, cursors, fonts, sounds, wallpapers, logos, or branding.
- The Windows flag, the Start button artwork, or any Microsoft-era UI artwork.
- Leaked Microsoft source code or leaked symbol/debug information.

No Microsoft binaries are patched, wrapped, or repackaged for redistribution.

## What this project contains

- Original C source code authored for CastaliaOS, under the [MIT License](LICENSE).
- Original UI artwork: the Castalia castle/shield identity, procedurally-drawn
  icons, the "Castalia Classic" color palette, and an **original 8×8 bitmap
  font** authored specifically for this project (`src/gfx/gfx_font_data.c`).
- Original documentation, package formats, and configuration schemas.

All colors, glyphs, icon silhouettes, and window ornamentation are our own
designs in the late-1990s idiom, not copies of Microsoft artwork.

## Relationship to FreeDOS and toolchains

- **FreeDOS** is a separate, independently licensed open-source project. This
  repository does not bundle FreeDOS. Users supply their own FreeDOS (or other
  DOS-compatible) installation. Obtain and use it under FreeDOS's own license.
- **Open Watcom** is the recommended build toolchain. It is licensed separately;
  obey its license and redistribution terms.
- **The DOS/4GW extender (`DOS4GW.EXE`)** is a third-party binary (DOS/4G ©
  Rational Systems, Inc. / Tenberry Software) that the Open-Watcom-built
  executables load at runtime. CastaliaOS **bundles** it in the `RELEASE/` tree
  and in release ZIPs under a DOS/4GW distribution license **held by Tombatossals
  Softworks LLC**. It is shipped unmodified. It is recorded in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); anyone redistributing a
  CastaliaOS build outside that license must supply their own `DOS4GW.EXE` under
  their own rights instead.
- **Allegro 4** is mentioned in the design as an *optional* backend behind the
  Castalia platform interface; it is **not** used or bundled by the current code.

Every third-party component that is ever bundled must be recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) with its license, source,
version, and redistribution notes.

## Windows 98 SE companion mode (roadmap)

A future, optional "Castalia Win98 Shell" companion is intended to run on a
user's **own, legally installed** copy of Windows 98 SE. If built, it must:

- Never redistribute Microsoft components.
- Never replace or patch Microsoft system files (e.g. Explorer, system DLLs).
- Always install reversibly, with a documented restore path (a restore batch
  file and DOS-level instructions) before any shell replacement.
- Use only original Castalia icons, controls, wallpapers, and sounds.

It is a prototype/companion, not the flagship, and must not be presented as a
Windows distribution.

## No warranty

This software is provided "as is", without warranty of any kind. See the
[MIT License](LICENSE). Retro hardware and old disks are fragile; back up your
data and test on a disk clone before experimenting. Do not expose a retro system
directly to the public internet.

## Trademarks

All product names, logos, and brands mentioned are the property of their
respective owners and are used for identification and descriptive purposes only.
Their use does not imply endorsement.

## Reporting a concern

If you believe any file in this repository infringes a copyright or trademark,
please open an issue so it can be reviewed and, if warranted, removed or
replaced with an original equivalent.
