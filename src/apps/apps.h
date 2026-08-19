/*
 * apps.h - Built-in application windows (Layer 5).
 *
 * Alongside the real app windows this header still declares the generic
 * multi-line text window used for About and for honest "planned for a later
 * phase" notices -- a launcher entry whose app has not been written yet opens
 * a truthful roadmap window rather than a fake feature.
 */
#ifndef CASTALIA_APPS_H
#define CASTALIA_APPS_H

#include "castalia/ctypes.h"
#include "castalia/gfx.h"   /* CRect, for the geometry accessors below */

/* Some accessors below take a window (the headless driver asks what a window
 * is showing). Declared as the struct rather than typedef'd here: wm.h already
 * owns that name, and two identical typedefs are not C89. */
struct WmWindow;

/* Open a generic, read-only, multi-line text window. Lines are copied. */
void app_info_open(const char *title, const char *const *lines, int nlines);
/* As app_info_open, plus: F2 writes the window's own text to save_path, and
 * the window says so on its last line. Pass NULL for a window with nothing
 * worth keeping. */
void app_info_open_ex(const char *title, const char *const *lines, int nlines,
                      const char *save_path);
/* What the info window holds versus what it can draw at the current video
 * mode. The two differ on a short screen, which is the case the saved report
 * exists for. -1 when 'win' is not an info window. */
int app_info_lines_held(struct WmWindow *win);
int app_info_lines_shown(struct WmWindow *win);

/* Real System Information window (live platform/memory/video data). */
void app_sysinfo_open(void);

/* File Manager: single-pane explorer with folder navigation, New Folder, and
 * safe delete-to-Trash. Uses the platform directory API. */
void app_fileman_open(void);
void app_fileman_open_path(const char *dir);
/* The highlighted context-menu label, or NULL. For the headless driver. */
const char *app_fileman_ctx_item(struct WmWindow *win);
/* The status line (the right-hand status-bar cell). NULL if not a File Manager. */
const char *app_fileman_status(struct WmWindow *win);
/* One row of the last search's results, as shown -- a match found inside a
 * file carries its line number. CFALSE when there is no such row. */
cbool       app_fileman_result(struct WmWindow *win, int i, char *dst, cu32 cap);
/* The folder currently being browsed. NULL if not a File Manager. For the
 * headless driver: navigation is the one thing a screenshot cannot show, since
 * two different folders holding the same names draw identically. */
const char *app_fileman_path(struct WmWindow *win);
/* The name of the highlighted row, "" if nothing is highlighted. For the
 * headless driver: which row is highlighted is what a refresh can quietly get
 * wrong, and it is not something a screenshot can be asked about by name. */
const char *app_fileman_sel_name(struct WmWindow *win);
/* How many rows the list holds, ".." included. */
int         app_fileman_row_count(struct WmWindow *win);
/* The SCREEN rect of one list row, for the headless driver -- an empty rect
 * for a row that is not on screen. What a selection LOOKS like is the thing
 * a focus treatment changes, and it has to be sampled where it is drawn. */
CRect       app_fileman_row_rect(struct WmWindow *win, int row);

/*
 * Two-pane (commander) state, for the headless driver.
 *
 * The point of two panes is that they are two INDEPENDENT locations, and that
 * is the one thing a screenshot cannot check: two panes showing the same
 * folder and two panes that are actually the same pane drawn twice look
 * identical. So the driver reads each pane's path separately.
 *
 * 'pane' is 0 for the window's own pane and 1 for its mate; the mate answers
 * NULL when two-pane mode is off.
 */
cbool       app_fileman_is_dual(struct WmWindow *win);
int         app_fileman_active_pane(struct WmWindow *win);
const char *app_fileman_pane_path(struct WmWindow *win, int pane);
/* The screen rect of one toolbar button, and the index of the 2-Pane one.
 * Clicking a hard-coded pixel offset is how a scene silently starts pressing
 * a different button after the toolbar gains an entry. */
