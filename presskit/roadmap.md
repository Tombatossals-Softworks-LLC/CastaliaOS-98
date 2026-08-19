# Roadmap — what we want to do and achieve

## The vision

CastaliaOS aims to be the most **complete, honest, and genuinely usable**
original retro desktop you can run on real period hardware — a system that feels
like the late‑90s/early‑2000s PC we loved, but that is legally clean, hackable,
and modern in its engineering. Not a skin, not an emulator theme: a real shell
with a real window manager and real apps, small enough to feel instant on a 386.

Three guiding principles:

1. **Original & clean.** Every line of code and every pixel of art is ours (or a
   clearly‑licensed, credited asset). No Microsoft code, art, or branding.
2. **Runs on the metal.** If it doesn't boot on period hardware, it doesn't ship.
   The host build exists to keep us honest and testable, not to replace DOS.
3. **Repairable & hackable.** Plain‑text settings, a documented `.CAPP` add‑on
   ABI, a crash boundary with safe mode, and a codebase you can read in a weekend.

## Where we are — v0.1.0‑preview1 (MVP / Phase 1)

A bootable desktop with a themeable shell, window manager, UI toolkit, four
virtual desktops, window snapping, and 30+ applications including a Windows
XP‑style file manager, a Winamp‑style media player, a benchmark suite, and
Solitaire. Verified by **7,547** unit checks, **74** scenes run end to end, and a
golden‑screenshot pipeline; the DOS product is compiled and linked on every push
by CI with Open Watcom, and the desktop boots end to end on FreeDOS under QEMU.

## Next — getting off the emulator

Everything above is verified in QEMU. The honest headline for the next phase is
that **CastaliaOS has not yet run on the target machine**, and three of the four
items here are things only that will settle.

- **Real hardware.** Inventory the target IBM (the tool ships: System
  Information writes `CASTINFO.TXT` on F2), first boot, then a QA matrix across
  PS/2, VESA modes, IDE, CD and audio. The VESA mode‑picking policy is a tested
  pure function over the awkward mode lists real cards report — but no real card
  has reported one to us yet.
- **Sound on DOS.** Tone cues play through the Sound Blaster's 8‑bit DMA path.
  Arbitrary PCM does not: `snd_pcm_play` on the DOS backend returns
  "unsupported", so the Media Player is silent on real hardware. The seam is
  defined and the host backend implements it; the DMA streaming behind it is the
  work.
- **Networking receive.** The packet‑driver client detects the driver, reads its
  MAC, and sends raw Ethernet frames, with ARP, IPv4, ICMP and UDP on top and a
  Network window that resolves addresses and pings. Nothing can be *received*
  yet: that needs the driver's real‑mode receiver callback. TCP and a minimal
  downloader come after it.
- **A user guide.** Recovery, build, architecture and testing guides are
  written. The one aimed at somebody who just wants to use the desktop is not.

## Near term

- **File Manager:** a drive selector on DOS — the last item on its list.
  Drag‑and‑drop between panes, thumbnail and large‑icon views, and file
  associations have all shipped.
- **Toolkit:** a multiline edit, a tab strip, a slider, a status bar and a titled
  group box all work today, but as private implementations, one app each.
  Promoting them to shared controls is deliberate, unfinished work.
- **Installer:** install, uninstall, boot‑file backup and restore are done.
  Explicit repair and upgrade modes are not.
- **DOS program profiles:** the launcher runs a program; the profile UI (working
  directory, arguments, EMS/XMS/CD/sound flags, pre/post batch) is still to come.

## Medium term

- **A bootable CD/USB image** with a friendly setup. A bootable FreeDOS floppy
  builder ships today (`tools/make_dos_floppy.sh`).
- **A software catalog** for `.CAPP` add‑ons — discover, install, and update. The
  format, the documented ABI, the SDK headers and two example packages already
  ship; the catalog is what is missing.
- **Localization** to additional languages. Nothing in the tree is translated yet.
- **Performance** work for lower‑end targets and true 8‑bit palette animation.
- **Research tracks**, neither of which blocks v1: the `/kernel-lab` native
  kernel is at milestone 4 of 8 (protected mode, GDT/IDT, remapped PICs and live
  IRQ0/IRQ1, verified in QEMU), and the reversible Windows 98 SE shell companion
  has not been started.

## The dream

An original desktop OS a community can extend — where a `.CAPP` you wrote on a
modern laptop drops onto a 386 and just runs; where the whole thing still fits on
a couple of floppies; and where "retro" means *lovingly engineered*, not
*emulated*.

We'd love press, collaborators, and testers. See `contact.md`.
