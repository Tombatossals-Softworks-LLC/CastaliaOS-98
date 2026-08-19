/*
 * ui.h - Layer 3 common controls and menus.
 *
 * The set has grown with the shell rather than being planned up front, so it
 * is what the applications actually needed: push buttons, menus, checkboxes,
 * radio buttons, a scroll bar, a listbox, a single-line edit, message boxes,
 * a prompt, a file dialog and a meter. Tree, tabs, slider and group box are
 * not here -- nothing has needed one yet, and a control with no caller is a
 * control with no design pressure on it. (The Media Player and About window
 * draw their own sliders and tabs, which is where a general version would come
 * from if a third caller appeared.)
 *
 * Controls here are drawn by helper functions rather than being heavyweight
 * windows; the shell and dialogs lay them out and route hits. This keeps
 * the idle footprint small.
 */
#ifndef CASTALIA_UI_H
#define CASTALIA_UI_H

#include "castalia/ctypes.h"
#include "castalia/rect.h"
#include "castalia/gfx.h"

/* ---- Push button ------------------------------------------------------ */
typedef enum {
    UI_BTN_NORMAL = 0,
    UI_BTN_HOVER,
    UI_BTN_PRESSED,
    UI_BTN_DISABLED
} UiButtonState;

/* Draw a classic beveled push button with centered label. */
void ui_draw_button(GfxSurface *s, const CRect *r, const char *label,
                    UiButtonState state);

/*
 * Which button of a strip the pointer is on, and which one it went down on.
 *
 * ui_draw_button has drawn UI_BTN_HOVER and UI_BTN_PRESSED since it was
 * written and almost nothing ever asked for either. The Calculator's whole
 * keypad, the File Manager's toolbar, the Viewer's, the Theme editor's and
 * the Control Centre's buttons were all drawn UI_BTN_NORMAL unconditionally,
 * so no button in any of those windows ever lit under the pointer or sank
 * when it was pressed. Two windows -- CastaliaSheet and CastaliaWrite --
 * tracked a hovered toolbar button with a field of their own, which is why
 * this is one shared thing rather than a sixth copy of it.
 *
 * Indices are whatever the caller numbers its buttons with; -1 is none.
 * Nothing here knows about rectangles: hit-testing stays with the layout
 * that owns the geometry.
 */
typedef struct {
    /*
     * Both are the index PLUS ONE, so that zero means "none".
     *
     * Every app struct in this system is sys_calloc'd, and a UiHot whose
     * sentinel were -1 would come back from that allocation saying button
     * zero is under the pointer AND held down -- a toolbar drawn with its
     * first button sunken until somebody touched it, on every window, with
     * nothing in any of them to hint at why. The API below is in ordinary
     * indices; the encoding stops at this struct.
     */
    int hot1;
    int down1;
    int was1;   /* what hot1 was before the last move, for the repaint */
} UiHot;

void  ui_hot_init(UiHot *h);
/* The pointer is now over button 'idx' (-1: none, including having left the
 * window). CTRUE when what should be DRAWN changed -- the caller's cue to
 * invalidate, and the reason a repaint does not happen on every mouse move. */
cbool ui_hot_move(UiHot *h, int idx);
cbool ui_hot_press(UiHot *h, int idx);
cbool ui_hot_release(UiHot *h);
/* Which button a press started on, or -1 when none is held. For the callers
 * that act on the RELEASE -- the only ones that let you slide off a button
 * you did not mean to press. */
int   ui_hot_pressed(const UiHot *h);
/*
 * The two buttons whose drawing just changed: the one the pointer left and
 * the one it arrived on, either of which may be -1.
 *
 * A caller that repaints its whole window on a hover transition sweeps ten
 * full repaints across a ten-button toolbar -- measured at 112% of the
 * client per transition on the Control Centre, and the pointer crosses a
 * toolbar in one gesture. These two are what actually has to come back.
 */
int   ui_hot_left(const UiHot *h);
int   ui_hot_hot(const UiHot *h);
/*
 * ...and the repaint that goes with them: invalidate just those two, given
 * the caller's button rectangles in CLIENT coordinates. Generous by two
 * pixels, because a button's default ring is drawn outside the rectangle the
 * button is drawn in. See ui_hotpaint.c.
 */
struct WmWindow;
void  ui_hot_repaint(struct WmWindow *win, const UiHot *h,
                     const CRect *rects, int n);
