/*
 * app_clock.c - CastaliaOS Clock, Calendar & Agenda.
 *
 * A live analog clock (classic beveled face, 60 minute ticks with bold hour
 * marks, thick hour/minute hands and a red sweeping second hand), an LCD-style
 * digital HH:MM:SS readout with the full date line below it, a monthly
 * calendar you can page through with the < / > buttons or the arrow keys, and
 * an appointment panel for the day you pick in that calendar.
 *
 * "Today" (or the Home key) snaps back to the real current month, whose cell
 * is highlighted with the accent color. A day that has appointments carries a
 * small dot under its number, so a month shows where the work is at a glance.
 * The face is on the animation timer, but it does NOT repaint every frame.
 * The second hand moves in whole seconds, so a repaint before the second
 * changes would draw exactly the same picture; and when it does change, only
 * the face and the digital readout are invalidated, not the calendar and the
 * agenda beside them. Repainting a 484x356 window sixty times a second to
 * move one red hand was, measurably, most of the work an idle desktop did.
 * Nothing here allocates per frame.
 *
 * Keys:  Left/Right, PgUp/PgDn page months  |  Up/Down move a week
 *        Home = today  |  Enter = new appointment  |  Del = delete the selected
 *
 * The appointments themselves live in agenda_core.c (pure logic, host-tested)
 * and persist as one plain line per event in CASTALIA_HOME\SYS\AGENDA.TXT, so
 * they survive a reboot and can be fixed in any text editor.
 *
 * All angle math uses the fixed-point sine/cosine tables below (values scaled
 * by 1000, indexed by the 0..59 tick position, 0 = 12 o'clock, clockwise), so
 * the app pulls in no floating point and no math library.
 */
#include "apps.h"
#include "../sys/cal_core.h"
#include "agenda_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include <stdlib.h>

/* ---- fixed-point trig (x1000), index = tick 0..59, 0 = 12 o'clock ---- */
static const int CK_SIN[60] = {
        0,   105,   208,   309,   407,   500,   588,   669,   743,   809,
      866,   914,   951,   978,   995,  1000,   995,   978,   951,   914,
      866,   809,   743,   669,   588,   500,   407,   309,   208,   105,
        0,  -105,  -208,  -309,  -407,  -500,  -588,  -669,  -743,  -809,
     -866,  -914,  -951,  -978,  -995, -1000,  -995,  -978,  -951,  -914,
     -866,  -809,  -743,  -669,  -588,  -500,  -407,  -309,  -208,  -105
};
static const int CK_COS[60] = {
    -1000,  -995,  -978,  -951,  -914,  -866,  -809,  -743,  -669,  -588,
     -500,  -407,  -309,  -208,  -105,     0,   105,   208,   309,   407,
      500,   588,   669,   743,   809,   866,   914,   951,   978,   995,
     1000,   995,   978,   951,   914,   866,   809,   743,   669,   588,
      500,   407,   309,   208,   105,     0,  -105,  -208,  -309,  -407,
     -500,  -588,  -669,  -743,  -809,  -866,  -914,  -951,  -978,  -995
};

