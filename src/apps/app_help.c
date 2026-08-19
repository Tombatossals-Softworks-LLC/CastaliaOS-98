/*
 * app_help.c - CastaliaOS Help: a contents list and a scrollable topic.
 *
 * The old Help was a single fixed page that still told you the File Manager
 * opened everything in Notepad. Documentation that lies is worse than none, so
 * this is a real (small) help browser: topics down the left, the text on the
 * right in a scrollable well, arrow keys to move between topics and PgUp/PgDn
 * to scroll one.
 *
 * The text lives in this file as plain lines rather than in a data file on
 * disk, because Help has to work on a machine where the install is incomplete
 * -- that is exactly when somebody reads it.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#define HP_PAD    8
#define HP_LIST_W 132
#define HP_LINE   12

typedef struct {
    const char *title;
    const char *const *lines;
    int count;
} HpTopic;

/* ---- the text ---------------------------------------------------------- */
static const char *const HP_START[] = {
    "CastaliaOS 98 PE",
    "",
    "An original Windows 9x-inspired desktop for DOS, written from",
    "scratch in portable C. No Microsoft code, art or branding.",
    "",
    "Getting around",
    "  Single-click a desktop icon to select it, double-click to open.",
    "  Right-click the desktop for Refresh, Line up Icons, System",
    "  Information, Control Center and About.",
    "  The Castalia button opens the launcher; so does Esc or Ctrl+Esc.",
    "  Arrow keys move within and between its two columns, Enter opens.",
    "",
    "Four desktops",
    "  Ctrl+Left and Ctrl+Right switch between them. Windows keep their",
    "  place and state on each one. The taskbar's 1 2 3 4 shows which.",
    "",
    "If something goes wrong",
    "  The Log Viewer holds the session log, newest last. Hold a key at",
    "  boot for Safe Mode (640x480, high contrast, no animations)."
};

static const char *const HP_WINDOWS[] = {
    "Windows and the keyboard",
    "",
    "  Drag the title bar to move a window; drag any edge or corner to",
    "  resize it. Double-click the title bar to maximize and restore.",
    "  On a slower machine, Control Center / Sound & Effects has \"Drag",
    "  windows by outline only\": the window stays put and a rubber band",
    "  follows the pointer, and it moves -- or resizes -- when you let",
    "  go. It is about a tenth of the work moving and a fifteenth",
    "  resizing. Safe Mode uses it whatever the setting says.",
    "  Drag a title bar to the left or right screen edge to tile the",
    "  window to that half, or to the top to maximize -- a preview",
    "  outline shows where it will land before you let go.",
    "",
    "  Arrows       walk the desktop icons (Enter opens one)",
    "  Alt+Tab      switch windows",
    "  Alt+Space    the window menu (see below)",
    "  Alt+F4       close the focused window",
    "  Ctrl+Esc     open the launcher",
    "  Ctrl+Left/Right   previous / next desktop",
    "  F10          open the menu bar of the focused app; the arrows",
    "               walk it, Enter chooses, Esc closes",
    "",
    "  The caret -- the thin bar that says what you type lands here --",
    "  is only ever in the window that has the keyboard. If you cannot",
    "  see one, click the window you meant to type in.",
    "",
    "  The mouse wheel scrolls whatever the POINTER is over -- three",
    "  lines a notch -- without bringing that window to the front or",
    "  taking the keyboard away from the one you are typing in. It",
    "  needs a wheel-aware DOS mouse driver (CuteMouse and its kin);",
    "  without one the wheel simply does nothing and every list still",
    "  scrolls with PgUp, PgDn and its scroll bar.",
    "",
    "  Every push button in the system lights up when the pointer is",
    "  over it and sinks while it is held -- and lifts again if you",
    "  slide off before letting go, which is how to back out of one",
    "  you did not mean to press.",
    "",
    "  A question with two answers marks the one Enter will give with a",
    "  dark ring. Tab, Left and Right move to the other; Enter presses",
    "  whichever is ringed with dots, and Esc always cancels.",
    "",
    "  The window menu holds everything a window can be told to do:",
    "  Restore, Move, Size, Minimize, Maximize, Send to Desktop 1-4,",
    "  and Close. Alt+Space opens it for the focused window;",
    "  right-clicking a title bar or a taskbar button opens it for that",
    "  one -- which is the way to reach a minimized window.",
    "",
    "  Move and Size borrow the arrow keys for a moment: they move or",
    "  resize the window, Enter keeps the result and Esc puts it back",
    "  exactly where it was. This is the way to place a window with no",
    "  mouse at all.",
    "",
    "Text fields everywhere accept Ctrl+C, Ctrl+X and Ctrl+V, and the",
    "clipboard is shared across every app.",
    "",
    "Show Desktop (the first Quick Launch button) minimizes everything",
    "in one click; clicking a taskbar button brings a window back --",
    "and clicking the button of the window you are already in puts it",
    "down again."
};