/* The state button 'i' should be drawn in, given what it would be with no
 * pointer involved: UI_BTN_NORMAL for an ordinary one, UI_BTN_DISABLED for
 * one that cannot be used, UI_BTN_PRESSED for one that is latched on. */
UiButtonState ui_hot_state(const UiHot *h, int i, UiButtonState base);

/*
 * The two marks a push button carries beyond its own state.
 *
 * A message box asking whether to throw away unsaved work drew Yes and No
 * identically. Which one Enter would press was not on the screen anywhere,
 * and nothing but Esc reached the other -- on a machine where the mouse is
 * optional, the answer to "lose it?" was whichever one somebody guessed.
 *
 * DEFAULT is the dark ring just outside the bevel: this is what Enter does.
 * FOCUS is the dotted ring inside it: this is what Enter does NOW, having
 * been moved. Both are drawn OVER a button that is already drawn, so they
 * compose with hover and pressed rather than multiplying the states.
 */
void ui_draw_button_default(GfxSurface *s, const CRect *r);
void ui_draw_button_focus(GfxSurface *s, const CRect *r);

/*
 * The modal dialog now up, for the headless driver: the SCREEN rect of one of
 * its buttons, how many it has, and which one Enter would press right now.
 * CFALSE / -1 when there is no dialog. Which button Enter answers is not
 * something a screenshot can be asked, and it is the whole point of the two
 * rings above.
 */
cbool ui_dialog_btn_rect(int i, CRect *out);
int   ui_dialog_btn_count(void);
int   ui_dialog_focus(void);

/* ---- Checkbox / radio (stateless: the caller owns the checked state) -- */
/* Draw a checkbox and its label within row 'r' (box on the left, label to the
 * right, vertically centered). 'checked' fills the box with a check glyph. */
void ui_draw_check(GfxSurface *s, const CRect *r, const char *label,
                   cbool checked, UiButtonState state);
/* Draw a radio button (a small ring; filled center when 'selected'). */
void ui_draw_radio(GfxSurface *s, const CRect *r, const char *label,
                   cbool selected, UiButtonState state);

/* ---- Meter (progress / gauge) ---------------------------------------- */
/*
 * A meter: a sunken well with a proportional fill. The About window's source
 * statistics, the Task Manager's memory gauge, the Benchmark score bars and
 * the boot splash each drew their own, with their own clamping.
 *
 * ui_meter_fill is the arithmetic on its own, because that is the part that
 * was wrong. value * width overflows 32 bits far sooner than it looks: the
 * memory gauge measures BYTES against an 8 MB budget across a 386-pixel bar,
 * so the product passed 2^31 at 5.31 MB, wrapped negative, and clamped to
 * zero -- the bar read EMPTY for the top third of its own range, saying "no
 * memory in use" exactly when memory was nearly gone. It returns the fill
 * width in pixels, clamped to [0, width], for any inputs at all.
 *
 * The arguments are cs32 and NOT long, and that is load-bearing. long is 32
 * bits on the DOS target and 64 on the host, so written with long this bug
 * existed only on the target and no host test could ever reach it -- the
 * first version of this check passed against the broken formula for exactly
 * that reason. Fixing the width makes both platforms compute the same thing
 * and makes the wrap reproducible here.
 */
int  ui_meter_fill(cs32 value, cs32 maxv, int width);
void ui_draw_meter(GfxSurface *s, const CRect *r, cs32 value, cs32 maxv,
                   CColor fill_color);

/*
 * "What percent of 'whole' is 'part'", 0..100.
 *
 * A percentage IS a meter a hundred pixels wide, so this is ui_meter_fill
 * with a width of 100 and not a second implementation -- which matters,
 * because the naive `part * 100 / whole` is the same overflow described above
 * and it kept reappearing in code that draws no meter at all. The File
 * Manager computed a compression ratio that way and wrapped negative once a
 * file shrank by more than 20.5 MB, since 21,474,836 * 100 is past what a
 * 32-bit long holds. Anything asking for a percentage should ask here.
 */
int  ui_percent(cs32 part, cs32 whole);

/* ---- directional triangle -------------------------------------------- */
/*
 * A solid triangle filling 'r', apex pointing the way 'dir' says:
 * UI_TRI_RIGHT, UI_TRI_LEFT, UI_TRI_UP or UI_TRI_DOWN.
 *
 * It exists because "which way does it point" is a one-character difference
 * in a height formula and looks identical in review. The Media Player wrote
 * that formula out three times and got two of them backwards -- its Play
 * button pointed left, and Next was drawn the same as Prev -- both plainly
 * wrong on screen for as long as the deck existed. The scroll bar, which has
 * taken the direction as an argument since it was written, has all four of
 * its arrows right. That is the whole argument for this function.
 */