/* ---- name tables ----------------------------------------------------- */
static const char *CK_WDAY[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
    "Saturday"
};
static const char *CK_WD2[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
static const char *CK_MON[12] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

/* ---- geometry -------------------------------------------------------- */
#define CK_MARGIN   6
#define CK_CLOCK_H  148
#define CK_LCD_H    24
#define CK_DATE_H   12
#define CK_HDR_H    22
#define CK_WD_H     14
#define CK_ROWS     6
#define CK_COLS     7
#define CK_PANEL_W  184          /* agenda column on the right       */
#define CK_ROW_H    13           /* one appointment row              */
#define CK_TIME_W   36           /* the time gutter inside a row     */
#define CK_DAY_MAX  32           /* appointments listed for one day  */

typedef struct {
    int    bm;      /* browsed month 1..12                             */
    int    by;      /* browsed year                                    */
    int    sd;      /* selected day of the browsed month, 1..31        */
    int    sel;     /* selected row in the day's list, or -1           */
    int    top;     /* first visible row of the day's list             */
    Agenda ag;
    char   status[80];   /* wide enough for the refusals below to read */
    cbool  ag_partial;   /* AGENDA.TXT held more than fits: do not save over it */
    int    last_sec;             /* second last drawn; -1 = never   */
    UiHot  hot;                  /* < > Today / New... under the pointer */
} Clock;

typedef struct {
    CRect clock;                 /* analog face area                */
    CRect lcd;                   /* digital readout well            */
    CRect dateline;              /* date text row                   */
    CRect calhdr, prev, next, today;
    CRect wdays;                 /* weekday header strip            */
    CRect grid;                  /* 6x7 day grid well               */
    CRect agtitle;               /* "Appointments" header bar       */
    CRect agday;                 /* the selected day, spelled out   */
    CRect aglist;                /* the day's appointments well     */
    CRect agbar;                 /* the list's scroll bar           */
    CRect agstat;                /* one line of feedback            */
    CRect agadd, agdel;
/* The four push buttons, in one order, so painting and hit-testing agree. */
#define CKB_PREV  0
#define CKB_NEXT  1
#define CKB_TODAY 2
#define CKB_ADD   3
} CkLayout;

/* ---- date helpers -----------------------------------------------------
 *
 * These were local until they were not: agenda_core.c had the same leap-year
 * rule and month table a few files away, and the two disagreed about how long
 * month 13 is. src/sys/cal_core.c holds the one copy now.
 *
 * ck_days_in_month used to answer 30 for a month that does not exist, which
 * is why the wrapper below still has to decide something -- the browsing
 * state clamps against this value, and a zero would collapse the month view.
 * The honest 0 comes back from cal_days_in_month and is turned into "leave
 * the day where it is" here, at the one place that cares.
 */
static int ck_days_in_month(int y, int m)
{
    int d = cal_days_in_month(y, m);
    return (d > 0) ? d : 30;
}

/* Keep the selected day inside the browsed month, and drop a list selection
 * that no longer points at anything (the day changed under it). */
static void ck_reselect(Clock *ck)
{
    int dim = ck_days_in_month(ck->by, ck->bm);
    if (ck->sd < 1) { ck->sd = 1; }
    if (ck->sd > dim) { ck->sd = dim; }
    ck->sel = -1;
    ck->top = 0;
}

static void ck_prev_month(Clock *ck)
{
    if (ck->bm <= 1) { ck->bm = 12; ck->by -= 1; } else { ck->bm -= 1; }
    ck_reselect(ck);
}

static void ck_next_month(Clock *ck)
{
    if (ck->bm >= 12) { ck->bm = 1; ck->by += 1; } else { ck->bm += 1; }
    ck_reselect(ck);
}

static void ck_go_today(Clock *ck)
{
    int y = 2000, mon = 1, day = 1;
    plat_wall_date(&y, &mon, &day, NULL);
    if (mon < 1 || mon > 12) { mon = 1; }
    if (day < 1 || day > 31) { day = 1; }
    ck->by = y;
    ck->bm = mon;
    ck->sd = day;
    ck_reselect(ck);
}

/* Move the selection by whole days, rolling into the next/previous month so a
 * week step never dead-ends at a month boundary. */
static void ck_move_days(Clock *ck, int delta)
{
    int dim;
    ck->sd += delta;
    while (ck->sd < 1) {
        if (ck->bm <= 1) { ck->bm = 12; ck->by -= 1; } else { ck->bm -= 1; }
        ck->sd += ck_days_in_month(ck->by, ck->bm);
    }
    for (;;) {
        dim = ck_days_in_month(ck->by, ck->bm);
        if (ck->sd <= dim) { break; }
        ck->sd -= dim;
        if (ck->bm >= 12) { ck->bm = 1; ck->by += 1; } else { ck->bm += 1; }
    }
    ck->sel = -1;
    ck->top = 0;
}

/* ---- agenda storage --------------------------------------------------- */
static void ck_ag_path(char *out, cu32 outsz)
{
    sys_home_path(out, outsz, "SYS/AGENDA.TXT");
}

static void ck_ag_load(Clock *ck)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    char *buf;
    cu32 got;

    ck_ag_path(path, sizeof path);
    f = plat_fopen(path, "rb");
    if (f == NULL) { return; }            /* no agenda yet is not an error */
    buf = (char *)sys_alloc((cu32)AG_FILE_MAX);
    if (buf == NULL) { plat_fclose(f); return; }
    got = plat_fread(f, buf, (cu32)AG_FILE_MAX - 1);
    plat_fclose(f);
    buf[got] = '\0';
    {
        int dropped = 0;
        if (!ag_parse(&ck->ag, buf, &dropped)) {
            /*
             * The file holds appointments this calendar cannot. Saving would
             * write back the ones it took and delete the rest, so it does not
             * save at all until the file is dealt with -- losing the entry
             * somebody just typed is bad, and silently deleting the ones
             * already in the file is worse, because nobody would ever know.
             */
            ck->ag_partial = CTRUE;
            sys_snprintf(ck->status, sizeof ck->status,
                         "%d appointment(s) in AGENDA.TXT did not fit. "
                         "It will not be saved over.", dropped);
            SYS_LOGW("app", "clock: agenda truncated, %d dropped", dropped);
        } else {
            ck->ag_partial = CFALSE;
            SYS_LOGI("app", "clock: agenda loaded");
        }
    }
    sys_free(buf, (cu32)AG_FILE_MAX);
}

static void ck_ag_save(Clock *ck)
{
    char path[CASTALIA_MAX_PATH], dir[CASTALIA_MAX_PATH];
    PlatFile *f;
    char *buf;
    int n;

    if (ck->ag_partial) {
        sys_strlcpy(ck->status,
                    "AGENDA.TXT holds more than fits. Not saving over it.",
                    sizeof ck->status);
        return;
    }
    sys_home_path(dir, (cu32)sizeof dir, "SYS");
    plat_mkdir(dir);
    ck_ag_path(path, sizeof path);

    buf = (char *)sys_alloc((cu32)AG_FILE_MAX);
    if (buf == NULL) {
        sys_strlcpy(ck->status, "Out of memory -- not saved",
                    sizeof ck->status);
        return;
    }
    n = ag_serialize(&ck->ag, buf, AG_FILE_MAX);
    /* AG_FILE_MAX is the worst case, so this cannot normally fire -- but never
     * hand plat_fwrite a length the buffer does not actually hold. */
    if (n < 0 || n >= AG_FILE_MAX) {
        sys_strlcpy(ck->status, "Agenda too large to save", sizeof ck->status);
        sys_free(buf, (cu32)AG_FILE_MAX);
        return;
    }
    f = plat_fopen(path, "wb");
    if (f == NULL) {
        sys_strlcpy(ck->status, "Could not write the agenda",
                    sizeof ck->status);
        sys_free(buf, (cu32)AG_FILE_MAX);
        return;
    }
    plat_fwrite(f, buf, (cu32)n);
    plat_fclose(f);
    sys_free(buf, (cu32)AG_FILE_MAX);
}

