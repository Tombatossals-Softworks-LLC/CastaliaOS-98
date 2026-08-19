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

## Where we are — v0.1.0 (MVP / Phase 1)

A bootable desktop with a themeable shell, window manager, UI toolkit, virtual
desktops, snapping, and 20+ applications including a Windows XP‑style file
manager, a Winamp‑style media player, a benchmark suite, and Solitaire. Verified
by 1,211 host test checks, nine end‑to‑end scenes, and a golden‑screenshot
pipeline.

## Near term (Phase 2–3)

- **File Manager:** drag‑and‑drop between panes, thumbnail/large‑icon views for
  images, and richer file associations.
- **Media Player:** real Sound Blaster DAC streaming (the PCM seam already
  exists) and more visualizer modes. (The three‑band equalizer from this list
  has already shipped.)
- **Networking:** ARP, IPv4, ICMP and UDP now sit on the packet‑driver seam,
  with a Network window that resolves addresses and pings. Still to come: the
  DOS receive path (the packet driver's receiver callback), TCP, and a minimal
  downloader.
- **More apps & polish:** richer file associations, and drag‑and‑drop between
  the two panes. (The terminal, the first‑run "welcome" tour, the
  calendar/agenda, the rewrite of Paint with its selection/clipboard/stretch,
  and image thumbnails in the file manager from this list have already
  shipped.)
- **Theming:** downloadable theme/icon packs and per‑window transparency where
  the budget allows. (The theme editor from this list has already shipped: it
  mixes every color, previews live, and saves to plain INI.)

## Medium term (Phase 4+)

- **A real installer** and a bootable CD/USB image with a friendly setup.
- **A software catalog** for `.CAPP` add‑ons — discover, install, and update.
- **A small SDK** so third parties can build add‑ons against the documented ABI.
- **Localization** to additional languages.
- **Performance** work for lower‑end targets and true 8‑bit palette animation.

## The dream

An original desktop OS a community can extend — where a `.CAPP` you wrote on a
modern laptop drops onto a 386 and just runs; where the whole thing still fits on
a couple of floppies; and where "retro" means *lovingly engineered*, not
*emulated*.

We'd love press, collaborators, and testers. See `contact.md`.
