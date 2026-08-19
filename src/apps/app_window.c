/*
 * app_window.c - Generic multi-line text window used by About, System Info,
 *                and roadmap notices. A minimal but genuine WM app: it owns a
 *                heap payload, paints in client coordinates, and cleans up on
 *                destroy (init/shutdown symmetry).
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

/* Room for a full machine report with a PCI bus on it. The window is not
 * obliged to DRAW them all (see the screen fit below) but it must HOLD them,
 * because F2 writes what is held. At 96 bytes a line this is 6 KB of heap
 * while a text window is open, against an 8 MB budget. */
#define APP_MAX_LINES 64
#define APP_LINE_MAX  96

/* Stamped into the payload so "is the focused window a text window?" is a
 * question the headless driver can actually answer, rather than a cast that
 * would read some other app's struct as line counts. Same reason as AR_MAGIC
 * in app_archive.c. */
#define AT_MAGIC 0x54455831UL          /* 'TEX1' */

typedef struct {
    cu32 magic;
    int  nlines;   /* lines held -- what F2 writes                         */
    int  nshown;   /* lines that fit this screen -- what gets drawn        */
    char lines[APP_MAX_LINES][APP_LINE_MAX];
    /* Where F2 writes the text, or "" when this window has nothing worth
     * keeping. A window that can be saved says so on its last line. */
    char save_path[CASTALIA_MAX_PATH];
} AppText;

/*
 * Write the window's text out verbatim.
 *
 * The point of this is a machine that will not boot properly: read what is on
 * the screen, save it, and take the file away on the floppy that got you
 * there. It writes every line the window HOLDS, not the subset it can draw --
 * the two differ precisely when the machine dropped to a small video mode,
 * which is the case where the report matters most and where the screen shows
 * least. Otherwise the file would be as truncated as the display and the
 * exercise would be pointless. No header this program invented and no
 * reformatting: the value of the file is that it is the same evidence.
 */
static cbool app_text_save(const AppText *t)
{
    PlatFile *f;
    int i;
    if (t == NULL || t->save_path[0] == '\0') { return CFALSE; }
    f = plat_fopen(t->save_path, "wb");
    if (f == NULL) {
        SYS_LOGE("app", "info: cannot write %s", t->save_path);
        return CFALSE;
    }
    for (i = 0; i < t->nlines; i++) {
        cu32 n = sys_strnlen(t->lines[i], APP_LINE_MAX);
        if (n > 0u && plat_fwrite(f, t->lines[i], n) != n) {
            plat_fclose(f);
            SYS_LOGE("app", "info: short write to %s", t->save_path);
            return CFALSE;
        }
        /* CRLF: this file is read on DOS, by EDIT and by TYPE. */
        if (plat_fwrite(f, "\r\n", 2u) != 2u) {
            plat_fclose(f);
            SYS_LOGE("app", "info: short write to %s", t->save_path);
            return CFALSE;
        }
    }
    if (plat_fclose(f) != CE_OK) {
        SYS_LOGE("app", "info: close failed on %s", t->save_path);
        return CFALSE;
    }
    SYS_LOGI("app", "info: saved %d line(s) to %s", t->nlines, t->save_path);
    return CTRUE;
}

static cbool app_text_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    AppText *t = (AppText *)wm_user(win);
    CASTALIA_UNUSED(b);
    switch (msg) {
    case WM_MSG_KEYDOWN:
        if (t != NULL && t->save_path[0] != '\0' && a == PLAT_KEY_F2) {
            char note[CASTALIA_MAX_PATH + 64];
            if (app_text_save(t)) {
                sys_snprintf(note, sizeof(note), "Saved to\n%s", t->save_path);
                ui_msgbox("Report Saved", note, UI_MB_OK, NULL, NULL);
            } else {
                sys_snprintf(note, sizeof(note),
                             "Could not write\n%s", t->save_path);
                ui_msgbox("Save Failed", note, UI_MB_OK, NULL, NULL);
            }
            return CTRUE;
        }
        return CFALSE;
    case WM_MSG_PAINT: {
        GfxSurface *s = (GfxSurface *)param;
        const UiPalette *p = ui_palette();
        CPoint o = wm_client_origin(win);
        CRect  c = wm_client_rect(win);
        int lh = gfx_font_height(GFX_FONT_SYSTEM) + 2;
        int y = o.y + 8;
        int i;
        char note[APP_LINE_MAX];
        /* A sunken content well for a document feel. */
        gfx_bevel(s, &c, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
        if (t == NULL) { return CTRUE; }
        for (i = 0; i < t->nshown; i++) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 10, y, t->lines[i], p->text);
            y += lh;
        }
        /* The two footers are drawn rather than stored, so that what F2 writes
         * stays the report and does not acquire notes about the report. */
        if (t->nshown < t->nlines) {
            sys_snprintf(note, sizeof(note), "... %d more line(s) below",
                         t->nlines - t->nshown);
            gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 10, y, note, p->text_disabled);
            y += lh;
        }
        if (t->save_path[0] != '\0') {
            sys_snprintf(note, sizeof(note), "Press F2 to save the full report"
                         " to %s", t->save_path);
            gfx_draw_text(s, GFX_FONT_SYSTEM, o.x + 10, y, note, p->text_disabled);
        }
        return CTRUE;
    }
    case WM_MSG_DESTROY:
        if (t != NULL) { sys_free(t, (cu32)sizeof(AppText)); }
        return CTRUE;
    default:
        return CFALSE;
    }
}