/* ---- layout ---------------------------------------------------------- */
static void ck_layout(WmWindow *win, CkLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c);
    int ch = crect_h(&c);
    int y = CK_MARGIN;
    int lw, inner, gh;
    int px, pw, py, bh, bty, lh;

    /* Left column: clock and calendar. Right column: the agenda panel. On a
     * window too narrow to hold both, the calendar keeps the space and the
     * panel collapses to nothing rather than drawing over it. */
    lw = cw - CK_PANEL_W;
    if (lw < 220) { lw = cw; }
    inner = lw - 2 * CK_MARGIN;
    if (inner < CK_COLS) { inner = CK_COLS; }

    L->clock = crect_make(0, y, lw, CK_CLOCK_H);
    y += CK_CLOCK_H;

    L->lcd = crect_make(CK_MARGIN, y, inner, CK_LCD_H);
    y += CK_LCD_H + 2;

    L->dateline = crect_make(0, y, lw, CK_DATE_H);
    y += CK_DATE_H + 5;

    L->calhdr = crect_make(0, y, lw, CK_HDR_H);
    L->prev  = crect_make(CK_MARGIN, y + 2, 24, CK_HDR_H - 4);
    L->next  = crect_make(lw - CK_MARGIN - 24, y + 2, 24, CK_HDR_H - 4);
    L->today = crect_make(lw - CK_MARGIN - 24 - 4 - 46, y + 2, 46, CK_HDR_H - 4);
    y += CK_HDR_H;

    L->wdays = crect_make(CK_MARGIN, y, inner, CK_WD_H);
    y += CK_WD_H;

    gh = ch - y - CK_MARGIN;
    if (gh < CK_ROWS) { gh = CK_ROWS; }
    L->grid = crect_make(CK_MARGIN, y, inner, gh);

    /* ---- agenda column ---- */
    px = lw;
    pw = cw - lw - CK_MARGIN;
    if (pw < 0) { pw = 0; }
    py = CK_MARGIN;

    L->agtitle = crect_make(px, py, pw, 18);
    py += 18 + 2;
    L->agday = crect_make(px, py, pw, 12);
    py += 12 + 3;

    bh = 20;
    bty = ch - CK_MARGIN - bh;
    L->agadd = crect_make(px, bty, pw / 2 - 2, bh);
    L->agdel = crect_make(px + pw / 2 + 2, bty, pw - pw / 2 - 2, bh);
    L->agstat = crect_make(px, bty - 13, pw, 11);

    lh = (bty - 13 - 3) - py;
    if (lh < CK_ROW_H + 4) { lh = CK_ROW_H + 4; }
    L->aglist = crect_make(px, py, pw, lh);
    L->agbar = crect_make(px + pw - UI_SB_W - 2, py + 2, UI_SB_W, lh - 4);
}

/* How many appointment rows the list well shows at once. */
static int ck_list_rows(const CkLayout *L)
{
    int n = (crect_h(&L->aglist) - 4) / CK_ROW_H;
    return (n < 1) ? 1 : n;
}

/* ---- analog-face drawing helpers ------------------------------------- */
/* A tick mark from radius r_in to r_out at tick position t, 'thick' extra px. */
static void ck_tick(GfxSurface *s, int cx, int cy, int t,
                    int r_out, int r_in, int thick, CColor col)
{
    int ox = cx + CK_SIN[t] * r_out / 1000;
    int oy = cy + CK_COS[t] * r_out / 1000;
    int ix = cx + CK_SIN[t] * r_in / 1000;
    int iy = cy + CK_COS[t] * r_in / 1000;
    int i;
    gfx_line(s, ix, iy, ox, oy, col);
    for (i = 1; i <= thick; i++) {
        gfx_line(s, ix + i, iy, ox + i, oy, col);
        gfx_line(s, ix, iy + i, ox, oy + i, col);
    }
}

/* A hand from the hub to radius 'len' at tick 't'; 'thick' widens it into a
 * small plus-shaped stroke so the hour/minute hands read as solid. */
static void ck_hand(GfxSurface *s, int cx, int cy, int t,
                    int len, int thick, CColor col)
{
    int ex = cx + CK_SIN[t] * len / 1000;
    int ey = cy + CK_COS[t] * len / 1000;
    int i;
    gfx_line(s, cx, cy, ex, ey, col);
    for (i = 1; i <= thick; i++) {
        gfx_line(s, cx + i, cy, ex + i, ey, col);
        gfx_line(s, cx - i, cy, ex - i, ey, col);
        gfx_line(s, cx, cy + i, ex, ey + i, col);
        gfx_line(s, cx, cy - i, ex, ey - i, col);
    }
}

