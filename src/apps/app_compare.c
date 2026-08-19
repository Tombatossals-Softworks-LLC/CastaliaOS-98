/*
 * app_compare.c - File Compare: what changed between two text files.
 *
 * The question this answers had no answer on this system. It can archive a
 * folder, compress a file, dump one as hex and edit it in four editors -- but
 * given a backup and the original there was nothing that would say what moved.
 * DOS shipped FC for exactly this, and a machine with no network and no second
 * computer is where it matters most: the backup you took last week is the only
 * other copy of that file in existence.
 *
 * The comparison itself is diff_core.c and knows nothing about windows, which
 * is what lets tests/test_diff.c drive the cases that go wrong -- a change on
 * the very first line, on the very last, an insert, a delete, one file a
 * prefix of the other, two files that differ only in their line endings.
 * This file is the skin: pick two files, read them, and show the answer.
 *
 * Both files are held in memory at once, which is what a comparison IS, and
 * the ceiling is stated rather than discovered: CMP_FILE_MAX per side, and a
 * file over it is refused with its size rather than silently compared to its
 * first 64K.
 */
#include "apps.h"
#include "diff_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

#define CMP_MAGIC 0x434D5031UL          /* 'CMP1' */
#define CMP_FILE_MAX  65536u            /* per side */
#define CMP_ROW_H     12
#define CMP_PAD       8
#define CMP_BAR_H     26
#define CMP_STATUS_H  16
#define CMP_LINE_MAX  128

/* One printable row of the report. */
typedef struct {
    char  kind;       /* ' ' heading, '-' only in A, '+' only in B          */
    int   lineno;     /* the line it came from, 0 for a heading             */
    char  text[CMP_LINE_MAX];
} CmpRow;

#define CMP_ROWS_MAX 600

typedef struct {
    cu32  magic;
    char  path_a[CASTALIA_MAX_PATH];
    char  path_b[CASTALIA_MAX_PATH];
    char  name_a[CASTALIA_MAX_NAME];
    char  name_b[CASTALIA_MAX_NAME];
    char  status[128];
    CmpRow row[CMP_ROWS_MAX];
    int   rows;
    int   top;
    int   visible;
    cbool have_a, have_b;
    UiHot hot;    /* which of the three buttons the pointer is on */
} Compare;

/* One window at a time: the two buffers are large and a second window would
 * double the cost of the one thing this app is for. Reopening points the
 * existing window at the new pair. */
static WmWindow *g_cmp_win = NULL;

static Compare *cmp_of(WmWindow *win)
{
    Compare *c = (Compare *)wm_user(win);
    return (c != NULL && c->magic == CMP_MAGIC) ? c : NULL;
}

/* ---- reading ---------------------------------------------------------- */
/*
 * Read a whole file into 'buf'. Returns bytes read, or -1 with 'why' filled
 * in. A file too large is refused BY SIZE rather than truncated: comparing the
 * first 64K of two files and reporting "identical" is the exact failure this
 * tool must never produce.
 */
static long cmp_read(const char *path, char *buf, cu32 cap,
                     char *why, cu32 whysz)
{
    PlatFile *f;
    long size = plat_file_size(path);
    cu32 got;
    if (size < 0) {
        sys_snprintf(why, whysz, "Cannot read %s", ui_path_base(path));
        return -1;
    }
    if ((cu32)size > cap) {
        sys_snprintf(why, whysz, "%s is %ldK -- the limit is %luK per side",
                     ui_path_base(path), size / 1024L,
                     (unsigned long)(cap / 1024u));
        return -1;
    }
    f = plat_fopen(path, "rb");
    if (f == NULL) {
        sys_snprintf(why, whysz, "Cannot open %s", ui_path_base(path));
        return -1;
    }
    got = plat_fread(f, buf, cap);
    plat_fclose(f);
    return (long)got;
}

static void cmp_row_add(Compare *c, char kind, int lineno, const char *text)
{
    CmpRow *r;
    if (c->rows >= CMP_ROWS_MAX) { return; }
    r = &c->row[c->rows++];
    r->kind = kind;
    r->lineno = lineno;
    sys_strlcpy(r->text, (text != NULL) ? text : "", sizeof(r->text));
}

