/*
 * vbe_report.c - Turning a card's mode list into something worth reading
 *                (see vbe_report.h).
 *
 * Insertion sort into the output array, checking for a duplicate on the way
 * past. The lists are at most a couple of hundred entries and this runs once
 * when a window opens, so the quadratic shape costs nothing and keeps the
 * whole thing to one pass with no scratch buffer -- which matters more here,
 * since the DOS build has to hold the array on the static side of a very small
 * stack.
 */
#include "vbe_report.h"
#include "vbe_pick.h"
#include "castalia/sys.h"

/* Ladder order: width, then height, then depth. Returns <0 if 'a' sorts
 * before 'b', >0 after, 0 when they are the same mode at the same depth --
 * which is the duplicate case, not a tie to break. */
static int vr_cmp(const PlatVideoMode *a, const PlatVideoMode *b)
{
    if (a->w != b->w)     { return (a->w < b->w) ? -1 : 1; }
    if (a->h != b->h)     { return (a->h < b->h) ? -1 : 1; }
    if (a->bpp != b->bpp) { return (a->bpp < b->bpp) ? -1 : 1; }
    return 0;
}

int vbe_report_modes(const PlatVideoMode *in, int n,
                     PlatVideoMode *out, int max)
{
    int i, count = 0;
    if (in == NULL || out == NULL || max <= 0 || n <= 0) { return 0; }

    for (i = 0; i < n; i++) {
        const PlatVideoMode *m = &in[i];
        int lo, k;
        /* A zero field means the BIOS query failed, not that the card offers
         * a 0x0 screen. Reporting it as a mode would be a lie the reader has
         * no way to catch. */
        if (m->w <= 0 || m->h <= 0 || m->bpp <= 0) { continue; }

        /* Find where it belongs, and notice a duplicate on the way. */
        lo = 0;
        while (lo < count && vr_cmp(&out[lo], m) < 0) { lo++; }
        if (lo < count && vr_cmp(&out[lo], m) == 0) {
            /* Same size and depth twice. Keep the linear-framebuffer entry,
             * because that is the one vbe_pick would take; showing both would
             * suggest a choice the user does not actually have. */
            if (m->lfb) { out[lo].lfb = CTRUE; }
            continue;
        }
        if (count == max) {
            /* Full. Drop the SMALLEST mode to make room, so a truncated list
             * keeps the top of the ladder.
             *
             * This is the diagnostic end. The report exists to answer "why is
             * the desktop at 640x480", and that answer is in the modes at and
             * above the current one -- what the card offers up there, and at
             * which depths. The small modes explain nothing, and the mode
             * actually in use is stated on its own line regardless. Truncating
             * by arrival order instead would make the report depend on
             * whatever sequence the BIOS happened to store its list in.
             */
            if (lo == 0) { continue; }   /* smaller than everything kept */
            for (k = 0; k < lo - 1; k++) { out[k] = out[k + 1]; }
            out[lo - 1] = *m;
            continue;
        }
        for (k = count; k > lo; k--) { out[k] = out[k - 1]; }
        out[lo] = *m;
        count++;
    }
    return count;
}

void vbe_mode_line(const PlatVideoMode *m, cbool current,
                   char *dst, cu32 dstsz)
{
    char size[32];
    if (dst == NULL || dstsz == 0) { return; }
    dst[0] = '\0';
    if (m == NULL) { return; }
    sys_snprintf(size, (cu32)sizeof size, "%dx%dx%d", m->w, m->h, m->bpp);
    /* The size column is padded so the framebuffer word lines up down the
     * page -- a ragged list of twenty modes gets read as prose instead of
     * scanned. The last column is NOT padded: this goes into a saved text
     * file, and a report full of trailing blanks is a report somebody has to
     * clean up before they can diff two machines. */
    if (vbe_depth_drivable(m->bpp)) {
        sys_snprintf(dst, dstsz, "%s %-14s %s",
                     current ? "*" : " ", size,
                     m->lfb ? "linear" : "banked");
    } else {
        sys_snprintf(dst, dstsz, "%s %-14s %-7s(cannot drive)",
                     current ? "*" : " ", size,
                     m->lfb ? "linear" : "banked");
    }
}
