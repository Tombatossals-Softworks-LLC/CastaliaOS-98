# Hardware Targets

CastaliaOS 98 PE is designed for one concrete machine profile: a late-1990s IBM
business desktop in the Pentium II class with Intel 440BX-era behavior. This
document records that target, what it implies for the design, and how the
emulators stand in for it.

See also: [ARCHITECTURE.md](ARCHITECTURE.md) for the layer design,
[TESTING.md](TESTING.md) for the validation matrix, and
[BUILDING.md](BUILDING.md) for producing binaries for this hardware.

---

## 1. Primary target machine

Derived from the Project Bible, §4 (Hardware Target Profile).

| Component | Target |
|-----------|--------|
| Class | IBM business desktop / minitower |
| CPU | Pentium II, ~400 MHz |
| Chipset | Intel 440BX-era behavior |
| RAM | 128 MB PC100 SDRAM |
| Video | Integrated S3 Trio3D-class, **4 MB VRAM**, VESA/VBE BIOS |
| Storage | IDE hard disk (ATA-33 class) + CD-ROM |
| Input | **PS/2 keyboard and PS/2 mouse** (first-class) |
| Audio | Integrated business audio (Crystal / ESS class) — optional |
| Networking | Intel Pro/100-class Ethernet — optional |
| USB | USB 1.1 — never a v1 dependency |
| Expansion / I/O | PCI, ISA, serial, parallel |

The design intent, in the Bible's words, is "a lost 1999 professional desktop":
comfortable for a 2D windowed environment on this hardware, but with a
disciplined scope — no pretending modern capabilities exist.

---

## 2. Implications for the design

**CPU budget.** A Pentium II at ~400 MHz is comfortable for a 2D desktop but
not for wasteful per-pixel work. The renderer uses **dirty rectangles**,
clips every control to its visible region, and avoids full-screen repaints
except on mode switch, theme switch, or crash recovery. There is no per-pixel
alpha blending in v1.

**4 MB VRAM math.** One visible framebuffer at 800x600x16 is
800 x 600 x 2 = 960,000 bytes (~0.94 MB). That fits comfortably in 4 MB VRAM,
but there is no room for generous offscreen caches or triple buffering there.

**Why the 32-bit back buffer lives in system RAM, not VRAM.** The whole upper
stack renders into a single **32-bit XRGB back buffer**, and the platform layer
converts to the hardware depth (8 or 16 bpp) only at present time. That back
buffer at 800x600 is 800 x 600 x 4 = 1,920,000 bytes (~1.83 MB) — which is why
the host build's idle footprint is ~3.7 MB (two full-screen 32-bit surfaces:
the back buffer and the desktop cache). This buffer is allocated in
**system RAM** (`sys_alloc` in `src/platform/dos/plat_dos.c`), where 128 MB is
plentiful, rather than in the scarce 4 MB VRAM. Keeping the master surface at a
fixed 32 bpp in RAM lets all rendering be depth-independent; VRAM holds only the
visible framebuffer at the real hardware depth. The DOS backend copies dirty
rectangles from the RAM back buffer to VRAM through a **banked (windowed)** VBE
path (see §3).

**IDE fragility, and CF/SD replacements.** Period IDE disks may be old and
unreliable. The project should support **CompactFlash or SD-to-IDE**
replacements as the boot disk, but **must not require** them — a stock IDE
disk must work.

**PS/2 is first-class.** Keyboard (INT 16h) and mouse (INT 33h) are the primary,
always-present input. The desktop assumes PS/2 input is available.

**USB is never required.** USB 1.1 is present on the hardware but is not a safe
primary input or storage dependency for v1; nothing in the boot or desktop path
depends on it.

**Sound and networking are optional and never block boot.** A failed sound init
must not prevent the desktop from coming up. Networking is not a v1 boot
dependency. Both are off by default.

---

## 3. Supported video modes and fallback ladder

The DOS backend talks to the card first through **VESA/VBE BIOS services**, not
custom S3 acceleration, for maximum compatibility. It sets modes through a
**banked (windowed) VBE path**: `INT 10h AX=4F02h` *without* the
linear-framebuffer bit, paging window A with `AX=4F05h`. Banked access works on
essentially every VBE BIOS, including S3 Trio3D-class parts. A DPMI-mapped
**linear-framebuffer fast path is a documented TODO** (`src/platform/dos/vesa.c`,
Phase 4).

| Mode | Depth conversion from the 32-bit back buffer | Role |
|------|----------------------------------------------|------|
| 800x600x16 | RGB565 (`gfx_pack_565`) | Preferred |
| 640x480x16 | RGB565 | Fallback |
| 640x480x8 | 3-3-2 palette (`gfx_pack_index_332`), programmed into the VGA DAC | Last resort / Safe Mode |

Fallback ladder (from `plat_dos.c`, matching Bible §17):

```
800x600x16  ->  640x480x16  ->  640x480x8
```

**Safe Mode** forces **640x480x8** directly. If every mode fails, the backend
logs the failure and returns to DOS with a message (never a blank hang).

A 1024x768x8 mode is noted in the Bible as *optional* for stable S3 Trio3D-class
cards; it is not part of the current fallback ladder.

---

## 4. Input

