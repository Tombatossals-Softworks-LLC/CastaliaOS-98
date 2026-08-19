/*
 * app_hex.c - Hex Viewer.
 *
 * The tool for "this file will not open". Is it really a bitmap, did the
 * download stop half way, what is actually in the first sector -- questions
 * with no other answer on a machine that has nothing else installed.
 *
 * It reads ONLY the rows on screen. plat_fseek lands on the row the view
 * starts at and one plat_fread brings back a screenful, so a forty-megabyte
 * file costs the same as a forty-byte one and the memory this window holds
 * does not depend on what it is looking at. That is the whole reason the
 * platform layer grew a seek.
 *
 * The layout -- addresses, the padding that keeps a short last row's text
 * column where the others are, and what a byte looks like as a character --
 * is hex_core.c, which is walked by tests/test_hex.c without a file.
 */
#include "apps.h"
#include "hex_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#define HX_MAGIC 0x48455831UL          /* 'HEX1' */
#define HX_ROWS_MAX  48
#define HX_ROW_H     12
#define HX_STATUS_H  16
#define HX_PAD       8

typedef struct {
    cu32  magic;
    char  path[CASTALIA_MAX_PATH];
    char  name[CASTALIA_MAX_NAME];
    long  size;
    long  top;                 /* file offset of the first visible row      */
    int   visible;             /* rows the well can show                    */
    int   have;                /* bytes actually in buf                     */
    unsigned char buf[HX_ROWS_MAX * HEX_ROW_BYTES];
    cbool unreadable;
} HexView;

static HexView *hx_of(WmWindow *win)
{
    HexView *h = (win != NULL) ? (HexView *)wm_user(win) : NULL;
    return (h != NULL && h->magic == HX_MAGIC) ? h : NULL;
}

/* Pull the bytes for the current view. Everything about how much to read is
 * derived from 'visible', so a resized window reads a different amount and
 * nothing else changes. */
static void hx_fetch(HexView *h)
{
    PlatFile *f;
    cu32 want;

    h->have = 0;
    if (h->visible < 1) { return; }
    want = (cu32)h->visible * HEX_ROW_BYTES;
    if (want > (cu32)sizeof(h->buf)) { want = (cu32)sizeof(h->buf); }

    f = plat_fopen(h->path, "rb");
    if (f == NULL) { h->unreadable = CTRUE; return; }
    if (plat_fseek(f, h->top) != CE_OK) {
        plat_fclose(f);
        h->unreadable = CTRUE;
        return;
    }
    h->have = (int)plat_fread(f, h->buf, want);
    plat_fclose(f);
    h->unreadable = CFALSE;
}

/* How many rows fit, and the well they fit in. */
static CRect hx_well(WmWindow *win, HexView *h)
{
    CRect c = wm_client_rect(win);
    CRect w = c;
    w.x0 += HX_PAD;
    w.x1 -= HX_PAD;
    w.y0 += HX_PAD;
    w.y1 -= HX_STATUS_H + HX_PAD;
    h->visible = (crect_h(&w) > 0) ? crect_h(&w) / HX_ROW_H : 0;
    if (h->visible > HX_ROWS_MAX) { h->visible = HX_ROWS_MAX; }
    if (h->visible < 1) { h->visible = 1; }
    return w;
}

static void hx_paint(WmWindow *win, GfxSurface *s)
{
    HexView *h = hx_of(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CRect  c = wm_client_rect(win);
    CRect  well;
    char line[HEX_LINE_MAX];
    char status[96];
    int y, i, rows;

    gfx_fill_rect(s, &c, p->face);
    if (h == NULL) { return; }

    well = hx_well(win, h);
    gfx_bevel(s, &well, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);

    y = well.y0 + 2;
    if (h->unreadable) {
        gfx_draw_text(s, GFX_FONT_SYSTEM, well.x0 + 6, y,
                      "This file could not be read.", p->text);
    } else if (h->size == 0) {
        /* An empty file is a real answer and a common one -- a download that
         * produced nothing looks exactly like this, and a blank window would
         * look like the viewer failing. */
        gfx_draw_text(s, GFX_FONT_SYSTEM, well.x0 + 6, y,
                      "The file is empty (0 bytes).", p->text);
    } else {
        rows = (h->have + HEX_ROW_BYTES - 1) / HEX_ROW_BYTES;
        for (i = 0; i < rows && i < h->visible; i++) {
            int n = h->have - i * HEX_ROW_BYTES;
            if (n > HEX_ROW_BYTES) { n = HEX_ROW_BYTES; }
            if (hex_format_row(line, sizeof(line),
                               (unsigned long)(h->top + i * HEX_ROW_BYTES),
                               h->buf + i * HEX_ROW_BYTES, n) > 0) {
                gfx_draw_text(s, GFX_FONT_SYSTEM, well.x0 + 6, y, line,
                              p->text);
            }
            y += HX_ROW_H;
        }
    }

    /* Where we are, in both the units somebody might be working in. */
    sys_snprintf(status, sizeof(status), "%s   %ld bytes   offset %08lX",
                 h->name, h->size, (unsigned long)h->top);
    gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + HX_PAD,
                  c.y1 - HX_STATUS_H, status, p->text_disabled);
    CASTALIA_UNUSED(o);
}

