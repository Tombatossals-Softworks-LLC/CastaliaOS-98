/*
 * office_ui.h - Shared "office suite" chrome for CastaliaWrite / CastaliaSheet.
 *
 * The 2001-era Office idiom, drawn with the engine's own primitives: a menu
 * bar whose words highlight, toolbars whose buttons stay FLAT until the
 * pointer is over them (the Office XP signature), small original icons, and a
 * status bar divided into sunken panels.
 *
 * Nothing here knows about documents -- it is pure chrome both apps share.
 */
#ifndef CASTALIA_OFFICE_UI_H
#define CASTALIA_OFFICE_UI_H

#include "castalia/gfx.h"
#include "castalia/rect.h"
#include "castalia/ui.h"   /* UiMenu, for the keyboard navigation below */

/* Toolbar button states. */
enum {
    OFB_NORMAL = 0,   /* flat: only the icon shows                        */
    OFB_HOVER,        /* raised outline (the pointer is over it)          */
    OFB_PRESSED,      /* sunken                                           */
    OFB_CHECKED,      /* latched on (bold/italic/alignment)               */
    OFB_DISABLED
};

/* Icon ids drawn by office_icon(). All original artwork. */
enum {
    OFI_NEW = 0, OFI_OPEN, OFI_SAVE, OFI_PRINT,
    OFI_CUT, OFI_COPY, OFI_PASTE,
    OFI_BOLD, OFI_ITALIC, OFI_UNDER,
    OFI_ALEFT, OFI_ACENTER, OFI_ARIGHT,
    OFI_SUM, OFI_FUNC, OFI_TABLE, OFI_COUNT
};

#define OF_ICON  16      /* icon cell (square)                              */
#define OF_BTN_W 22      /* toolbar button                                  */
#define OF_BTN_H 20
#define OF_MENUBAR_H 16
#define OF_TOOLBAR_H 24
#define OF_STATUS_H  18

/* The pale vertical wash Office uses behind a toolbar / menu bar. */
void office_band(GfxSurface *s, const CRect *r);

/* Menu bar: draw the words, and get the clickable rect of word 'idx'.
 * 'hot' is the index drawn highlighted (-1 for none). */
CRect office_menu_word(const CRect *bar, const char *const *names, int idx);
void  office_menubar(GfxSurface *s, const CRect *bar,
                     const char *const *names, int count, int hot);

/*
 * ---- keyboard navigation for a menu bar ---------------------------------
 *
 * The menus in this suite could be opened by MOUSE ONLY. On the target that
 * is not a small thing: the mouse is a loadable driver on DOS, and a machine
 * booted without one could not reach Paint's Image menu, Write's alignment,
 * Sheet's Recalculate or Notepad's Word Wrap at all -- there is no keystroke
 * for any of them. Meanwhile the launcher and the File Manager had been
 * keyboard-complete for months, and Help says so.
 *
 * F10 opens the first menu, the arrows walk the bar and the list, Enter
 * chooses and Esc closes -- the idiom the File Manager already uses for its
 * context menu, and the one a 1999 user would try first.
 *
 * One implementation rather than four: "Right at the last menu wraps to the
 * first" is exactly the sort of rule four copies would come to disagree
 * about, and this tree has paid for that lesson repeatedly.
 */
typedef struct {
    int    *open;                  /* the app's menu_open field (-1 closed) */
    int    *x, *y;                 /* where its drop-down is drawn          */
    UiMenu *menu;
    const char *const *names;
    int     count;
    CRect   bar;                   /* the menu bar, client coordinates      */
    void  (*build)(void *user, int idx);   /* refill 'menu' for menu 'idx'  */
    void   *user;
} OfficeMenuNav;

/*
 * Handle one key against the menu bar. Returns CTRUE if the key was consumed,
 * in which case the caller repaints. When an item is chosen *cmd receives its
 * id; otherwise *cmd is set to -1.
 */
cbool office_menu_key(const OfficeMenuNav *nav, int key, int *cmd);

/*
 * ...and the same thing with the repaint included, which is what the four
 * apps actually want.
 *
 * Walking a menu with the arrow keys moves a highlight from one row to
 * another, and all four repainted the whole window for it -- 111% of the
 * client per Down, measured on Notepad, the same price the mouse used to
 * pay for the same gesture. Only the two rows changed, so only the two
 * rows come back; everything else the key can do (opening a different
 * menu, closing this one) still repaints whole, because it moves the
 * drop-down or removes it.
 *
 * Returns CTRUE if the key was consumed. When an item was CHOSEN *cmd
 * receives its id and nothing is repainted -- running the command is the
 * caller's business, and so is the repaint that follows it.
 */
struct WmWindow;
cbool office_menu_key_win(struct WmWindow *win, const OfficeMenuNav *nav,
                          int key, int *cmd);

/* One toolbar button: flat, hovered, pressed, latched, or disabled. */
void office_button(GfxSurface *s, const CRect *r, int icon, int state);

/* A 16x16 icon at (x,y). 'dim' draws it in the disabled gray. */
void office_icon(GfxSurface *s, int x, int y, int icon, cbool dim);

/* A vertical separator between toolbar groups. */
void office_sep(GfxSurface *s, int x, int y, int h);

/* Status bar: the band, then sunken panels inside it. */
void office_statusbar(GfxSurface *s, const CRect *r);
void office_status_panel(GfxSurface *s, const CRect *r, const char *text,
                         int align_flags);

/*
 * Where a document goes when the user did not say.
 *
 * The Spreadsheet and the Writer had a byte-for-byte identical pair of these
 * each (sh_docs_dir/wa_docs_dir, sh_doc_path/wa_doc_path). Two apps that save
 * into "the documents folder" must mean the same folder, and two copies of
 * the rule is how they stop meaning it.
 *
 * office_docs_dir() returns <home>/DOCS, creating it -- an app that offers
 * Save must not fail because the folder it defaults to was never made. The
 * pointer is to static storage, valid until the next call.
 *
 * office_doc_path() resolves a name the user typed or picked: one that
 * already carries a location (absolute, drive-qualified, or with a folder in
 * it) is used exactly as given, and only a bare name is placed in DOCS.
 * Getting this backwards would quietly move a file the user picked from
 * somewhere else into the documents folder on save.
 */
const char *office_docs_dir(void);
void        office_doc_path(char *out, cu32 outsz, const char *name);

#endif /* CASTALIA_OFFICE_UI_H */
