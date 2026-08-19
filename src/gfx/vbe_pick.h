/*
 * vbe_pick.h - Choosing a VESA mode from what the card offers.
 *
 * The DOS backend asks for 800x600x16, then 640x480x16, then 640x480x8, and
 * takes the first mode whose width, height AND depth match exactly. That rule
 * has two costs on real hardware, and neither shows up in an emulator that
 * offers the textbook mode list:
 *
 *   - A card that offers 800x600 in 15bpp and 8bpp but not 16bpp matches
 *     nothing at 800x600, so the ladder drops all the way to 640x480x8. The
 *     screen loses resolution it did not have to lose; 800x600x8 was there.
 *   - The first matching mode wins, so a BANKED mode is taken even when the
 *     same size and depth is also offered with a linear framebuffer further
 *     down the list. Banked writes cost a bank switch every few scanlines,
 *     which is the difference between a smooth desktop and a visibly slow one.
 *
 * The exact-match rule is also load-bearing in a way that is easy to miss, so
 * relaxing it needs care: the presenter can drive exactly two depths. At 8bpp
 * it packs a 3:3:2 index; at anything else it writes two bytes of 5:6:5 per
 * pixel. Hand it a 15bpp mode and every colour comes out wrong; hand it 24bpp
 * and the picture skews, because it writes two bytes into three-byte pixels.
 * Today nothing selects those depths only because the exact match happens to
 * exclude them. That is luck, not design, and this file replaces it with a
 * rule that says so: a mode is a candidate only if the presenter can actually
 * drive it.
 *
 * All of it is pure decision-making over a list the caller has already read
 * off the card, so tests/test_vbe.c can walk mode lists no machine here has.
 * The DOS-side plumbing that fills that list cannot be exercised from this
 * environment and is not pretended otherwise.
 */
#ifndef CASTALIA_VBE_PICK_H
#define CASTALIA_VBE_PICK_H

#include "castalia/ctypes.h"

typedef struct {
    cu16  mode;        /* the VBE mode number to hand back to the BIOS   */
    int   w, h, bpp;
    cbool supported;   /* mode attributes bit 0                          */
    cbool graphics;    /* mode attributes bit 4                          */
    cbool lfb;         /* a linear framebuffer is available              */
} VbeMode;

/*
 * Can the presenter write pixels of this depth correctly? Only 8 (3:3:2
 * indexed) and 16 (5:6:5). This is the presenter's actual capability, not a
 * policy choice, and everything else here defers to it.
 */
cbool vbe_depth_drivable(int bpp);

/*
 * The best mode for a request, as an index into 'list', or -1 if the card
 * offers nothing usable at that size.
 *
 * Candidates must be supported, graphics, drivable, and exactly the requested
 * size -- the caller owns the size ladder, and a picker that quietly returned
 * a different resolution than asked for would make that ladder impossible to
 * reason about. Among candidates: the requested depth wins; failing that the
 * deepest drivable one; and at equal depth a linear framebuffer beats a banked
 * one. Ties resolve to the earlier entry, so the choice is stable across calls.
 */
int vbe_pick(const VbeMode *list, int n, int want_w, int want_h, int want_bpp);

#endif /* CASTALIA_VBE_PICK_H */
