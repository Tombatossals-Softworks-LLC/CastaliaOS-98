/*
 * paint_core.h - Raster editing for Paint (no wm, no platform, no input).
 *
 * Everything Paint does TO an image lives here so the host tests can drive it
 * on a plain off-screen GfxSurface and check the pixels -- the same split the
 * other apps use (sheet_core, write_core, agenda_core).
 *
 * The rules the whole file obeys:
 *   - every function clips to the surface, so no coordinate can walk off it;
 *   - nothing allocates except the undo stack, which says so in its name;
 *   - the airbrush takes its randomness from a caller-owned seed, so a spray
 *     is reproducible and can be asserted on.
 */
#ifndef CASTALIA_PAINT_CORE_H
#define CASTALIA_PAINT_CORE_H

#include "castalia/ctypes.h"
#include "castalia/rect.h"
#include "castalia/gfx.h"

/* ---- brushes and strokes ---------------------------------------------- */
/* One dab of a round brush 'size' px across (1 = a single pixel). */
void pc_dab(GfxSurface *cv, int x, int y, int size, CColor col);

/* A stroke from (x0,y0) to (x1,y1) laid down with that brush -- this is what
 * the pencil, the brush and the eraser all draw with. */
void pc_stroke(GfxSurface *cv, int x0, int y0, int x1, int y1,
               int size, CColor col);

/* One puff of the airbrush: 'density' pixels scattered inside 'radius'.
 * '*seed' is advanced, so the same seed always gives the same spray. */
void pc_spray(GfxSurface *cv, int x, int y, int radius, int density,
              CColor col, cu32 *seed);

/* ---- shapes ------------------------------------------------------------ */
/* Shape styles, matching the era's three-way options box. */
typedef enum {
    PC_OUTLINE = 0,   /* outline in the foreground color                  */
    PC_FILLED,        /* outline in the foreground, interior in 'fill'    */
    PC_SOLID          /* no outline, interior in 'fill'                   */
} PcStyle;

void pc_rect(GfxSurface *cv, const CRect *r, int width,
             CColor line, CColor fill, PcStyle style);
/* Midpoint ellipse inscribed in 'r' (integer only, no trig, no floats). */
void pc_ellipse(GfxSurface *cv, const CRect *r, int width,
                CColor line, CColor fill, PcStyle style);

/* ---- fill and pick ----------------------------------------------------- */
/* Bounded scanline flood fill from (x,y). Returns the pixels painted, which
 * is 0 when the point is outside or already the target color. */
int  pc_flood(GfxSurface *cv, int x, int y, CColor col);
/* The color under (x,y), or 'fallback' when the point is off the canvas. */
CColor pc_pick(const GfxSurface *cv, int x, int y, CColor fallback);

/* ---- regions: the selection, the clipboard, and resizing --------------- */
/* Lift a copy of 'r' out of the canvas into a new surface the caller owns
 * (gfx_surface_free). Clips to the canvas; NULL when the rect misses it. */
GfxSurface *pc_copy_region(const GfxSurface *cv, const CRect *r);
/* Paint 'src' into the canvas with its top-left at (x,y), clipped. */
void pc_paste(GfxSurface *cv, const GfxSurface *src, int x, int y);
/* Fill a region with 'col' -- what Cut leaves behind. */
void pc_clear_region(GfxSurface *cv, const CRect *r, CColor col);
/* Nearest-neighbour resample into a new surface of w x h. Nearest neighbour
 * on purpose: it keeps hard pixel edges, which is what an image drawn one
 * pixel at a time should look like when it is scaled. */
GfxSurface *pc_scale(const GfxSurface *src, int w, int h);

/* ---- whole-image operations (the Image menu) -------------------------- */
void pc_invert(GfxSurface *cv);
void pc_grayscale(GfxSurface *cv);
void pc_flip_h(GfxSurface *cv);
void pc_flip_v(GfxSurface *cv);

/* ---- undo -------------------------------------------------------------- */
/*
 * A small ring of full-canvas snapshots. Snapshots are allocated on first use
 * and reused after that, so a stroke never allocates twice; if a snapshot
 * cannot be allocated the editor simply has no undo left rather than failing
 * the edit. Redo is the mirror image and shares the same ring.
 */
#define PC_UNDO_LEVELS 4

typedef struct {
    GfxSurface *slot[PC_UNDO_LEVELS];
    int         head;     /* where the next snapshot goes                 */
    int         depth;    /* how many undos are available                 */
    int         redo;     /* how many redos are available                 */
} PcUndo;

void  pc_undo_init(PcUndo *u);
void  pc_undo_free(PcUndo *u);
/* Snapshot 'cv' before an edit. Drops the oldest snapshot when full and
 * discards any pending redo. Returns CFALSE when it could not snapshot. */
cbool pc_undo_push(PcUndo *u, const GfxSurface *cv);
/* Step back / forward one snapshot, swapping the canvas contents in. Returns
 * CFALSE when there is nothing to step to. */
cbool pc_undo_undo(PcUndo *u, GfxSurface *cv);
cbool pc_undo_redo(PcUndo *u, GfxSurface *cv);
int   pc_undo_depth(const PcUndo *u);
int   pc_undo_redo_depth(const PcUndo *u);

#endif /* CASTALIA_PAINT_CORE_H */