static cbool hx_key(WmWindow *win, HexView *h, int key)
{
    long before = h->top;
    switch (key) {
    case PLAT_KEY_DOWN:  h->top = hex_scroll(h->top, h->size, h->visible, 1); break;
    case PLAT_KEY_UP:    h->top = hex_scroll(h->top, h->size, h->visible, -1); break;
    case PLAT_KEY_PGDN:  h->top = hex_scroll(h->top, h->size, h->visible,
                                             h->visible); break;
    case PLAT_KEY_PGUP:  h->top = hex_scroll(h->top, h->size, h->visible,
                                             -h->visible); break;
    case PLAT_KEY_HOME:  h->top = hex_scroll(0, h->size, h->visible, 0); break;
    /* End goes as far as the clamp allows, which lands the file's last row on
     * the bottom of the window rather than at the top of a blank page. */
    case PLAT_KEY_END:   h->top = hex_scroll(h->top, h->size, h->visible,
                                             0x40000000); break;
    default: return CFALSE;
    }
    if (h->top != before) { hx_fetch(h); }
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool hx_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    HexView *h = hx_of(win);
    CASTALIA_UNUSED(b);
    switch (msg) {
    case WM_MSG_PAINT:
        hx_paint(win, (GfxSurface *)param);
        return CTRUE;
    case WM_MSG_SIZE:
        if (h != NULL) {
            hx_well(win, h);
            /* A taller window shows more, so the clamp has to be re-applied:
             * a view that was scrolled to the bottom of a short window would
             * otherwise sit past the end of a tall one. */
            h->top = hex_scroll(h->top, h->size, h->visible, 0);
            hx_fetch(h);
        }
        return CTRUE;
    case WM_MSG_MOUSEWHEEL:
        /*
         * The Hex Viewer scrolls by FILE OFFSET rather than by row index --
         * it reads only the rows on screen -- so the shared row rule does not
         * apply and hex_scroll, which is pure and host-tested, does the
         * clamping. A wheel at either end lands on the same offset and
         * repaints nothing.
         */
        if (h != NULL) {
            long was = h->top;
            h->top = hex_scroll(h->top, h->size, h->visible,
                                -(int)a * UI_WHEEL_LINES);
            if (h->top != was) { hx_fetch(h); wm_invalidate(win, NULL); }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        return (h != NULL) ? hx_key(win, h, (int)a) : CFALSE;
    case WM_MSG_DESTROY:
        if (h != NULL) { sys_free(h, (cu32)sizeof(HexView)); }
        return CTRUE;
    default:
        return CFALSE;
    }
}

/* What the headless driver asks: how far into the file the view is, and how
 * many bytes it is holding. -1 for a window that is not a Hex Viewer. */
long app_hex_offset(WmWindow *win)
{
    HexView *h = hx_of(win);
    return (h != NULL) ? h->top : -1L;
}

int app_hex_have(WmWindow *win)
{
    HexView *h = hx_of(win);
    return (h != NULL) ? h->have : -1;
}

/* How many rows the well can show. With app_hex_have() this pins the memory
 * claim exactly -- "it holds a screenful" has to mean THIS screenful, not
 * merely less than the file, or a viewer that read twice what it draws would
 * pass. */
int app_hex_visible(WmWindow *win)
{
    HexView *h = hx_of(win);
    return (h != NULL) ? h->visible : -1;
}

/* The i-th byte of what is currently on screen, or -1. This is what lets a
 * scene check that the seek landed where the address column claims it did --
 * without it, "the offset changed" is all anyone could verify, and a viewer
 * that showed the first screenful forever would satisfy that. */
int app_hex_byte(WmWindow *win, int i)
{
    HexView *h = hx_of(win);
    if (h == NULL || i < 0 || i >= h->have) { return -1; }
    return (int)h->buf[i];
}

void app_hex_open(const char *path)
{
    HexView *h;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    const char *base;
    int cw, ch, fx, fy;

    if (path == NULL || path[0] == '\0') { return; }
    h = (HexView *)sys_calloc(1, (cu32)sizeof(HexView));
    if (h == NULL) { SYS_LOGE("app", "hex: OOM"); return; }
    h->magic = HX_MAGIC;
    sys_strlcpy(h->path, path, sizeof(h->path));

    base = ui_path_base(path);
    sys_strlcpy(h->name, base, sizeof(h->name));

    h->size = plat_file_size(path);
    if (h->size < 0) { h->size = 0; h->unreadable = CTRUE; }
    h->top = 0;

    /* Sized to the dump rather than to a round number: 76 characters at the
     * system face's 6px advance, so no row ever wraps or gets clipped. */
    cw = 76 * 6 + HX_PAD * 2 + 12;
    ch = 24 * HX_ROW_H + HX_STATUS_H + HX_PAD * 2 + 4;
    plat_video_info(&vi);
    if (cw > vi.width - 20)  { cw = vi.width - 20; }
    if (ch > vi.height - 60) { ch = vi.height - 60; }
    fx = (vi.width  - (cw + 8)) / 2;
    fy = (vi.height - (ch + 28)) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw + 8, ch + 28);

    w = wm_create("Hex Viewer", &frame, WM_STYLE_APP, hx_proc, h);
    if (w == NULL) { sys_free(h, (cu32)sizeof(HexView)); return; }
    hx_well(w, h);
    hx_fetch(h);
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "hex: opened %s (%ld bytes)", h->name, h->size);
}