enum { UI_TRI_RIGHT = 0, UI_TRI_LEFT, UI_TRI_UP, UI_TRI_DOWN };
void ui_draw_tri(GfxSurface *s, const CRect *r, int dir, CColor col);

/* ---- eliding a path to fit --------------------------------------------- */
/*
 * Copy 'path' into 'dst', cut from the LEFT and marked "..." if it needs more
 * than 'width' pixels in 'font'.
 *
 * From the left, deliberately: the tail of a path is the part that says where
 * you are, so a deep path should lose its root rather than its folder. Disk
 * Usage worked this out and did it correctly in its own painting code; the
 * Log Viewer's path bar simply ran off the right edge with no sign that
 * anything had been cut. Shared so the two agree.
 *
 * A command PROMPT is not this and should not use it -- DOS shows the whole
 * path and lets the line wrap, which is what the Console does.
 */
void ui_path_fit(char *dst, cu32 dstsz, const char *path, int width,
                 GfxFontId font);

/*
 * The same for a LABEL, cut from the RIGHT: "LONGFILENAME.TXT" -> "LONGFIL...".
 *
 * The opposite end from ui_path_fit, and for the opposite reason -- a file
 * name is identified by how it starts, a path by where it ends.
 *
 * The File Manager's Icons view had no fitting at all: it centred the raw
 * name in an 82-pixel cell with `(FM_CELL_W - 4 - tw) / 2`, which goes
 * NEGATIVE once the name is wider than the cell. Long names ran outside the
 * list area on the left, past the window on the right, and into each other in
 * between -- three labels abutting as one unreadable run.
 */
void ui_text_fit(char *dst, cu32 dstsz, const char *text, int width,
                 GfxFontId font);


/* ---- Scroll bar ------------------------------------------------------- */
/*
 * A classic beveled scroll bar. The caller owns the model -- 'total' units of
 * content, 'visible' of them on screen, scrolled to 'pos' -- and this control
 * only maps it to pixels, so the same helpers serve a text view, a grid, or a
 * list. The geometry is pure integer math and host-tested.
 */
#define UI_SB_W          15   /* bar thickness / arrow button size          */
#define UI_SB_MIN_THUMB  12   /* a thumb is never smaller than this         */

enum {
    UI_SB_NONE = 0,
    UI_SB_LINE_UP,     /* the arrow at the top / left                       */
    UI_SB_LINE_DOWN,   /* the arrow at the bottom / right                   */
    UI_SB_PAGE_UP,     /* trough above / left of the thumb                  */
    UI_SB_PAGE_DOWN,
    UI_SB_THUMB
};

/* Thumb offset and length within a track of 'track_len' pixels. Returns a
 * full-length thumb when the content fits (total <= visible). */
void ui_scroll_thumb(int track_len, int total, int visible, int pos,
                     int *out_off, int *out_len);

/* Which part of a bar 'bar_len' px long sits under 'coord' (0 = its start).
 * Bars shorter than three button widths drop the arrows. */
int  ui_scroll_part(int bar_len, int total, int visible, int pos, int coord);

/* The scroll position that puts the thumb's middle at 'coord' (dragging). */
int  ui_scroll_pos_from_coord(int bar_len, int total, int visible, int coord);

/* Lines a wheel notch moves. Three, which is what the mice of this era were
 * shipped set to and what every list in the system therefore agrees on. */
#define UI_WHEEL_LINES 3

/*
 * Apply a wheel scroll to a position, clamped. 'notches' is what the mouse
 * reported: positive is away from the user, which scrolls the content UP --
 * so the first visible line goes DOWN in the list. Returns CTRUE only if the
 * position actually moved, so a caller knows whether it has anything to
 * repaint; a wheel turned at the end of a document changes nothing and must
 * not cost a frame.
 *
 * One implementation rather than nine. "Three lines per notch, clamped to
 * the end" is exactly the sort of rule nine copies would come to disagree
 * about -- and one of them would clamp to 'total' instead of to
 * 'total - visible' and scroll the last page off the bottom of the window.
 */
cbool ui_scroll_wheel(int *pos, int notches, int total, int visible);