static void ck_paint_clock(GfxSurface *s, const CRect *area)
{
    const UiPalette *p = ui_palette();
    CColor black = GFX_RGB(0, 0, 0);
    CColor facec = GFX_RGB(0xF6, 0xF4, 0xEA);
    CColor tickc = GFX_RGB(0x35, 0x35, 0x35);
    CColor handc = GFX_RGB(0x18, 0x18, 0x18);
    CColor secc  = GFX_RGB(0xCC, 0x24, 0x24);
    CColor shadowc = gfx_tint(facec, black, 96);
    int cx = area->x0 + crect_w(area) / 2;
    int cy = area->y0 + crect_h(area) / 2;
    int rad = crect_h(area) / 2 - 8;
    int h = 10, m = 8, sec = 30;
    int t, th, tm, ts, tail;

    if (rad < 20) { rad = 20; }
    plat_wall_clock(&h, &m, &sec);
    if (h < 0) { h = 0; }
    if (m < 0 || m > 59) { m = 0; }
    if (sec < 0 || sec > 59) { sec = 0; }

    /* Soft drop shadow, then the two-tone raised rim and the pale face. */
    gfx_fill_circle(s, cx + 2, cy + 3, rad, shadowc);
    gfx_fill_circle(s, cx, cy, rad, p->darker);
    gfx_fill_circle(s, cx, cy, rad - 2, p->light);
    gfx_fill_circle(s, cx, cy, rad - 4, facec);

    /* 60 minute ticks; longer/bold on the hours, longest on 12/3/6/9. */
    for (t = 0; t < 60; t++) {
        if (t % 15 == 0) {
            ck_tick(s, cx, cy, t, rad - 6, rad - 18, 1, tickc);
        } else if (t % 5 == 0) {
            ck_tick(s, cx, cy, t, rad - 6, rad - 15, 1, tickc);
        } else {
            ck_tick(s, cx, cy, t, rad - 6, rad - 10, 0, tickc);
        }
    }

    /* Hands. Hour: short/thick. Minute: long/medium. Second: thin red. */
    th = ((h % 12) * 5 + m / 12) % 60;
    tm = m % 60;
    ts = sec % 60;
    ck_hand(s, cx, cy, th, rad * 48 / 100, 2, handc);
    ck_hand(s, cx, cy, tm, rad * 74 / 100, 1, handc);

    /* Red second hand plus a short counterweight tail. */
    tail = (ts + 30) % 60;
    gfx_line(s, cx, cy,
             cx + CK_SIN[tail] * (rad * 16 / 100) / 1000,
             cy + CK_COS[tail] * (rad * 16 / 100) / 1000, secc);
    ck_hand(s, cx, cy, ts, rad * 82 / 100, 0, secc);

    /* Center hub. */
    gfx_fill_circle(s, cx, cy, 4, handc);
    gfx_fill_circle(s, cx, cy, 2, secc);
}

/* ---- digital + date -------------------------------------------------- */
static void ck_paint_digital(GfxSurface *s, const CkLayout *L, CPoint o)
{
    const UiPalette *p = ui_palette();
    CColor lcd_bg = GFX_RGB(0x08, 0x1C, 0x12);
    CColor lcd_fg = GFX_RGB(0x46, 0xF0, 0x74);
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CRect r, well;
    char buf[48];
    int h = 0, m = 0, sec = 0;
    int y = 2000, mon = 1, day = 1, wd = 0;

    plat_wall_clock(&h, &m, &sec);
    plat_wall_date(&y, &mon, &day, &wd);
    if (h < 0 || h > 23) { h = 0; }
    if (m < 0 || m > 59) { m = 0; }
    if (sec < 0 || sec > 59) { sec = 0; }
    if (mon < 1 || mon > 12) { mon = 1; }
    if (wd < 0 || wd > 6) { wd = 0; }

    /* LCD-style sunken well with bright monospaced-looking readout. */
    r = crect_offset(&L->lcd, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    well = crect_inset(&r, 2);
    gfx_fill_rect(s, &well, lcd_bg);
    sys_snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, sec);
    gfx_draw_text_rect(s, GFX_FONT_BOLD, &well, buf, lcd_fg,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);

    /* Full date line, e.g. "Friday, 10 July 2026". */
    r = crect_offset(&L->dateline, o.x, o.y);
    sys_snprintf(buf, sizeof(buf), "%s, %d %s %d",
                 CK_WDAY[wd], day, CK_MON[mon - 1], y);
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r, buf, p->text,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
}

/* ---- calendar -------------------------------------------------------- */
/* One pass over the store per repaint instead of one per day cell: bit d-1 is
 * set when day d of this month has appointments. The face is animated, so this
 * runs every frame and a 42 x count scan would be real work on a 386. */
static cu32 ck_month_marks(const Agenda *a, int y, int m, int *out_total)
{
    int i, n = ag_count(a), total = 0;
    cu32 marks = 0;
    for (i = 0; i < n; i++) {
        const AgEvent *e = ag_get(a, i);
        if (e != NULL && e->y == y && e->m == m && e->d >= 1 && e->d <= 31) {
            marks |= (cu32)1 << (e->d - 1);
            total++;
        }
    }
    if (out_total != NULL) { *out_total = total; }
    return marks;
}