void app_info_open(const char *title, const char *const *lines, int nlines)
{
    app_info_open_ex(title, lines, nlines, NULL);
}

void app_info_open_ex(const char *title, const char *const *lines, int nlines,
                      const char *save_path)
{
    AppText *t;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int i, maxw = 0;
    int cw, ch, lh, fits, drawn, footers = 0;

    plat_video_info(&vi);
    lh = gfx_font_height(GFX_FONT_SYSTEM) + 2;

    t = (AppText *)sys_calloc(1, (cu32)sizeof(AppText));
    if (t == NULL) { SYS_LOGE("app", "info: OOM"); return; }
    t->magic = AT_MAGIC;
    if (save_path != NULL && save_path[0] != '\0') {
        sys_strlcpy(t->save_path, save_path, sizeof(t->save_path));
        footers++;                       /* the "press F2" line            */
    }
    for (i = 0; i < nlines && i < APP_MAX_LINES; i++) {
        sys_strlcpy(t->lines[t->nlines], (lines[i] ? lines[i] : ""), APP_LINE_MAX);
        t->nlines++;
    }
    /* Past this the lines are gone -- not off the bottom of the screen, gone,
     * and not in the saved file either. Nothing in this system hands over
     * that many, so this is a tripwire rather than a policy, and it says so
     * instead of quietly keeping the first sixty-four. */
    if (nlines > APP_MAX_LINES) {
        SYS_LOGW("app", "info '%s': %d line(s) given, %d kept", title,
                 nlines, APP_MAX_LINES);
    }

    /*
     * How many of those fit on THIS screen, rather than how many the array
     * holds.
     *
     * The array bound used to be the only limit, and it was the wrong one:
     * forty lines is 584 pixels, which runs off the bottom of a 640x480 mode
     * -- the fallback the DOS backend drops to on exactly the unfamiliar
     * hardware whose System Information somebody needs to read. The window
     * drew all forty and the screen simply ended. Now the drawing stops where
     * the screen does, the count of what is below is shown, and F2 still
     * writes every line.
     */
    {
        /*
         * ...measured against the WORK area, not the screen. The screen
         * includes the taskbar, so this allowed one taskbar's worth of lines
         * too many and the window opened ten pixels past the bottom of the
         * usable desktop -- with its last line, and the "press F2" hint,
         * underneath the bar.
         */
        CRect wa = wm_work_area();
        fits = (crect_h(&wa) - 28 - 24 - 16) / lh;
    }
    if (fits < 3) { fits = 3; }
    drawn = t->nlines;
    if (drawn + footers > fits) {
        /* Room for the "N more below" line as well, since it appears exactly
         * when this branch is taken. */
        drawn = fits - footers - 1;
        if (drawn < 1) { drawn = 1; }
        footers++;
    }
    t->nshown = drawn;

    for (i = 0; i < t->nshown; i++) {
        int lw = gfx_text_width(GFX_FONT_SYSTEM, t->lines[i]);
        if (lw > maxw) { maxw = lw; }
    }
    if (t->save_path[0] != '\0') {
        /* The hint carries a full path and is routinely the widest line, so
         * measuring only the content would clip it. */
        char hint[APP_LINE_MAX];
        int lw;
        sys_snprintf(hint, sizeof(hint),
                     "Press F2 to save the full report to %s", t->save_path);
        lw = gfx_text_width(GFX_FONT_SYSTEM, hint);
        if (lw > maxw) { maxw = lw; }
    }

    /* Size the client to what will actually be drawn, then center on screen. */
    cw = maxw + 40;
    if (cw < 220) { cw = 220; }
    ch = (t->nshown + footers) * lh + 24;
    if (ch < 80) { ch = 80; }

    frame = wm_place_centered(cw + 8, ch + 28);

    w = wm_create(title, &frame, WM_STYLE_TITLE | WM_STYLE_CLOSE |
                                 WM_STYLE_MINIMIZE | WM_STYLE_BORDER,
                  app_text_proc, t);
    if (w == NULL) { sys_free(t, (cu32)sizeof(AppText)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened info window '%s'", title);
}

/* Only a genuine text window answers; anything else gives -1 rather than
 * whatever integer happens to sit at that offset in another app's payload. */
static AppText *at_of(WmWindow *win)
{
    AppText *t = (win != NULL) ? (AppText *)wm_user(win) : NULL;
    return (t != NULL && t->magic == AT_MAGIC) ? t : NULL;
}

int app_info_lines_held(WmWindow *win)
{
    const AppText *t = at_of(win);
    return (t != NULL) ? t->nlines : -1;
}

int app_info_lines_shown(WmWindow *win)
{
    const AppText *t = at_of(win);
    return (t != NULL) ? t->nshown : -1;
}

void app_planned_open(const char *feature, int phase)
{
    char l0[APP_LINE_MAX], l2[APP_LINE_MAX];
    const char *lines[5];
    sys_snprintf(l0, sizeof(l0), "%s", feature);
    sys_snprintf(l2, sizeof(l2), "Scheduled for Phase %d of the roadmap.", phase);
    lines[0] = l0;
    lines[1] = "";
    lines[2] = l2;
    lines[3] = "The launcher entry is wired; the full app arrives";
    lines[4] = "in a later phase. See docs/BACKLOG.md.";
    app_info_open(feature, lines, 5);
}
