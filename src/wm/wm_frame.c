/*
 * wm_frame.c - Non-client frame geometry and painting.
 *
 * Draws the classic beveled window: a 3D raised outer border, a gradient
 * title bar (active vs inactive) with caption buttons, and the client fill.
 * Client content is produced by the window's proc via WM_MSG_PAINT.
 */
#include "wm_internal.h"
#include "castalia/sys.h"

static int border_w(void) { return (g_wm.theme.border_width > 0) ? g_wm.theme.border_width : 2; }
static int title_h(void)  { return (g_wm.theme.title_height > 0) ? g_wm.theme.title_height : 18; }

CRect wm_frame_titlebar(const struct WmWindow *w)
{
    int bw = border_w();
    CRect r;
    r.x0 = w->frame.x0 + bw;
    r.y0 = w->frame.y0 + bw;
    r.x1 = w->frame.x1 - bw;
    r.y1 = w->frame.y0 + bw + title_h();
    return r;
}

/* Caption buttons laid out right-to-left: close, then max, then min. */
static CRect nth_caption_btn(const struct WmWindow *w, int slot)
{
    CRect tb = wm_frame_titlebar(w);
    int bs = title_h() - 4;
    int y0 = tb.y0 + 2;
    int x1 = tb.x1 - 2 - slot * (bs + 2);
    CRect r;
    if (bs < 6) { bs = 6; }
    r.x1 = x1;
    r.x0 = x1 - bs;
    r.y0 = y0;
    r.y1 = y0 + bs;
    return r;
}

/*
 * Which slot a button sits in, counting from the right over the buttons this
 * window ACTUALLY HAS -- not over the three it might have had.
 *
 * The slots used to be fixed: close 0, maximize 1, minimize 2. A window with
 * no maximize box therefore left slot 1 empty and put its minimize box a whole
 * button-width away from the close box, with a hole between them. The hole is
 * exactly where a lifetime of muscle memory says "maximize" is, and clicking
 * it drags the window instead.
 *
 * Nobody saw it while the fixed-size windows had only a close box, because a
 * gap needs two buttons to be between.
 */
static int caption_slot(const struct WmWindow *w, unsigned which)
{
    int slot = 0;
    if (which == WM_STYLE_CLOSE) { return 0; }
    if (w->style & WM_STYLE_CLOSE) { slot++; }
    if (which == WM_STYLE_MAXIMIZE) { return slot; }
    if (w->style & WM_STYLE_MAXIMIZE) { slot++; }
    return slot;                       /* minimize, the leftmost of the three */
}

CRect wm_frame_close_btn(const struct WmWindow *w)
{
    if (!(w->style & WM_STYLE_CLOSE)) { return crect_make(0, 0, 0, 0); }
    return nth_caption_btn(w, caption_slot(w, WM_STYLE_CLOSE));
}
CRect wm_frame_max_btn(const struct WmWindow *w)
{
    if (!(w->style & WM_STYLE_MAXIMIZE)) { return crect_make(0, 0, 0, 0); }
    return nth_caption_btn(w, caption_slot(w, WM_STYLE_MAXIMIZE));
}
CRect wm_frame_min_btn(const struct WmWindow *w)
{
    if (!(w->style & WM_STYLE_MINIMIZE)) { return crect_make(0, 0, 0, 0); }
    return nth_caption_btn(w, caption_slot(w, WM_STYLE_MINIMIZE));
}

/* The shell asks this before raising the window menu on a right-click, so it
 * never has to know the border width or the title height. The caption buttons
 * are excluded: they are already a click target, and a menu appearing over the
 * close box would be a menu in the way of the thing you were aiming at. */
cbool wm_titlebar_hit(WmWindow *win, int x, int y)
{
    CRect tb, b;
    if (win == NULL || !(win->style & WM_STYLE_TITLE)) { return CFALSE; }
    tb = wm_frame_titlebar(win);
    if (!crect_contains(&tb, x, y)) { return CFALSE; }
    b = wm_frame_close_btn(win);
    if (crect_contains(&b, x, y)) { return CFALSE; }
    b = wm_frame_max_btn(win);
    if (crect_contains(&b, x, y)) { return CFALSE; }
    b = wm_frame_min_btn(win);
    if (crect_contains(&b, x, y)) { return CFALSE; }
    return CTRUE;
}