static void ck_paint_calendar(GfxSurface *s, const CkLayout *L, CPoint o,
                              Clock *ck)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor sun = GFX_RGB(0xC0, 0x20, 0x20);
    CColor sat = GFX_RGB(0x28, 0x48, 0xC0);
    CColor gridline = gfx_tint(p->face, p->dark, 90);
    CRect r, cell, grid;
    char buf[32];
    int cw, rowh, colw;
    int first, dim, i, col, row, day;
    int cy = 2000, cmon = 1, cday = 1;
    cu32 marks;

    plat_wall_date(&cy, &cmon, &cday, NULL);
    marks = ck_month_marks(&ck->ag, ck->by, ck->bm, NULL);

    /* Header: prev/next/today controls with the month + year centered. */
    r = crect_offset(&L->calhdr, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    cell = crect_offset(&L->prev, o.x, o.y);
    ui_draw_button(s, &cell, "<",
                   ui_hot_state(&ck->hot, CKB_PREV, UI_BTN_NORMAL));
    cell = crect_offset(&L->next, o.x, o.y);
    ui_draw_button(s, &cell, ">",
                   ui_hot_state(&ck->hot, CKB_NEXT, UI_BTN_NORMAL));
    cell = crect_offset(&L->today, o.x, o.y);
    ui_draw_button(s, &cell, "Today",
                   ui_hot_state(&ck->hot, CKB_TODAY, UI_BTN_NORMAL));
    sys_snprintf(buf, sizeof(buf), "%s %d", CK_MON[ck->bm - 1], ck->by);
    gfx_draw_text_rect(s, GFX_FONT_BOLD, &r, buf, p->text,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);

    /* Weekday header strip: Su .. Sa, weekends tinted. */
    r = crect_offset(&L->wdays, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    cw = crect_w(&r);
    colw = cw / CK_COLS;
    for (col = 0; col < CK_COLS; col++) {
        CColor tc = p->text;
        int tx;
        if (col == 0) { tc = sun; }
        else if (col == 6) { tc = sat; }
        tx = r.x0 + col * colw + (colw - gfx_text_width(GFX_FONT_BOLD, CK_WD2[col])) / 2;
        gfx_draw_text(s, GFX_FONT_BOLD, tx, r.y0 + 3, CK_WD2[col], tc);
    }

    /* Grid well. */
    grid = crect_offset(&L->grid, o.x, o.y);
    gfx_bevel(s, &grid, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    colw = crect_w(&grid) / CK_COLS;
    rowh = crect_h(&grid) / CK_ROWS;
    if (colw < 1) { colw = 1; }
    if (rowh < 1) { rowh = 1; }

    /* Faint separators. */
    for (col = 1; col < CK_COLS; col++) {
        gfx_vline(s, grid.x0 + col * colw, grid.y0 + 1, rowh * CK_ROWS - 2, gridline);
    }
    for (row = 1; row < CK_ROWS; row++) {
        gfx_hline(s, grid.x0 + 1, grid.y0 + row * rowh, colw * CK_COLS - 2, gridline);
    }

    first = cal_weekday(ck->by, ck->bm, 1);
    dim = ck_days_in_month(ck->by, ck->bm);
    for (i = 0; i < CK_ROWS * CK_COLS; i++) {
        col = i % CK_COLS;
        row = i / CK_COLS;
        day = i - first + 1;
        if (day < 1 || day > dim) { continue; }
        cell = crect_make(grid.x0 + col * colw + 1, grid.y0 + row * rowh + 1,
                          colw - 1, rowh - 1);
        {
            CColor tc = p->text;
            cbool today = (ck->by == cy && ck->bm == cmon && day == cday);
            cbool picked = (day == ck->sd);
            int tx, ty;
            if (col == 0) { tc = sun; }
            else if (col == 6) { tc = sat; }
            /* Highlight today when the browsed month is the current month. */
            if (today) {
                gfx_fill_rect(s, &cell, p->accent);
                tc = p->accent_text;
            } else if (picked) {
                gfx_fill_rect(s, &cell, gfx_tint(white, p->accent, 56));
            }
            sys_snprintf(buf, sizeof(buf), "%d", day);
            tx = cell.x0 + (crect_w(&cell) - gfx_text_width(GFX_FONT_SYSTEM, buf)) / 2;
            ty = cell.y0 + (crect_h(&cell) - 8) / 2 - 1;
            gfx_draw_text(s, GFX_FONT_SYSTEM, tx, ty, buf, tc);

            /* A dot under the number marks a day that has appointments, so a
             * month shows where the work is without opening anything. */
            if (marks & ((cu32)1 << (day - 1))) {
                CColor dc = today ? p->accent_text : p->accent;
                int dx = cell.x0 + crect_w(&cell) / 2;
                int dy = ty + 9;
                if (dy > cell.y1 - 3) { dy = cell.y1 - 3; }
                gfx_hline(s, dx - 1, dy, 3, dc);
                gfx_hline(s, dx - 1, dy + 1, 3, dc);
            }
            /* A focus frame on the day the agenda panel is showing. It is drawn
             * in the text color so it reads on the white grid and on top of
             * today's accent fill alike. */
            if (picked) {
                gfx_frame_rect(s, &cell, p->text);
            }
        }
    }
}

/* ---- agenda panel ----------------------------------------------------- */
/* Copy 'text' into 'out', trimmed with a ".." tail when it would not fit in
 * 'px' pixels -- a clipped half-word reads like a bug, an ellipsis does not. */
static void ck_fit(char *out, cu32 outsz, const char *text, int px)
{
    int n, dw;
    sys_strlcpy(out, text, outsz);
    if (gfx_text_width(GFX_FONT_SYSTEM, out) <= px) { return; }
    dw = gfx_text_width(GFX_FONT_SYSTEM, "..");
    n = (int)sys_strnlen(out, outsz);
    while (n > 0 && gfx_text_width(GFX_FONT_SYSTEM, out) > px - dw) {
        out[--n] = '\0';
    }
    while (n > 0 && out[n - 1] == ' ') { out[--n] = '\0'; }
    if ((cu32)n + 3 <= outsz) {
        out[n] = '.'; out[n + 1] = '.'; out[n + 2] = '\0';
    }
}

static void ck_paint_agenda(GfxSurface *s, const CkLayout *L, CPoint o,
                            Clock *ck)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor timec = GFX_RGB(0x20, 0x40, 0x90);
    CRect r, well, row;
    CRect saved;
    char buf[64], line[64], tb[8];
    int idx[CK_DAY_MAX];
    int n, rows, i, wd;

    if (crect_w(&L->agtitle) <= 8) { return; }   /* panel collapsed */

    n = ag_day_events(&ck->ag, ck->by, ck->bm, ck->sd, idx, CK_DAY_MAX);
    rows = ck_list_rows(L);
    if (ck->top > n - rows) { ck->top = n - rows; }
    if (ck->top < 0) { ck->top = 0; }

    /* Title bar, in the era's gradient caption style. */
    r = crect_offset(&L->agtitle, o.x, o.y);
    gfx_vgradient(s, &r, p->accent, gfx_tint(p->accent, white, 90));
    gfx_frame_rect(s, &r, gfx_tint(p->accent, GFX_RGB(0, 0, 0), 60));
    gfx_draw_text_rect(s, GFX_FONT_BOLD, &r, "Appointments", p->accent_text,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);

    /* The day the list is showing, spelled out. */
    wd = cal_weekday(ck->by, ck->bm, ck->sd);
    if (wd < 0 || wd > 6) { wd = 0; }
    sys_snprintf(line, sizeof line, "%s, %d %s", CK_WDAY[wd], ck->sd,
                 CK_MON[ck->bm - 1]);
    r = crect_offset(&L->agday, o.x, o.y);
    ck_fit(buf, sizeof buf, line, crect_w(&r) - 2);
    gfx_draw_text_rect(s, GFX_FONT_BOLD, &r, buf, p->text,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);

    /* The list itself, in a sunken well. */
    r = crect_offset(&L->aglist, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    well = crect_inset(&r, 2);
    saved = gfx_clip_narrow(s, &well);
    if (n == 0) {
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &well, "(nothing scheduled)",
                           p->text_disabled,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }
    for (i = 0; i < rows && ck->top + i < n; i++) {
        const AgEvent *e = ag_get(&ck->ag, idx[ck->top + i]);
        CColor tc = p->text, gc = timec;
        int roww = crect_w(&well) - ((n > rows) ? UI_SB_W + 1 : 0);
        if (e == NULL) { continue; }
        /* Rows stop short of the scroll bar when there is one, so neither the
         * selection fill nor the text ever runs underneath it. */
        row = crect_make(well.x0, well.y0 + i * CK_ROW_H, roww, CK_ROW_H);
        if (ck->top + i == ck->sel) {
            gfx_fill_rect(s, &row, p->accent);
            tc = p->accent_text;
            gc = p->accent_text;
        }
        ag_fmt_time(e, tb, (int)sizeof tb);
        gfx_draw_text(s, GFX_FONT_BOLD, row.x0 + 3, row.y0 + 2, tb, gc);
        ck_fit(buf, sizeof buf, e->text, crect_w(&row) - CK_TIME_W - 8);
        gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 3 + CK_TIME_W, row.y0 + 2,
                      buf, tc);
    }
    gfx_set_clip(s, &saved);

    if (n > rows) {
        r = crect_offset(&L->agbar, o.x, o.y);
        ui_scrollbar_draw(s, &r, CTRUE, n, rows, ck->top, UI_SB_NONE);
    }

    /* Status line: whatever the last action reported, else a month total. */
    r = crect_offset(&L->agstat, o.x, o.y);
    if (ck->status[0] != '\0') {
        sys_strlcpy(buf, ck->status, sizeof buf);
    } else {
        int total = 0;
        (void)ck_month_marks(&ck->ag, ck->by, ck->bm, &total);
        sys_snprintf(line, sizeof line, "%d appointment%s in %s", total,
                     (total == 1) ? "" : "s", CK_MON[ck->bm - 1]);
        ck_fit(buf, sizeof buf, line, crect_w(&r) - 2);
    }
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r, buf, p->text_disabled,
                       GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);

    r = crect_offset(&L->agadd, o.x, o.y);
    ui_draw_button(s, &r, "New...",
                   ui_hot_state(&ck->hot, CKB_ADD, UI_BTN_NORMAL));
    r = crect_offset(&L->agdel, o.x, o.y);
    ui_draw_button(s, &r, "Delete",
                   (ck->sel >= 0 && ck->sel < n) ? UI_BTN_NORMAL
                                                 : UI_BTN_DISABLED);
}

