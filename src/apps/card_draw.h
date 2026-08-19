/*
 * card_draw.h - How a playing card is drawn, once.
 *
 * The pips, the faces and the card back lived inside app_solitaire.c as
 * statics. FreeCell needs exactly the same artwork, and the alternative to
 * this file was a second copy of it -- which is how the Media Player ended up
 * with a Play button pointing the wrong way: the same drawing written out
 * more than once, differing where nobody looked.
 *
 * There is real craft in here worth keeping in one place. The corner pips are
 * drawn pixel by pixel rather than scaled down from the central ones, because
 * a scaled spade at corner size reads as a blob -- see the note in the .c.
 */
#ifndef CASTALIA_CARD_DRAW_H
#define CASTALIA_CARD_DRAW_H

#include "castalia/gfx.h"
#include "sol_core.h"

/* "A", "2".."10", "J", "Q", "K" -- and "" for a rank that is not a card. */
const char *card_rank_str(int rank);

/* One suit pip of radius 'r', centred. */
void card_pip(GfxSurface *s, int suit, int cx, int cy, int r, CColor col);
/* The small corner variety, which is separate art rather than a small pip. */
void card_pip_small(GfxSurface *s, int suit, int cx, int cy, CColor col);

/* A face-up card filling 'r', with its accent frame when 'selected'. */
void card_draw_face(GfxSurface *s, const CRect *r, const Card *c,
                    cbool selected);
/* The patterned back. */
void card_draw_back(GfxSurface *s, const CRect *r);

#endif /* CASTALIA_CARD_DRAW_H */