CRect       app_fileman_btn_rect(struct WmWindow *win, int which);
int         app_fileman_btn_dual(void);

/* Notepad: a plain-text multiline editor (open/save/edit, Ln:Col status). */
void app_notepad_open(void);
/* Open Notepad on a specific file (used by the File Manager). */
void app_notepad_open_file(const char *path);

/*
 * The text Notepad is holding, for --undo-demo. An undo stack fails silently:
 * the document afterwards is still a document, with the wrong characters in
 * it, so the only check worth making is against the exact string.
 */
const char *app_notepad_text(struct WmWindow *win);
/* Where a menu-bar word sits, for --menuaccel-demo (see app_paint above). */
CRect app_notepad_menu_word(struct WmWindow *win, int idx);
/* The SCREEN rect of one toolbar button (0..4: New, Open, Save, Find, Wrap),
 * for the headless driver -- an empty rect for anything else. */
CRect app_notepad_btn_rect(struct WmWindow *win, int idx);
/* Notepad's scroll bar and text well, in SCREEN coordinates, for the
 * headless driver -- one to grab the thumb by, the other to bound what a
 * scroll may repaint. */
cbool app_notepad_scroll_rects(struct WmWindow *win, CRect *bar, CRect *well);
int   app_notepad_sel_len(struct WmWindow *win);
/* What the status line says, for --bigfile-demo: a file too big to hold has
 * to be refused OUT LOUD, or the refusal reads as an open. */
void  app_notepad_status(struct WmWindow *win, char *out, cu32 cap);
/* The open menu's highlighted row, for --menuaccel-demo -- so the scene can
 * find an item by NAME rather than by counting rows and separators. */
const char *app_notepad_menu_item(struct WmWindow *win);

/* Calculator: standard arithmetic with button grid + keyboard. */
void app_calc_open(void);
/* The SCREEN rect of one key, for the headless driver -- CFALSE for a cell
 * that is not a key's origin. Moving the pointer to a hard-coded pixel offset
 * is how a scene silently starts hovering a different key after the grid
 * changes shape. */
cbool app_calc_key_rect(struct WmWindow *win, int row, int col, CRect *out);
/* What the display is showing, for the headless driver. The state machine is
 * unit-tested in full; this is the other question -- whether a keypress
 * reaches it at all. */
const char *app_calc_display(struct WmWindow *win);

/* Log Viewer: scrollable, color-coded tail of the session log. */
void app_logview_open(void);
/* The Log Viewer's scroll bar, in SCREEN coordinates, for the headless
 * driver -- so a scene can grab the thumb where it actually is. */
cbool app_logview_vbar(struct WmWindow *win, CRect *out);
cbool app_logview_well(struct WmWindow *win, CRect *out);
int   app_logview_top(struct WmWindow *win);

/* Control Center: categorized settings (theme, clock, boot) persisted to INI. */
void app_control_open(void);
/* Open the Control Center at a category. Indices match the enum in
 * app_control.c: 0 Appearance, 1 Desktop, 2 Screen Saver, ... */
#define APP_CC_DESKTOP 1
void app_control_open_cat(int cat);
/* Date & Time panel, for the headless driver. 'which': 0 = the field's minus
 * button, 1 = its plus button, 2 = the Set button (field ignored). Rects are
 * in client coordinates. */
cbool app_control_dt_rect(struct WmWindow *win, int field, int which,
                          CRect *out);
int   app_control_dt_field(struct WmWindow *win, int field);
/* A settings row rectangle (client coords), for the headless driver. */
cbool app_control_row_rect(struct WmWindow *win, int row, CRect *out);
/* The SCREEN rect of one of the window's own buttons (0 OK, 1 Apply,
 * 2 Close). For the headless driver: moving BETWEEN two buttons is what
 * proves the one the pointer left is repainted too, and a scene that has to
 * guess where they are cannot do that. */
