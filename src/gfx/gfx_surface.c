/*
 * gfx_surface.c - Surface allocation, wrapping, and clip management.
 */
#include "castalia/gfx.h"
#include "castalia/sys.h"

static void set_full_clip(GfxSurface *s)
{
    s->clip = crect_make(0, 0, s->w, s->h);
}

GfxSurface *gfx_surface_new(int w, int h)
{
    GfxSurface *s;
    cu32 bytes;
    if (w <= 0 || h <= 0) { return NULL; }
    s = (GfxSurface *)sys_calloc(1, (cu32)sizeof(GfxSurface));
    if (s == NULL) { return NULL; }
    bytes = (cu32)w * (cu32)h * (cu32)sizeof(CColor);
    s->pixels = (CColor *)sys_alloc(bytes);
    if (s->pixels == NULL) {
        sys_free(s, (cu32)sizeof(GfxSurface));
        return NULL;
    }
    s->w = w;
    s->h = h;
    s->pitch = w;
    s->owns_pixels = CTRUE;
    set_full_clip(s);
    return s;
}

void gfx_surface_wrap(GfxSurface *s, CColor *pixels, int w, int h, int pitch)
{
    s->w = w;
    s->h = h;
    s->pitch = (pitch > 0) ? pitch : w;
    s->pixels = pixels;
    s->owns_pixels = CFALSE;
    set_full_clip(s);
}

void gfx_surface_free(GfxSurface *s)
{
    if (s == NULL) { return; }
    if (s->owns_pixels && s->pixels != NULL) {
        sys_free(s->pixels, (cu32)s->w * (cu32)s->h * (cu32)sizeof(CColor));
    }
    /* Only free the struct if it was heap-allocated by gfx_surface_new.
     * Wrapped surfaces are typically stack/embedded; callers of _wrap do not
     * call _free on the struct. We detect ownership via owns_pixels. */
    if (s->owns_pixels) {
        sys_free(s, (cu32)sizeof(GfxSurface));
    } else {
        s->pixels = NULL;
    }
}

void gfx_set_clip(GfxSurface *s, const CRect *r)
{
    CRect full = crect_make(0, 0, s->w, s->h);
    s->clip = crect_intersect(&full, r);
}

CRect gfx_clip_narrow(GfxSurface *s, const CRect *r)
{
    CRect was = s->clip;
    s->clip = crect_intersect(&was, r);
    return was;
}

void gfx_reset_clip(GfxSurface *s)
{
    set_full_clip(s);
}

CRect gfx_get_clip(const GfxSurface *s)
{
    return s->clip;
}

void gfx_scale_map(int *cols, int out_w, int src_w)
{
    int i;
    if (cols == NULL || out_w < 1) { return; }
    if (src_w < 1) {
        for (i = 0; i < out_w; i++) { cols[i] = 0; }
        return;
    }
    for (i = 0; i < out_w; i++) {
        cols[i] = (int)(((long)i * src_w) / out_w);
    }
}
