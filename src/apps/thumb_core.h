/*
 * thumb_core.h - The bookkeeping behind image thumbnails (no file IO).
 *
 * The File Manager's Icons view wants a small picture of every image in the
 * folder. Two things make that dangerous on the target hardware: decoding a
 * hundred bitmaps inside one repaint, and holding a hundred decoded bitmaps in
 * memory. So this cache is bounded on both axes -- a fixed number of slots
 * with least-recently-used eviction, and a per-frame LOAD BUDGET, after which
 * the rest are reported as pending and the view simply repaints next frame.
 *
 * The decode itself is a callback the caller supplies, which is what keeps
 * this file free of platform code and lets the host tests drive every path --
 * eviction, the budget, and the negative caching of files that turn out not to
 * be images (tests/test_thumb.c).
 */
#ifndef CASTALIA_THUMB_CORE_H
#define CASTALIA_THUMB_CORE_H

#include "castalia/ctypes.h"
#include "castalia/gfx.h"

#define TC_SLOTS 16          /* thumbnails kept; at 32x32 that is ~64 KB    */

/* Decode 'path' and scale it to at most 'size' px. Return NULL when the file
 * is not an image -- the cache remembers that and never asks again. */
typedef GfxSurface *(*TcLoad)(const char *path, int size, void *user);

typedef struct {
    char        path[TC_SLOTS][CASTALIA_MAX_PATH];
    GfxSurface *img[TC_SLOTS];
    int         age[TC_SLOTS];    /* 0 = free slot, else last-used stamp    */
    cbool       bad[TC_SLOTS];    /* tried, and it was not an image         */
    int         clock;
    int         budget;           /* decodes still allowed this frame       */
    int         pending;          /* asked for but not decoded yet          */
} ThumbCache;

void tc_init(ThumbCache *c);
void tc_free(ThumbCache *c);

/* Start a repaint: 'budget' is how many files may be decoded this frame.
 * Keep it small (2-3) so scrolling into a folder full of images costs a few
 * frames instead of one long stall. */
void tc_begin_frame(ThumbCache *c, int budget);

/* The thumbnail for 'path', or NULL when it is not an image or has not been
 * decoded yet. When it returns NULL because the budget ran out, tc_pending()
 * is non-zero and the caller should ask for another frame. */
const GfxSurface *tc_get(ThumbCache *c, const char *path, int size,
                         TcLoad load, void *user);

int tc_pending(const ThumbCache *c);
int tc_count(const ThumbCache *c);

#endif /* CASTALIA_THUMB_CORE_H */