/* ---- paint ----------------------------------------------------------- */
static void ck_paint(WmWindow *win, GfxSurface *s)
{
    Clock *ck = (Clock *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CkLayout L;
    CRect c, area, sep;

    if (ck == NULL) { return; }
    ck_layout(win, &L);

    /* Background. */
    c = wm_client_rect(win);
    gfx_fill_rect(s, &c, p->face);

    area = crect_offset(&L.clock, o.x, o.y);
    ck_paint_clock(s, &area);
    ck_paint_digital(s, &L, o);

    /* Etched separator between the display and the calendar. */
    sep = crect_offset(&L.dateline, o.x, o.y);
    gfx_hline(s, sep.x0 + CK_MARGIN, sep.y1 + 2, crect_w(&sep) - 2 * CK_MARGIN, p->dark);
    gfx_hline(s, sep.x0 + CK_MARGIN, sep.y1 + 3, crect_w(&sep) - 2 * CK_MARGIN, p->light);

    ck_paint_calendar(s, &L, o, ck);

    /* Etched divider between the two columns, then the agenda. */
    if (crect_w(&L.agtitle) > 8) {
        int dx = o.x + L.agtitle.x0 - 3;
        gfx_vline(s, dx, o.y + CK_MARGIN, crect_h(&c) - 2 * CK_MARGIN, p->dark);
        gfx_vline(s, dx + 1, o.y + CK_MARGIN, crect_h(&c) - 2 * CK_MARGIN,
                  p->light);
    }
    ck_paint_agenda(s, &L, o, ck);
}

/* ---- appointments ----------------------------------------------------- */
/*
 * One typed line becomes one appointment: a leading "HH:MM" (or "H:MM") sets
 * the time and the rest is the text; anything else is taken whole as an
 * all-day entry. That keeps a single prompt instead of marching the user
 * through three of them.
 */
static void ck_add_typed(Clock *ck, const char *in)
{
    const char *t = in;
    int hh = AG_ALLDAY, mm = 0;

    while (*t == ' ') { t++; }
    if (t[0] >= '0' && t[0] <= '9') {
        const char *q = t;
        int v = 0;
        while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; }
        if (*q == ':' && q[1] >= '0' && q[1] <= '9' &&
            q[2] >= '0' && q[2] <= '9') {
            hh = v;
            mm = (q[1] - '0') * 10 + (q[2] - '0');
            t = q + 3;
        }
    }
    while (*t == ' ') { t++; }

    if (*t == '\0') {
        sys_strlcpy(ck->status, "Type what the appointment is",
                    sizeof ck->status);
        return;
    }
    if (!ag_add(&ck->ag, ck->by, ck->bm, ck->sd, hh, mm, t)) {
        sys_strlcpy(ck->status,
                    (ag_count(&ck->ag) >= AG_MAX_EVENTS) ? "Agenda is full"
                                                         : "Bad time -- use HH:MM",
                    sizeof ck->status);
        return;
    }
    ck->sel = -1;
    ck->top = 0;
    ck_ag_save(ck);
    if (ck->status[0] == '\0') {
        sys_strlcpy(ck->status, "Appointment added", sizeof ck->status);
    }
}