/* Draw the bar in 'r'. 'hot_part' (a UI_SB_* value) is drawn active. */
void ui_scrollbar_draw(GfxSurface *s, const CRect *r, cbool vertical,
                       int total, int visible, int pos, int hot_part);

/* UI_SB_* under (px,py), or UI_SB_NONE when the point misses the bar. */
int  ui_scrollbar_hit(const CRect *r, cbool vertical,
                      int total, int visible, int pos, int px, int py);

/* ---- Listbox (scrollable list of short strings with a selection) ------ */
#define UI_LIST_MAX 48
typedef struct {
    char items[UI_LIST_MAX][CASTALIA_MAX_NAME];
    int  count;
    int  sel;   /* selected index, or -1              */
    int  top;   /* first visible row (scroll)         */
} UiList;
void    ui_list_clear(UiList *l);
CResult ui_list_add(UiList *l, const char *text);
/* Draw the list in a sunken well 'r'; the selected row uses the accent color. */
void    ui_list_draw(GfxSurface *s, const CRect *r, UiList *l, cbool focused);

/*
 * The band and ink a selection is drawn in, given whether the window has the
 * keyboard. Quiet when it does not: an accent-blue selection says "the arrow
 * keys move this", which is false in a window nobody is typing into. One
 * definition, because five things in this system draw a selection.
 */
void    ui_sel_colors(cbool focused, CColor *band, CColor *ink);
/* Click at (px,py): if it lands on a row, select it. Returns CTRUE if the
 * selection changed. */
cbool   ui_list_click(UiList *l, const CRect *r, int px, int py);
/* Up/Down move the selection (and scroll to keep it visible). CTRUE if moved. */
cbool   ui_list_key(UiList *l, const CRect *r, int key);
int     ui_list_sel(const UiList *l);

/* ---- Menus (launcher + context menus) -------------------------------- */
#define UI_MENU_MAX_ITEMS 24
#define UI_MENU_SEPARATOR_ID (-1)

typedef struct {
    int   id;                 /* command id, or UI_MENU_SEPARATOR_ID       */
    char  label[CASTALIA_MAX_NAME];
    cbool enabled;
    cbool has_submenu;        /* draws a right-pointing arrow (reserved)   */
    const GfxSurface *icon;   /* optional icon drawn in the left gutter    */
} UiMenuItem;

typedef struct {
    UiMenuItem items[UI_MENU_MAX_ITEMS];
    int        count;
    int        highlight;     /* index under the cursor, or -1             */
} UiMenu;

void ui_menu_clear(UiMenu *m);
/* Append an item; returns CE_OVERFLOW if the menu is full. */
CResult ui_menu_add(UiMenu *m, int id, const char *label, cbool enabled);
CResult ui_menu_add_separator(UiMenu *m);
/* Attach an icon to the most recently added item (NULL clears it). Drawn in the
 * left gutter; a NULL icon just leaves the gutter empty. */
void ui_menu_set_last_icon(UiMenu *m, const GfxSurface *icon);

/* Measured pixel size of the menu when drawn at the given font. */
void ui_menu_measure(const UiMenu *m, GfxFontId font, int *out_w, int *out_h);

/* Draw the menu with its top-left at (x,y). The highlighted row is filled
 * with the accent color. */
void ui_menu_draw(GfxSurface *s, const UiMenu *m, int x, int y, GfxFontId font);

/* Given the menu's origin and a point, return the item index under the
 * point, or -1. Separators and disabled items return -1. */
int  ui_menu_hit(const UiMenu *m, int x, int y, GfxFontId font, int px, int py);

/* Whether item 'i' can be chosen: in range, not a separator, and enabled. */
cbool ui_menu_selectable(const UiMenu *m, int i);

/*
 * The rectangle row 'i' is highlighted in, for a menu drawn at (x,y) -- the
 * same rectangle ui_menu_draw fills, so a window can repaint a highlight
 * change instead of repainting itself. CFALSE for a separator or an index
 * outside the menu.
 */
cbool ui_menu_row_rect(const UiMenu *m, int x, int y, GfxFontId font, int i,
                       CRect *out);

/*
 * ...and the repaint that goes with it: the highlight moved from 'prev' to
 * 'now', so invalidate those two rows and nothing else. (mx,my) is the
 * menu's origin in CLIENT coordinates. See ui_hotpaint.c.
 */
void ui_menu_repaint(struct WmWindow *win, const UiMenu *m, int mx, int my,
                     GfxFontId font, int prev, int now);