cbool app_control_btn_rect(struct WmWindow *win, int i, CRect *out);
const char *app_control_dt_status(struct WmWindow *win);
/* The staged key-repeat delay, for --mousekey-demo's keyboard-only pass. */
int app_control_key_delay(struct WmWindow *win);
/* The settings panel's rect, for the containment check: a caption that runs
 * past it paints over the window frame, and nothing else would notice. */
cbool app_control_panel_rect(struct WmWindow *win, CRect *out);
/* Field indices, matching the panel's rows. */
#define APP_CC_DT_YEAR  0
#define APP_CC_DT_MONTH 1
#define APP_CC_DT_DAY   2
#define APP_CC_DT_HOUR  3
#define APP_CC_DT_MIN   4

/* Task Manager: live list of open windows + memory/uptime; activate or end a
 * task (better than the Win9x task list -- keyboard driven, with a memory bar). */
void app_taskman_open(void);
/* The memory gauge's screen rect, plus the two numbers it is drawing, for the
 * headless driver. The point is to check that what is PAINTED matches what
 * ui_meter_fill computes: four windows used to do this arithmetic themselves
 * and one of them overflowed. */
cbool app_taskman_mem_bar(struct WmWindow *win, CRect *out,
                          long *live, long *budget);

/* CastaliaPaint: the raster editor -- eleven tools (pencil, brush, airbrush,
 * eraser, fill, color picker, line, rectangle, ellipse, text and a rectangular
 * selection), a foreground / background pair over the 28-colour palette, cut /
 * copy / paste, Stretch, three levels of undo, the Image operations, and a
 * 24-bit BMP round trip. The pixels are paint_core.c. */
void app_paint_open(void);
/* ...and straight onto a bitmap (a double-click in the File Manager). */
void app_paint_open_file(const char *path);
/*
 * Where a menu-bar word sits, and how wide the open drop-down is, both for
 * --menuaccel-demo. A menu is the one widget whose correctness lives entirely
 * in its pixels, so the scene takes the app's own geometry rather than
 * keeping a second copy of the menu-bar arithmetic that could drift from it.
 * The width is 0 when no menu is open.
 */
CRect app_paint_menu_word(struct WmWindow *win, int idx);
int   app_paint_menu_width(struct WmWindow *win);

/* Theme Editor: every color the shell draws with, an RGB mixer, a live
 * miniature preview of the desktop, and the INI round trip. */
void app_theme_open(void);

/* Network: adapter status, the TCP/IP configuration, the live ARP table and
 * a ping tool -- a thin skin over net_stack.c. While it is open the machine
 * also answers ARP requests and echo requests addressed to it. */
void app_net_open(void);
/*
 * The Ping button's rectangle in CLIENT coordinates, CFALSE if 'win' is not a
 * Network window. Exposed for the --net-ping-demo scene, which used to
 * recompute the position from app_net.c's group-box heights -- so growing a
 * group by ten pixels moved the button out from under the click and the scene
 * pressed empty background instead. A harness that copies a layout constant
 * is a copy that drifts; this asks the window where the button actually is.
 */
cbool app_net_ping_button(struct WmWindow *win, CRect *out);

/* Viewer: display a BMP image or a hex dump of any file. */
void app_view_open(void);
/* Open the viewer on a specific file (used by the File Manager). */
void app_view_open_file(const char *path);

/* Mines: the classic hidden-mines grid puzzle (board logic in mines_core.c). */
void app_mines_open(void);

/* Reversi: the disc-flipping game, against the machine. Rules in rev_core.c;
 * the window shows the legal moves, because a Reversi board that does not is
 * one most people give up on. */
void app_reversi_open(void);
/* Discs on the board (REV_BLACK / REV_WHITE, or anything else for the total),
 * a cell's rect, and whose turn it is -- for --reversi-demo. Conservation and
 * legality are exactly what a screenshot of a disc game cannot check. */