static void draw_caption_button(GfxSurface *s, const CRect *r, char glyph)
{
    const WmTheme *t = &g_wm.theme;
    int cx, cy;
    if (crect_empty(r)) { return; }
    /* XP signature: a red, glossy close box; the others take a light gloss. */
    if (t->glossy && glyph == 'X') {
        CColor rtop = GFX_RGB(0xE8, 0x6A, 0x50), rmid = GFX_RGB(0xD8, 0x3A, 0x22);
        CColor rbot = GFX_RGB(0xA8, 0x20, 0x14);
        gfx_bevel(s, r, GFX_BEVEL_RAISED_THIN, gfx_tint(rtop, 0xFFFFFF, 90),
                  rbot, GFX_NO_FILL);
        {
            CRect in = crect_make(r->x0 + 1, r->y0 + 1, crect_w(r) - 2, crect_h(r) - 2);
            gfx_vgradient3(s, &in, rtop, rmid, rbot, 340);
        }
        gfx_line(s, r->x0 + 3, r->y0 + 3, r->x1 - 4, r->y1 - 4, GFX_RGB(0xFF,0xFF,0xFF));
        gfx_line(s, r->x1 - 4, r->y0 + 3, r->x0 + 3, r->y1 - 4, GFX_RGB(0xFF,0xFF,0xFF));
        return;
    }
    if (t->glossy) {
        gfx_bevel(s, r, GFX_BEVEL_RAISED_THIN, t->light, t->dark, GFX_NO_FILL);
        {
            CRect in = crect_make(r->x0 + 1, r->y0 + 1, crect_w(r) - 2, crect_h(r) - 2);
            gfx_vgradient3(s, &in, gfx_tint(t->face, 0xFFFFFF, 140), t->face,
                           gfx_tint(t->face, t->dark, 90), 320);
        }
    } else {
        gfx_bevel(s, r, GFX_BEVEL_RAISED, t->light, t->dark, t->face);
    }
    cx = (r->x0 + r->x1) / 2;
    cy = (r->y0 + r->y1) / 2;
    switch (glyph) {
    case 'X': /* close: an X */
        gfx_line(s, r->x0 + 3, r->y0 + 3, r->x1 - 4, r->y1 - 4, t->darker);
        gfx_line(s, r->x1 - 4, r->y0 + 3, r->x0 + 3, r->y1 - 4, t->darker);
        break;
    case '_': /* minimize: a bottom bar */
        gfx_hline(s, r->x0 + 3, r->y1 - 4, crect_w(r) - 6, t->darker);
        gfx_hline(s, r->x0 + 3, r->y1 - 5, crect_w(r) - 6, t->darker);
        break;
    case 'O': /* maximize: a hollow box */
    default:
        {
            CRect b = crect_make(r->x0 + 3, r->y0 + 3, crect_w(r) - 6, crect_h(r) - 6);
            gfx_frame_rect(s, &b, t->darker);
            gfx_hline(s, b.x0, b.y0 + 1, crect_w(&b), t->darker);
        }
        break;
    }
    CASTALIA_UNUSED(cx); CASTALIA_UNUSED(cy);
}

