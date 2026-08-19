/*
 * gfx_blit.c - Surface-to-surface blits and rectangle capture/restore.
 *
 * Copy and color-keyed blits, both clipped to the destination clip rect.
 * capture/restore back the software cursor's save-under.
 */
#include "castalia/gfx.h"

#include <string.h>

void gfx_blit(GfxSurface *dst, int dx, int dy,
              const GfxSurface *src, const CRect *src_r, GfxBlitMode mode)
{
    CRect sr;
    CRect dr;
    CRect cd;
    int sx0, sy0;
    int y;

    /* Clamp the source rect to the source surface. */
    {
        CRect full = crect_make(0, 0, src->w, src->h);
        if (src_r != NULL) {
            sr = crect_intersect(&full, src_r);
        } else {
            sr = full;
        }
    }
    if (crect_empty(&sr)) { return; }

    /* Destination rectangle before clipping. */
    dr = crect_make(dx, dy, crect_w(&sr), crect_h(&sr));
    cd = crect_intersect(&dst->clip, &dr);
    if (crect_empty(&cd)) { return; }

    /* Where in the source we start, accounting for dst clipping. */
    sx0 = sr.x0 + (cd.x0 - dr.x0);
    sy0 = sr.y0 + (cd.y0 - dr.y0);

    /* Bounds and strides into plain locals before the loop. Written against
     * the CRects directly, the compiler must assume each pixel store can
     * modify cd.x1 -- CColor is an unsigned int and a CRect is made of ints,
     * so they may alias -- and re-reads the bound from the stack every pixel.
     * This is the second-hottest primitive after gfx_fill_rect; it is worth
     * the four extra lines. */
    {
        const CColor *srow = src->pixels + (long)sy0 * src->pitch + sx0;
        CColor *drow = dst->pixels + (long)cd.y0 * dst->pitch + cd.x0;
        int spitch = src->pitch, dpitch = dst->pitch;
        int n = cd.x1 - cd.x0;
        int rows = cd.y1 - cd.y0;
        if (mode == GFX_BLIT_KEYED) {
            int x;
            for (y = 0; y < rows; y++) {
                for (x = 0; x < n; x++) {
                    CColor c = srow[x];
                    if (c != GFX_COLORKEY) { drow[x] = c; }
                }
                srow += spitch;
                drow += dpitch;
            }
        } else {
            /* An opaque blit is a straight row copy, and the library's move
             * is the one routine every target already has a tuned version of
             * -- rep movsd on DOS, a vector loop on the host. memmove rather
             * than memcpy: every caller today blits between two distinct
             * surfaces, but a future self-blit (scrolling a surface within
             * itself) would silently corrupt with memcpy, and the difference
             * is one branch per ROW. */
            for (y = 0; y < rows; y++) {
                memmove(drow, srow, (size_t)n * sizeof(CColor));
                srow += spitch;
                drow += dpitch;
            }
        }
    }
}
