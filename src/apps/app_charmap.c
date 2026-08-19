/*
 * app_charmap.c - CastaliaOS Character Map.
 *
 * A grid of the system font's glyphs (codes 32..255) laid out like the
 * Windows Character Map: highlight a glyph, pick glyphs into a "characters to
 * copy" sample string, and copy the sample to the system clipboard. The
 * highlighted cell shows its character with the decimal and hex code. Fully
 * keyboard driven -- the arrows move the highlight, a typed ASCII character
 * jumps the highlight to it, Enter selects (appends). Codes with no glyph in
 * the bitmap face render as the font's small box, which honestly shows what
 * the face actually contains.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/clip.h"
#include "cmap_core.h"

#define CM_COLS       16
#define CM_CELL_W     21
#define CM_CELL_H     16
#define CM_MARGIN     6
#define CM_SAMPLE_MAX 48

/*
 * The grid covers what the FONT has, asked at open time rather than assumed.
 *
 * It used to run 32..255, and 129 of those 224 cells drew the same hollow
 * box -- the glyph gfx_font.c renders for a code it has no art for, which is
 * the right thing for text and exactly the wrong thing for a window whose
 * entire job is to show what is available. Better than half the map was
 * offering characters this system cannot draw, as though it could.
 */
static CmapGrid g_cm;
#define g_cm_first (g_cm.first)
#define g_cm_last  (g_cm.last)
#define g_cm_rows  (g_cm.rows)

static void cm_sync_range(void)
{
    int f = 32, l = 126;
    gfx_font_range(GFX_FONT_SYSTEM, &f, &l);
    cmap_grid_init(&g_cm, f, l, CM_COLS);
}

enum { CM_BTN_SELECT = 0, CM_BTN_COPY, CM_BTN_CLEAR, CM_BTN_COUNT };
static const char *CM_BTN_LABEL[CM_BTN_COUNT] = { "Select", "Copy", "Clear" };

typedef struct {
    int  sel;                    /* highlighted code, g_cm_first..g_cm_last */
    char sample[CM_SAMPLE_MAX];  /* accumulated characters to copy      */
    char status[48];             /* bottom status line                  */
} CharMap;

typedef struct {
    CRect grid;                  /* sunken well holding the glyph cells */
    CRect info;                  /* highlighted-glyph info line         */
    CRect field;                 /* sunken white sample field           */
    CRect btn[CM_BTN_COUNT];
    CRect status;                /* status line                         */
    int   cell_w, cell_h;
} CmLayout;

/* ---- geometry -------------------------------------------------------- */
static void cm_layout(WmWindow *win, CmLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c);
    int inner = cw - CM_MARGIN * 2;
    int gw = CM_COLS * CM_CELL_W + 2;
    int gh = g_cm_rows * CM_CELL_H + 2;
    int bw = 66;
    int y, i, bx;

    L->cell_w = CM_CELL_W;
    L->cell_h = CM_CELL_H;
    L->grid = crect_make(CM_MARGIN, CM_MARGIN, gw, gh);

    y = L->grid.y1 + 5;
    L->info = crect_make(CM_MARGIN, y, inner, 10);

    y = L->info.y1 + 15;                 /* leave a line above for the label */
    L->field = crect_make(CM_MARGIN, y, inner, 20);

    y = L->field.y1 + 8;
    bx = CM_MARGIN;
    for (i = 0; i < CM_BTN_COUNT; i++) {
        L->btn[i] = crect_make(bx, y, bw, 22);
        bx += bw + 6;
    }

    y = L->btn[0].y1 + 8;
    L->status = crect_make(CM_MARGIN, y, inner, 12);
}

/* ---- sample buffer helpers ------------------------------------------- */
static void cm_append(CharMap *cm, int code)
{
    int n;
    if (code < g_cm_first || code > g_cm_last) { return; }
    n = (int)sys_strnlen(cm->sample, sizeof(cm->sample));
    if (n < CM_SAMPLE_MAX - 1) {
        cm->sample[n] = (char)code;
        cm->sample[n + 1] = '\0';
        sys_strlcpy(cm->status, "Ready", sizeof(cm->status));
    } else {
        sys_strlcpy(cm->status, "Sample is full", sizeof(cm->status));
    }
}

static void cm_backspace(CharMap *cm)
{
    int n = (int)sys_strnlen(cm->sample, sizeof(cm->sample));
    if (n > 0) { cm->sample[n - 1] = '\0'; }
    sys_strlcpy(cm->status, "Ready", sizeof(cm->status));
}

