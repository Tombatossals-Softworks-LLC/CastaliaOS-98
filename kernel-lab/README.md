# CastaliaOS — Native Kernel Lab (research track, isolated)

> **This directory is an isolated research track. It is NOT part of the v1
> product and MUST NOT block or gate the FreeDOS-hosted desktop.** Nothing in
> the main build (`Makefile`, `Makefile.dos`, `build.sh`) references anything
> here. The flagship product is the DOS-hosted Castalia DOS Shell; this is a
> forge for tools and knowledge, not the town people live in.

## Purpose

A place to experiment with a from-scratch x86 boot + kernel, entirely separate
from the shipping desktop. Progress here is optional and never a prerequisite
for a CastaliaOS release.

## Milestone plan (long horizon)

1. ✅ Stage-1 boot sector that prints text (`boot16.asm`, minimal, inspect-only).
2. ✅ Stage-2 loader (`bootstage.asm` loads `stage2.asm` from disk and jumps to
   it); **enters 32-bit protected mode** (`stage2.asm`: A20 gate, flat GDT,
   CR0.PE, far jump to 32-bit code, VGA banner).
3. ✅ GDT + **IDT initialization** (`stage2.asm` builds the first 32 exception
   gates and `lidt`s them, so a fault is caught, not a triple-fault reboot).
4. ✅ **PIC remap + PIT (IRQ0) + PS/2 keyboard (IRQ1)** (`stage2.asm`): the two
   8259 controllers are re-initialized so hardware IRQs land at vectors
   0x20..0x2F instead of colliding with the CPU exception range; IDT gates
   0x20/0x21 point at a timer and a keyboard ISR; PIT channel 0 is programmed
   for a ~100 Hz tick; `sti` enables interrupts and the kernel idles in `hlt`.
   The timer ISR increments an on-screen tick counter (and sends EOI, so it
   keeps firing); the keyboard ISR reads port 0x60 and shows the last scancode.
5. ⬜ Physical memory map (INT 15h E820 captured in real mode) + a simple heap.
6. ⬜ VESA mode setup via the BIOS before PM (or VBE thunking).
7. ⬜ FAT12 floppy reader; then FAT16/FAT32.
8. ⬜ Cooperative task scheduler; a userland ABI experiment.

> Status: milestones 2-4 **boot and were verified in QEMU** (see below). The
> `castalia-klab.img` two-stage image reaches 32-bit protected mode with the GDT
> and IDT loaded and prints its banner; a QMP register dump confirms CS is a
> 32-bit code selector, DS/SS are the flat data selector, CR0.PE is set, A20 is
> on, and GDTR/IDTR point at our tables. With the PICs remapped and interrupts
> enabled, a headless VGA text-memory dump shows the on-screen timer-tick counter
> advancing on its own (IRQ0 firing and being acknowledged) and an injected
> keypress updating the scancode readout (IRQ1). Milestones 5+ remain future
> work.

## Explicit non-goals (until far in the future)

Win32 binary compatibility, VxD drivers, Plug and Play, a USB stack, a full
TCP/IP stack, DirectX, SMP. Attempting these as early milestones is the road to
the "bog of heroic lies" the Project Bible warns against.

## Status and honesty

`boot16.asm` is the original print-only Stage-1 artifact. `bootstage.asm` +
`stage2.asm` build the two-stage `castalia-klab.img`, which **has been assembled
with NASM and booted in QEMU** from this project: it enters 32-bit protected
mode with a flat GDT and a 256-entry IDT, remaps the PICs, and takes real
hardware interrupts. Observed register state at protected-mode entry (QEMU QMP
`info registers`):

```
CS =0008 ... CS32   DS/SS/ES/FS/GS =0010 ... DS (flat 4 GB)
CR0=00000011 (PE set)   A20=1
GDT= base 0x8098 limit 0x17     IDT= base 0x80f8 limit 0x7ff
```

Milestone 4 was verified by dumping VGA text memory over the QEMU monitor
(`pmemsave 0xb8000 4000 ...`) while the kernel ran headless:

```
row0  CastaliaOS kernel-lab: 32-bit PM + PIC remap + IRQ dispatch.
row1  PIT IRQ0 and PS/2 keyboard IRQ1 are live. Type in QEMU.
row2  timer ticks: 00CE   ->  (1 s later)  timer ticks: 018C
row3  last scancode:            ->  (after `sendkey a`)  last scancode: 9E
```

The tick counter advancing on its own confirms IRQ0 is firing ~100 Hz and each
interrupt is acknowledged with an EOI (otherwise it would stop after one); the
`9E` is the *break* code for `a` (`0x1E | 0x80` — QEMU's `sendkey` sends press
and release, and the ISR shows the last scancode read from port 0x60),
confirming IRQ1 is delivered and serviced.

Treat everything past milestone 4 as unverified research. See the local
`Makefile` for build/run commands.

## Building (only if you opt in)

Requires `nasm` and `qemu-system-i386`, neither of which the main project needs.

```sh
cd kernel-lab
make            # assemble boot16.bin (the print-only Stage-1 artifact)
make run        # boot it in QEMU

make image      # build castalia-klab.img (Stage-1 loader + Stage-2 PM/IRQs)
make run-image  # boot the two-stage image; expect the banner + a live tick count
```

Booting `castalia-klab.img` should show the Stage-1 "loading stage 2" line,
then, after 32-bit code installs the GDT/IDT and remaps the PICs, a banner on a
blue screen with a `timer ticks:` counter that climbs and a `last scancode:`
field that updates as you type. A red `X` in the top-right corner instead means
one of the first 32 CPU exceptions fired
and was caught by the IDT stub.