/*
 * Run the comparison and turn it into rows.
 *
 * The buffers are static rather than on the stack: 64K each is not something
 * to put there on the DOS target, and they are dead the moment this returns.
 */
static void cmp_run(Compare *c)
{
    static char buf_a[CMP_FILE_MAX];
    static char buf_b[CMP_FILE_MAX];
    static DiffResult res;
    long na, nb;
    int h;

    c->rows = 0;
    c->top = 0;
    c->status[0] = '\0';
    if (!c->have_a || !c->have_b) {
        sys_strlcpy(c->status, "Pick two files to compare", sizeof(c->status));
        return;
    }

    na = cmp_read(c->path_a, buf_a, CMP_FILE_MAX, c->status, sizeof(c->status));
    if (na < 0) { return; }
    nb = cmp_read(c->path_b, buf_b, CMP_FILE_MAX, c->status, sizeof(c->status));
    if (nb < 0) { return; }

    diff_compare(buf_a, (cu32)na, buf_b, (cu32)nb, &res);
    diff_summary(&res, c->status, sizeof(c->status));

    if (res.identical) { return; }

    for (h = 0; h < res.count; h++) {
        const DiffHunk *k = &res.hunk[h];
        char head[CMP_LINE_MAX];
        char line[CMP_LINE_MAX];
        int i;
        /* A heading a person can act on: which lines, in which file. The two
         * files are named, not called "left" and "right" -- somebody reading
         * this later will not remember which was which. */
        if (k->a_count > 0 && k->b_count > 0) {
            sys_snprintf(head, sizeof(head), "Line %d changed", k->a_start);
        } else if (k->a_count > 0) {
            sys_snprintf(head, sizeof(head), "Line %d only in %s",
                         k->a_start, c->name_a);
        } else {
            sys_snprintf(head, sizeof(head), "Line %d only in %s",
                         k->b_start, c->name_b);
        }
        if (k->a_count > 1 || k->b_count > 1) {
            char more[32];
            sys_snprintf(more, sizeof(more), " (%d/%d lines)",
                         k->a_count, k->b_count);
            sys_strlcat(head, more, sizeof(head));
        }
        cmp_row_add(c, ' ', 0, head);
        for (i = 0; i < k->a_count; i++) {
            if (diff_line(buf_a, (cu32)na, k->a_start + i, line, sizeof(line))) {
                cmp_row_add(c, '-', k->a_start + i, line);
            }
        }
        for (i = 0; i < k->b_count; i++) {
            if (diff_line(buf_b, (cu32)nb, k->b_start + i, line, sizeof(line))) {
                cmp_row_add(c, '+', k->b_start + i, line);
            }
        }
        if (c->rows >= CMP_ROWS_MAX) {
            sys_strlcat(c->status, " -- the list is full", sizeof(c->status));
            break;
        }
    }
}

/* ---- layout ----------------------------------------------------------- */
/* crect_offset takes a pointer; these rects are computed and used in one
 * expression, so this is the value-taking form of it. */
static CRect cmp_at(CRect r, int dx, int dy)
{
    return crect_offset(&r, dx, dy);
}

static CRect cmp_bar_rect(int cw)
{
    return crect_make(0, 0, cw, CMP_BAR_H);
}
static CRect cmp_list_rect(int cw, int ch)
{
    return crect_make(CMP_PAD, CMP_BAR_H, cw - CMP_PAD * 2,
                      ch - CMP_BAR_H - CMP_STATUS_H - CMP_PAD);
}
static CRect cmp_btn_a(void)  { return crect_make(CMP_PAD, 4, 92, 18); }
static CRect cmp_btn_b(void)  { return crect_make(CMP_PAD + 98, 4, 92, 18); }
static CRect cmp_btn_go(void) { return crect_make(CMP_PAD + 196, 4, 74, 18); }

/* The three, in one order. */
enum { CMB_A = 0, CMB_B, CMB_GO, CMB_N };

static CRect cmp_btn(int i)
{
    return (i == CMB_A) ? cmp_btn_a() : (i == CMB_B) ? cmp_btn_b()
                                                     : cmp_btn_go();
}