static const char *const HP_FILES[] = {
    "File Manager",
    "",
    "  Details, Tiles and Icons views (the View menu cycles them). The",
    "  Icons view shows a thumbnail of every bitmap in the folder.",
    "  Home and End jump to the first and last entry.",
    "",
    "  Double-click or Enter opens a file with the app that owns it:",
    "    .TXT .LOG .MD .C .H      Notepad",
    "    .BMP                     Paint",
    "    .CSV                     CastaliaSheet",
    "    .DOC .WRI                CastaliaWrite",
    "    .WAV                     Media Player",
    "    anything else            the Viewer (image, or a hex dump)",
    "  A .EXE, .COM or .BAT is named rather than launched -- run it",
    "  deliberately from Start / DOS Program.",
    "",
    "  Find (F3 or the toolbar) looks at NAMES and at what is INSIDE",
    "  files, case-insensitively, so it finds a document whether you",
    "  remember what it is called or only what it says. A match found",
    "  inside a file shows the line it is on. It reads at most 64K of",
    "  any one file and opens at most 400 of them; when either limit",
    "  bites, the status line says so -- \"no matches\" and \"I stopped",
    "  looking\" are different answers.",
    "",
    "  Copy, Cut, Paste, Rename, New folder, Find, and Delete (which",
    "  moves to the Recycle Bin rather than destroying anything).",
    "  F2 renames the selected item. F5 re-reads the folder, keeping",
    "  the highlight on the same file. F10 opens its menu -- Properties,",
    "  Compress, View Bytes, Back Up -- with arrows and Enter, so none",
    "  of it needs a mouse.",
    "  Compressing, backing up and restoring all ask before they write",
    "  over a file that is already there, and name the one at risk.",
    "  Inside the Recycle Bin that menu also offers Restore, which puts",
    "  the item back in the folder it was deleted from, under the name",
    "  it had. Two files of the same name from different folders each",
    "  go back to their own. If something is already sitting there it",
    "  is not overwritten -- the restored file gets a ~1 on its name",
    "  and the status line says so.",
    "  2-Pane splits the window in two; Tab switches sides, and a file",
    "  can be dragged from one pane to the other.",
    "",
    "File Compare -- what changed between two text files",
    "  Pick two files and it lists every difference: the lines only in",
    "  one, the lines only in the other, and the ones that changed, each",
    "  with its line number. F2 and F3 pick the files, Enter compares.",
    "  A DOS file and a Unix file with the same text compare EQUAL --",
    "  the line ending is not a difference anybody wants reported.",
    "  The Console does the same job with: fc FILE1 FILE2",
    "",
    "Open and Save As, in every app, browse a real folder listing:",
    "folders first, an extension filter, and full keyboard navigation.",
    "",
    "Notepad -- the plain-text editor",
    "  File / Edit / Search menus over the same commands the toolbar",
    "  buttons run. Word wrap, Find and Find Next, a Ln:Col readout,",
    "  and undo sixty-four edits deep.",
    "  Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Z / Ctrl+Y undo and",
    "  redo, Ctrl+X / C / V cut, copy and paste, Ctrl+A select all,",
    "  Ctrl+F find, F3 find next."
};