static void cm_copy(CharMap *cm)
{
    int n = (int)sys_strnlen(cm->sample, sizeof(cm->sample));
    if (n == 0) { return; }
    clip_set_text(cm->sample, -1);
    sys_snprintf(cm->status, sizeof(cm->status), "Copied %d character(s)", n);
}

static void cm_clear(CharMap *cm)
{
    cm->sample[0] = '\0';
    sys_strlcpy(cm->status, "Cleared", sizeof(cm->status));
}

/* ---- paint ----------------------------------------------------------- */
static void cm_paint(WmWindow *win, GfxSurface *s)
{
    CharMap *cm = (CharMap *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor grid_line = GFX_RGB(0xD4, 0xD8, 0xDC);
    CColor field_text = GFX_RGB(0x10, 0x10, 0x10);
    CmLayout L;
    CRect r, gr, saved;
    int gx, gy, i, row, col, code;
    char info[64];
    char g[2];

    if (cm == NULL) { return; }
    cm_layout(win, &L);
    g[1] = '\0';

    /* Glyph grid well. */
    gr = crect_offset(&L.grid, o.x, o.y);
    gfx_bevel(s, &gr, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    gx = gr.x0 + 1;
    gy = gr.y0 + 1;
    for (i = 1; i < CM_COLS; i++) {
        gfx_vline(s, gx + i * L.cell_w, gy, g_cm_rows * L.cell_h, grid_line);
    }
    for (i = 1; i < g_cm_rows; i++) {
        gfx_hline(s, gx, gy + i * L.cell_h, CM_COLS * L.cell_w, grid_line);
    }

    /* Highlighted cell background (drawn over the grid lines). A selection
     * off the map draws no highlight rather than a rectangle outside the
     * grid, which is what the old subtraction produced for a negative code. */
    if (cmap_cell_of(&g_cm, cm->sel, &row, &col)) {
        CRect hl = crect_make(gx + col * L.cell_w, gy + row * L.cell_h,
                              L.cell_w, L.cell_h);
        gfx_fill_rect(s, &hl, p->accent);
    }

    /* Glyphs, one 2-char string per cell. */
    for (row = 0; row < g_cm_rows; row++) {
        for (col = 0; col < CM_COLS; col++) {
            CRect cell;
            CColor tc;
            code = cmap_code_at(&g_cm, row, col);
            if (code < 0) { continue; }
            cell = crect_make(gx + col * L.cell_w, gy + row * L.cell_h,
                              L.cell_w, L.cell_h);
            tc = (code == cm->sel) ? p->accent_text : p->text;
            g[0] = (char)code;
            gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &cell, g, tc,
                               GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        }
    }

    /* Info line for the highlighted glyph. */
    r = crect_offset(&L.info, o.x, o.y);
    g[0] = (char)cm->sel;
    sys_snprintf(info, sizeof(info), "Char: %s   Code: %d (U+%04X)",
                 g, cm->sel, (unsigned)cm->sel);
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r, info, p->text,
                       GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);

    /* Sample field with its label. */
    gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + CM_MARGIN,
                  o.y + L.field.y0 - 11, "Characters to copy:", p->text);
    r = crect_offset(&L.field, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    {
        CRect ti = crect_inset(&r, 3);
        saved = gfx_clip_narrow(s, &ti);
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &ti, cm->sample, field_text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
        gfx_set_clip(s, &saved);
    }

    /* Buttons (Copy/Clear disabled while the sample is empty). */
    {
        cbool empty = (cm->sample[0] == '\0');
        for (i = 0; i < CM_BTN_COUNT; i++) {
            CRect br = crect_offset(&L.btn[i], o.x, o.y);
            UiButtonState st = UI_BTN_NORMAL;
            if ((i == CM_BTN_COPY || i == CM_BTN_CLEAR) && empty) {
                st = UI_BTN_DISABLED;
            }
            ui_draw_button(s, &br, CM_BTN_LABEL[i], st);
        }
    }

    /* Status line. */
    r = crect_offset(&L.status, o.x, o.y);
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r,
                       (cm->status[0] != '\0') ? cm->status : "Ready",
                       ui_text_dim(), GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
}

/* ---- input ----------------------------------------------------------- */
static cbool cm_click(WmWindow *win, CharMap *cm, int x, int y)
{
    CmLayout L;
    int gx, gy, i;
    cbool empty;

    if (cm == NULL) { return CFALSE; }
    cm_layout(win, &L);
    empty = (cm->sample[0] == '\0');

    gx = L.grid.x0 + 1;
    gy = L.grid.y0 + 1;
    if (x >= gx && x < gx + CM_COLS * L.cell_w &&
        y >= gy && y < gy + g_cm_rows * L.cell_h) {
        int code = cmap_hit(&g_cm, x - gx, y - gy, L.cell_w, L.cell_h);
        if (code >= 0) {
            /* Re-clicking the already-highlighted cell picks it (the
             * single-message equivalent of a double-click). */
            if (code == cm->sel) { cm_append(cm, code); }
            else { cm->sel = code; }
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    }

    for (i = 0; i < CM_BTN_COUNT; i++) {
        if (crect_contains(&L.btn[i], x, y)) {
            switch (i) {
            case CM_BTN_SELECT: cm_append(cm, cm->sel);       break;
            case CM_BTN_COPY:   if (!empty) { cm_copy(cm); }  break;
            case CM_BTN_CLEAR:  if (!empty) { cm_clear(cm); } break;
            default: break;
            }
            wm_invalidate(win, NULL);
            return CTRUE;
        }
    }
    return CFALSE;
}

static cbool cm_key(WmWindow *win, CharMap *cm, int key, int ch)
{
    if (cm == NULL) { return CFALSE; }
    switch (key) {
    case PLAT_KEY_LEFT:   cm->sel = cmap_move(&g_cm, cm->sel, -1,  0); break;
    case PLAT_KEY_RIGHT:  cm->sel = cmap_move(&g_cm, cm->sel,  1,  0); break;
    case PLAT_KEY_UP:     cm->sel = cmap_move(&g_cm, cm->sel,  0, -1); break;
    case PLAT_KEY_DOWN:   cm->sel = cmap_move(&g_cm, cm->sel,  0,  1); break;
    case PLAT_KEY_HOME:   cm->sel = cmap_home(&g_cm); break;
    case PLAT_KEY_END:    cm->sel = cmap_end(&g_cm);  break;
    case PLAT_KEY_ENTER:  cm_append(cm, cm->sel); wm_invalidate(win, NULL); return CTRUE;
    case PLAT_KEY_BACKSP: cm_backspace(cm);       wm_invalidate(win, NULL); return CTRUE;
    default: {
        /*
         * A typed character jumps the highlight to that code -- but only if
         * the FONT has it. This asked `ch >= 32 && ch <= 126` before, which
         * is the hard-coded 126 gfx_font_range()'s own comment warned about,
         * sitting one file away from the function written to prevent it. It
         * agrees with today's font exactly, so nothing misbehaved and no test
         * using the real font could have caught it; the day a wider face
         * arrives it would have made every character above 126 unreachable
         * by typing, and a narrower one would have highlighted a cell that
         * is not on the map. cmap_jump asks the grid instead.
         */
        int to = cmap_jump(&g_cm, ch);
        if (to < 0) { return CFALSE; }
        cm->sel = to;
        break;
    }
    }
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool cm_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    CharMap *cm = (CharMap *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       cm_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return cm_click(win, cm, (int)a, (int)b);
    case WM_MSG_KEYDOWN:     return cm_key(win, cm, (int)a, (int)b);
    case WM_MSG_DESTROY:
        if (cm != NULL) { sys_free(cm, (cu32)sizeof(CharMap)); }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_charmap_open(void)
{
    CharMap *cm;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 356, ch, fx, fy;

    cm_sync_range();
    cm = (CharMap *)sys_calloc(1, (cu32)sizeof(CharMap));
    if (cm == NULL) { SYS_LOGE("app", "charmap: OOM"); return; }
    cm->sel = 'A';
    /* Say what the map covers, so the codes that are NOT here read as a fact
     * about the font rather than as the window having lost them. */
    sys_snprintf(cm->status, sizeof(cm->status),
                 "Codes %d to %d -- every glyph this face has",
                 g_cm_first, g_cm_last);

    /* The window is as tall as the grid needs, so a face with more glyphs in
     * it gets more rows instead of a scrollbar or a clipped last row. */
    ch = 366 - (14 - g_cm_rows) * CM_CELL_H;

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Character Map", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, cm_proc, cm);
    if (w == NULL) { sys_free(cm, (cu32)sizeof(CharMap)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Character Map");
}
