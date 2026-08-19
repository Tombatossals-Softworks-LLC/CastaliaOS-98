/*
 * vbe_bank.c - Walking a banked VESA framebuffer (see vbe_bank.h).
 */
#include "vbe_bank.h"

void vbe_bank_seek(VbeBank *w, long gran, long abs_off)
{
    if (w == NULL) { return; }
    if (gran < 1) { gran = 1; }
    w->gran = gran;
    if (abs_off < 0) { abs_off = 0; }
    w->bank  = abs_off / gran;
    w->local = abs_off - w->bank * gran;
}

void vbe_bank_step(VbeBank *w, long bytes)
{
    if (w == NULL || w->gran < 1) { return; }
    w->local += bytes;
    /* A step is one or two bytes and a window is kilobytes, so these loops
     * run at most once in the presenter. They are loops rather than a single
     * compare so that a caller stepping by a whole scanline still lands in
     * the right place. */
    while (w->local >= w->gran) { w->local -= w->gran; w->bank++; }
    while (w->local < 0)        { w->local += w->gran; w->bank--; }
}