static const char *const HP_OFFICE[] = {
    "CastaliaWrite and CastaliaSheet",
    "",
    "CastaliaWrite -- a word processor",
    "  Bold, italic and underline per character; left, centre and right",
    "  alignment per paragraph. A ruler, a page on a gray workspace, and",
    "  Find / Replace. Documents save to a plain, repairable markup.",
    "  Ctrl+B bold, Ctrl+U underline, Ctrl+Z / Ctrl+Y undo and redo,",
    "  Ctrl+A select all, Ctrl+F find, Ctrl+S save, Ctrl+O open. Italic",
    "  has no shortcut: Ctrl+I and Tab are the same key to a PC.",
    "",
    "CastaliaSheet -- a spreadsheet",
    "  Three sheets per workbook. Formulas start with '=' and take cell",
    "  references and ranges:",
    "    =A1+B2*2      =SUM(A1:A10)     =AVG(B1:B9)",
    "    =MIN =MAX =COUNT =ABS =INT =ROUND",
    "  A circular reference is reported as #CYCLE! rather than hanging.",
    "  Files save as CSV with the formulas preserved.",
    "  Ctrl+Z / Ctrl+Y undo and redo, Ctrl+X / C / V move a cell,",
    "  Ctrl+S save, Ctrl+O open -- all of them while not mid-edit,",
    "  where they would be characters instead.",
    "",
    "CastaliaPaint -- a raster editor",
    "  Eleven tools: pencil, brush, airbrush, eraser, fill, colour",
    "  picker, line, rectangle, ellipse, text and a selection.",
    "  Left click draws with the foreground colour; right-click a swatch",
    "  to set the background one (the eraser paints with it).",
    "  Ctrl+Z / Ctrl+Y undo and redo three deep; Ctrl+X / C / V cut,",
    "  copy and paste the selection. Image / Stretch resizes."
};

static const char *const HP_DESK[] = {
    "The rest of the desk",
    "",
    "  Clock, Calendar & Agenda -- a live analog clock, a monthly",
    "    calendar, and appointments for the day you pick. Days with",
    "    something on them carry a dot. Enter adds, Del removes.",
    "  Media Player -- WAV playback with a live visualizer, a playlist",
    "    you can reorder, shuffle, repeat, and a three-band equalizer.",
    "  Network -- adapter status, the TCP/IP settings, the ARP table",
    "    and a ping that resolves the address first.",
    "  Control Center -- every setting, and all of it keyboard-driven:",
    "    arrows pick a category, Tab moves into the panel, arrows walk",
    "    its rows and Enter chooses. Left/Right step the clock fields.",
    "  Theme Editor -- every colour the shell draws with, an RGB mixer",
    "    and a live preview. Apply changes the running desktop; Save",
    "    writes a theme file you can keep or send to somebody.",
    "  Console -- a DOS-flavoured shell: dir, cd, type, del, mem, ver,",
    "    date, time, echo, cls, and tree, with history and scrollback.",
    "    tree draws the folders below you; it stops at six levels or two",
    "    hundred lines and says so rather than trailing off silently.",
    "  Capture Screen (right-click the desktop) writes what is on it to",
    "    PHOTOS as the next SHOTnnnn.BMP -- open it in Paint.",
    "  Character Map, Calculator, Task Manager, Log Viewer, Viewer,",
    "  System Information, Benchmark Suite.",
    "  Games, for when the work is done: Mines, Solitaire, FreeCell",
    "    and Reversi. Reversi marks the squares you may play, says so",
    "    when a side has to pass, and H offers the move it would make",
    "    in your place -- arrows and Enter, or the mouse. N starts a",
    "    new game in any of them."
};

