/*
 * vbe_pick.c - Choosing a VESA mode (see vbe_pick.h).
 *
 * One scoring pass, no allocation, no BIOS. The score is built so that a
 * higher number is always the better mode, which keeps the comparison to one
 * line and makes the preference order readable as data rather than as nested
 * conditionals.
 */
#include "castalia/ctypes.h"
#include "vbe_pick.h"

cbool vbe_depth_drivable(int bpp)
{
    /* The presenter packs 3:3:2 at 8bpp and 5:6:5 at 16. Nothing else is
     * written correctly, so nothing else may be chosen. */
    return (bpp == 8 || bpp == 16) ? CTRUE : CFALSE;
}

int vbe_pick(const VbeMode *list, int n, int want_w, int want_h, int want_bpp)
{
    int i, best = -1;
    long best_score = -1;
    if (list == NULL || n <= 0) { return -1; }
    for (i = 0; i < n; i++) {
        const VbeMode *m = &list[i];
        long score;
        if (!m->supported || !m->graphics) { continue; }
        if (m->w != want_w || m->h != want_h) { continue; }
        if (!vbe_depth_drivable(m->bpp)) { continue; }
        /* Requested depth first, then the deepest drivable one, then a linear
         * framebuffer over a banked one. The weights are far enough apart that
         * a lower term can never outrank a higher one. */
        score = 0;
        if (m->bpp == want_bpp) { score += 1000; }
        score += (long)m->bpp;
        if (m->lfb) { score += 1; }
        if (score > best_score) { best_score = score; best = i; }
    }
    return best;
}
