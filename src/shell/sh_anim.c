/*
 * sh_anim.c - Lightweight, frame-budgeted UI animations for the shell.
 *
 * The only animation kind today is the window open/close "zoom": a bright
 * rectangle outline that grows from a small seed at the window's center out to
 * its full frame when it opens, and shrinks back when it closes -- the classic
 * Win9x/XP flourish. It is deliberately an OVERLAY (a marching outline drawn
 * over the composited scene) rather than a scaled copy of the window, so it
 * costs only a few frame_rect draws and fits the dirty-rectangle compositor:
 * each frame the old and new outline bounds are added to the repaint region so
 * the layers underneath erase the previous outline, and the new one is drawn on
 * top just below the cursor.
 *
 * Animations are short (a handful of frames), never block, and are skipped in
 * safe mode or when the user disables them ([Shell] Animations = 0). With the
 * interactive shell loop running continuously they advance on their own; the
 * headless demo drives them a frame at a time for verification.
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

#define SH_ANIM_MAX   6    /* concurrent window animations                    */
#define SH_ANIM_STEPS 6    /* frames from seed to full frame (or back)        */
#define SH_ANIM_SEED  46   /* seed size as /256 of the target (~18%)          */

typedef struct {
    cbool       active;
    int         step;      /* 0..SH_ANIM_STEPS, then one terminal erase frame */
    CRect       from;      /* outline at step 0                               */
    CRect       to;        /* outline at the final step                       */
    const char *tag;       /* label for the debug log                         */
    CRect       prev;      /* last outline drawn, to invalidate next frame    */
    cbool       have_prev;
} ShAnim;

static ShAnim g_anim[SH_ANIM_MAX];
static cbool  g_anim_enabled = CTRUE;
static cbool  g_anim_debug   = CFALSE;   /* log each drawn outline (host demo) */

void sh_anim_set_enabled(cbool en) { g_anim_enabled = en; }
void sh_anim_set_debug(cbool on)   { g_anim_debug = on; }

/* The outline for a step: linearly interpolate every corner from 'from' to
 * 'to'. This handles the centered open/close zoom AND the off-center
 * minimize/restore zoom toward a taskbar button with the same code. */
static CRect anim_rect(const ShAnim *a, int step)
{
    int t = (step * 256) / SH_ANIM_STEPS;   /* 0..256 */
    CRect r;
    r.x0 = a->from.x0 + (a->to.x0 - a->from.x0) * t / 256;
    r.y0 = a->from.y0 + (a->to.y0 - a->from.y0) * t / 256;
    r.x1 = a->from.x1 + (a->to.x1 - a->from.x1) * t / 256;
    r.y1 = a->from.y1 + (a->to.y1 - a->from.y1) * t / 256;
    return r;
}

/* Start an outline animation from rect 'from' to rect 'to'. */
static void anim_start(const CRect *from, const CRect *to, const char *tag)
{
    int i;
    if (!g_anim_enabled || g_sh.safe_mode || from == NULL || to == NULL) { return; }
    for (i = 0; i < SH_ANIM_MAX; i++) {
        if (!g_anim[i].active) {
            g_anim[i].active    = CTRUE;
            g_anim[i].step      = 0;
            g_anim[i].from      = *from;
            g_anim[i].to        = *to;
            g_anim[i].tag       = tag;
            g_anim[i].have_prev = CFALSE;
            return;
        }
    }
}

/* A small box centered on 'frame' (~SH_ANIM_SEED/256 of its size), the seed the
 * open/close zoom grows from / shrinks to. */
static CRect centered_seed(const CRect *frame)
{
    int cx = (frame->x0 + frame->x1) / 2;
    int cy = (frame->y0 + frame->y1) / 2;
    int w = crect_w(frame) * SH_ANIM_SEED / 256;
    int h = crect_h(frame) * SH_ANIM_SEED / 256;
    return crect_make(cx - w / 2, cy - h / 2, w, h);
}

void sh_anim_window(const CRect *frame, cbool opening)
{
    CRect seed;
    if (frame == NULL || crect_w(frame) < 16 || crect_h(frame) < 16) { return; }
    seed = centered_seed(frame);
    if (opening) { anim_start(&seed, frame, "open"); }
    else         { anim_start(frame, &seed, "close"); }
}

void sh_anim_zoom(const CRect *from, const CRect *to, cbool restoring)
{
    if (from == NULL || to == NULL) { return; }
    anim_start(from, to, restoring ? "restore" : "minimize");
}

cbool sh_anim_active(void)
{
    int i;
    for (i = 0; i < SH_ANIM_MAX; i++) { if (g_anim[i].active) { return CTRUE; } }
    return CFALSE;
}

/*
 * The outline the first running animation is drawing right now, for the
 * headless driver.
 *
 * --anim-demo used to turn on the debug log and leave a person to read the
 * numbers, which is not a check: an animation that had stopped animating, or
 * one that ran backwards, printed different numbers and still reported
 * success. A scene can ask for the rectangle and watch it grow.
 */
cbool sh_anim_bounds(CRect *out)
{
    int i;
    for (i = 0; i < SH_ANIM_MAX; i++) {
        ShAnim *a = &g_anim[i];
        if (!a->active || a->step > SH_ANIM_STEPS) { continue; }
        if (out != NULL) { *out = anim_rect(a, a->step); }
        return CTRUE;
    }
    return CFALSE;
}

/* Add each animation's old + current outline bounds to the frame region so the
 * scene underneath repaints (erasing last frame's outline) before we redraw. */
void sh_anim_collect(CRegion *region)
{
    int i;
    for (i = 0; i < SH_ANIM_MAX; i++) {
        ShAnim *a = &g_anim[i];
        if (!a->active) { continue; }
        if (a->have_prev) { cregion_add(region, &a->prev); }
        if (a->step <= SH_ANIM_STEPS) {
            CRect cur = anim_rect(a, a->step);
            cregion_add(region, &cur);
        }
    }
}

/* Draw the current outline for each active animation (called after the windows
 * and taskbar, before the cursor), then advance. The terminal step draws
 * nothing -- its only job is to let the region added by sh_anim_collect erase
 * the final outline -- and then retires the animation. */
void sh_anim_draw(GfxSurface *s)
{
    const ShTheme *th = &g_sh.theme;
    CColor outer = gfx_tint(th->ui.accent, 0xFFFFFF, 150);
    CColor inner = gfx_tint(th->ui.accent, 0xFFFFFF, 40);
    int i;
    gfx_reset_clip(s);
    for (i = 0; i < SH_ANIM_MAX; i++) {
        ShAnim *a = &g_anim[i];
        if (!a->active) { continue; }
        if (a->step > SH_ANIM_STEPS) { a->active = CFALSE; continue; }
        {
            CRect cur = anim_rect(a, a->step);
            if (g_anim_debug) {
                SYS_LOGI("anim", "zoom %s step %d rect %d,%d %dx%d",
                         (a->tag ? a->tag : "?"), a->step,
                         cur.x0, cur.y0, crect_w(&cur), crect_h(&cur));
            }
            gfx_frame_rect(s, &cur, outer);
            if (crect_w(&cur) > 4 && crect_h(&cur) > 4) {
                CRect in = crect_make(cur.x0 + 1, cur.y0 + 1,
                                      crect_w(&cur) - 2, crect_h(&cur) - 2);
                gfx_frame_rect(s, &in, inner);
            }
            a->prev = cur;
            a->have_prev = CTRUE;
        }
        a->step++;
    }
}

void sh_anim_clear(void)
{
    int i;
    for (i = 0; i < SH_ANIM_MAX; i++) { g_anim[i].active = CFALSE; }
}