/*
 * Move the highlight by 'dir' (+1 down, -1 up), skipping separators and
 * disabled rows and wrapping at the ends. Returns CTRUE only if the highlight
 * actually moved, so a caller knows whether it has anything to repaint.
 *
 * This lived twice -- once in the shell's desktop menu and once in the
 * launcher -- and not at all in the File Manager, whose context menu could
 * therefore only be used with a mouse. Keeping one copy is what stops the
 * third caller from being written a fourth time, or forgotten a second.
 */
cbool ui_menu_step(UiMenu *m, int dir);

/* ---- Shared theme colors (populated by the shell theme, see shell.h) -- */
typedef struct {
    CColor face;        /* control face gray                              */
    CColor light;       /* bevel highlight                                */
    CColor dark;        /* bevel shadow                                   */
    CColor darker;      /* outer shadow                                   */
    CColor text;        /* label text                                     */
    CColor text_disabled;
    CColor accent;      /* selection / highlight fill                     */
    CColor accent_text; /* text on accent                                 */
} UiPalette;

/* The active palette pointer, set by the shell at theme load. Controls read
 * from it so a theme switch recolors everything. Never NULL after shell init. */
const UiPalette *ui_palette(void);

/*
 * SECONDARY text: quieter than ui.text, and deliberately not ui.text_disabled.
 *
 * A description under a heading, an unselected tab, a status line -- none of
 * those are disabled, and drawing them in the disabled colour tells somebody
 * they cannot use a thing they can. It was doing exactly that: the Welcome
 * tour described eight working features in the greyed-out tone, and About
 * drew its clickable tabs in it.
 *
 * Derived from the theme rather than stored, so a preset cannot forget it --
 * the same reasoning as the inactive title bar in sh_theme_io.c.
 */
CColor ui_text_dim(void);
void             ui_set_palette(const UiPalette *pal);

/*
 * Whether controls may draw the glossy XP treatment (gradients, sheens, tinted
 * gutters) or must stay flat. The shell sets it alongside the palette, from the
 * same condition the window frames use: truecolor and not safe mode.
 *
 * It is a switch of its own rather than a field in UiPalette because a palette
 * is a set of COLORS -- it is written to and read from a theme file -- and
 * this is a statement about the pipeline underneath, which no theme should be
 * able to claim. Off by default, so anything that draws before a shell exists
 * (and every host test) gets the flat, always-safe path.
 */
void  ui_set_glossy(cbool on);
cbool ui_glossy(void);

/* ---------------------------------------------------------------------- */
/* Modal dialogs (implemented as window-manager windows, non-blocking)    */
/* ---------------------------------------------------------------------- */
/*
 * Dialogs are asynchronous to fit the cooperative single-thread loop: they
 * open a modal window and invoke a callback when dismissed, rather than
 * blocking. A modal window (WM_STYLE_MODAL) captures all input until closed.
 */
#define UI_MB_OK        0x01   /* [OK]                */
#define UI_MB_OKCANCEL  0x02   /* [OK] [Cancel]       */
#define UI_MB_YESNO     0x04   /* [Yes] [No]          */

typedef enum {
    UI_DR_OK = 0,
    UI_DR_CANCEL,
    UI_DR_YES,
    UI_DR_NO
} UiDialogResult;

typedef void (*UiDialogCb)(UiDialogResult result, void *user);

/* Open a modal message box. 'message' may contain '\n' line breaks. When the
 * user picks a button (or presses Enter=default / Esc=cancel), 'cb' is called
 * with the result and the dialog closes. 'cb' may be NULL. */
void ui_msgbox(const char *title, const char *message, int buttons,
               UiDialogCb cb, void *user);

/* ---------------------------------------------------------------------- */
/* Single-line text input control                                         */
/* ---------------------------------------------------------------------- */
#define UI_EDIT_MAX 128

typedef struct {
    char text[UI_EDIT_MAX];
    int  len;
    int  caret;    /* caret index in [0,len]         */
    int  scroll;   /* first visible character        */
} UiEdit;

void  ui_edit_init(UiEdit *e, const char *initial);
const char *ui_edit_text(const UiEdit *e);
/* Draw the field; 'focused' shows the caret. Updates the horizontal scroll to
 * keep the caret visible. */
void  ui_edit_draw(GfxSurface *s, const CRect *r, UiEdit *e, cbool focused);
/* Handle a key (PLAT_KEY_* / ascii). 'mods' is the PLAT_MOD_* bitmask so
 * Ctrl+C/X/V reach the system clipboard. Returns CTRUE if the content changed
 * or the key was consumed. */