static const char *const HP_WHERE[] = {
    "Where things live",
    "",
    "Everything is under the install root (CASTALIA_HOME, normally",
    "C:\\CASTALIA), and all of it is plain text you may edit by hand:",
    "",
    "  SYS\\CASTALIA.INI   settings: theme, wallpaper, clock, boot",
    "  SYS\\AGENDA.TXT     appointments, one line each",
    "  THEMES\\            saved themes (.INI)",
    "  DOCS\\              documents and spreadsheets",
    "  MEDIA\\             sounds the Media Player finds at startup",
    "  LOGS\\              the session log",
    "  TRASH\\             the Recycle Bin",
    "  TRASH\\TRASH.IDX    where each binned item came from, one line",
    "                     each, so Restore knows where to put it back",
    "  APPS\\              .CAPP add-on packages",
    "",
    "Nothing is hidden in a binary registry. If a setting is wrong you",
    "can fix it with any editor, including the one in this system.",
    "",
    "The Control Center edits the same file through a UI: theme,",
    "wallpaper, screen saver, clock format and boot options."
};

static const HpTopic HP_TOPICS[] = {
    { "Getting started", HP_START,   (int)(sizeof(HP_START) / sizeof(char *)) },
    { "Windows & keys",  HP_WINDOWS, (int)(sizeof(HP_WINDOWS) / sizeof(char *)) },
    { "Files",           HP_FILES,   (int)(sizeof(HP_FILES) / sizeof(char *)) },
    { "Write & Sheet",   HP_OFFICE,  (int)(sizeof(HP_OFFICE) / sizeof(char *)) },
    { "The other apps",  HP_DESK,    (int)(sizeof(HP_DESK) / sizeof(char *)) },
    { "Where things live", HP_WHERE, (int)(sizeof(HP_WHERE) / sizeof(char *)) }
};
#define HP_COUNT (int)(sizeof(HP_TOPICS) / sizeof(HP_TOPICS[0]))

typedef struct {
    int topic;
    int top;        /* first visible line of the topic */
} Help;

typedef struct { CRect list, text, bar; } HpLayout;

static void hp_layout(WmWindow *win, HpLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    L->list = crect_make(HP_PAD, HP_PAD, HP_LIST_W, ch - 2 * HP_PAD);
    L->text = crect_make(HP_PAD + HP_LIST_W + 8, HP_PAD,
                         cw - HP_LIST_W - 3 * HP_PAD - 8, ch - 2 * HP_PAD);
    L->bar = crect_make(L->text.x1 - UI_SB_W - 1, L->text.y0 + 1, UI_SB_W,
                        crect_h(&L->text) - 2);
}

static int hp_rows(const HpLayout *L)
{
    int n = (crect_h(&L->text) - 6) / HP_LINE;
    return (n < 1) ? 1 : n;
}

