# History — the story and the "why"

## Why build a new old OS?

The desktops of the late‑90s and early‑2000s had a particular magic: instant,
tactile, and legible. Beveled buttons you could almost press. A Start menu. A
file explorer with a friendly blue task pane. Glossy title bars. You always knew
where you were. CastaliaOS began as a simple question — *could you build that
feeling from scratch, legally clean, small enough to run on a 386?* — and turned
into a full system.

## The approach

Rather than mod an existing OS or theme a modern one, CastaliaOS is written from
the platform layer up in portable C. A strict rule shaped everything: **nothing
above the platform layer may touch the hardware.** That single constraint made
the system both portable (it develops and unit‑tests on a modern host) and
disciplined (every subsystem has a clean `_init` / `_shutdown` pair and is tested
in isolation). The software renderer, the window manager, the UI toolkit, the
shell, and the apps were built layer by layer, each verified by host tests and a
golden‑screenshot pipeline before the next layer landed.

## What's original — and what isn't

Everything in CastaliaOS is original code and artwork. The name "Castalia," the
castle crest, the "Aurora" theme, the icons drawn by the engine's own renderer,
and every application are ours. The few bundled third‑party pieces are freely
licensed and credited in `THIRD_PARTY_NOTICES.md`: the **Spleen** bitmap font
(BSD) and, optionally, the **Tango** icon set. There is **no Microsoft code,
artwork, or branding** anywhere in the project — CastaliaOS is *inspired by* an
era, not copied from a product.

## The name

*Castalia* is the spring of the Muses at Delphi — a source of inspiration. It felt
right for a system that is, at heart, a tribute: to the machines that taught a
generation to love computers, and to the idea that you can still build the whole
stack yourself and understand every pixel on the screen.

## The developers

CastaliaOS is created by **Dave Abellan** and **Claudio di Castello** under the
banner of **Tombatossals Softworks**, an independent studio. It is free and open
source under the MIT license — a project built for the love of the craft.

Contact: hello@tombatossalssoftworks.com · tombatossalssoftworks.com
