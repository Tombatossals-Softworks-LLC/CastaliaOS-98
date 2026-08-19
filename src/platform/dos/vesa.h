/*
 * vesa.h - VESA VBE video services for the DOS backend (windowed/banked).
 *
 * Compiled only under CASTALIA_DOS. Presents the 32-bit XRGB back buffer to
 * an 8bpp or 16bpp VBE mode, converting per pixel via the gfx pack kernels.
 *
 * A linear framebuffer is used where the card offers one and the extender can
 * map it, which is the fast path; a banked (window) framebuffer is the
 * fallback, for maximum compatibility with S3 Trio3D-class VBE BIOSes that
 * report no LFB. Which one is in use shows up as the driver name in System
 * Information (vesa-linear or vesa-banked).
 */
#ifndef CASTALIA_VESA_H
#define CASTALIA_VESA_H

#ifdef CASTALIA_DOS

#include "castalia/ctypes.h"
#include "castalia/gfx.h"
#include "../../gfx/vbe_pick.h"

typedef struct {
    int  width;
    int  height;
    int  bpp;                /* 8 or 16                                    */
    int  bytes_per_pixel;    /* 1 or 2                                     */
    cu16 bytes_per_scanline;
    cu16 win_seg;            /* real-mode segment of window A (e.g. 0xA000)*/
    cu32 win_granularity;    /* bytes per bank step                        */
    cu16 mode_number;        /* VBE mode used                              */
    cbool active;
    /* Linear-framebuffer fast path (VBE2 LFB). When 'linear' is CTRUE the
     * present step writes straight to 'lfb' with no bank switching; otherwise
     * the banked window path above is used. */
    cbool linear;
    unsigned char *lfb;      /* mapped framebuffer near pointer, or NULL   */
    cu32 phys_base;          /* PhysBasePtr reported by the BIOS           */
} VesaMode;

/* Detect VBE. Returns CE_OK if a usable VBE 2.0+ BIOS is present. */
CResult vesa_init(void);
void    vesa_shutdown(void);   /* restores text mode (INT 10h AX=0003h)    */

/*
 * Try to set 'w'x'h' at 'bpp'. On success fills 'out'. On failure returns an
 * error and leaves the previous mode. The caller (plat_dos) applies the
 * 800x600x16 -> 640x480x16 -> 640x480x8 fallback ladder.
 */
CResult vesa_set_mode(int w, int h, int bpp, VesaMode *out);

/*
 * Describe every mode in the BIOS list, into a caller-owned array, and return
 * how many were written. The raw reading: unsorted, duplicates and all,
 * including depths the presenter cannot drive.
 *
 * Both the mode chooser and the System Information report walk this, and they
 * want different things from it -- the chooser discards what it cannot use,
 * the report keeps it and says so. One walk, two policies, rather than two
 * walks that drift.
 */
/* The BIOS mode list is capped here. A caller that wants every mode the card
 * offers sizes its array to this; the count was private to vesa.c until the
 * System Information report needed to walk the same list. */
#define VESA_MAX_MODES 256

int vesa_enum_modes(VbeMode *out, int max);

/* Convert & copy a back-buffer rectangle to the display. */
void    vesa_present_rect(const GfxSurface *back, const CRect *r);

/* Load an 8bpp palette (RGB 6-bit DAC) from a 256-entry XRGB table. Only
 * meaningful in 8bpp modes. */
void    vesa_set_palette_332(void);

#endif /* CASTALIA_DOS */
#endif /* CASTALIA_VESA_H */