static void ck_on_new(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Clock *ck = (win != NULL) ? (Clock *)wm_user(win) : NULL;
    if (ck == NULL) { return; }
    ck->status[0] = '\0';
    if (ok && text != NULL) { ck_add_typed(ck, text); }
    wm_invalidate(win, NULL);
}

static void ck_new_appointment(WmWindow *win, Clock *ck)
{
    char label[64];
    sys_snprintf(label, sizeof label, "New on %d %s %d:",
                 ck->sd, CK_MON[ck->bm - 1], ck->by);
    ui_prompt("New Appointment", label, "09:00 ", ck_on_new, win);
}

static void ck_delete_selected(Clock *ck)
{
    int idx[CK_DAY_MAX];
    int n = ag_day_events(&ck->ag, ck->by, ck->bm, ck->sd, idx, CK_DAY_MAX);
    ck->status[0] = '\0';
    if (ck->sel < 0 || ck->sel >= n) {
        sys_strlcpy(ck->status, "Pick an appointment first",
                    sizeof ck->status);
        return;
    }
    if (ag_remove(&ck->ag, idx[ck->sel])) {
        ck->sel = -1;
        ck_ag_save(ck);
        if (ck->status[0] == '\0') {
            sys_strlcpy(ck->status, "Appointment deleted", sizeof ck->status);
        }
    }
}

/* ---- input ----------------------------------------------------------- */
/* Which day cell (1..dim), if any, sits under a point in the grid. */
static int ck_day_at(const CkLayout *L, Clock *ck, int x, int y)
{
    int colw, rowh, col, row, day;
    if (!crect_contains(&L->grid, x, y)) { return 0; }
    colw = crect_w(&L->grid) / CK_COLS;
    rowh = crect_h(&L->grid) / CK_ROWS;
    if (colw < 1 || rowh < 1) { return 0; }
    col = (x - L->grid.x0) / colw;
    row = (y - L->grid.y0) / rowh;
    if (col < 0 || col >= CK_COLS || row < 0 || row >= CK_ROWS) { return 0; }
    day = row * CK_COLS + col - cal_weekday(ck->by, ck->bm, 1) + 1;
    if (day < 1 || day > ck_days_in_month(ck->by, ck->bm)) { return 0; }
    return day;
}

/* Which of the four buttons is at (x,y), or -1. */
static int ck_btn_at(const CkLayout *L, int x, int y)
{
    if (crect_contains(&L->prev, x, y))  { return CKB_PREV; }
    if (crect_contains(&L->next, x, y))  { return CKB_NEXT; }
    if (crect_contains(&L->today, x, y)) { return CKB_TODAY; }
    if (crect_w(&L->agtitle) > 8 && crect_contains(&L->agadd, x, y)) {
        return CKB_ADD;
    }
    return -1;
}