int   app_reversi_discs(struct WmWindow *win, int who);
cbool app_reversi_cell_rect(struct WmWindow *win, int r, int c, CRect *out);
int   app_reversi_turn(struct WmWindow *win);
/* The Hint key's answer, and whether the game has ended -- both used by
 * --reversi-demo to play a complete game through the window. */
cbool app_reversi_hint(struct WmWindow *win, int *out_r, int *out_c);
cbool app_reversi_over(struct WmWindow *win);
/* The cell cursor and the status line, so the scene can press H and check the
 * KEY did something -- not merely that the hint function has an answer. */
void        app_reversi_cursor(struct WmWindow *win, int *out_r, int *out_c);
const char *app_reversi_status(struct WmWindow *win);

/* About: a rich, tabbed "About CastaliaOS" window (identity, hardware, live
 * source statistics, and credits) with a glossy crest banner. */
void app_about_open(void);

/* Benchmark Suite: runs live CPU / memory / graphics micro-benchmarks and
 * reports them as animated bar meters plus an overall CastaliaMark score. */
void app_bench_open(void);

/* Media Player: a Winamp-style WAV player -- LCD + visualizer, transport,
 * seek/volume, and a playlist that can add a file or scan a folder. */
void app_media_open(void);
/* The transport button band and the deck's label colour, for --repaint-demo:
 * a label drawn into a button is a two-pixel defect no eye reliably catches. */
cbool  app_media_transport_rect(struct WmWindow *win, CRect *out);
CColor app_media_label_color(void);
/* The deck face those labels sit on. The pair is what --repaint-demo checks
 * for legibility: one colour used against two different backgrounds is how
 * the VOL/BAL labels came to sit right on the line where a pixel stops
 * reading as ink. */
CColor app_media_deck_color(void);
/* ...and straight onto one sound, which starts playing. */
void app_media_open_file(const char *path);

/* Clock, Calendar & Agenda: a live analog clock with a sweeping second hand, a
 * digital readout, a navigable monthly calendar, and an appointment panel for
 * the day you pick (stored by agenda_core.c, persisted to SYS\AGENDA.TXT). */
void app_clock_open(void);

/* Character Map: a grid of the system font's glyphs; pick characters into a
 * sample string and copy them to the system clipboard. */
void app_charmap_open(void);

/* Console: a DOS-flavored command shell with real built-in commands
 * (dir / cd / type / mem / ver / echo / cls ...) and command history. */
void app_console_open(void);
/* The console's working directory -- what CD changed and what the prompt is
 * drawn from. NULL if not a Console. For the headless driver. */
const char *app_console_cwd(struct WmWindow *win);
/*
 * How many scrollback lines the console is holding, and how many commands are
 * in its history. Exposed for --console-demo.
 *
 * These exist because of a regression that no check in this tree could see:
 * wiring the shared ring in left the scrollback uninitialised, the capacity
 * came out as one, and the console showed a single line. Every suite stayed
 * green -- it was caught by opening the window and looking at it. A count is
 * the cheapest thing that would have failed.
 */
int app_console_line_count(struct WmWindow *win);
/* One scrollback line, oldest first (NULL past the end). */
const char *app_console_line(struct WmWindow *win, int i);
int app_console_hist_count(struct WmWindow *win);

/* How many samples the Task Manager's Performance graphs hold. For
 * --taskman-demo; see the note on app_console_line_count. */
int app_taskman_samples(struct WmWindow *win);

/* Welcome: the first-run tour (glossy banner + live links to key apps and a
 * persisted "show at startup" checkbox). Reopenable from the launcher. */
void app_welcome_open(void);

/* Properties: a read-only info dialog for a file or folder (type, location,
 * size; folders are walked for their totals). Opened from the File Manager. */
void app_props_open(const char *path, cbool is_dir, long size);

/* Solitaire: the classic Klondike patience card game. */
void app_solitaire_open(void);

/* FreeCell: the other patience game, with no hidden cards. */
/* Force the next deal, so a scene can check a known board; 0 restores the
 * clock-seeded shuffle. */
