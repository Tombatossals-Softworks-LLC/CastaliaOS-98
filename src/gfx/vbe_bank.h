/*
 * vbe_bank.h - Walking a banked VESA framebuffer.
 *
 * A card without a linear framebuffer shows the screen through one small
 * window -- typically 64 KB -- that has to be re-pointed ("banked") as the
 * write position moves past it. Turning an absolute byte offset into a bank
 * number and an offset within that window is a divide and a multiply.
 *
 * The presenter did both PER PIXEL. On an 800x600 screen that is 480,000
 * divides and 480,000 multiplies for a single full-screen present, every
 * frame -- on the fallback path, which is exactly the path taken by the older
 * and slower cards that can least afford it. A divide is twenty to forty
 * cycles on a 486.
 *
 * It does not need to be computed at all after the first pixel of a row: the
 * write position advances by a fixed number of bytes each step, so the bank
 * and the window offset can be carried forward with an add and a compare.
 *
 * This is that carry, as a pure walker, so it can be host-tested against the
 * divide it replaces. The DOS backend keeps only what has to happen there --
 * the BIOS call that actually re-points the window. Same split as vbe_pick.c:
 * the hardware poking stays in src/platform/dos, the arithmetic that has to
 * be RIGHT lives where a test can reach it.
 */
#ifndef CASTALIA_VBE_BANK_H
#define CASTALIA_VBE_BANK_H

#include "castalia/ctypes.h"

typedef struct {
    long gran;    /* bytes per bank step (the window's granularity)      */
    long bank;    /* which bank the window is pointed at                 */
    long local;   /* offset within the window: always in [0, gran)       */
} VbeBank;

/*
 * Point the walker at an absolute byte offset. One divide, once per row.
 *
 * A granularity of zero or less is treated as 1 rather than dividing by it.
 * The VBE convention that a reported granularity of 0 means 64 KB belongs to
 * the caller reading the mode info, not here -- it is already applied in
 * vesa.c, and applying it twice in two places is how the two drift apart.
 */
void vbe_bank_seek(VbeBank *w, long gran, long abs_off);

/*
 * Advance by 'bytes', re-banking as needed. No divide, no multiply.
 *
 * Stepping backwards is allowed (a negative 'bytes'), because a caller that
 * rewinds within a row should get the same answer as one that seeks.
 */
void vbe_bank_step(VbeBank *w, long bytes);

#endif /* CASTALIA_VBE_BANK_H */
