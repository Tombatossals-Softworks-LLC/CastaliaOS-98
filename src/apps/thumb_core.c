/*
 * thumb_core.c - Bounded thumbnail cache (pure, host-tested).
 *
 * See thumb_core.h. Every path here is O(TC_SLOTS) with no allocation of its
 * own: the only memory is the surfaces the loader hands back, and there are
 * never more of those than there are slots.
 */
#include "thumb_core.h"
#include "castalia/sys.h"

void tc_init(ThumbCache *c)
{
    int i;
    if (c == NULL) { return; }
    for (i = 0; i < TC_SLOTS; i++) {
        c->path[i][0] = '\0';
        c->img[i] = NULL;
        c->age[i] = 0;
        c->bad[i] = CFALSE;
    }
    c->clock = 0;
    c->budget = 0;
    c->pending = 0;
}

void tc_free(ThumbCache *c)
{
    int i;
    if (c == NULL) { return; }
    for (i = 0; i < TC_SLOTS; i++) {
        if (c->img[i] != NULL) { gfx_surface_free(c->img[i]); }
    }
    tc_init(c);
}

void tc_begin_frame(ThumbCache *c, int budget)
{
    if (c == NULL) { return; }
    c->budget = (budget < 0) ? 0 : budget;
    c->pending = 0;
}

static int tc_find(const ThumbCache *c, const char *path)
{
    int i;
    for (i = 0; i < TC_SLOTS; i++) {
        if (c->age[i] != 0 && sys_stricmp(c->path[i], path) == 0) { return i; }
    }
    return -1;
}

/* A free slot, or the least recently used one (whose picture we drop). */
static int tc_slot_for(ThumbCache *c)
{
    int i, oldest = 0;
    for (i = 0; i < TC_SLOTS; i++) {
        if (c->age[i] == 0) { return i; }
        if (c->age[i] < c->age[oldest]) { oldest = i; }
    }
    if (c->img[oldest] != NULL) {
        gfx_surface_free(c->img[oldest]);
        c->img[oldest] = NULL;
    }
    return oldest;
}

const GfxSurface *tc_get(ThumbCache *c, const char *path, int size,
                         TcLoad load, void *user)
{
    int at;
    GfxSurface *img;
    if (c == NULL || path == NULL || path[0] == '\0') { return NULL; }

    at = tc_find(c, path);
    if (at >= 0) {
        c->clock++;
        c->age[at] = c->clock;
        return c->bad[at] ? NULL : c->img[at];   /* a hit, good or bad */
    }
    if (load == NULL) { return NULL; }
    if (c->budget <= 0) {
        /* Out of decoding time for this frame: say so, and let the caller
         * come back next frame rather than stalling this one. */
        c->pending++;
        return NULL;
    }
    c->budget--;
    img = load(path, size, user);

    at = tc_slot_for(c);
    sys_strlcpy(c->path[at], path, (cu32)CASTALIA_MAX_PATH);
    c->img[at] = img;
    c->bad[at] = (img == NULL) ? CTRUE : CFALSE;
    c->clock++;
    c->age[at] = c->clock;
    return img;
}

int tc_pending(const ThumbCache *c) { return (c == NULL) ? 0 : c->pending; }

int tc_count(const ThumbCache *c)
{
    int i, n = 0;
    if (c == NULL) { return 0; }
    for (i = 0; i < TC_SLOTS; i++) {
        if (c->age[i] != 0) { n++; }
    }
    return n;
}