void wm_draw_window(struct WmWindow *w, const CRect *clip)
{
    GfxSurface *s = g_wm.back;
    const WmTheme *t = &g_wm.theme;
    CRect bounds = w->frame;
    CRect drawclip;
    cbool focused = (g_wm.focus == w) ? CTRUE : CFALSE;

    /* Include the shadow strip in the draw clip so a repaint that touches only
     * the shadow area still redraws it. */
    if (t->glossy) { bounds.x1 += WM_SHADOW; bounds.y1 += WM_SHADOW; }
    drawclip = crect_intersect(clip, &bounds);
    if (crect_empty(&drawclip)) { return; }
    gfx_set_clip(s, &drawclip);

    /* Soft drop shadow under the window (glossy chrome only). Drawn first so
     * the frame body paints over its inner edge. */
    if (t->glossy) { gfx_drop_shadow(s, &w->frame, WM_SHADOW, 96); }

    if (w->style & WM_STYLE_POPUP) {
        /* Frameless popup (menus): the proc paints everything. */
        wm_send(w, WM_MSG_PAINT, 0, 0, s);
        gfx_reset_clip(s);
        return;
    }

    /* Body fill + raised 3D outer border. */
    gfx_fill_rect(s, &w->frame, t->face);
    gfx_bevel(s, &w->frame, GFX_BEVEL_RAISED, t->light, t->dark, GFX_NO_FILL);

    /* Title bar. */
    if (w->style & WM_STYLE_TITLE) {
        CRect tb = wm_frame_titlebar(w);
        CColor cl = focused ? t->title_active_l : t->title_inactive_l;
        CColor cr = focused ? t->title_active_r : t->title_inactive_r;
        if (t->glossy) {
            /* XP sheen: a bright highlight band in the upper third over the
             * base gradient, plus a 1px specular line along the very top. */
            gfx_vgradient3(s, &tb, cl, gfx_tint(cl, 0xFFFFFF, 120), cr, 300);
            gfx_hline(s, tb.x0, tb.y0, crect_w(&tb), gfx_tint(cl, 0xFFFFFF, 175));
        } else {
            gfx_vgradient(s, &tb, cl, cr);
        }
        {
            CRect txt = tb;
            CRect cb, mb, nb;
            txt.x0 += 4;
            /* App icon at the left of the title bar (classic Win9x). */
            if (w->icon != NULL) {
                const GfxSurface *ic = w->icon;
                int cap = title_h() - 4;
                int iw = (ic->w < cap) ? ic->w : cap;
                int ih = (ic->h < cap) ? ic->h : cap;
                CRect src = crect_make(0, 0, iw, ih);
                gfx_blit(s, tb.x0 + 4, tb.y0 + (crect_h(&tb) - ih) / 2,
                         ic, &src, GFX_BLIT_KEYED);
                txt.x0 += iw + 3;
            }
            cb = wm_frame_close_btn(w);
            mb = wm_frame_max_btn(w);
            nb = wm_frame_min_btn(w);
            {
                /*
                 * Fitted to the space BEFORE the caption buttons, which is why
                 * their rectangles are computed first now. The frame's clip
                 * stops the title escaping the window, but nothing stopped it
                 * running under the minimise/maximise/close boxes -- it simply
                 * vanished beneath them, since they are painted afterwards.
                 * Win9x truncates the title instead, and so does this.
                 */
                char tt[96];
                int room = ((w->style & WM_STYLE_CLOSE) ? nb.x0 : txt.x1)
                           - txt.x0 - 6;
                gfx_text_fit(tt, sizeof tt, w->title, room, GFX_FONT_BOLD);
                gfx_draw_text_rect(s, GFX_FONT_BOLD, &txt, tt,
                                   t->title_text,
                                   GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
            }
            draw_caption_button(s, &cb, 'X');
            draw_caption_button(s, &mb, 'O');
            draw_caption_button(s, &nb, '_');
        }
    }

    /* Client: separate the client well with a thin sunken edge, then paint. */
    {
        CRect client = wm_client_rect(w);
        CRect cclip = crect_intersect(&drawclip, &client);
        gfx_fill_rect(s, &client, t->face);
        if (!crect_empty(&cclip)) {
            /* Narrowed, not replaced: cclip is already the intersection with
             * drawclip, but going through the narrowing call keeps the one
             * rule -- inside a window's paint, the clip only ever shrinks. */
            (void)gfx_clip_narrow(s, &cclip);
            wm_send(w, WM_MSG_PAINT, 0, 0, s);
        }
    }
    gfx_reset_clip(s);
}