static void hp_paint(WmWindow *win, GfxSurface *s)
{
    Help *h = (Help *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    HpLayout L;
    CRect c, r, in;
    const HpTopic *t;
    int rows, i;

    if (h == NULL) { return; }
    hp_layout(win, &L);
    c = wm_client_rect(win);
    gfx_fill_rect(s, &c, p->face);
    t = &HP_TOPICS[h->topic];
    rows = hp_rows(&L);
    if (h->top > t->count - rows) { h->top = t->count - rows; }
    if (h->top < 0) { h->top = 0; }

    /* Contents. */
    r = crect_offset(&L.list, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    for (i = 0; i < HP_COUNT; i++) {
        CRect row = crect_make(r.x0 + 2, r.y0 + 2 + i * 16, crect_w(&r) - 4, 16);
        CColor tc = p->text;
        if (i == h->topic) {
            gfx_fill_rect(s, &row, p->accent);
            tc = p->accent_text;
        }
        gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 6, row.y0 + 4,
                      HP_TOPICS[i].title, tc);
    }

    /* The topic. */
    r = crect_offset(&L.text, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    in = crect_inset(&r, 3);
    {
        int w = crect_w(&in) - ((t->count > rows) ? UI_SB_W + 2 : 0);
        CRect saved = gfx_clip_narrow(s, &in);
        for (i = 0; i < rows && h->top + i < t->count; i++) {
            const char *line = t->lines[h->top + i];
            /* The first line of a topic, and any line that starts hard left,
             * is a heading -- that is all the structure this needs. */
            GfxFontId font = (line[0] != '\0' && line[0] != ' ')
                           ? GFX_FONT_BOLD : GFX_FONT_SYSTEM;
            gfx_draw_text(s, font, in.x0 + 3, in.y0 + 2 + i * HP_LINE,
                          line, p->text);
        }
        (void)w;
        gfx_set_clip(s, &saved);
    }
    if (t->count > rows) {
        CRect bar = crect_offset(&L.bar, o.x, o.y);
        ui_scrollbar_draw(s, &bar, CTRUE, t->count, rows, h->top, UI_SB_NONE);
    }
}

static cbool hp_click(WmWindow *win, Help *h, int px, int py)
{
    HpLayout L;
    int rows;
    hp_layout(win, &L);
    rows = hp_rows(&L);
    if (crect_contains(&L.list, px, py)) {
        int row = (py - L.list.y0 - 2) / 16;
        if (row >= 0 && row < HP_COUNT && row != h->topic) {
            h->topic = row;
            h->top = 0;
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (HP_TOPICS[h->topic].count > rows && crect_contains(&L.bar, px, py)) {
        int part = ui_scrollbar_hit(&L.bar, CTRUE, HP_TOPICS[h->topic].count,
                                    rows, h->top, px, py);
        if (part == UI_SB_LINE_UP)        { h->top--; }
        else if (part == UI_SB_LINE_DOWN) { h->top++; }
        else if (part == UI_SB_PAGE_UP)   { h->top -= rows; }
        else if (part == UI_SB_PAGE_DOWN) { h->top += rows; }
        else if (part == UI_SB_THUMB) {
            h->top = ui_scroll_pos_from_coord(crect_h(&L.bar),
                                              HP_TOPICS[h->topic].count,
                                              rows, py - L.bar.y0);
        }
        /* The text and the bar; the topic list beside them did not move. */
        {
            CPoint o = wm_client_origin(win);
            CRect r = crect_union(&L.text, &L.bar);
            r = crect_offset(&r, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    return CFALSE;
}

static cbool hp_key(WmWindow *win, Help *h, int key)
{
    HpLayout L;
    int rows;
    hp_layout(win, &L);
    rows = hp_rows(&L);
    switch (key) {
    case PLAT_KEY_UP:
        if (h->topic > 0) { h->topic--; h->top = 0; }
        break;
    case PLAT_KEY_DOWN:
        if (h->topic < HP_COUNT - 1) { h->topic++; h->top = 0; }
        break;
    case PLAT_KEY_PGUP:  h->top -= rows; break;
    case PLAT_KEY_PGDN:  h->top += rows; break;
    case PLAT_KEY_HOME:  h->top = 0; break;
    case PLAT_KEY_END:   h->top = HP_TOPICS[h->topic].count; break;
    default: return CFALSE;
    }
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool help_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Help *h = (Help *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       hp_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return hp_click(win, h, (int)a, (int)b);
    case WM_MSG_MOUSEWHEEL: {
        HpLayout L;
        if (h == NULL) { return CFALSE; }
        hp_layout(win, &L);
        if (ui_scroll_wheel(&h->top, (int)a, HP_TOPICS[h->topic].count,
                            hp_rows(&L))) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_union(&L.text, &L.bar);
            r = crect_offset(&r, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return hp_key(win, h, (int)a);
    case WM_MSG_DESTROY:
        if (h != NULL) { sys_free(h, (cu32)sizeof(Help)); }
        return CTRUE;
    default:
        (void)b;
        return CFALSE;
    }
}

void app_help_open(void)
{
    Help *h;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = 620, fh = 356, fx, fy;

    h = (Help *)sys_calloc(1, (cu32)sizeof(Help));
    if (h == NULL) { SYS_LOGE("app", "help: OOM"); return; }

    plat_video_info(&vi);
    if (fw > vi.width)  { fw = vi.width; }
    if (fh > vi.height) { fh = vi.height; }
    fx = (vi.width - fw) / 2;
    fy = (vi.height - fh) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);

    w = wm_create("Help", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, help_proc, h);
    if (w == NULL) { sys_free(h, (cu32)sizeof(Help)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Help (%d topics)", HP_COUNT);
}