- **Keyboard** — polled BIOS enhanced-keyboard calls (`INT 16h` AH=11h check,
  AH=10h read). Printable ASCII maps to itself; arrows, function keys, and
  Delete map into the `PLAT_KEY_*` space (`src/platform/dos/keyboard.c`).
- **Mouse** — `INT 33h`: reset/detect (function 0), coordinate range clamped to
  the screen (functions 7/8), position and buttons polled (function 3). Left,
  right, and middle buttons are reported (`src/platform/dos/mouse.c`).

The polled DOS input state is synthesized into the same event stream the host
backend produces (`PLAT_EV_MOUSE_MOVE/DOWN/UP`, `PLAT_EV_KEY_DOWN`), so the
window manager and shell are platform-agnostic.

---

## 5. Storage

- File I/O uses the C runtime, which maps to DOS `INT 21h` under Open Watcom
  (`plat_fopen/fread/fwrite/fclose`, exists/remove/rename/size).
- Directory enumeration uses `_dos_findfirst` / `_dos_findnext`
  (`plat_opendir/readdir/closedir`).
- The install lives under `C:\CASTALIA` with subdirectories for `BIN`, `SYS`,
  `THEMES`, `APPS`, `FONTS`, `ICONS`, `HELP`, `LOGS`, `TEMP`, and `TRASH`
  (see `dist/cdroot/CASTALIA/README.TXT`).
- IDE disks (or CF/SD-to-IDE replacements) and a CD-ROM are the expected media.
  Long filename support is a roadmap item; v1 assumes 8.3 DOS names.

---

## 6. Audio (optional)

- Target chips: integrated **Crystal** or **ESS** class business audio; a
  Sound Blaster-compatible path is the initial route (Bible §26).
- v1 stance: a **null sound driver**; the UI must not require sound. A failed
  sound init must never prevent boot — "the desktop is a ship, not a jukebox."
- `CASTALIA.INI` `[Sound] Enabled=0` by default.
- **Status:** no audio driver is implemented yet; this is a roadmap item.

---

## 7. Networking (optional)

- Target hardware: **Intel Pro/100**-class 10/100 Ethernet (Bible §25).
- Roadmap: v1 has no networking dependency; v1.5 aims at **packet drivers**
  where available; later versions may interoperate with mTCP-style tooling for
  LAN file transfer and crash-log shipping, licensing permitting.
- Security stance: LAN-only, disabled by default, never exposed to the public
  internet.
- **Status:** not implemented; roadmap item.

---

## 8. Emulator stand-ins

Real 440BX hardware is the final authority, but three emulator tiers stand in
during development (Bible §7, §22). Ready-to-use profiles live in
`emulators/`; see [TESTING.md](TESTING.md).

| Emulator | Role | Profile in repo | Models |
|----------|------|-----------------|--------|
| **QEMU** | Fast boot smoke test | `emulators/qemu_run.sh` / `.bat` | `-M pc -cpu pentium2 -m 128 -hda castalia.img -boot c -vga std` (i440FX PC, 128 MB, PS/2, IDE, standard VBE VGA) |
| **Bochs** | Slower, detailed device-level debugging (VESA/VBE, PS/2) | `emulators/bochs_castalia.bxrc` | `pentium_mmx`, 128 MB, `vga: extension=vbe`, PS/2 kbd+mouse, IDE `ata0`, verbose logging |
| **86Box / PCem** | Period-feel hardware validation before touching the real IBM | (no profile shipped) | Cycle-accurate 440BX-class boards, period S3 video |

QEMU maps to the target's i440FX/PC peripheral set; Bochs' feature list includes
i440BX and legacy device models useful for reproducing card behavior; 86Box/PCem
give the closest period feel prior to real hardware. All three need a FreeDOS
disk image with `C:\CASTALIA` populated and `AUTOEXEC.BAT` calling `CBOOT`.

---

## 9. Known unknowns — must validate on real hardware

None of the following is verified from the host-only environment; each must be
confirmed on the emulators and, ultimately, the real IBM machine (see the
Layer 3–4 matrices in [TESTING.md](TESTING.md)):

- **VBE behavior on a real S3 Trio3D-class BIOS** — whether the exact
  800x600x16 and 640x480x8 modes exist, and whether banked window switching
  (`AX=4F05h`) behaves as assumed across bank boundaries at 16 bpp.
- **Banked-present performance** — whether dirty-rectangle repaint through a
  banked window meets the budget (640x480 full repaint < 100 ms; 800x600 dirty
  repaint < 33 ms) without the linear-framebuffer fast path.
- **PS/2 mouse/keyboard timing** on real hardware vs. emulator.
- **BIOS timer / APM / keyboard-controller reset** — the ~18.2 Hz tick math,
  APM power-off, and the `0x64 <- 0xFE` reboot pulse are best-effort and
  untested here.
- **IDE reliability** on aged disks, and whether CF/SD-to-IDE replacements boot
  cleanly.
- **Integrated audio chip identification** (Crystal vs. ESS vs. other) if/when
  sound is implemented.
- **DOS/4GW behavior** under FreeDOS with the target's memory configuration.

Record the machine's BIOS version, disk model, video BIOS/VESA mode list, input
behavior, audio chip ID, and PCI device list during real-hardware bring-up
(Bible §22 real hardware profile).