/*
 * Which button is at (px,py), in CLIENT coordinates -- which is what a mouse
 * message carries.
 *
 * The click handler used to offset each button to SCREEN coordinates and then
 * test the client-coordinate pointer against it, so none of the three could
 * be pressed at all: the rectangle it was comparing against was a window's
 * width and height away from where the button is. What it did instead was
 * answer to clicks in the middle of the diff list, at whatever offset
 * happened to land inside the shifted rectangle. Nothing caught it because
 * --compare-demo opens the window with both paths already chosen and never
 * presses anything.
 */
static int cmp_btn_at(int px, int py)
{
    int i;
    for (i = 0; i < CMB_N; i++) {
        CRect r = cmp_btn(i);
        if (crect_contains(&r, px, py)) { return i; }
    }
    return -1;
}

/* ---- painting --------------------------------------------------------- */
static void cmp_paint(WmWindow *win, GfxSurface *s)
{
    Compare *c = cmp_of(win);
    const UiPalette *p = ui_palette();
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    int cw = crect_w(&client), ch = crect_h(&client);
    CRect bar, list, r;
    int i, y;
    char label[CASTALIA_MAX_NAME + 16];

    if (c == NULL) { return; }

    bar = cmp_at(cmp_bar_rect(cw), o.x, o.y);
    gfx_fill_rect(s, &bar, p->face);
    gfx_hline(s, bar.x0, bar.y1 - 1, crect_w(&bar), p->dark);

    sys_snprintf(label, sizeof(label), "File 1...");
    r = cmp_at(cmp_btn_a(), o.x, o.y);
    ui_draw_button(s, &r, label, ui_hot_state(&c->hot, CMB_A, UI_BTN_NORMAL));
    sys_snprintf(label, sizeof(label), "File 2...");
    r = cmp_at(cmp_btn_b(), o.x, o.y);
    ui_draw_button(s, &r, label, ui_hot_state(&c->hot, CMB_B, UI_BTN_NORMAL));
    r = cmp_at(cmp_btn_go(), o.x, o.y);
    ui_draw_button(s, &r, "Compare",
                   ui_hot_state(&c->hot, CMB_GO,
                                (c->have_a && c->have_b) ? UI_BTN_NORMAL
                                                         : UI_BTN_DISABLED));

    /* The two names, right of the buttons, so what is being compared is on
     * screen and not only in the title bar. */
    {
        CRect nr = bar;
        nr.x0 += CMP_PAD + 280;
        sys_snprintf(label, sizeof(label), "%s  vs  %s",
                     c->have_a ? c->name_a : "(none)",
                     c->have_b ? c->name_b : "(none)");
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &nr, label, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }

    list = cmp_at(cmp_list_rect(cw, ch), o.x, o.y);
    gfx_bevel(s, &list, GFX_BEVEL_SUNKEN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    c->visible = (crect_h(&list) - 4) / CMP_ROW_H;
    if (c->visible < 1) { c->visible = 1; }

    y = list.y0 + 2;
    for (i = c->top; i < c->rows && i < c->top + c->visible; i++) {
        const CmpRow *row = &c->row[i];
        char text[CMP_LINE_MAX + 24];
        CColor ink;
        if (row->kind == ' ') {
            /* A heading: the theme's own accent, so it reads as structure
             * rather than as another line of file content. */
            CRect hr = crect_make(list.x0 + 2, y, crect_w(&list) - 4,
                                  CMP_ROW_H);
            gfx_fill_rect(s, &hr, gfx_tint(p->accent, GFX_RGB(0xFF,0xFF,0xFF),
                                           200));
            ink = p->text;
            sys_strlcpy(text, row->text, sizeof(text));
        } else {
            ink = (row->kind == '-') ? GFX_RGB(0xA0, 0x20, 0x20)
                                     : GFX_RGB(0x10, 0x70, 0x20);
            sys_snprintf(text, sizeof(text), "%c %4d  %s",
                         row->kind, row->lineno, row->text);
        }
        gfx_draw_text(s, GFX_FONT_SYSTEM, list.x0 + 4, y + 2, text, ink);
        y += CMP_ROW_H;
    }

    {
        CRect sr = crect_make(o.x + CMP_PAD, o.y + ch - CMP_STATUS_H,
                              cw - CMP_PAD * 2, CMP_STATUS_H);
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &sr, c->status, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }
}

/* ---- picking files ---------------------------------------------------- */
static void cmp_set(Compare *c, int which, const char *path)
{
    if (which == 0) {
        sys_strlcpy(c->path_a, path, sizeof(c->path_a));
        sys_strlcpy(c->name_a, ui_path_base(path), sizeof(c->name_a));
        c->have_a = CTRUE;
    } else {
        sys_strlcpy(c->path_b, path, sizeof(c->path_b));
        sys_strlcpy(c->name_b, ui_path_base(path), sizeof(c->name_b));
        c->have_b = CTRUE;
    }
}

static void on_pick_a(cbool ok, const char *path, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Compare *c = cmp_of(win);
    if (!ok || c == NULL || path == NULL || path[0] == '\0') { return; }
    cmp_set(c, 0, path);
    if (c->have_b) { cmp_run(c); }
    wm_invalidate(win, NULL);
}

static void on_pick_b(cbool ok, const char *path, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Compare *c = cmp_of(win);
    if (!ok || c == NULL || path == NULL || path[0] == '\0') { return; }
    cmp_set(c, 1, path);
    if (c->have_a) { cmp_run(c); }
    wm_invalidate(win, NULL);
}

static void cmp_click(WmWindow *win, int px, int py)
{
    Compare *c = cmp_of(win);
    int hit;
    if (c == NULL) { return; }
    hit = cmp_btn_at(px, py);
    (void)ui_hot_press(&c->hot, hit);
    if (hit == CMB_A) {
        ui_file_dialog("Compare -- first file", NULL, NULL, CFALSE, NULL,
                       on_pick_a, win);
        return;
    }
    if (hit == CMB_B) {
        ui_file_dialog("Compare -- second file", NULL, NULL, CFALSE, NULL,
                       on_pick_b, win);
        return;
    }
    if (hit == CMB_GO && c->have_a && c->have_b) {
        cmp_run(c);
    }
    wm_invalidate(win, NULL);
}

static void cmp_key(WmWindow *win, int key)
{
    Compare *c = cmp_of(win);
    int max;
    if (c == NULL) { return; }
    max = c->rows - c->visible;
    if (max < 0) { max = 0; }
    switch (key) {
    case PLAT_KEY_DOWN:  c->top++; break;
    case PLAT_KEY_UP:    c->top--; break;
    case PLAT_KEY_PGDN:  c->top += c->visible; break;
    case PLAT_KEY_PGUP:  c->top -= c->visible; break;
    case PLAT_KEY_HOME:  c->top = 0; break;
    case PLAT_KEY_END:   c->top = max; break;
    /* The two pickers and the run, from the keyboard: this window is a row of
     * buttons and would otherwise need a mouse for every single thing it does.
     * Enter runs it, the way any form's default action does -- not F5, which
     * this system's key table does not have. */
    case PLAT_KEY_F2:
        ui_file_dialog("Compare -- first file", NULL, NULL, CFALSE, NULL,
                       on_pick_a, win);
        return;
    case PLAT_KEY_F3:
        ui_file_dialog("Compare -- second file", NULL, NULL, CFALSE, NULL,
                       on_pick_b, win);
        return;
    case PLAT_KEY_ENTER:
        if (c->have_a && c->have_b) { cmp_run(c); }
        break;
    default: return;
    }
    if (c->top > max) { c->top = max; }
    if (c->top < 0) { c->top = 0; }
    wm_invalidate(win, NULL);
}

static cbool cmp_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_PAINT:       cmp_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: cmp_click(win, (int)a, (int)b);      return CTRUE;
    case WM_MSG_MOUSEMOVE: {
        Compare *c = cmp_of(win);
        if (c == NULL) { return CFALSE; }
        if (ui_hot_move(&c->hot, cmp_btn_at((int)a, (int)b))) {
            CRect br[CMB_N];
            int k;
            for (k = 0; k < CMB_N; k++) { br[k] = cmp_btn(k); }
            ui_hot_repaint(win, &c->hot, br, CMB_N);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        Compare *c = cmp_of(win);
        cbool redraw;
        if (c == NULL) { return CFALSE; }
        redraw = ui_hot_release(&c->hot);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&c->hot, -1)) {
            redraw = CTRUE;
        }
        if (redraw) {
            CRect br[CMB_N];
            int k;
            for (k = 0; k < CMB_N; k++) { br[k] = cmp_btn(k); }
            ui_hot_repaint(win, &c->hot, br, CMB_N);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSEWHEEL: {
        Compare *c = (Compare *)wm_user(win);
        CRect client;
        if (c == NULL) { return CFALSE; }
        client = wm_client_rect(win);
        if (ui_scroll_wheel(&c->top, (int)a, c->rows, c->visible)) {
            CPoint o = wm_client_origin(win);
            CRect r = cmp_list_rect(crect_w(&client), crect_h(&client));
            r = crect_offset(&r, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     cmp_key(win, (int)a);                return CTRUE;
    case WM_MSG_DESTROY: {
        Compare *c = cmp_of(win);
        if (win == g_cmp_win) { g_cmp_win = NULL; }
        if (c != NULL) { sys_free(c, (cu32)sizeof(Compare)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

/*
 * What the window is showing, for the scene check. The comparison itself is
 * tested without a window in tests/test_diff.c; what only a running system can
 * answer is whether the two files named here reached it and whether the answer
 * came back on screen.
 */
CRect app_compare_btn_rect(WmWindow *win, int i)
{
    CRect none = crect_make(0, 0, 0, 0), r;
    CPoint o;
    if (win == NULL || cmp_of(win) == NULL || i < 0 || i >= CMB_N) {
        return none;
    }
    o = wm_client_origin(win);
    r = cmp_btn(i);
    return crect_offset(&r, o.x, o.y);
}

int app_compare_rows(void)
{
    Compare *c = (g_cmp_win != NULL) ? cmp_of(g_cmp_win) : NULL;
    return (c != NULL) ? c->rows : -1;
}

void app_compare_status(char *dst, cu32 dstsz)
{
    Compare *c = (g_cmp_win != NULL) ? cmp_of(g_cmp_win) : NULL;
    if (dst == NULL || dstsz == 0u) { return; }
    sys_strlcpy(dst, (c != NULL) ? c->status : "", dstsz);
}

cbool app_compare_row(int i, char *dst, cu32 dstsz)
{
    Compare *c = (g_cmp_win != NULL) ? cmp_of(g_cmp_win) : NULL;
    if (dst == NULL || dstsz == 0u) { return CFALSE; }
    dst[0] = '\0';
    if (c == NULL || i < 0 || i >= c->rows) { return CFALSE; }
    sys_snprintf(dst, dstsz, "%c%s", c->row[i].kind, c->row[i].text);
    return CTRUE;
}

void app_compare_open(const char *first, const char *second)
{
    Compare *c;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw, ch, fx, fy;

    /* Already open: point it at the new pair rather than stacking a second
     * copy of two 64K buffers. */
    if (g_cmp_win != NULL) {
        c = cmp_of(g_cmp_win);
        if (c != NULL) {
            if (first  != NULL && first[0]  != '\0') { cmp_set(c, 0, first); }
            if (second != NULL && second[0] != '\0') { cmp_set(c, 1, second); }
            cmp_run(c);
            wm_focus(g_cmp_win);
            wm_invalidate(g_cmp_win, NULL);
        }
        return;
    }

    c = (Compare *)sys_calloc(1, (cu32)sizeof(Compare));
    if (c == NULL) { SYS_LOGE("app", "compare: OOM"); return; }
    c->magic = CMP_MAGIC;
    if (first  != NULL && first[0]  != '\0') { cmp_set(c, 0, first); }
    if (second != NULL && second[0] != '\0') { cmp_set(c, 1, second); }

    cw = 560;
    ch = CMP_BAR_H + 22 * CMP_ROW_H + CMP_STATUS_H + CMP_PAD;
    plat_video_info(&vi);
    if (cw > vi.width - 20)  { cw = vi.width - 20; }
    if (ch > vi.height - 60) { ch = vi.height - 60; }
    fx = (vi.width  - (cw + 8)) / 2;
    fy = (vi.height - (ch + 28)) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw + 8, ch + 28);

    w = wm_create("File Compare", &frame, WM_STYLE_APP, cmp_proc, c);
    if (w == NULL) { sys_free(c, (cu32)sizeof(Compare)); return; }
    g_cmp_win = w;
    cmp_run(c);
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "compare: opened");
}