cbool ui_edit_key(UiEdit *e, int key, int ch, int mods);
/* Move the caret to the character nearest to pixel x within rect r. */
void  ui_edit_click(UiEdit *e, const CRect *r, int px);

/* Prompt dialog: a modal with a label, a text field, and OK/Cancel. Calls
 * 'cb' with ok=CTRUE and the entered text, or ok=CFALSE on cancel. */
typedef void (*UiPromptCb)(cbool ok, const char *text, void *user);
void ui_prompt(const char *title, const char *label, const char *initial,
               UiPromptCb cb, void *user);

/* ---------------------------------------------------------------------- */
/* File dialog                                                            */
/* ---------------------------------------------------------------------- */
/*
 * "Browse for a file" instead of "type the path and hope": a modal listing of
 * a folder with a filename field, used by every app that opens or saves.
 *
 * The path helpers below are pure (ui_path.c) and host-tested in
 * tests/test_path.c; both separators are accepted on input.
 */

/* Join 'dir' and 'name' with one separator (the one 'dir' already uses). An
 * absolute 'name' replaces the directory entirely. */
void  ui_path_join(char *out, cu32 outsz, const char *dir, const char *name);
/*
 * The part of a path after the last separator -- a pointer into the original,
 * never a copy.
 *
 * This existed four times under four names (cz_base, base_name in the File
 * Manager, base_name in the Media Player, pr_base in Properties) and a fifth
 * time inline in the Hex Viewer, all identical. It belongs beside the other
 * path helpers, which are tested.
 *
 * Only '/' and '\\' separate. A drive-relative "C:FILE.TXT" therefore comes
 * back whole rather than as "FILE.TXT" -- which is what all five copies did,
 * and is kept deliberately: no caller in this system builds such a path, and
 * quietly changing behaviour while removing duplicates is how a tidy-up
 * turns into a bug.
 */
const char *ui_path_base(const char *path);
/* Does 'name' already carry a location -- absolute, drive-qualified, or with
 * a folder in it? An app that would otherwise put a bare name in its own
 * documents folder must leave a complete path alone. */
cbool ui_path_is_complete(const char *name);

/*
 * Place 'name' in 'dir' unless it already says where it goes.
 *
 * Every app with a Save has this rule -- the Spreadsheet, the Writer and the
 * Theme Editor each had their own copy, differing only in the folder name.
 * The direction is the part worth stating: a name the user picked from
 * somewhere else must be written back THERE, not copied into the app's own
 * folder, which would leave the original untouched and the edit apparently
 * lost.
 */
void ui_path_default_dir(char *out, cu32 outsz, const char *dir,
                         const char *name);
/* Walk one level up, in place. CFALSE when already at a root ("/", "C:\"). */
cbool ui_path_up(char *path);

/*
 * The parent of a directory, written to a separate buffer, for the browsers
 * that work in RELATIVE paths -- the File Manager and the Console both fall
 * back to "." when CASTALIA_HOME is unset, and from there "up" means "..".
 *
 * That is the one thing ui_path_up() will not do: it treats "." as a root and
 * refuses, which is right for a file dialog (a picker should not wander above
 * where it was opened) and wrong for a shell. The two policies looked like
 * the same function to a duplicate-finder and are not, so they are two named
 * functions rather than one with a flag.
 */
void ui_path_parent_rel(const char *dir, char *dst, cu32 dstsz);
/* Does 'name' end in ".<ext>"? Case-insensitive; a NULL/empty ext matches
 * everything, and ".BMPX" does not match "BMP". */
cbool ui_path_match_ext(const char *name, const char *ext);
/* Listing order: the parent entry, then directories, then files, each
 * alphabetically and case-insensitively. Returns <0, 0 or >0. */
int   ui_path_compare(const char *a, cbool a_dir, const char *b, cbool b_dir);

/* Open the dialog. 'dir' starts the browse (NULL = CASTALIA_HOME), 'ext'
 * filters the listing (NULL = every file), 'saving' titles the accept button
 * Save instead of Open and lets a name that does not exist through. The
 * callback receives the full path, or ok=CFALSE when it was cancelled. */
typedef void (*UiFileCb)(cbool ok, const char *path, void *user);
void ui_file_dialog(const char *title, const char *dir, const char *ext,
                    cbool saving, const char *initial_name,
                    UiFileCb cb, void *user);

#endif /* CASTALIA_UI_H */