static cbool ck_click(WmWindow *win, Clock *ck, int x, int y)
{
    CkLayout L;
    int day;
    ck_layout(win, &L);
    (void)ui_hot_press(&ck->hot, ck_btn_at(&L, x, y));
    if (crect_contains(&L.prev, x, y)) { ck_prev_month(ck); wm_invalidate(win, NULL); return CTRUE; }
    if (crect_contains(&L.next, x, y)) { ck_next_month(ck); wm_invalidate(win, NULL); return CTRUE; }
    if (crect_contains(&L.today, x, y)) { ck_go_today(ck); wm_invalidate(win, NULL); return CTRUE; }

    day = ck_day_at(&L, ck, x, y);
    if (day > 0) {
        ck->sd = day;
        ck->sel = -1;
        ck->top = 0;
        ck->status[0] = '\0';
        wm_invalidate(win, NULL);
        return CTRUE;
    }

    if (crect_w(&L.agtitle) > 8) {
        int idx[CK_DAY_MAX];
        int n = ag_day_events(&ck->ag, ck->by, ck->bm, ck->sd, idx, CK_DAY_MAX);
        int rows = ck_list_rows(&L);

        if (crect_contains(&L.agadd, x, y)) {
            ck_new_appointment(win, ck);
            return CTRUE;
        }
        if (crect_contains(&L.agdel, x, y)) {
            ck_delete_selected(ck);
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        if (n > rows && crect_contains(&L.agbar, x, y)) {
            int part = ui_scrollbar_hit(&L.agbar, CTRUE, n, rows, ck->top, x, y);
            if (part == UI_SB_LINE_UP)        { ck->top--; }
            else if (part == UI_SB_LINE_DOWN) { ck->top++; }
            else if (part == UI_SB_PAGE_UP)   { ck->top -= rows; }
            else if (part == UI_SB_PAGE_DOWN) { ck->top += rows; }
            else if (part == UI_SB_THUMB) {
                ck->top = ui_scroll_pos_from_coord(crect_h(&L.agbar), n, rows,
                                                   y - L.agbar.y0);
            }
            if (ck->top > n - rows) { ck->top = n - rows; }
            if (ck->top < 0) { ck->top = 0; }
            /* The agenda list and its bar; the calendar beside them did not
             * move, and neither did the clock face above. */
            {
                CPoint o = wm_client_origin(win);
                CRect r = crect_union(&L.aglist, &L.agbar);
                r = crect_offset(&r, o.x, o.y);
                wm_invalidate(win, &r);
            }
            return CTRUE;
        }
        if (crect_contains(&L.aglist, x, y)) {
            int r = (y - (L.aglist.y0 + 2)) / CK_ROW_H;
            ck->status[0] = '\0';
            ck->sel = (r >= 0 && r < rows && ck->top + r < n)
                      ? ck->top + r : -1;
            wm_invalidate(win, NULL);
            return CTRUE;
        }
    }
    return CFALSE;
}

static cbool ck_key(WmWindow *win, Clock *ck, int key)
{
    switch (key) {
    case PLAT_KEY_LEFT:  ck_prev_month(ck); break;
    case PLAT_KEY_RIGHT: ck_next_month(ck); break;
    case PLAT_KEY_PGUP:  ck_prev_month(ck); break;
    case PLAT_KEY_PGDN:  ck_next_month(ck); break;
    case PLAT_KEY_HOME:  ck_go_today(ck);   break;
    case PLAT_KEY_UP:    ck_move_days(ck, -7); break;
    case PLAT_KEY_DOWN:  ck_move_days(ck, 7);  break;
    case PLAT_KEY_ENTER: ck_new_appointment(win, ck); return CTRUE;
    case PLAT_KEY_DELETE: ck_delete_selected(ck); break;
    default: return CFALSE;
    }
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool clock_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Clock *ck = (Clock *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       ck_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return ck_click(win, ck, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        CkLayout L;
        if (ck == NULL) { return CFALSE; }
        ck_layout(win, &L);
        if (ui_hot_move(&ck->hot, ck_btn_at(&L, (int)a, (int)b))) {
            CRect br[4];
            br[CKB_PREV] = L.prev; br[CKB_NEXT] = L.next;
            br[CKB_TODAY] = L.today; br[CKB_ADD] = L.agadd;
            ui_hot_repaint(win, &ck->hot, br, 4);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE:
        if (ck != NULL) {
            cbool redraw = ui_hot_release(&ck->hot);
            CkLayout L;
            CRect br[4];
            if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&ck->hot, -1)) {
                redraw = CTRUE;
            }
            ck_layout(win, &L);
            br[CKB_PREV] = L.prev; br[CKB_NEXT] = L.next;
            br[CKB_TODAY] = L.today; br[CKB_ADD] = L.agadd;
            if (redraw) { ui_hot_repaint(win, &ck->hot, br, 4); }
        }
        return CTRUE;
    case WM_MSG_MOUSEWHEEL: {
        CkLayout L;
        int idx[CK_DAY_MAX], n;
        if (ck == NULL) { return CFALSE; }
        ck_layout(win, &L);
        n = ag_day_events(&ck->ag, ck->by, ck->bm, ck->sd, idx, CK_DAY_MAX);
        if (ui_scroll_wheel(&ck->top, (int)a, n, ck_list_rows(&L))) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_union(&L.aglist, &L.agbar);
            r = crect_offset(&r, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return ck_key(win, ck, (int)a);
    case WM_MSG_TIMER:
        if (ck != NULL) {
            int h = 0, m = 0, sec = 0;
            plat_wall_clock(&h, &m, &sec);
            if (sec == ck->last_sec) { return CTRUE; }  /* same picture */
            ck->last_sec = sec;
            if (h == 0 && m == 0 && sec == 0) {
                /* Midnight moved "today": the calendar changes too. */
                wm_invalidate(win, NULL);
            } else {
                CkLayout L;
                CPoint o = wm_client_origin(win);
                CRect r;
                ck_layout(win, &L);
                /* Generous by two pixels: the face's drop shadow sits just
                 * outside the circle it was measured from. */
                r = crect_offset(&L.clock, o.x, o.y);
                r = crect_inset(&r, -2);
                wm_invalidate(win, &r);
                r = crect_offset(&L.lcd, o.x, o.y);
                wm_invalidate(win, &r);
                r = crect_offset(&L.dateline, o.x, o.y);
                wm_invalidate(win, &r);
            }
        }
        return CTRUE;
    case WM_MSG_DESTROY:
        if (ck != NULL) { sys_free(ck, (cu32)sizeof(Clock)); }
        return CTRUE;
    default:
        (void)b;
        return CFALSE;
    }
}

void app_clock_open(void)
{
    Clock *ck;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 300 + CK_PANEL_W, ch = 356, fx, fy;

    ck = (Clock *)sys_calloc(1, (cu32)sizeof(Clock));
    if (ck == NULL) { SYS_LOGE("app", "clock: OOM"); return; }
    ck->sel = -1;
    ck->last_sec = -1;
    ck_go_today(ck);
    ck_ag_load(ck);

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Clock, Calendar & Agenda", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, clock_proc, ck);
    if (w == NULL) { sys_free(ck, (cu32)sizeof(Clock)); return; }

    wm_set_animated(w, CTRUE);
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Clock & Calendar");
}