void app_freecell_set_seed(cu32 seed);
void app_freecell_open(void);
/* For --freecell-demo: moves made, and how many cards are on the table at
 * all (which must always be 52 -- a move that loses or duplicates a card is
 * invisible on a board that still looks like a board). */
int  app_freecell_moves(struct WmWindow *win);
int  app_freecell_cards_on_table(struct WmWindow *win);
/* Where a free cell (kind 0), foundation (1) or a cascade's top card (2) is,
 * in client coordinates, so a scene clicks what is drawn. */
cbool app_freecell_rect(struct WmWindow *win, int kind, int i, CRect *out);
/* How many cards have reached the foundations. */
int  app_freecell_found_count(struct WmWindow *win);
void app_sheet_open(void);
void app_write_open(void);
/* ...and straight onto a file (a double-click in the File Manager). */
void app_sheet_open_file(const char *path);

/* The raw text of the current cell, for --undo-demo. A spreadsheet undo that
 * restores the wrong value still recalculates and still prints, so the only
 * check worth making is against the exact string. */
const char *app_sheet_cur_raw(struct WmWindow *win);

/*
 * CastaliaWrite's text, and the attribute byte of one character. Both are
 * needed by --undo-demo: an undo that restores the WORDS without their
 * emphasis is a partial undo that reads as a complete one, so the check has
 * to look at the formatting too.
 */
const char *app_write_text(struct WmWindow *win);
int         app_write_attr_at(struct WmWindow *win, int index);
void app_write_open_file(const char *path);

/* Disk Usage: where the space went. Walks a folder tree (bounded, and honest
 * when it hits the bound) and shows it as a squarified treemap beside a sorted
 * list -- area is share, so the biggest folder is the biggest rectangle. The
 * geometry is map_core.c. NULL opens on CASTALIA_HOME. */
void app_diskuse_open(void);
void app_diskuse_open_path(const char *dir);
/* Archive viewer: list a .CAR's members without extracting them, and say how
 * many of them already exist where they would land. Opening a .CAR from the
 * File Manager comes here instead of unpacking it on the spot. */
/* Hex Viewer: what is actually in a file, byte by byte. Reads only the rows
 * on screen, so the cost does not depend on the file's size. */
void app_hex_open(const char *path);

/*
 * File Compare: what changed between two text files. Either path may be NULL
 * or empty -- the window then asks for it. One window at a time; opening it
 * again points the existing one at the new pair.
 */
void app_compare_open(const char *first, const char *second);
/* What the open window is showing (for --compare-demo); rows is -1 when the
 * window is not open. A row reads as its kind character then its text. */
int   app_compare_rows(void);
void  app_compare_status(char *dst, cu32 dstsz);
cbool app_compare_row(int i, char *dst, cu32 dstsz);
/* The SCREEN rect of one toolbar button (0 File 1, 1 File 2, 2 Compare), for
 * the headless driver. Empty when 'win' is not a Compare window. */
CRect app_compare_btn_rect(struct WmWindow *win, int i);
/* What an open Hex Viewer is showing, for the headless driver. Each returns
 * -1 for a window that is not one. */
long app_hex_offset(struct WmWindow *win);
int  app_hex_have(struct WmWindow *win);
int  app_hex_visible(struct WmWindow *win);
int  app_hex_byte(struct WmWindow *win, int i);

void app_archive_open(const char *path);
/* What an open Archive window is showing, for the headless driver. Each
 * returns -1 for a window that is not an Archive view. */
int  app_archive_count(struct WmWindow *win);
int  app_archive_clashes(struct WmWindow *win);
long app_archive_original(struct WmWindow *win);
long app_archive_stored(struct WmWindow *win);

/* Help window covering the desktop, windows, apps, and shortcuts. */
void app_help_open(void);

/* Honest roadmap notice for a not-yet-implemented feature. */
void app_planned_open(const char *feature, int phase);

#endif /* CASTALIA_APPS_H */
