/*
 * dos_dpmi.h - Minimal DPMI real-mode services for the DOS/4GW-style flat
 *              32-bit backend (Open Watcom).
 *
 * Under a 32-bit DOS extender the CPU runs in protected mode, but VESA BIOS
 * calls that pass a pointer (VbeInfoBlock, ModeInfoBlock) must run in real
 * mode with ES:DI pointing at conventional memory below 1 MB. We therefore:
 *   1. Allocate a DOS (low) transfer buffer via DPMI INT 31h AX=0100h.
 *   2. Invoke the target BIOS interrupt via DPMI "simulate real-mode
 *      interrupt" INT 31h AX=0300h, with the buffer's real-mode segment in
 *      ES and DI=0.
 *
 * Register-only BIOS calls (set mode, bank switch, INT 33h mouse) do not
 * need this and are issued directly with int386().
 *
 * COMPILED ONLY under CASTALIA_DOS with Open Watcom. This code cannot be
 * exercised from the portable host build; it is validated on the QEMU/Bochs
 * and real-hardware passes documented in docs/TESTING.md.
 */
#ifndef CASTALIA_DOS_DPMI_H
#define CASTALIA_DOS_DPMI_H

#ifdef CASTALIA_DOS

#include "castalia/ctypes.h"

/* DPMI real-mode call register frame (INT 31h AX=0300h), 50 bytes. */
typedef struct {
    cu32 edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    cu16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} DpmiRegs;

/* Allocate 'paragraphs'*16 bytes of conventional memory. On success returns
 * CE_OK and fills *rm_seg (real-mode segment) and *sel (PM selector used to
 * free it later). */
CResult dpmi_alloc_dos(cu16 paragraphs, cu16 *rm_seg, cu16 *sel);
void    dpmi_free_dos(cu16 sel);

/* Simulate a real-mode interrupt with the given register frame. */
CResult dpmi_int(int intno, DpmiRegs *regs);

/* Copy between a linear (protected-mode) pointer and the DOS transfer buffer
 * segment:offset. These use the low memory identity mapping the extender
 * provides for conventional RAM. */
void    dpmi_copy_to_dos(cu16 rm_seg, cu16 off, const void *src, cu32 bytes);
void    dpmi_copy_from_dos(void *dst, cu16 rm_seg, cu16 off, cu32 bytes);

/* Map 'bytes' of physical memory at 'phys' (e.g. a VBE linear framebuffer
 * PhysBasePtr) into the linear address space via DPMI INT 31h AX=0800h. On the
 * zero-based flat model DOS/4GW uses, the returned linear address IS a usable
 * near pointer. Returns NULL on failure (caller falls back to banked video). */
void   *dpmi_map_physical(cu32 phys, cu32 bytes);
/* Release a mapping from dpmi_map_physical (AX=0801h). Safe on NULL. */
void    dpmi_unmap_physical(void *linear);

/* Read the real-mode interrupt vector 'vec' (INT 31h AX=0200h). On success
 * fills 'seg' and 'off' with the handler's real-mode address. Used to scan for
 * an installed packet driver. Returns CE_OK or an error. */
CResult dpmi_get_real_vector(int vec, cu16 *seg, cu16 *off);

#endif /* CASTALIA_DOS */
#endif /* CASTALIA_DOS_DPMI_H */
