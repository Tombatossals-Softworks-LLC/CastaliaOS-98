/*
 * main.c - CastaliaOS 98 PE entry point.
 *
 * Brings up the layers in order (log -> crash -> platform -> window manager
 * -> shell), runs the session loop, and handles the exit reasons (restart
 * shell, exit to DOS, shut down, reboot). This is the one file allowed to
 * differ per platform because it is the integration seam:
 *   - CASTALIA_HOST: a headless driver opens a representative desktop and
 *     writes a screenshot, so the whole stack is verifiable in CI.
 *   - CASTALIA_DOS : the real interactive session loop on VESA + PS/2.
 *
 * Everything below the platform layer is identical between the two.
 */
#include "castalia/castalia.h"
#include "castalia/capp.h"
#include "castalia/capp_loader.h"
#include "castalia/clip.h"
#include "castalia/net.h"
#include "castalia/net_stack.h"
#include "castalia/plat.h"
#include "castalia/settings.h"
#include "castalia/shell.h"
#include "castalia/snd.h"
#include "castalia/sys.h"
#include "castalia/wm.h"
/* Not a public header, but the last-known-good decision is taken on every
 * platform: cfg_apply_lastgood() below runs before either backend's session. */
#include "../cfg/lastgood.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef CASTALIA_HOST
#include "../src/platform/host/plat_host.h"
#include "apps.h"
#include "cz_file.h"
#include "trash_core.h"
#include "paint_core.h"   /* PC_UNDO_LEVELS: the canvas-budget scene */
#include "../sys/cpu_core.h"
#include "castalia/ui.h"
#include "car_core.h"
#include "lzss_core.h"
#include "office_ui.h"
/* The window-menu scene needs two things the shell does not publish: whether a
 * popup menu is up, and where the taskbar drew a window's button so a click
 * can be aimed at it. Both are questions about the shell's own layout, so they
 * stay internal and the driver reaches in, as it already does for the pure
 * cores above. */
#include "../shell/sh_internal.h"
#include "../wm/wm_move.h"
#include "../wm/wm_internal.h"   /* caption-button rects, for the scene below */
#endif

/* ---- config / paths -------------------------------------------------- */
static void resolve_paths(char *logpath, cu32 logsz,
                          char *crashpath, cu32 crashsz,
                          char *dirtypath, cu32 dirtysz,
                          char *inipath, cu32 inisz)
{
    const char *home = sys_home();
    /* State lives under the documented, installer-created layout so CBOOT (which
     * runs from BIN) and the shell agree on the SAME absolute files: rotating
     * logs in LOGS\, crash log + boot-dirty flag + settings INI in SYS\. Paths
     * are absolute via CASTALIA_HOME, so they do not depend on the CWD. */
    sys_snprintf(logpath,   logsz,   "%s/LOGS/castalia.log",  home);
    sys_snprintf(crashpath, crashsz, "%s/SYS/crash.log",      home);
    sys_snprintf(dirtypath, dirtysz, "%s/SYS/bootdirty.flg",  home);
    sys_snprintf(inipath,   inisz,   "%s/SYS/CASTALIA.INI",   home);
}

/* The known-good copy sits beside the live config, under a name DOS is happy
 * with and the File Manager will show plainly rather than hide. */
static void resolve_bak(const char *inipath, char *bak, cu32 baksz)
{
    cu32 n;
    sys_strlcpy(bak, inipath, baksz);
    n = sys_strnlen(bak, baksz);
    while (n > 0u && bak[n - 1] != '.') { n--; }
    if (n > 0u) { sys_strlcpy(bak + n, "BAK", baksz - n); }
    else { sys_strlcat(bak, ".BAK", baksz); }
}

/* ---- last-known-good configuration ------------------------------------ */
/*
 * Is this file a configuration we recognise?
 *
 * "It parsed into at least one section" is NOT good enough, and finding that
 * out cost a real bug: three thousand random bytes almost certainly contain a
 * '[' and a ']' on some line, so a garbage file passed that test -- which meant
 * the corrupt config was used AND then promoted over the known-good copy, the
 * one outcome this whole mechanism exists to prevent.
 *
 * So it has to be a section this program actually writes, holding at least one
 * key. Random bytes do not produce "[Shell]" with a key under it.
 */
static cbool cfg_looks_valid(const char *path)
{
    static const char *KNOWN[6] = {
        "Shell", "Theme", "Assets", "Boot", "Sound", "Taskbar"
    };
    CfgFile *cfg;
    cbool ok = CFALSE;
    int i;
    if (plat_file_size(path) <= 0) { return CFALSE; }
    cfg = cfg_load(path, NULL);
    if (cfg == NULL) { return CFALSE; }
    for (i = 0; i < 6; i++) {
        if (cfg_key_count(cfg, KNOWN[i]) > 0) { ok = CTRUE; break; }
    }
    cfg_free(cfg);
    return ok;
}

static CResult cfg_copy_file(const char *from, const char *to)
{
    PlatFile *a, *b;
    static char buf[2048];
    cu32 n;
    a = plat_fopen(from, "rb");
    if (a == NULL) { return CE_NOTFOUND; }
    b = plat_fopen(to, "wb");
    if (b == NULL) { plat_fclose(a); return CE_IO; }
    for (;;) {
        n = plat_fread(a, buf, (cu32)sizeof(buf));
        if (n == 0u) { break; }
        if (plat_fwrite(b, buf, n) != n) {
            plat_fclose(a); plat_fclose(b); return CE_IO;
        }
        if (n < (cu32)sizeof(buf)) { break; }
    }
    plat_fclose(a);
    return plat_fclose(b);
}

/*
 * Decide what config this boot should run on, and put it in place.
 *
 * The abuse suite proves a corrupt CASTALIA.INI cannot stop the shell coming
 * up. What it comes up ON, without this, is defaults -- every setting the user
 * ever chose gone because one file got a bad write. If a session has booted
 * and run cleanly before, its config was kept aside, and that is a far better
 * answer than defaults.
 *
 * The judgement lives in lastgood.c and is exhaustively tested; this only
 * gathers the facts and moves the bytes.
 */
static void cfg_apply_lastgood(const char *inipath, const char *bakpath)
{
    cbool have  = (plat_file_size(inipath) > 0) ? CTRUE : CFALSE;
    cbool valid = cfg_looks_valid(inipath);
    cbool bak   = (plat_file_size(bakpath) > 0) ? CTRUE : CFALSE;
    cbool dirty = sys_crash_previous_was_dirty();
    LgAction act = lg_decide(have, valid, bak, dirty);
    if (act == LG_RESTORE) {
        if (cfg_copy_file(bakpath, inipath) == CE_OK) {
            SYS_LOGW("cfg", "CASTALIA.INI was unusable -- restored the last "
                     "configuration that booted cleanly");
        } else {
            SYS_LOGW("cfg", "CASTALIA.INI was unusable and the known-good copy "
                     "could not be restored; using defaults");
        }
    } else if (act == LG_DEFAULTS && have) {
        SYS_LOGW("cfg", "CASTALIA.INI is unusable and there is no known-good "
                 "copy yet; using defaults");
    }
}

/* Keep the config that produced a clean session, so the next boot has
 * something better than defaults to fall back on. */
static void cfg_promote_lastgood(const char *inipath, const char *bakpath)
{
    cbool have  = (plat_file_size(inipath) > 0) ? CTRUE : CFALSE;
    cbool valid = cfg_looks_valid(inipath);
    if (!lg_should_promote(have, valid, CTRUE)) { return; }
    if (cfg_copy_file(inipath, bakpath) == CE_OK) {
        SYS_LOGI("cfg", "kept this session's configuration as known-good");
    }
}

/* ---- one full graphical session; returns the exit reason ------------- */
typedef struct {
    int   width, height, bpp;
    cbool safe;
    cbool headless;
    int   frames;
    cbool open_launcher;
    cbool open_sysinfo;
    cbool open_fileman;
    cbool open_dialog;
    cbool open_apps;
    cbool open_calc;
    cbool open_about;
    cbool open_bench;
    cbool open_media;
    cbool open_clock;
    cbool open_charmap;
    cbool open_solitaire;
    cbool open_freecell;
    cbool open_sheet;
    cbool open_write;
    cbool open_logview;
    cbool open_notepad;
    cbool open_console;
    cbool open_welcome;
    int   nav_downs;   /* >0: open launcher via keyboard, press Down N times,
                          then Enter -- exercises the launcher keyboard path  */
    cbool nav_close;   /* after nav-open, send Alt+F4 to close the window     */
    cbool open_ctx;    /* right-click the desktop to raise the context menu   */
    cbool maximize;    /* open File Manager and maximize it (taskbar reserve)  */
    cbool resize;      /* open File Manager and drag its corner to resize      */
    cbool cc_demo;     /* open Control Center, pick Forest theme, Apply        */
    int   fm_open_row; /* open File Manager, Down N times, Enter (0 = off)     */
    cbool alttab;      /* open two windows and Alt+Tab between them            */
    cbool np_wrap;     /* open Notepad, type a long line, toggle word-wrap     */
    cbool clip_demo;   /* Notepad: type, Ctrl+A select, Ctrl+C copy, Ctrl+V paste */
    cbool snap_demo;   /* drag a window's title bar to the left edge -> snap    */
    cbool taskman_demo;/* open apps + Task Manager, End Task the first one      */
    cbool vdesk_demo;  /* open apps across virtual desktops and switch          */
    cbool paint_demo;  /* Paint: draw a rectangle, Save BMP, load it back       */
    cbool paint_demo_keep; /* ...and keep Paint on top for the screenshot     */
    cbool open_net;    /* open the Network window                               */
    cbool open_theme;  /* open the Theme Editor                                 */
    cbool open_help;   /* open the Help browser                                 */
    cbool help_walk;   /* ...and step through every topic                       */
    cbool open_diskuse;/* open Disk Usage on CASTALIA_HOME                      */
    cbool clock_tick_demo;/* prove the clock's partial repaint loses nothing    */
    cbool repaint_demo;/* ...and the same for the media deck and the console   */
    cbool cz_demo;     /* compress + restore a file through the File Manager    */
    cbool trash_demo;  /* delete two same-named files, put both back where they were */
    cbool orb_demo;    /* the Start button is a button: a disc, three states, whole */
    cbool vale_demo;   /* the wallpaper castle is lit from where the sun is */
    cbool launch_demo; /* every program on the Start menu actually starts */
    cbool lscroll_demo;/* a Start column too tall to fit still reaches its end */
    cbool compare_demo;/* File Compare reads two real files and reports them */
    cbool bigfile_demo;/* a file too big for Notepad survives being opened   */
    cbool renmany_demo;/* ren with a wildcard renames a folder, or nothing      */
    cbool hotbtn_demo; /* buttons light under the pointer and sink when pressed */
    cbool focus_demo;  /* an unfocused window stops claiming the keyboard      */
    cbool shrink_demo; /* every window stays inside itself at its smallest size */
    cbool cost_demo;   /* what one keystroke costs, in pixels composited        */
    cbool wheel_demo;  /* the mouse wheel, and which window gets it            */
    cbool stale_demo;  /* pixels changed with nothing invalidating them        */
    cbool unsaved_demo;/* closing a changed document asks before losing it      */
    cbool screen_check;/* assert the shell reached a real desktop, not a blank one */
    cbool cpu_demo;    /* the CPUID read and the decoder agree                  */
    cbool switch_demo; /* Alt+Tab walks most-recently-used order, with a panel  */
    cbool sysmenu_demo;/* the window menu: every window command from the keyboard */
    cbool sysmenu_keep;/* ...and stop with it open, for the screenshot         */
    cbool menulook_demo;/* the glossy menu treatment, and its flat fallback    */
    cbool deskcache_demo;/* the desktop cache rebuilds only when it must      */
    cbool archive_demo;/* list a .CAR without unpacking it                    */
    cbool deskkeys_demo;/* desktop icons reached with the keyboard alone      */
    cbool sysinfo_demo;/* the machine report, and F2 saving all of it        */
    cbool cursor_demo; /* the pointer draws, and puts back what it covered    */
    cbool hex_demo;    /* the hex viewer reads a window, not a file           */
    cbool datetime_demo;/* the Control Center can set the machine clock       */
    cbool mousekey_demo;/* Mouse and Keyboard panels change real behaviour    */
    cbool mem_check;   /* the idle desktop stays inside its memory budget      */
    cbool car_demo;    /* pack a folder, wipe it, unpack it, compare every file */
    cbool crash_demo;  /* the crash screen says something a person can act on   */
    cbool diskuse_demo;/* Disk Usage: build a known tree, check area == share    */
    cbool capture_demo;/* capture the screen and read the BMP back              */
    cbool theme_demo;  /* ...and edit, apply and save a theme through its UI    */
    cbool thumbs_demo; /* File Manager in Icons view, thumbnailing a folder     */
    cbool nav_demo;    /* walk into a folder and back out, in both browsers     */
    cbool dlg_exhaust; /* every dialog refused, with the window pool full        */
    cbool console_demo; /* the console keeps a scrollback and a history          */
    cbool freecell_demo; /* FreeCell: a move happens and no card is lost         */
    cbool reversi_demo;  /* Reversi: a legal move flips, an illegal one does not */
    cbool winicon_demo;  /* every app window carries its icon              */
    cbool undo_demo;     /* Notepad: Ctrl+Z takes typing back, Ctrl+Y puts it   */
    cbool menuaccel_demo; /* menu shortcuts sit in their own column        */
    cbool assoc_demo;  /* double-click each file type and see what opens        */
    cbool filedlg_demo;/* browse for a file in the Viewer's Open dialog         */
    cbool filedlg_keep;/* ...and leave the dialog open for the screenshot       */
    cbool net_loopback;/* host: bring up the simulated wire (net_host.c)        */
    cbool net_demo;    /* ...and run ARP + ping + UDP echo across it            */
    cbool net_ping_demo;/* open the Network window and ping across that wire    */
    cbool clock_demo;  /* Clock: pick a day, add an appointment, read it back   */
    cbool clock_demo_keep; /* ...and leave the prompt open for the screenshot   */
    cbool search_demo; /* File Manager: click Find, search, check matches        */
    cbool dual_demo;   /* File Manager: enable two-pane, Tab, navigate right     */
    cbool capp_demo;   /* .CAPP loader: package a builtin plugin and run it      */
    cbool anim_demo;   /* window open/close zoom: log the outline growth/shrink  */
    cbool desk_bench;  /* time desktop repaint: cached blit vs live gradient     */
    cbool hover_demo;  /* taskbar hover highlight: hovered vs baseline shots      */
    cbool tip_demo;    /* rest on the clock: date tooltip pops above the tray      */
    cbool mines_demo;  /* open Mines, reveal a cell from the keyboard, screenshot  */
    cbool ql_demo;     /* Quick Launch: Show Desktop clears windows, cell 4 opens Mines */
    cbool min_demo;    /* minimize/restore zoom: log the outline frame<->button   */
    cbool wall_demo;   /* wallpaper: author a BMP, set it, confirm it renders      */
    cbool splash_demo; /* render the boot splash and screenshot it                 */
    cbool saver_demo;  /* render a screensaver frame and screenshot it             */
    cbool shutdn_demo; /* render the shutdown screen and screenshot it             */
    cbool tb_max;      /* open File Manager and double-click its title bar     */
    cbool icon_sel;    /* single-click the Documents desktop icon (select)     */
    const char *icons_dir; /* override [Assets] Icons= (desktop icon pack dir) */
    int   font_face;   /* override [Assets] Font (-1 = use settings)           */
    const char *shot_path;
    const char *record_dir; /* capture a numbered BMP per frame here (marketing) */
    int   scene;        /* which scripted recorder scene to run                */
} RunOptions;

#ifdef CASTALIA_HOST
/* ---- marketing frame recorder ---------------------------------------- */
/* Drives scripted interactions and captures one BMP per shell frame, so the
 * sequence can be assembled into animated GIFs / MP4s (tools/make_clips.sh).
 * Host-only; never part of the DOS build or normal boot. */
static const char *g_rec_dir = NULL;
static int  g_rec_n = 0;
static int  g_rec_mx = 0, g_rec_my = 0;   /* tracked cursor for smooth glides */

static void rec_shot(void)
{
    char path[CASTALIA_MAX_PATH];
    if (g_rec_dir == NULL) { return; }
    sys_snprintf(path, sizeof(path), "%s/f%05d.bmp", g_rec_dir, g_rec_n++);
    plat_screenshot(path);
}
static void rec_wait(int frames)
{
    int i;
    for (i = 0; i < frames; i++) { sh_run_frame(); rec_shot(); }
}
/* Ease-in-out (smoothstep) so the pointer accelerates away and decelerates
 * into its target, the way a hand moves a mouse -- not a constant-speed slide. */
static void rec_move(int x, int y, int steps)
{
    int i, fx = g_rec_mx, fy = g_rec_my;
    if (steps < 1) { steps = 1; }
    for (i = 1; i <= steps; i++) {
        PlatEvent e;
        long t = (long)i * 1000 / steps;              /* 0..1000            */
        long s = (t * t * (3000 - 2 * t)) / 1000000;  /* smoothstep 0..1000 */
        int cx = fx + (int)((long)(x - fx) * s / 1000);
        int cy = fy + (int)((long)(y - fy) * s / 1000);
        memset(&e, 0, sizeof(e)); e.type = PLAT_EV_MOUSE_MOVE;
        e.mouse_x = cx; e.mouse_y = cy;
        plat_host_set_mouse(cx, cy, 0); plat_host_push_event(&e);
        sh_run_frame(); rec_shot();
    }
    g_rec_mx = x; g_rec_my = y;
}
static void rec_press(int buttons)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_DOWN; e.mouse_x = g_rec_mx; e.mouse_y = g_rec_my;
    e.buttons = buttons; plat_host_push_event(&e); sh_run_frame(); rec_shot();
    e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
    sh_run_frame(); rec_shot();
}
static void rec_click(void)    { rec_press(PLAT_MB_LEFT); }
static void rec_rclick(void)   { rec_press(PLAT_MB_RIGHT); }
static void rec_dblclick(void) { rec_press(PLAT_MB_LEFT); rec_press(PLAT_MB_LEFT); }
/*
 * The other half of the same question. probe_repaint asks "did the partial
 * path draw what a full one would"; this asks "does the rect the app
 * invalidates actually COVER everything that moves".
 *
 * Two consecutive FULL repaints, one tick apart. Every pixel that differs is
 * a pixel the animation changes, and every one of them must fall inside
 * 'allowed'. Both images come from full repaints, so the tick that separates
 * them cannot make the comparison lie -- which is exactly the trap the naive
 * version of this test falls into.
 */
static CColor g_rp_prev[640 * 480];

static void probe_moving(const char *what, const CRect *allowed, int iters)
{
    GfxSurface *bb;
    CRect fr = wm_frame_rect(wm_focused());
    int x, y, w = crect_w(&fr), h = crect_h(&fr), i, outside = 0, moved = 0;
    if (w > 640) { w = 640; }
    if (h > 480) { h = 480; }
    sh_invalidate_all();
    sh_run_frame();
    bb = plat_backbuffer();
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            g_rp_prev[y * 640 + x] = gfx_get_pixel(bb, fr.x0 + x, fr.y0 + y);
        }
    }
    for (i = 0; i < iters; i++) {
        sh_invalidate_all();
        sh_run_frame();
        bb = plat_backbuffer();
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                CColor now = gfx_get_pixel(bb, fr.x0 + x, fr.y0 + y);
                if (now != g_rp_prev[y * 640 + x]) {
                    moved++;
                    if (!crect_contains(allowed, fr.x0 + x, fr.y0 + y)) {
                        outside++;
                    }
                    g_rp_prev[y * 640 + x] = now;
                }
            }
        }
    }
    SYS_LOGI("main", "REPAINT-DEMO: %s -- %d px move, %d of them outside the "
             "invalidated rect (%s)", what, moved, outside,
             (moved > 0 && outside == 0) ? "OK" : "MISMATCH");
}

/*
 * Prove a window's PARTIAL repaint draws what a full one would.
 *
 * Several windows animate a small strip and invalidate only that strip -- the
 * clock's face, the media deck's display, the console's caret. That is where
 * the frame budget went, and it is also the one optimization that pays in
 * speed and bills in stale pixels: a partial repaint that misses something
 * leaves last second's image on screen, and no test of the drawing code would
 * ever see it. So: snapshot the focused window after it has been animating,
 * force a repaint of absolutely everything, and compare. Zero difference is
 * the only acceptable answer.
 */
static CColor g_rp_snap[640 * 480];

/*
 * "Did these pixels change?" -- the other question about a repaint, and the
 * one probe_repaint cannot ask because it forces a full redraw of its own.
 *
 * probe_snap remembers a rectangle; probe_diff counts how many of its pixels
 * differ now. Between the two calls the scene does whatever it is testing.
 * It is here rather than as another per-app accessor because "the list
 * scrolled" is a question about what a person sees, and every window in the
 * system can be asked it the same way -- adding a top-line getter to each one
 * would be nine functions to answer it nine times.
 *
 * Shares g_rp_snap with probe_repaint, so the two must not be interleaved.
 */
static CRect g_probe_area;

static void probe_snap(const CRect *area)
{
    GfxSurface *bb = plat_backbuffer();
    int x, y, w, h;
    g_probe_area = (area != NULL) ? *area : crect_make(0, 0, 0, 0);
    w = crect_w(&g_probe_area);
    h = crect_h(&g_probe_area);
    if (w > 640) { w = 640; }
    if (h > 480) { h = 480; }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            g_rp_snap[y * 640 + x] = gfx_get_pixel(bb, g_probe_area.x0 + x,
                                                   g_probe_area.y0 + y);
        }
    }
}

/*
 * ...and WHERE, when the caller wants it. A count says a partial repaint
 * missed something and leaves working out what to a bisect; the bounding box
 * of the stale pixels usually names the culprit on sight. 'box' may be NULL.
 */
static int probe_diff_box(CRect *box)
{
    GfxSurface *bb = plat_backbuffer();
    int x, y, w = crect_w(&g_probe_area), h = crect_h(&g_probe_area), n = 0;
    int lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    if (w > 640) { w = 640; }
    if (h > 480) { h = 480; }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (g_rp_snap[y * 640 + x] != gfx_get_pixel(bb, g_probe_area.x0 + x,
                                                        g_probe_area.y0 + y)) {
                if (n == 0) { lo_x = hi_x = x; lo_y = hi_y = y; }
                if (x < lo_x) { lo_x = x; }
                if (x > hi_x) { hi_x = x; }
                if (y < lo_y) { lo_y = y; }
                if (y > hi_y) { hi_y = y; }
                n++;
            }
        }
    }
    if (box != NULL) {
        *box = crect_make_xyxy(lo_x, lo_y, hi_x + 1, hi_y + 1);
    }
    return n;
}

static int probe_diff(void)
{
    return probe_diff_box((CRect *)0);
}

static void probe_repaint_rect(const char *tag, const char *what,
                               const CRect *area)
{
    GfxSurface *bb = plat_backbuffer();
    CRect fr = (area != NULL) ? *area : wm_frame_rect(wm_focused());
    int x, y, w = crect_w(&fr), h = crect_h(&fr), diff = 0;
    int lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    if (w > 640) { w = 640; }
    if (h > 480) { h = 480; }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            g_rp_snap[y * 640 + x] = gfx_get_pixel(bb, fr.x0 + x, fr.y0 + y);
        }
    }
    sh_invalidate_all();
    sh_run_frame();
    bb = plat_backbuffer();
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (g_rp_snap[y * 640 + x] != gfx_get_pixel(bb, fr.x0 + x,
                                                        fr.y0 + y)) {
                if (diff == 0) { lo_x = hi_x = x; lo_y = hi_y = y; }
                if (x < lo_x) { lo_x = x; }
                if (x > hi_x) { hi_x = x; }
                if (y < lo_y) { lo_y = y; }
                if (y > hi_y) { hi_y = y; }
                diff++;
            }
        }
    }
    /*
     * WHERE, not just how many. A count alone says a partial repaint missed
     * something and leaves working out what to a bisect; the bounding box of
     * the stale pixels usually names the culprit on sight -- an 11x12 blob is
     * the mouse pointer, a full-width band is a row, a strip down one edge is
     * a scrollbar. The coordinates are relative to the rectangle checked.
     */
    if (diff > 0) {
        SYS_LOGI("main", "%s: %s -- %d px differ after a forced full repaint,"
                 " within %d,%d..%d,%d of the window (MISMATCH)",
                 tag, what, diff, lo_x, lo_y, hi_x, hi_y);
    } else {
        SYS_LOGI("main", "%s: %s -- 0 px differ after a forced full repaint "
                 "(OK)", tag, what);
    }
}

static void probe_repaint(const char *tag, const char *what)
{
    probe_repaint_rect(tag, what, (const CRect *)0);
}

/* ---- Disk Usage demo helper ------------------------------------------- */
/* Count what the treemap actually painted inside its well: pixels still
 * showing the empty background, pixels where green dominates, pixels where
 * blue does. That is enough to check the only claim a treemap makes -- area
 * is share -- through the real window rather than through its internals.
 *
 * The well is derived the way the window lays it out: right of the list,
 * between the toolbar and the status bar. */
static void du_probe_call(int *ground, int *green, int *blue)
{
    GfxSurface *bb = plat_backbuffer();
    CRect c = wm_client_rect(wm_focused());
    CRect well = crect_make_xyxy(c.x0 + 186 + 7 + 2, c.y0 + 25 + 2,
                                 c.x1 - 3 - 2, c.y1 - 19 - 3 - 2);
    int px, py;
    *ground = 0; *green = 0; *blue = 0;
    for (py = well.y0; py < well.y1; py++) {
        for (px = well.x0; px < well.x1; px++) {
            CColor col = gfx_get_pixel(bb, px, py);
            int r = GFX_R(col), g = GFX_G(col), b = GFX_B(col);
            if (col == GFX_RGB(0x36, 0x38, 0x40)) { (*ground)++; }
            else if (g > r && g > b) { (*green)++; }
            else if (b > r && b > g) { (*blue)++; }
        }
    }
}

static void rec_drag(int x0, int y0, int x1, int y1, int steps)
{
    PlatEvent e; int i;
    rec_move(x0, y0, 8);
    memset(&e, 0, sizeof(e)); e.type = PLAT_EV_MOUSE_DOWN;
    e.mouse_x = x0; e.mouse_y = y0; e.buttons = PLAT_MB_LEFT;
    plat_host_push_event(&e); sh_run_frame(); rec_shot();
    if (steps < 1) { steps = 1; }
    for (i = 1; i <= steps; i++) {
        PlatEvent m;
        long t = (long)i * 1000 / steps;
        long s = (t * t * (3000 - 2 * t)) / 1000000;
        int cx = x0 + (int)((long)(x1 - x0) * s / 1000);
        int cy = y0 + (int)((long)(y1 - y0) * s / 1000);
        memset(&m, 0, sizeof(m)); m.type = PLAT_EV_MOUSE_MOVE;
        m.mouse_x = cx; m.mouse_y = cy; m.buttons = PLAT_MB_LEFT;
        plat_host_set_mouse(cx, cy, 0); plat_host_push_event(&m);
        sh_run_frame(); rec_shot();
    }
    memset(&e, 0, sizeof(e)); e.type = PLAT_EV_MOUSE_UP;
    e.mouse_x = x1; e.mouse_y = y1; e.buttons = 0;
    plat_host_push_event(&e); sh_run_frame(); rec_shot();
    g_rec_mx = x1; g_rec_my = y1;
}
static void rec_key(int key, int ch)
{
    PlatEvent e; memset(&e, 0, sizeof(e)); e.type = PLAT_EV_KEY_DOWN;
    e.key = key; e.ch = ch; plat_host_push_event(&e); sh_run_frame(); rec_shot();
}

/* Synthetic input for the checking scenes. Same idea as the rec_* helpers
 * above, without the screenshot: those exist to record a film, these to drive
 * a scene that then asserts something. */
static void feed_key(int key)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_KEY_DOWN; e.key = key;
    plat_host_push_event(&e); sh_run_frame();
}
/* Type one printable character at the focused window. */
static void feed_char(int ch)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_KEY_DOWN; e.key = ch; e.ch = ch;
    plat_host_push_event(&e); sh_run_frame();
}
/*
 * A menu row's NAME, without its accelerator. The label format is
 * "New\tCtrl+N", and a scene comparing two of them raw is comparing the
 * shortcut as well -- which is fine until one of the two has been trimmed
 * for a log line and the other has not, at which point every row looks like
 * a different row and the walk stops on the first one. That cost a wrong
 * measurement here.
 */
static void menu_name(char *out, int cap, const char *lab)
{
    int i = 0;
    if (out == NULL || cap < 1) { return; }
    if (lab != NULL) {
        while (lab[i] != '\0' && lab[i] != '\t' && i < cap - 1) {
            out[i] = lab[i];
            i++;
        }
    }
    out[i] = '\0';
}

static void feed_move(int x, int y)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_MOVE; e.mouse_x = x; e.mouse_y = y;
    plat_host_set_mouse(x, y, 0);
    plat_host_push_event(&e); sh_run_frame();
}
/*
 * A drag, in its three parts. feed_click is press-and-release in one frame,
 * which is the wrong shape for anything held: a scroll thumb, a rubber band,
 * a window being moved.
 *
 * The button state matters on the MOVE as well as on the press. The window
 * manager only sends a MOUSELEAVE when no button is down -- a drag that
 * wandered off a window would otherwise be told the pointer had left, in the
 * middle of holding something -- so a "drag" fed with buttons clear is not
 * the gesture it is meant to be measuring.
 */
static void feed_press(int x, int y)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_DOWN; e.mouse_x = x; e.mouse_y = y;
    e.buttons = PLAT_MB_LEFT;
    plat_host_set_mouse(x, y, PLAT_MB_LEFT);
    plat_host_push_event(&e); sh_run_frame();
}
static void feed_drag(int x, int y)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_MOVE; e.mouse_x = x; e.mouse_y = y;
    e.buttons = PLAT_MB_LEFT;
    plat_host_set_mouse(x, y, PLAT_MB_LEFT);
    plat_host_push_event(&e); sh_run_frame();
}
static void feed_release(int x, int y)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_UP; e.mouse_x = x; e.mouse_y = y;
    plat_host_set_mouse(x, y, 0);
    plat_host_push_event(&e); sh_run_frame();
}
/*
 * A wheel notch at a point. Positive is away from the user.
 *
 * The position matters as much as the count: the manager routes a wheel to
 * the window under the POINTER, so a scene that fed one without saying where
 * would be scrolling whatever happened to be under the last mouse move.
 */
static void feed_wheel(int x, int y, int notches)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_WHEEL;
    e.mouse_x = x; e.mouse_y = y; e.wheel = notches;
    plat_host_set_mouse(x, y, 0);
    plat_host_push_event(&e); sh_run_frame();
}
static void feed_click(int x, int y)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_DOWN; e.mouse_x = x; e.mouse_y = y;
    e.buttons = PLAT_MB_LEFT;
    plat_host_set_mouse(x, y, PLAT_MB_LEFT);
    plat_host_push_event(&e); sh_run_frame();
    e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
    plat_host_set_mouse(x, y, 0);
    plat_host_push_event(&e); sh_run_frame();
}
static void feed_rclick(int x, int y)
{
    PlatEvent e; memset(&e, 0, sizeof(e));
    e.type = PLAT_EV_MOUSE_DOWN; e.mouse_x = x; e.mouse_y = y;
    e.buttons = PLAT_MB_RIGHT;
    plat_host_set_mouse(x, y, PLAT_MB_RIGHT);
    plat_host_push_event(&e); sh_run_frame();
    e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
    plat_host_set_mouse(x, y, 0);
    plat_host_push_event(&e); sh_run_frame();
}
/* Does 'hay' contain 'needle'? The scenes that read a line of UI back need
 * this and nothing more. */
static cbool demo_contains(const char *hay, const char *needle)
{
    int i, j;
    if (hay == NULL || needle == NULL) { return CFALSE; }
    for (i = 0; hay[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0' && hay[i + j] == needle[j]; j++) { }
        if (needle[j] == '\0') { return CTRUE; }
    }
    return CFALSE;
}

/*
 * The same text with its line breaks shown rather than taken.
 *
 * A scene that logs file contents puts a real '\n' into the middle of its own
 * report line, which splits the "(OK)" off onto a line of its own -- and the
 * harness reads these line by line.
 *
 * The buffers ROTATE, because one static buffer is only safe if no report
 * needs two of these -- and the first one that did printed the same string
 * twice and read as a bug in what it was reporting on.
 */
#define DEMO_ONELINE_SLOTS 4
static const char *demo_oneline(const char *s)
{
    static char out[DEMO_ONELINE_SLOTS][96];
    static int slot = 0;
    char *o_ = out[slot];
    int i = 0, o = 0;
    slot = (slot + 1) % DEMO_ONELINE_SLOTS;
    if (s == NULL) { o_[0] = '\0'; return o_; }
    while (s[i] != '\0' && o < 96 - 3) {
        if (s[i] == '\n')      { o_[o++] = '\\'; o_[o++] = 'n'; }
        else if (s[i] == '\r') { o_[o++] = '\\'; o_[o++] = 'r'; }
        else                   { o_[o++] = s[i]; }
        i++;
    }
    o_[o] = '\0';
    return o_;
}

/*
 * Clear a prompt's pre-filled field, type 'text' into it, and accept.
 *
 * The backspace count has to beat the LONGEST thing a field can be holding,
 * not the longest name: a Save As box arrives pre-filled with the document's
 * full path, and 32 backspaces merely trimmed its tail. What was left --
 * "/home/user/Castalia" with "PRECIOUS.TXT" stuck on the end -- was a
 * perfectly good absolute path to somewhere nobody meant, so the scene saved
 * a file, found the one it was watching untouched, and reported a bug in the
 * product.
 */
static void demo_prompt_fill(const char *text)
{
    int k;
    /*
     * ...and the backspaces are DRAINED as they go. The host's event queue
     * holds 256 and drops what does not fit, silently, so sending enough
     * backspaces to clear a full path in one go threw away the end of the
     * burst -- the typed name and the Enter after it. The dialog then sat
     * there having received only backspaces, and the scene reported that
     * saving had not happened. Which was true, and not for the reason it
     * looked like.
     */
    for (k = 0; k < CASTALIA_MAX_PATH + 8; k++) {
        PlatEvent be;
        memset(&be, 0, sizeof(be));
        be.type = PLAT_EV_KEY_DOWN;
        be.key = PLAT_KEY_BACKSP;
        plat_host_push_event(&be);
        if ((k % 64) == 63) { sh_run_frame(); }
    }
    for (k = 0; text[k] != '\0'; k++) {
        PlatEvent ke;
        memset(&ke, 0, sizeof(ke));
        ke.type = PLAT_EV_KEY_DOWN;
        ke.key = (unsigned char)text[k];
        ke.ch  = (unsigned char)text[k];
        plat_host_push_event(&ke);
    }
    {
        PlatEvent en;
        memset(&en, 0, sizeof(en));
        en.type = PLAT_EV_KEY_DOWN;
        en.key = PLAT_KEY_ENTER;
        en.ch = '\n';
        plat_host_push_event(&en);
    }
    sh_run_frame();
}

/* Type a line at the focused console and press Enter, the way a person would.
 * The scenes that drive commands need exactly this and nothing more. */
static void demo_console_cmd(const char *cmd)
{
    int j;
    for (j = 0; cmd[j] != '\0'; j++) {
        PlatEvent ke;
        memset(&ke, 0, sizeof(ke));
        ke.type = PLAT_EV_KEY_DOWN;
        ke.key = (unsigned char)cmd[j];
        ke.ch  = (unsigned char)cmd[j];
        plat_host_push_event(&ke);
    }
    {
        PlatEvent en;
        memset(&en, 0, sizeof(en));
        en.type = PLAT_EV_KEY_DOWN;
        en.key = PLAT_KEY_ENTER;
        en.ch = '\n';
        plat_host_push_event(&en);
    }
    sh_run_frame();
}

/* A short text file, written and read back. The scenes that move files around
 * compare CONTENTS, not just names: the failures worth catching are the ones
 * where a file is still there and is the wrong file. */
static void feed_write_text(const char *path, const char *text)
{
    PlatFile *f = plat_fopen(path, "wb");
    if (f == NULL) { return; }
    plat_fwrite(f, text, sys_strnlen(text, 1024u));
    plat_fclose(f);
}
static void feed_read_text(const char *path, char *out, cu32 cap)
{
    PlatFile *f;
    cu32 n = 0;
    if (cap == 0) { return; }
    out[0] = '\0';
    f = plat_fopen(path, "rb");
    if (f == NULL) { return; }
    n = plat_fread(f, out, cap - 1u);
    plat_fclose(f);
    if (n >= cap) { n = cap - 1u; }
    out[n] = '\0';
}

/* How far apart two colours are, summed over the channels. Used by the scenes
 * that check a drawn treatment rather than a computed value: "these two pixels
 * are the same colour" and "these two are visibly different" are the only
 * things a screenshot can honestly say about a gradient. */
static int colour_gap(CColor a, CColor b)
{
    int d = 0, i;
    for (i = 0; i < 3; i++) {
        int ca = (int)((a >> (i * 8)) & 0xFF);
        int cb = (int)((b >> (i * 8)) & 0xFF);
        d += (ca > cb) ? (ca - cb) : (cb - ca);
    }
    return d;
}

/*
 * How many pixels of 'col' sit in the two-pixel band just outside 'r'.
 *
 * The default-button ring is drawn there. Counted rather than sampled at a
 * point, because the pointer is parked wherever the previous scene left it
 * and one dark pixel is exactly the check that passes because the cursor was
 * on it.
 */
static int demo_ring_px(const CRect *r, CColor col)
{
    GfxSurface *bb = plat_backbuffer();
    CRect band = crect_inset(r, -2);
    int x, y, n = 0;
    for (y = band.y0; y <= band.y1; y++) {
        for (x = band.x0; x <= band.x1; x++) {
            if (x > r->x0 && x < r->x1 && y > r->y0 && y < r->y1) { continue; }
            if (gfx_get_pixel(bb, x, y) == col) { n++; }
        }
    }
    return n;
}

/* How many pixels of exactly 'col' are inside 'r'. */
static int demo_count_px(GfxSurface *bb, const CRect *r, CColor col)
{
    int x, y, n = 0;
    for (y = r->y0; y < r->y1; y++) {
        for (x = r->x0; x < r->x1; x++) {
            if (gfx_get_pixel(bb, x, y) == col) { n++; }
        }
    }
    return n;
}

/* Step down 'n' times in an open menu, then activate. */
static void feed_menu_pick(int n)
{
    int i;
    for (i = 0; i < n; i++) { feed_key(PLAT_KEY_DOWN); }
    feed_key(PLAT_KEY_ENTER);
}

/*
 * Right-click at (x,y) in a File Manager, hover down the context menu until
 * the item under the pointer is the one named, and click it. CFALSE -- with
 * the menu dismissed -- if that item was never offered.
 *
 * By NAME, never by row number. A scene that counted "six rows and three
 * separators above Compress" clicked Decompress the day an entry was added
 * above it, and then reported that compression had written a -1 byte file.
 * Counting rows measures the menu's current shape, not the thing under test.
 *
 * The menu's geometry is the real one, spelled out: 16px rows and 3px of
 * padding from its top-left, which is where the click landed.
 */
static cbool demo_ctx_pick(WmWindow *fw, int x, int y, const char *label)
{
    int row, found = -1;
    feed_rclick(x, y);
    for (row = 0; row < 20 && found < 0; row++) {
        const char *lab;
        int ry = y + 3 + row * 16 + 8;
        feed_move(x + 30, ry);
        lab = app_fileman_ctx_item(fw);
        if (lab != NULL && strcmp(lab, label) == 0) { found = ry; }
    }
    if (found < 0) { feed_key(PLAT_KEY_ESC); return CFALSE; }
    feed_click(x + 30, found);
    return CTRUE;
}

static ShExitReason run_recorder(const RunOptions *opt, const PlatVideoInfo *info)
{
    int W = info->width, H = info->height;
    g_rec_dir = opt->record_dir;
    g_rec_n = 0;
    g_rec_mx = W / 2; g_rec_my = H / 2;
    plat_host_set_mouse(g_rec_mx, g_rec_my, 0);
    rec_wait(6);   /* let the desktop settle */

    switch (opt->scene) {
    case 1: {   /* A human session: hunt for an app in Start, open + use it  */
        int fw = 660, fh = 440, fx = (W - fw) / 2, fy = (H - fh) / 2;
        int lcol = 80, rcol = 245, itop = H - 289, ih = 18;  /* menu items    */
        rec_wait(12);                               /* settle, glance around */
        rec_move(20, H - 12, 22); rec_wait(6);      /* aim at the Start orb  */
        rec_click(); rec_wait(12);                  /* menu slides up        */
        /* browse the programs column as if reading the list */
        rec_move(lcol, itop + 1 * ih, 14); rec_wait(7);
        rec_move(lcol, itop + 4 * ih, 10); rec_wait(7);
        rec_move(lcol, itop + 7 * ih, 10); rec_wait(7);
        /* glance across to Places & System, settle on File Manager */
        rec_move(rcol, itop + 3 * ih, 12); rec_wait(6);
        rec_move(rcol, itop + 0 * ih, 8);  rec_wait(9);
        rec_click(); rec_wait(16);                  /* launch File Manager   */
        /* use it: hover the toolbar, then snap it to the left half          */
        rec_move(fx + 120, fy + 34, 16); rec_wait(8);
        rec_drag(fx + fw / 2, fy + 12, 5, H / 2, 34);
        rec_wait(16);
        break;
    }
    case 2: {   /* File Manager: drag a file onto a folder (floating ghost) */
        int fw = 660, fh = 440, fx = (W - fw) / 2, fy = (H - fh) / 2;
        int rowy0 = fy + 113, rh = 16;    /* the ".." row, then 16px per row */
        app_fileman_open();
        rec_wait(14);
        /* drag a file (row 4) up onto a folder (row 2), ghost following */
        rec_move(fx + 260, rowy0 + 4 * rh, 18); rec_wait(4);
        rec_drag(fx + 260, rowy0 + 4 * rh, fx + 200, rowy0 + 2 * rh, 30);
        rec_wait(16);
        break;
    }
    case 3: {   /* Benchmark Suite: Run All, animated tier bars fill in     */
        app_bench_open();
        rec_wait(10);
        rec_key(PLAT_KEY_ENTER, 0);
        rec_wait(48);
        break;
    }
    case 4: {   /* Media Player: play (live visualizer), reorder a track    */
        int mw = 380, mh = 430, mx = (W - mw) / 2, my = (H - mh) / 2;
        app_media_open();
        rec_wait(8);
        rec_key(PLAT_KEY_ENTER, 0);                 /* play -> visualizer   */
        rec_wait(30);
        /* drag playlist row 0 down past row 2 */
        rec_drag(mx + 60, my + 262, mx + 60, my + 312, 26);
        rec_wait(16);
        break;
    }
    case 5: {   /* Solitaire: the bouncing-cards win cascade                */
        app_solitaire_open();
        rec_wait(120);   /* a forced win is set up by the SOL_WIN hook       */
        break;
    }
    case 6: {   /* Desktop: rearrange icons, right-click the bin, open an app */
        rec_wait(10);
        rec_move(60, 40, 18); rec_wait(4);
        rec_drag(60, 40, 300, 210, 30);             /* drag This Machine    */
        rec_wait(8);
        rec_drag(60, 114, 430, 330, 30);            /* drag Documents       */
        rec_wait(10);
        rec_move(60, 484, 18); rec_wait(4);
        rec_rclick(); rec_wait(16);                 /* Recycle Bin menu     */
        rec_move(300, 250, 16); rec_click();        /* dismiss the menu     */
        rec_wait(6);
        rec_move(60, 188, 16); rec_wait(5);         /* the Control Center   */
        rec_dblclick(); rec_wait(20);               /* double-click to open */
        break;
    }
    case 7: {   /* CastaliaSheet: type a formula, switch sheets, use a menu */
        const char *typed = "=SUM(D2:D7)/COUNT(D2:D7)";
        int k;
        app_sheet_open();
        rec_wait(14);
        rec_move(428, 290, 22); rec_wait(4);       /* an empty cell         */
        rec_click();
        rec_wait(6);
        for (k = 0; typed[k] != '\0'; k++) {
            rec_key((unsigned char)typed[k], (unsigned char)typed[k]);
        }
        rec_wait(10);
        rec_key(PLAT_KEY_ENTER, 0);                /* commit -> recalc      */
        rec_wait(14);
        rec_move(198, 500, 18); rec_wait(4);       /* the Sheet2 tab        */
        rec_click();
        rec_wait(12);
        rec_move(145, 500, 14); rec_wait(4);       /* back to Sheet1        */
        rec_click();
        rec_wait(10);
        rec_move(195, 99, 18); rec_wait(4);        /* open a menu           */
        rec_click();
        rec_wait(16);
        break;
    }
    case 8: {   /* CastaliaWrite: type, format a run, use the Format menu */
        const char *typed = " Formatting is per character.";
        int k;
        app_write_open();
        rec_wait(14);
        rec_move(150, 205, 22); rec_wait(4);       /* into the body text    */
        rec_click();
        rec_wait(5);
        for (k = 0; typed[k] != '\0'; k++) {
            rec_key((unsigned char)typed[k], (unsigned char)typed[k]);
        }
        rec_wait(12);
        rec_drag(120, 216, 290, 216, 24);          /* select the new run    */
        rec_wait(8);
        rec_move(241, 123, 18); rec_wait(5);       /* hover the I button    */
        rec_click();                               /* italicise it          */
        rec_wait(12);
        rec_move(265, 79, 18); rec_wait(4);        /* the Format menu       */
        rec_click();
        rec_wait(10);
        rec_move(285, 131, 12); rec_wait(6);       /* hover "Underline"     */
        rec_click();
        rec_wait(18);
        break;
    }
    default:
        rec_wait(20);
        break;
    }
    rec_wait(8);
    fprintf(stderr, "CastaliaOS: recorded %d frames to %s\n",
            g_rec_n, opt->record_dir);
    return SH_EXIT_TO_DOS;
}
#endif /* CASTALIA_HOST */

static ShExitReason run_session(const RunOptions *opt)
{
    PlatVideoRequest req;
    PlatVideoInfo    info;
    ShExitReason     reason;

    req.width = opt->width;
    req.height = opt->height;
    req.bpp = opt->bpp;
    req.prefer_safe = opt->safe;

    if (plat_init(&req, &info) != CE_OK) {
        SYS_LOGF("main", "graphics init failed; returning to DOS");
        fprintf(stderr,
            "CastaliaOS: could not initialize a graphics mode.\n"
            "Try Safe Mode (CBOOT /safe) or see docs/RECOVERY.md.\n");
        return SH_EXIT_TO_DOS;
    }
    if (wm_init(plat_backbuffer()) != CE_OK) {
        plat_shutdown();
        return SH_EXIT_TO_DOS;
    }
    if (sh_init(opt->safe) != CE_OK) {
        wm_shutdown();
        plat_shutdown();
        return SH_EXIT_TO_DOS;
    }
    /* Give plugins real, window-manager-backed windows now that the wm is up. */
    capp_host_wm_install();

    /* Optional sound: null on host, PC speaker on DOS. Honor the saved mute
     * setting; a brief startup chime plays when enabled. Absent/muted audio is
     * not an error (the null backend runs silently). */
    snd_init();
    snd_set_enabled(settings_get()->sound_enabled);
    snd_startup();

#ifdef CASTALIA_HOST
    if (opt->headless && opt->record_dir != NULL) {
        ShExitReason rr = run_recorder(opt, &info);
        snd_shutdown(); capp_shutdown_all(); capp_host_wm_shutdown();
        sh_shutdown(); wm_shutdown(); plat_shutdown();
        return rr;
    }
    if (opt->headless) {
        int f;
        int menu_x = 60, menu_y = info.height - 60;
        if (opt->open_fileman) { app_fileman_open(); }
        if (opt->open_sysinfo) { app_sysinfo_open(); }
        if (opt->open_about)   { app_about_open(); }
        if (opt->open_clock)   { app_clock_open(); }
        if (opt->open_charmap) { app_charmap_open(); }
        if (opt->open_solitaire) { app_solitaire_open(); }
        if (opt->open_freecell) { app_freecell_open(); }
        if (opt->open_sheet) { app_sheet_open(); }
        if (opt->open_write) { app_write_open(); }
        if (opt->open_logview) { app_logview_open(); }
        if (opt->open_notepad) {
            /* A document from the user's own home, not a path in the source
             * tree. This opened "docs/BUILDING.md", which only resolves when
             * the binary is started from a checkout -- every headless run
             * works from a throwaway CASTALIA_HOME, so a screenshot taken
             * this way captured Notepad saying it could not open a file. */
            char np[CASTALIA_MAX_PATH];
            sys_home_path(np, (cu32)sizeof np, "NOTES.TXT");
            app_notepad_open_file(np);
        }
        if (opt->open_welcome) {
            /*
             * Secondary text is not DISABLED text.
             *
             * The Welcome tour described eight working features in
             * ui.text_disabled -- the greyed-out tone -- which tells somebody
             * they cannot use a thing they can, and measured 187 on the
             * perceptual scale against the panel behind it where the headings
             * above them measure 590. About drew its clickable tabs in it too.
             *
             * Both the COLOUR and the PIXELS are checked: comparing two
             * palette entries would pass even if nothing were drawn in
             * either, which is exactly how the media deck's spill check went
             * hollow when its labels moved to a new colour.
             */
            WmWindow *ww;
            GfxSurface *wbb;
            const UiPalette *wp = ui_palette();
            CColor dim = ui_text_dim();
            int q, xx, yy, painted = 0;
            app_welcome_open();
            for (q = 0; q < 8; q++) { sh_run_frame(); }
            ww = wm_focused();
            wbb = plat_backbuffer();
            if (ww != NULL && wbb != NULL) {
                CRect wc = wm_client_rect(ww);
                CPoint wo = wm_client_origin(ww);
                for (yy = wo.y; yy < wo.y + crect_h(&wc); yy++) {
                    for (xx = wo.x; xx < wo.x + crect_w(&wc); xx++) {
                        if (gfx_get_pixel(wbb, xx, yy) == dim) { painted++; }
                    }
                }
            }
            SYS_LOGI("main", "WELCOME: secondary text is %d from the panel, "
                     "%d from the disabled grey, and %d pixel(s) of it are "
                     "painted (%s)",
                     colour_gap(dim, wp->face), colour_gap(dim, wp->text_disabled),
                     painted,
                     (colour_gap(dim, wp->face) >= 200 &&
                      colour_gap(dim, wp->text_disabled) >= 60 &&
                      painted >= 200) ? "OK" : "MISMATCH");
        }
        if (opt->open_net) { app_net_open(); }
        if (opt->open_theme) { app_theme_open(); }
        if (opt->open_diskuse) { app_diskuse_open(); }
        if (opt->open_help) {
            app_help_open();
            /* Walk every topic so each one is drawn at least once: a topic
             * that overflows or crashes shows up here, not on a user. */
            if (opt->help_walk) {
                int t;
                sh_run_frame();
                for (t = 0; t < 8; t++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN; ke.key = PLAT_KEY_DOWN;
                    plat_host_push_event(&ke);
                    sh_run_frame();
                }
                SYS_LOGI("main", "HELP-DEMO: walked every topic (%s)",
                         (wm_focused() != NULL) ? "OK" : "MISMATCH");
            }
        }
        if (opt->diskuse_demo) {
            /* Build a folder whose contents have a known RATIO, open Disk
             * Usage on it, and read the answer back off the screen.
             *
             * The treemap's whole claim is that area is share, so that is
             * what gets checked: a .TXT of 3000 bytes beside a .BMP of 1000
             * must paint three times as much of the map as the bitmap does,
             * and together they must leave none of the well's background
             * showing. Counting coloured pixels is the only way to check
             * that claim through the real window -- and it is exactly the
             * check that catches the layout landing in the wrong place,
             * which is the bug this kind of code actually has.
             */
            char root[CASTALIA_MAX_PATH], sub[CASTALIA_MAX_PATH];
            char fp[CASTALIA_MAX_PATH];
            const char *home = sys_home();
            PlatFile *f;
            static char blob[5000];
            int ground = 0, green = 0, blue = 0, q;
            PlatEvent ke;

            sys_snprintf(root, sizeof(root), "%s/DUTEST", home);
            sys_snprintf(sub, sizeof(sub), "%s/SUB", root);
            plat_mkdir(root);
            for (q = 0; q < (int)sizeof(blob); q++) { blob[q] = 'x'; }
            sys_snprintf(fp, sizeof(fp), "%s/BIG.TXT", root);
            f = plat_fopen(fp, "wb");
            if (f != NULL) { plat_fwrite(f, blob, 3000u); plat_fclose(f); }
            sys_snprintf(fp, sizeof(fp), "%s/SMALL.BMP", root);
            f = plat_fopen(fp, "wb");
            if (f != NULL) { plat_fwrite(f, blob, 1000u); plat_fclose(f); }

            app_diskuse_open_path(root);
            sh_run_frame();
            sh_run_frame();

            du_probe_call(&ground, &green, &blue);
            SYS_LOGI("main", "DISKUSE-DEMO: well ground=%d green=%d blue=%d",
                     ground, green, blue);
            /* Nothing of the empty well may still show through. */
            SYS_LOGI("main", "DISKUSE-DEMO: treemap covers the well (%s)",
                     (ground == 0) ? "OK" : "MISMATCH");
            /* And the 3:1 byte ratio must be a 3:1 area ratio. Slack is for
             * the block borders and the labels drawn on top. */
            {
                int both = green + blue;
                int pct = (both > 0) ? (green * 100) / both : -1;
                SYS_LOGI("main", "DISKUSE-DEMO: bitmap is %d%% of the map (%s)",
                         pct, (pct >= 17 && pct <= 33) ? "OK" : "MISMATCH");
            }

            /* Now add a subfolder that dominates everything, rescan, and
             * descend into it. Inside there is one text file and nothing
             * else, so the map must go entirely blue -- which is how this
             * check knows the window really navigated rather than merely
             * redrawing what it had. */
            plat_mkdir(sub);
            sys_snprintf(fp, sizeof(fp), "%s/INNER.TXT", sub);
            f = plat_fopen(fp, "wb");
            if (f != NULL) { plat_fwrite(f, blob, 5000u); plat_fclose(f); }
            memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
            ke.key = PLAT_KEY_F2;   plat_host_push_event(&ke);  /* rescan  */
            ke.key = PLAT_KEY_HOME; plat_host_push_event(&ke);  /* biggest */
            ke.key = PLAT_KEY_ENTER;plat_host_push_event(&ke);  /* descend */
            sh_run_frame();
            du_probe_call(&ground, &green, &blue);
            SYS_LOGI("main", "DISKUSE-DEMO: inside SUB ground=%d green=%d "
                     "blue=%d (%s)", ground, green, blue,
                     (ground == 0 && green == 0 && blue > 0) ? "OK"
                                                            : "MISMATCH");

            /* Backspace climbs back out, and the bitmap reappears. */
            ke.key = PLAT_KEY_BACKSP; plat_host_push_event(&ke);
            sh_run_frame();
            du_probe_call(&ground, &green, &blue);
            SYS_LOGI("main", "DISKUSE-DEMO: back out, bitmap visible again "
                     "(%s)", (green > 0 && blue > 0) ? "OK" : "MISMATCH");

            /* ...but Backspace at the root stays put rather than wandering
             * out of the folder the window was opened on. */
            ke.key = PLAT_KEY_BACKSP; plat_host_push_event(&ke);
            sh_run_frame();
            du_probe_call(&ground, &green, &blue);
            SYS_LOGI("main", "DISKUSE-DEMO: Backspace at the root is a no-op "
                     "(%s)", (green > 0 && blue > 0) ? "OK" : "MISMATCH");

            /* Leave the world as it was found -- the next scene reads it. */
            sys_snprintf(fp, sizeof(fp), "%s/INNER.TXT", sub);
            plat_file_remove(fp);
            plat_dir_remove(sub);
            sys_snprintf(fp, sizeof(fp), "%s/BIG.TXT", root);
            plat_file_remove(fp);
            sys_snprintf(fp, sizeof(fp), "%s/SMALL.BMP", root);
            plat_file_remove(fp);
            plat_dir_remove(root);
        }
        if (opt->clock_tick_demo) {
            /* The Clock repaints only the face and the readout, and only when
             * the second actually changes. That is a large win -- it took an
             * idle desktop from redrawing a 484x356 window sixty times a
             * second to redrawing nothing -- and it carries exactly one risk:
             * a partial repaint that misses something leaves stale pixels on
             * screen, which no test of the drawing code would ever notice.
             *
             * So this checks the only thing worth checking. Let real seconds
             * pass with the partial path doing the work, snapshot the window,
             * then force a full repaint of everything and compare. If the two
             * images differ by a single pixel, the partial path is losing an
             * update.
             */
            static CColor before[520 * 400];
            GfxSurface *bb;
            CRect fr;
            int x, y, w, hh, ticks = 0;

            app_clock_open();
            sh_run_frame();
            fr = wm_frame_rect(wm_focused());
            w = crect_w(&fr); hh = crect_h(&fr);
            if (w > 520) { w = 520; }
            if (hh > 400) { hh = 400; }

            /* Two and a bit seconds of real time, driven the way the shell
             * drives itself: frames at roughly 60 Hz, nothing forced. */
            for (x = 0; x < 90; x++) { plat_sleep_ms(25); sh_run_frame(); }
            bb = plat_backbuffer();
            for (y = 0; y < hh; y++) {
                for (x = 0; x < w; x++) {
                    before[y * 520 + x] = gfx_get_pixel(bb, fr.x0 + x, fr.y0 + y);
                }
            }
            /* The hands must actually have moved, or the comparison below
             * would pass on a clock that stopped. */
            for (y = 0; y < hh; y++) {
                for (x = 0; x < w; x++) {
                    if (before[y * 520 + x] != gfx_get_pixel(bb, fr.x0 + x,
                                                             fr.y0 + y)) {
                        ticks++;
                    }
                }
            }
            plat_sleep_ms(1100);
            sh_run_frame();
            bb = plat_backbuffer();
            for (y = 0; y < hh && ticks == 0; y++) {
                for (x = 0; x < w; x++) {
                    if (before[y * 520 + x] != gfx_get_pixel(bb, fr.x0 + x,
                                                             fr.y0 + y)) {
                        ticks++; break;
                    }
                }
            }
            SYS_LOGI("main", "CLOCK-TICK-DEMO: the second hand moved (%s)",
                     (ticks > 0) ? "OK" : "MISMATCH");

            /* Snapshot what the partial path produced, then redraw the
             * world from scratch and diff. */
            probe_repaint("REPAINT-DEMO", "clock face");
        }
        if (opt->repaint_demo) {
            int f;
            PlatEvent ke;
            memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;

            /*
             * Before anything moves: nothing may paint a LABEL inside a
             * transport button.
             *
             * The equalizer's LO / MID / HI captions were drawn two pixel
             * rows into the bottom of the SHUF and REP buttons -- the labels
             * are eight pixels tall and were placed ten above their sliders,
             * where only eight of clearance exist. Nobody would file that;
             * it reads as the deck looking a little soft. It survived being
             * looked at directly, twice: a two-row shift is genuinely
             * invisible in a before-and-after, and it took counting the
             * label-coloured pixel rows to see that the fix had done
             * anything at all. So the scene counts them instead.
             */
            {
                WmWindow *mw0;
                CRect tb;
                GfxSurface *bb0;
                CPoint mo;
                int q, xx, yy, spill = 0, have_tb = 0;
                CColor lab = app_media_label_color();
                mo.x = 0; mo.y = 0;
                tb = crect_make(0, 0, 0, 0);
                app_media_open();
                for (q = 0; q < 12; q++) { sh_run_frame(); }
                mw0 = wm_focused();
                bb0 = plat_backbuffer();
                if (mw0 != NULL && bb0 != NULL &&
                    app_media_transport_rect(mw0, &tb)) {
                    have_tb = 1;
                    mo = wm_client_origin(mw0);
                    for (yy = mo.y + tb.y0; yy < mo.y + tb.y1; yy++) {
                        for (xx = mo.x + tb.x0; xx < mo.x + tb.x1; xx++) {
                            if (gfx_get_pixel(bb0, xx, yy) == lab) { spill++; }
                        }
                    }
                }
                SYS_LOGI("main", "REPAINT-DEMO: %d label pixel(s) inside the "
                         "transport buttons (%s)", spill,
                         (spill == 0) ? "OK" : "MISMATCH");
                /*
                 * ...and those labels have to be READABLE on the deck.
                 *
                 * VOL, BAL and LO/MID/HI were drawn in the LCD's dim green,
                 * which is right inside the readout -- against near black --
                 * and measured 111 against the deck face, where 110 is the
                 * line below which a pixel stops reading as ink. The playlist
                 * text beside them measured 534. One colour, two backgrounds,
                 * and only one of them had ever been looked at.
                 *
                 * The painted COUNT is checked with it. Comparing two
                 * constants would pass even if nothing were drawn in either
                 * -- which is exactly what happened to the spill check above
                 * when the labels moved to a new colour and this accessor was
                 * left answering the old one.
                 */
                {
                    CColor deck = app_media_deck_color();
                    int gap = colour_gap(lab, deck), painted = 0;
                    if (have_tb) {
                        /*
                         * Counted in the SLIDER BAND -- the rows just under
                         * the transport buttons, where these labels live --
                         * and not across the whole window. The readout above
                         * paints hundreds of pixels in the LCD greens, so a
                         * window-wide count says "something green is on
                         * screen", which is true of every colour this deck
                         * uses and therefore says nothing.
                         */
                        CRect cr5 = wm_client_rect(mw0);
                        int y0 = mo.y + tb.y1;
                        int y1 = mo.y + tb.y1 + 30;
                        if (y1 > mo.y + crect_h(&cr5)) {
                            y1 = mo.y + crect_h(&cr5);
                        }
                        for (yy = y0; yy < y1; yy++) {
                            for (xx = mo.x; xx < mo.x + crect_w(&cr5); xx++) {
                                if (gfx_get_pixel(bb0, xx, yy) == lab) {
                                    painted++;
                                }
                            }
                        }
                    }
                    SYS_LOGI("main", "REPAINT-DEMO: the deck labels are %d "
                             "apart from the face behind them and %d pixel(s) "
                             "of them are actually painted (%s)", gap, painted,
                             (gap >= 140 && painted >= 40) ? "OK" : "MISMATCH");
                }
                while (wm_window_count() > 0) {
                    WmWindow *dw4 = wm_window_at(0);
                    if (dw4 == NULL) { break; }
                    wm_destroy(dw4);
                }
                for (q = 0; q < 3; q++) { sh_run_frame(); }
            }

            /*
             * The media deck animates a display strip -- visualizer, title,
             * elapsed readout, seek head -- and leaves the transport, the
             * sliders and the playlist alone. Asking whether that is true by
             * diffing the partial result against a forced full repaint does
             * NOT work here: forcing a repaint runs a frame, a frame ticks
             * the animation, and a deck that is playing has moved by the
             * time the comparison happens.
             *
             * So the question is turned around. Take two full repaints one
             * tick apart and find every pixel that moved; all of them must
             * lie inside the strip. The rect below is the strip, derived
             * from the same layout numbers app_media.c uses (LCD at client
             * y 8, seek bar ending at y 65) -- if that layout moves, this
             * moves with it.
             */
            app_media_open();
            sh_run_frame();
            ke.key = PLAT_KEY_ENTER; ke.ch = 0;
            plat_host_push_event(&ke);              /* play the selection   */
            for (f = 0; f < 20; f++) { plat_sleep_ms(16); sh_run_frame(); }
            {
                WmWindow *w = wm_focused();
                CRect c = wm_client_rect(w);
                CRect strip = crect_make_xyxy(c.x0 + 6, c.y0 + 6,
                                              c.x1 - 6, c.y0 + 67);
                probe_moving("media deck", &strip, 12);
            }
            wm_destroy(wm_focused());
            sh_run_frame();

            /* The console's caret gets the same treatment -- six pixels
             * invalidated instead of the whole window, measured at 633
             * filled pixels a blink against 600,462 before -- but it is
             * deliberately NOT asserted here. The repaint region merges
             * nearby rectangles (the caret's and the mouse cursor's), so a
             * caret-sized error in the invalidated rect is absorbed before
             * it can reach the screen, and a check that cannot fail is not
             * a check. The clock's face, which is far larger than anything
             * the region will quietly merge, is where that property is
             * actually pinned down -- see --clock-tick-demo.
             */
        }
        if (opt->cz_demo) {
            /* Compress a real file on disk, DELETE the original, restore it
             * from the archive and compare the bytes.
             *
             * The unit tests already prove the compressor round-trips buffers;
             * what they cannot reach is the part that touches a filesystem --
             * that the header's remembered name is what the restore writes,
             * that the two paths agree, that a damaged archive is refused
             * rather than half-restored. That is what this covers. It drives
             * cz_file.c directly, NOT the File Manager's menu item, so it
             * says nothing about that menu entry being wired to the right
             * command; it is the file layer that is under test here.
             */
            char home[CASTALIA_MAX_PATH], src[CASTALIA_MAX_PATH];
            char arc[CASTALIA_MAX_PATH];
            const char *hp = sys_home();
            static char body[9000];
            static char back[9000];
            PlatFile *f;
            long orig_size, arc_size;
            int q;

            sys_strlcpy(home, hp, sizeof(home));
            sys_snprintf(src, sizeof(src), "%s/SQUEEZE.TXT", home);
            sys_snprintf(arc, sizeof(arc), "%s/SQUEEZE.CZ", home);
            /* Repetitive but not trivial -- the shape ordinary text has. */
            for (q = 0; q < (int)sizeof(body); q++) {
                static const char *l = "the vale of Castalia, where the hills "
                                       "are green and the castle is small. ";
                body[q] = l[q % 73];
            }
            f = plat_fopen(src, "wb");
            if (f != NULL) { plat_fwrite(f, body, (cu32)sizeof(body));
                             plat_fclose(f); }
            orig_size = plat_file_size(src);

            {
                long saved = 0;
                CResult rc = cz_compress_file(src, arc, &saved);
                arc_size = plat_file_size(arc);
                SYS_LOGI("main", "CZ-DEMO: %ld -> %ld bytes, saved %ld (%s)",
                         orig_size, arc_size, saved,
                         (rc == CE_OK && arc_size > 0 && arc_size < orig_size)
                             ? "OK" : "MISMATCH");
            }

            /* Delete the original: the restore must not be able to cheat by
             * reading it. */
            plat_file_remove(src);
            SYS_LOGI("main", "CZ-DEMO: original removed (%s)",
                     (plat_file_size(src) < 0) ? "OK" : "MISMATCH");

            {
                char got[CASTALIA_MAX_NAME];
                CResult rc = cz_decompress_file(arc, home, got, sizeof(got));
                long back_size = plat_file_size(src);
                SYS_LOGI("main", "CZ-DEMO: restored '%s' at %ld bytes (%s)",
                         got, back_size,
                         (rc == CE_OK && back_size == orig_size) ? "OK"
                                                                 : "MISMATCH");
            }
            /* Byte for byte, or it did not work. */
            {
                cu32 n = 0;
                int same = 1;
                f = plat_fopen(src, "rb");
                if (f != NULL) { n = plat_fread(f, back, (cu32)sizeof(back));
                                 plat_fclose(f); }
                if (n != (cu32)sizeof(body)) { same = 0; }
                else { for (q = 0; q < (int)sizeof(body); q++) {
                           if (back[q] != body[q]) { same = 0; break; } } }
                SYS_LOGI("main", "CZ-DEMO: restored bytes are identical (%s)",
                         same ? "OK" : "MISMATCH");
            }

            /* A damaged archive must be refused, not half-restored. */
            {
                char bad[CASTALIA_MAX_PATH];
                char got[CASTALIA_MAX_NAME];
                CResult rc;
                sys_snprintf(bad, sizeof(bad), "%s/BROKEN.CZ", home);
                f = plat_fopen(bad, "wb");
                if (f != NULL) { plat_fwrite(f, "CZ1\0\xff\xff\xff\x7f", 8u);
                                 plat_fclose(f); }
                rc = cz_decompress_file(bad, home, got, sizeof(got));
                SYS_LOGI("main", "CZ-DEMO: a damaged archive is refused (%s)",
                         (rc != CE_OK) ? "OK" : "MISMATCH");
                plat_file_remove(bad);
            }

            plat_file_remove(arc);

            /* Now the same thing through the File Manager's right-click
             * menu, which is the part the block above deliberately does not
             * cover -- and then the question that every one of these writes
             * has to ask before it destroys a file that was already there.
             *
             * The file gets a folder to itself so its rows are known without
             * searching for them: the list starts under the menu bar,
             * toolbar, address bar and column header (16+28+22+17) and right
             * of the task pane (152), with 16px rows.
             */
            {
                WmWindow *fw;
                CPoint fo;
                int mx, row0;
                cbool asked = CFALSE;  /* was the question actually put? */
                char sub[CASTALIA_MAX_PATH], subfile[CASTALIA_MAX_PATH];
                char subarc[CASTALIA_MAX_PATH];
                char subdir[CASTALIA_MAX_PATH], subcar[CASTALIA_MAX_PATH];
                sys_snprintf(sub, sizeof(sub), "%s/CZTEST", home);
                sys_snprintf(subfile, sizeof(subfile), "%s/SQUEEZE.TXT", sub);
                sys_snprintf(subarc, sizeof(subarc), "%s/SQUEEZE.CZ", sub);
                sys_snprintf(subdir, sizeof(subdir), "%s/DATA", sub);
                sys_snprintf(subcar, sizeof(subcar), "%s/DATA.CAR", sub);
                plat_mkdir(sub);
                f = plat_fopen(subfile, "wb");
                if (f != NULL) { plat_fwrite(f, body, (cu32)sizeof(body));
                                 plat_fclose(f); }
                app_fileman_open_path(sub);
                sh_run_frame();
                fw = wm_focused();
                fo = wm_client_origin(fw);
                mx = fo.x + 152 + 40;
                row0 = fo.y + (16 + 28 + 22 + 17) + 8;
#define CZ_ROW(r) (row0 + (r) * 16)

                /* ---- writing where nothing is in the way ---------------- */
                /* Row 0 is "..", row 1 is the only file in the folder. */
                SYS_LOGI("main", "CZ-DEMO: the pointer found Compress on the "
                         "menu (%s)",
                         demo_ctx_pick(fw, mx, CZ_ROW(1), "Compress")
                             ? "OK" : "MISMATCH");
                arc_size = plat_file_size(subarc);
                SYS_LOGI("main", "CZ-DEMO: the Compress menu item wrote "
                         "SQUEEZE.CZ at %ld bytes, without asking anything "
                         "(%s)", arc_size,
                         (arc_size > 0 && arc_size < orig_size &&
                          !wm_has_modal()) ? "OK" : "MISMATCH");
                /* And the original must still be there: a miss that landed on
                 * Delete would also leave no .CZ, so check both. */
                SYS_LOGI("main", "CZ-DEMO: the original survived the menu (%s)",
                         (plat_file_size(subfile) == orig_size) ? "OK"
                                                                : "MISMATCH");
                /* The archive says what it holds without being expanded --
                 * which is what lets the question below name a file. */
                {
                    char nm[CASTALIA_MAX_NAME];
                    cbool ok_nm = cz_member_name(subarc, nm, sizeof(nm));
                    SYS_LOGI("main", "CZ-DEMO: its header names '%s' from 28 "
                             "bytes read (%s)", ok_nm ? nm : "?",
                             (ok_nm && strcmp(nm, "SQUEEZE.TXT") == 0)
                                 ? "OK" : "MISMATCH");
                }
                /* Finally: OPENING a .CZ must give the file back, not a hex
                 * dump of the container. The archive is the only thing left in
                 * the folder once the original is gone, so Home-Down-Enter
                 * lands on it. */
                plat_file_remove(subfile);
                feed_key(PLAT_KEY_F5);            /* re-read the folder */
                feed_key(PLAT_KEY_HOME);
                feed_key(PLAT_KEY_DOWN);
                feed_key(PLAT_KEY_ENTER);
                SYS_LOGI("main", "CZ-DEMO: opening the archive restored the "
                         "file, without asking anything (%s)",
                         (plat_file_size(subfile) == orig_size &&
                          !wm_has_modal()) ? "OK" : "MISMATCH");

                /*
                 * ---- and writing where something IS in the way -----------
                 *
                 * Every write above landed on a name nobody was using. Each
                 * of them would just as happily have landed on a name
                 * somebody was: compressing REPORT.DOC destroys the .CZ made
                 * from REPORT.TXT, restoring an old .CZ destroys the edited
                 * file it remembers, and two folders whose names share eight
                 * characters back up onto one archive. Pasting over a file
                 * has asked since the beginning; these three did not.
                 *
                 * The sentinel is a different LENGTH from the thing it stands
                 * in for, because "the file is still there" cannot tell a
                 * cancel from a redo -- the second archive is byte-identical
                 * to the first.
                 */
                feed_write_text(subarc, "KEEP THIS\n");   /* 10 bytes */
                feed_key(PLAT_KEY_F5);
                /* Rows now: 0 "..", 1 SQUEEZE.CZ, 2 SQUEEZE.TXT. */
                (void)demo_ctx_pick(fw, mx, CZ_ROW(2), "Compress");
                asked = wm_has_modal();
                SYS_LOGI("main", "CZ-DEMO: compressing onto an existing .CZ "
                         "asks first, and says which file (\"%s\") (%s)",
                         demo_oneline(app_fileman_status(fw)),
                         (asked &&
                          demo_contains(app_fileman_status(fw), "SQUEEZE.CZ"))
                             ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ESC);                   /* Esc is No */
                SYS_LOGI("main", "CZ-DEMO: saying no left the 10 bytes there "
                         "(%ld) (%s)", plat_file_size(subarc),
                         (asked && !wm_has_modal() &&
                          plat_file_size(subarc) == 10L) ? "OK" : "MISMATCH");
                (void)demo_ctx_pick(fw, mx, CZ_ROW(2), "Compress");
                /* Whether it asked is checked HERE, not implied by what the
                 * Enter below does: an Enter that answers nothing still
                 * writes the archive, and the check could not fail. */
                asked = wm_has_modal();
                feed_key(PLAT_KEY_ENTER);                 /* Enter is Yes */
                SYS_LOGI("main", "CZ-DEMO: saying yes wrote the archive over "
                         "it (%ld bytes) (%s)", plat_file_size(subarc),
                         (asked && !wm_has_modal() &&
                          plat_file_size(subarc) == arc_size)
                             ? "OK" : "MISMATCH");

                /* The same question on the way back: an old archive must not
                 * quietly replace the file somebody has since edited. */
                feed_write_text(subfile, "EDITED\n");      /* 7 bytes */
                feed_key(PLAT_KEY_F5);
                feed_key(PLAT_KEY_HOME);
                feed_key(PLAT_KEY_DOWN);
                feed_key(PLAT_KEY_ENTER);                  /* open SQUEEZE.CZ */
                asked = wm_has_modal();
                SYS_LOGI("main", "CZ-DEMO: restoring over a file that is "
                         "already here asks first, naming it (\"%s\") (%s)",
                         demo_oneline(app_fileman_status(fw)),
                         (asked &&
                          demo_contains(app_fileman_status(fw), "SQUEEZE.TXT"))
                             ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ESC);
                SYS_LOGI("main", "CZ-DEMO: saying no left the edited file "
                         "alone (%ld bytes) (%s)", plat_file_size(subfile),
                         (asked && !wm_has_modal() &&
                          plat_file_size(subfile) == 7L) ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ENTER);                  /* open it again */
                asked = wm_has_modal();
                feed_key(PLAT_KEY_ENTER);                  /* Enter is Yes */
                SYS_LOGI("main", "CZ-DEMO: saying yes restored it (%ld bytes) "
                         "(%s)", plat_file_size(subfile),
                         (asked && !wm_has_modal() &&
                          plat_file_size(subfile) == orig_size)
                             ? "OK" : "MISMATCH");

                /*
                 * F5 re-reads the folder, and the highlight stays on the same
                 * NAME. The folder made below sorts above the selection and
                 * groups ahead of the files besides, so a refresh that kept
                 * the ROW would leave the highlight on something else -- and
                 * the next Delete would take that instead.
                 */
                feed_key(PLAT_KEY_HOME);
                feed_key(PLAT_KEY_DOWN);          /* SQUEEZE.CZ */
                SYS_LOGI("main", "CZ-DEMO: the highlight starts on '%s' (%s)",
                         app_fileman_sel_name(fw),
                         (strcmp(app_fileman_sel_name(fw), "SQUEEZE.CZ") == 0)
                             ? "OK" : "MISMATCH");

                /* And the third of them: backing a folder up onto an archive
                 * that is already there. Same gate, different write. */
                plat_mkdir(subdir);
                {
                    char inner[CASTALIA_MAX_PATH];
                    sys_snprintf(inner, sizeof(inner), "%s/INNER.TXT", subdir);
                    feed_write_text(inner, "inside the folder\n");
                }
                feed_key(PLAT_KEY_F5);
                SYS_LOGI("main", "CZ-DEMO: F5 found the new folder (%d rows) "
                         "and left the highlight on '%s' (%s)",
                         app_fileman_row_count(fw), app_fileman_sel_name(fw),
                         (app_fileman_row_count(fw) == 4 &&
                          strcmp(app_fileman_sel_name(fw), "SQUEEZE.CZ") == 0)
                             ? "OK" : "MISMATCH");
                /* Folders group first: 0 "..", 1 DATA, 2 SQUEEZE.CZ, 3 .TXT. */
                (void)demo_ctx_pick(fw, mx, CZ_ROW(1), "Back Up to .CAR");
                SYS_LOGI("main", "CZ-DEMO: backing up a folder to a free name "
                         "asks nothing (%ld bytes) (%s)",
                         plat_file_size(subcar),
                         (!wm_has_modal() && plat_file_size(subcar) > 0L)
                             ? "OK" : "MISMATCH");
                feed_write_text(subcar, "KEEP THIS\n");   /* 10 bytes */
                feed_key(PLAT_KEY_F5);
                /* 0 "..", 1 DATA, 2 DATA.CAR, 3 SQUEEZE.CZ, 4 SQUEEZE.TXT. */
                (void)demo_ctx_pick(fw, mx, CZ_ROW(1), "Back Up to .CAR");
                asked = wm_has_modal();
                SYS_LOGI("main", "CZ-DEMO: backing up onto an existing .CAR "
                         "asks first (%s)", asked ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ESC);
                SYS_LOGI("main", "CZ-DEMO: saying no left the 10 bytes there "
                         "(%ld) (%s)", plat_file_size(subcar),
                         (asked && !wm_has_modal() &&
                          plat_file_size(subcar) == 10L) ? "OK" : "MISMATCH");
#undef CZ_ROW

                {
                    char inner[CASTALIA_MAX_PATH];
                    sys_snprintf(inner, sizeof(inner), "%s/INNER.TXT", subdir);
                    plat_file_remove(inner);
                }
                plat_dir_remove(subdir);
                plat_file_remove(subcar);
                plat_file_remove(subfile);
                plat_file_remove(subarc);
                plat_dir_remove(sub);
            }

            plat_file_remove(src);
            plat_file_remove(arc);
        }
        if (opt->unsaved_demo) {
            /*
             * Closing a document with unsaved changes asks before losing it.
             *
             * It did not. Type into Notepad, press the close box, and the
             * text was gone -- no dialog, nothing to undo it with. The window
             * manager has always sent WM_MSG_CLOSE so an app could prompt or
             * veto; nothing in this system had ever used it.
             *
             * The scene closes the way the close box does (wm_request_close),
             * not with wm_destroy, which deliberately does not ask.
             */
            WmWindow *nw2;
            int k2, before_n, after_n;

            app_notepad_open();
            for (k2 = 0; k2 < 6; k2++) { sh_run_frame(); }
            nw2 = wm_focused();
            before_n = wm_window_count();

            /* An UNTOUCHED document closes without a word: a guard that asked
             * every time would be as wrong as one that never asked. */
            wm_request_close(nw2);
            for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
            SYS_LOGI("main", "UNSAVED-DEMO: an unchanged document closes "
                     "without asking (%d -> %d windows) (%s)",
                     before_n, wm_window_count(),
                     (wm_window_count() == before_n - 1) ? "OK" : "MISMATCH");

            /* ...now type something and try again. */
            app_notepad_open();
            for (k2 = 0; k2 < 6; k2++) { sh_run_frame(); }
            nw2 = wm_focused();
            {
                const char *t = "unsaved work";
                int j;
                for (j = 0; t[j] != '\0'; j++) {
                    PlatEvent ke;
                    memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN;
                    ke.key = (unsigned char)t[j];
                    ke.ch  = (unsigned char)t[j];
                    plat_host_push_event(&ke);
                }
                sh_run_frame();
            }
            before_n = wm_window_count();
            wm_request_close(nw2);
            for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
            after_n = wm_window_count();
            /* The window is still there AND a dialog went up -- so the count
             * rose by one rather than falling by one. */
            SYS_LOGI("main", "UNSAVED-DEMO: a changed document is not closed "
                     "on the spot (%d -> %d windows) and the text is still "
                     "there (\"%s\") (%s)", before_n, after_n,
                     demo_oneline(app_notepad_text(nw2)),
                     (after_n > before_n &&
                      strcmp(app_notepad_text(nw2), "unsaved work") == 0)
                         ? "OK" : "MISMATCH");

            /*
             * ---- and the question SHOWS which answer Enter gives ---------
             *
             * Yes and No were drawn identically. Which one Enter would press
             * was not on the screen anywhere, and nothing but Esc reached the
             * other -- so on a machine with no mouse driver the answer to
             * "throw this away?" was whichever one somebody guessed.
             *
             * The ring is counted rather than sampled at a point: the pointer
             * is parked wherever the last scene left it, and a single pixel
             * is exactly the kind of check that passes because the cursor
             * happened to be dark there.
             */
            {
                CRect b0, b1;
                if (!ui_dialog_btn_rect(0, &b0) || !ui_dialog_btn_rect(1, &b1)) {
                    SYS_LOGI("main", "UNSAVED-DEMO: the question has no "
                             "buttons to look at (MISMATCH)");
                } else {
                    CColor dark = ui_palette()->darker;
                    int r0 = demo_ring_px(&b0, dark), r1 = demo_ring_px(&b1, dark);
                    SYS_LOGI("main", "UNSAVED-DEMO: [Yes] carries the default "
                             "ring and [No] does not (%d vs %d px) (%s)",
                             r0, r1,
                             (r0 > 100 && r1 < 20) ? "OK" : "MISMATCH");
                    /* ...and the keyboard can move to the other answer. */
                    feed_key(PLAT_KEY_RIGHT);
                    SYS_LOGI("main", "UNSAVED-DEMO: Right moves the choice to "
                             "[No] (focus %d) (%s)", ui_dialog_focus(),
                             (ui_dialog_focus() == 1) ? "OK" : "MISMATCH");
                    /*
                     * ...and Enter then answers NO, so the document lives.
                     * This is the check the ring exists for: before it, Enter
                     * always meant Yes and nothing on screen said so.
                     *
                     * Counted in WINDOWS, not in the document's text. Reading
                     * the text back through a window that Yes has just
                     * destroyed reads freed memory, which on this allocator
                     * still says "unsaved work" -- the first version of this
                     * check passed against an Enter that ignored the focus
                     * entirely, for exactly that reason. Answering closes the
                     * question either way, so No is one window fewer and Yes
                     * is two.
                     */
                    {
                        int n_before = wm_window_count();
                        feed_key(PLAT_KEY_ENTER);
                        for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
                        SYS_LOGI("main", "UNSAVED-DEMO: Enter on [No] closed "
                                 "the question and kept the document (%d -> "
                                 "%d windows) (%s)", n_before,
                                 wm_window_count(),
                                 (!wm_has_modal() &&
                                  wm_window_count() == n_before - 1)
                                     ? "OK" : "MISMATCH");
                    }
                }
            }

            /* Answering No leaves it open -- the point of asking. */
            wm_request_close(nw2);
            for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
            feed_key(PLAT_KEY_ESC);
            for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
            SYS_LOGI("main", "UNSAVED-DEMO: cancelling the question keeps the "
                     "document (\"%s\") (%s)",
                     demo_oneline(app_notepad_text(nw2)),
                     (strcmp(app_notepad_text(nw2), "unsaved work") == 0)
                         ? "OK" : "MISMATCH");

            /*
             * ...and answering YES really does close it. A guard that vetoed
             * the close whatever you answered would pass every check above
             * and leave a window nobody can shut.
             */
            before_n = wm_window_count();
            wm_request_close(nw2);
            for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
            feed_key(PLAT_KEY_ENTER);        /* [Yes] is the default button */
            for (k2 = 0; k2 < 4; k2++) { sh_run_frame(); }
            SYS_LOGI("main", "UNSAVED-DEMO: answering yes closes it after all "
                     "(%d -> %d windows) (%s)", before_n, wm_window_count(),
                     (wm_window_count() < before_n) ? "OK" : "MISMATCH");

            /*
             * ...and the same for the other three editors.
             *
             * They track no "modified" flag at all -- each derives a checksum
             * of its own document instead, so there is no edit path that can
             * forget to set one. Each is driven the same way: open it, close
             * it untouched (must go), open it, change something, close it
             * (must stay and ask).
             *
             * The untouched half is not a formality. A checksum stamped at
             * the wrong moment -- before the sample workbook is filled in,
             * before Paint clears its canvas -- makes every fresh window look
             * edited, and the first thing anyone would notice is being
             * nagged for closing a window they never touched.
             */
            /*
             * ...and SHUTTING DOWN asks too.
             *
             * Shutdown ends the session loop without closing a window, so
             * every close-box guard above is bypassed on that path. The count
             * comes from asking each window (WM_MSG_QUERY_UNSAVED) rather
             * than from a list the shell keeps, because a list is a second
             * place to forget an app -- and the forgotten one is the one
             * whose work goes.
             */
            {
                int dirty0, dirty1;
                app_notepad_open();
                for (k2 = 0; k2 < 6; k2++) { sh_run_frame(); }
                nw2 = wm_focused();
                dirty0 = wm_unsaved_count();
                {
                    PlatEvent ke;
                    memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN;
                    ke.key = 'q'; ke.ch = 'q';
                    plat_host_push_event(&ke);
                    sh_run_frame();
                }
                dirty1 = wm_unsaved_count();
                SYS_LOGI("main", "UNSAVED-DEMO: the shell can see unsaved work "
                         "without a list of its own (%d before typing, %d "
                         "after) (%s)", dirty0, dirty1,
                         (dirty0 == 0 && dirty1 == 1) ? "OK" : "MISMATCH");

                /*
                 * ...and Shut Down asks rather than ending the session on the
                 * spot. sh_run_frame() returns whether the session is still
                 * running, which is the thing being checked -- a dialog that
                 * went up while the loop had already been told to stop would
                 * be no use to anybody.
                 */
                {
                    int n_before, still;
                    sh_dispatch_command(SH_CMD_SHUTDOWN);
                    for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
                    n_before = wm_window_count();
                    feed_key(PLAT_KEY_ENTER);      /* OK on the shutdown dialog */
                    still = 0;
                    for (k2 = 0; k2 < 3; k2++) {
                        if (sh_run_frame()) { still = 1; }
                    }
                    SYS_LOGI("main", "UNSAVED-DEMO: Shut Down with unsaved work "
                             "asks instead of ending the session (%d -> %d "
                             "windows, still running %d) (%s)",
                             n_before, wm_window_count(), still,
                             (still && wm_window_count() >= n_before)
                                 ? "OK" : "MISMATCH -- it just went");
                    feed_key(PLAT_KEY_ESC);        /* answer no */
                    for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }

                    /*
                     * ...and so do the OTHER ways out. Shut Down's dialog was
                     * the only one guarded; Restart Shell and Exit to DOS end
                     * the loop straight from the Start menu, and the host's
                     * quit event ends it with no menu at all. Three doors,
                     * and the guard was on the fourth.
                     */
                    {
                        int door, guarded = 0;
                        static const int DOORS[2] = {
                            SH_CMD_EXIT_TO_DOS, SH_CMD_RESTART_SHELL
                        };
                        for (door = 0; door < 2; door++) {
                            int alive = 0;
                            sh_dispatch_command(DOORS[door]);
                            for (k2 = 0; k2 < 3; k2++) {
                                if (sh_run_frame()) { alive = 1; }
                            }
                            if (alive) { guarded++; }
                            feed_key(PLAT_KEY_ESC);   /* answer no */
                            for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
                        }
                        SYS_LOGI("main", "UNSAVED-DEMO: Exit to DOS and Restart "
                                 "Shell ask as well (%d of 2 still running) "
                                 "(%s)", guarded,
                                 (guarded == 2) ? "OK" : "MISMATCH");
                    }
                }
                while (wm_window_count() > 0) {
                    WmWindow *dw6 = wm_window_at(0);
                    if (dw6 == NULL) { break; }
                    wm_destroy(dw6);
                }
                for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
            }

            {
                int a2;
                for (a2 = 0; a2 < 3; a2++) {
                    WmWindow *ew;
                    int base_n, untouched_n, changed_n;
                    const char *who = (a2 == 0) ? "CastaliaSheet"
                                    : (a2 == 1) ? "CastaliaWrite" : "Paint";
                    /* untouched */
                    base_n = wm_window_count();
                    if (a2 == 0)      { app_sheet_open(); }
                    else if (a2 == 1) { app_write_open(); }
                    else              { app_paint_open(); }
                    for (k2 = 0; k2 < 8; k2++) { sh_run_frame(); }
                    ew = wm_focused();
                    wm_request_close(ew);
                    for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
                    untouched_n = wm_window_count();

                    /* ...and again, with one change made through real input */
                    if (a2 == 0)      { app_sheet_open(); }
                    else if (a2 == 1) { app_write_open(); }
                    else              { app_paint_open(); }
                    for (k2 = 0; k2 < 8; k2++) { sh_run_frame(); }
                    ew = wm_focused();
                    if (a2 == 2) {
                        /* a stroke on the canvas */
                        CPoint po = wm_client_origin(ew);
                        PlatEvent me;
                        memset(&me, 0, sizeof(me));
                        me.type = PLAT_EV_MOUSE_DOWN;
                        me.buttons = PLAT_MB_LEFT;
                        me.mouse_x = po.x + 57 + 60; me.mouse_y = po.y + 21 + 60;
                        plat_host_push_event(&me);
                        me.type = PLAT_EV_MOUSE_MOVE;
                        me.mouse_x = po.x + 57 + 140;
                        plat_host_push_event(&me);
                        me.type = PLAT_EV_MOUSE_UP; me.buttons = 0;
                        plat_host_push_event(&me);
                        sh_run_frame();
                    } else {
                        const char *t = "Z";
                        int j;
                        for (j = 0; t[j] != '\0'; j++) {
                            PlatEvent ke;
                            memset(&ke, 0, sizeof(ke));
                            ke.type = PLAT_EV_KEY_DOWN;
                            ke.key = (unsigned char)t[j];
                            ke.ch  = (unsigned char)t[j];
                            plat_host_push_event(&ke);
                        }
                        if (a2 == 0) {
                            PlatEvent en;
                            memset(&en, 0, sizeof(en));
                            en.type = PLAT_EV_KEY_DOWN;
                            en.key = PLAT_KEY_ENTER; en.ch = '\n';
                            plat_host_push_event(&en);   /* commit the cell */
                        }
                        sh_run_frame();
                    }
                    base_n = wm_window_count();
                    wm_request_close(ew);
                    for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
                    changed_n = wm_window_count();
                    SYS_LOGI("main", "UNSAVED-DEMO: %s closes untouched but "
                             "asks once changed (%d after untouched, %d -> %d "
                             "changed) (%s)", who, untouched_n, base_n,
                             changed_n,
                             (untouched_n == 0 && changed_n > base_n)
                                 ? "OK" : "MISMATCH");
                    /* leave nothing behind for the next app's counts */
                    while (wm_window_count() > 0) {
                        WmWindow *dw5 = wm_window_at(0);
                        if (dw5 == NULL) { break; }
                        wm_destroy(dw5);
                    }
                    for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }
                }
            }
        }
        if (opt->cost_demo) {
            /*
             * What one keystroke costs, in pixels composited and pushed.
             *
             * Typing in Notepad repainted the WINDOW: 167,325 pixels per
             * character, 111% of the client -- the whole text area plus the
             * frame and the shadow strip every dirty rect is inflated by. On
             * the target each of those crosses the ISA bus, and that is the
             * difference between an editor that feels instant and one that
             * feels like a modem. It is now the edited row and the status
             * line, and nothing else.
             *
             * Measured after the OPEN ANIMATION has finished. It draws a
             * growing outline and marks its bounds dirty every frame, so
             * measuring through it measures the animation: the first version
             * of this scene reported 73,491 for a keystroke that cost 17,091,
             * and 180,671 for the one after it.
             */
            WmWindow *nw3;
            CRect cr;
            long client_px, typed, entered;
            int q;

            app_notepad_open();
            for (q = 0; q < 24; q++) { sh_run_frame(); }
            nw3 = wm_focused();
            cr = wm_client_rect(nw3);
            client_px = (long)crect_w(&cr) * (long)crect_h(&cr);

            feed_char('a');
            feed_char('b');
            typed = sh_last_frame_px();
            SYS_LOGI("main", "COST-DEMO: one character costs %ld px of a "
                     "%ld-pixel client (%ld%%) (%s)", typed, client_px,
                     (client_px > 0) ? (typed * 100 / client_px) : 0,
                     (typed > 0 && typed * 4 < client_px) ? "OK" : "MISMATCH");

            /* ...and what it drew is what a full repaint would have drawn.
             * This is the check that stops a region which is too SMALL: a
             * band that missed the row it was meant to cover leaves the old
             * glyphs on screen, and only this notices. */
            probe_repaint("COST-DEMO", "the page after typing into it");

            /*
             * Enter adds a row, which pushes every row under it down, so the
             * band runs to the bottom of the well -- more than a character
             * and still not the window. The point of measuring it is that the
             * rule has two branches and only one of them was on the hot path.
             */
            feed_key(PLAT_KEY_ENTER);
            feed_char('c');
            feed_key(PLAT_KEY_ENTER);
            entered = sh_last_frame_px();
            SYS_LOGI("main", "COST-DEMO: a newline costs %ld px (%ld%%) (%s)",
                     entered, (client_px > 0) ? (entered * 100 / client_px) : 0,
                     (entered > 0 && entered <= client_px) ? "OK" : "MISMATCH");
            probe_repaint("COST-DEMO", "the page after a newline");

            {
                long wrapped;
                app_notepad_open();              /* a fresh, empty page */
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                nw3 = wm_focused();
                feed_char('x');
                wrapped = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: and on a second window it is the "
                         "same %ld px (%s)", wrapped,
                         (wrapped == typed) ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the second page after typing");
            }

            /*
             * ---- and the Console, which is the other window people type
             *      into all day.
             *
             * Its scrollback does not move while you are typing a command --
             * only the prompt line does -- and it was repainting every line
             * of it per character. The prompt is NOT the bottom row of the
             * window: the scrollback is drawn from the top, so with four
             * lines of output the prompt sits on row five, and the region
             * has to follow it there.
             */
            {
                WmWindow *cw3;
                CRect ccr;
                long cclient, ctyped;
                app_console_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                cw3 = wm_focused();
                ccr = wm_client_rect(cw3);
                cclient = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                /* Run something first, so the scrollback is not empty and
                 * the prompt is somewhere other than the first row. */
                demo_console_cmd("ver");
                feed_char('d');
                feed_char('i');
                ctyped = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: one character in the Console "
                         "costs %ld px of a %ld-pixel client (%ld%%) (%s)",
                         ctyped, cclient,
                         (cclient > 0) ? (ctyped * 100 / cclient) : 0,
                         (ctyped > 0 && ctyped * 4 < cclient)
                             ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the console after typing into it");
            }

            /*
             * ---- a cell being typed into ---------------------------------
             *
             * Nothing in a spreadsheet is re-evaluated until an edit is
             * COMMITTED, so while somebody is typing the only things that
             * can differ are the cell, the formula bar echoing it, the name
             * box and the status line. Moving the cursor, committing and
             * clearing still repaint whole, and should: each of those really
             * can change any cell in the book.
             */
            {
                WmWindow *ww;
                CRect wcr;
                long wclient, wtyped;

                app_sheet_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                ww = wm_focused();
                wcr = wm_client_rect(ww);
                wclient = (long)crect_w(&wcr) * (long)crect_h(&wcr);
                feed_char('7');
                feed_char('5');
                wtyped = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: one character in CastaliaSheet "
                         "costs %ld px of a %ld-pixel client (%ld%%) (%s)",
                         wtyped, wclient,
                         (wclient > 0) ? (wtyped * 100 / wclient) : 0,
                         (wtyped > 0 && wtyped * 4 < wclient)
                             ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the sheet after typing into a cell");

                /* ...and the number really is in the cell afterwards, not
                 * merely somewhere the repaint happened to cover. */
                app_write_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                ww = wm_focused();
                wcr = wm_client_rect(ww);
                wclient = (long)crect_w(&wcr) * (long)crect_h(&wcr);
                /*
                 * CastaliaWrite reflows on every key, so the line breaks
                 * UNDER the caret can move and the rows below it really may
                 * all be different -- but the rows above it cannot, and
                 * neither can the toolbars, the ruler or the grey workspace
                 * around the page.
                 *
                 * Measured at BOTH ends of the page, because a region that
                 * runs from the caret downwards is cheap exactly where a
                 * document is being written and expensive on the first line,
                 * and reporting only one of those would be choosing the
                 * number.
                 */
                feed_char('z');
                wtyped = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: a character on the FIRST line of "
                         "CastaliaWrite costs %ld px of %ld (%ld%%) (%s)",
                         wtyped, wclient,
                         (wclient > 0) ? (wtyped * 100 / wclient) : 0,
                         (wtyped > 0 && wtyped < wclient) ? "OK" : "MISMATCH");
                for (q = 0; q < 14; q++) { feed_key(PLAT_KEY_ENTER); }
                feed_char('y');
                wtyped = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: ...and on the fifteenth, %ld px "
                         "(%ld%%) (%s)", wtyped,
                         (wclient > 0) ? (wtyped * 100 / wclient) : 0,
                         (wtyped > 0 && wtyped * 2 < wclient)
                             ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the page after typing into it");
            }

            /*
             * ---- and what the POINTER costs -----------------------------
             *
             * Lighting a button under the pointer is a treatment added a few
             * commits ago, and every window that got it repainted WHOLE on
             * every transition -- so sweeping the mouse across a toolbar cost
             * one full window repaint per button it passed over. The two
             * buttons whose drawing actually changed are the one the pointer
             * left and the one it arrived on, and ui_hot_left/ui_hot_hot are
             * how a window asks which those were.
             */
            {
                WmWindow *cc;
                CRect ccr, b0, b1;
                long ccl, moved;
                app_control_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                cc = wm_focused();
                ccr = wm_client_rect(cc);
                ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                /*
                 * From one BUTTON to another, not from empty space onto one.
                 * Moving in from outside leaves nothing behind, so a window
                 * that forgot to repaint the button the pointer LEFT would
                 * pass -- the first version of this check did exactly that.
                 */
                if (!app_control_btn_rect(cc, 0, &b0) ||
                    !app_control_btn_rect(cc, 2, &b1)) {
                    SYS_LOGI("main", "COST-DEMO: no Control Center buttons "
                             "to hover (MISMATCH)");
                    b0 = ccr; b1 = ccr;
                }
                feed_move((b0.x0 + b0.x1) / 2, (b0.y0 + b0.y1) / 2);
                feed_move((b1.x0 + b1.x1) / 2, (b1.y0 + b1.y1) / 2);
                moved = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: hovering a Control Center button "
                         "costs %ld px of %ld (%ld%%) (%s)", moved, ccl,
                         (ccl > 0) ? (moved * 100 / ccl) : 0,
                         (moved > 0 && moved * 10 < ccl) ? "OK" : "MISMATCH");
                /* ...and two buttons repainted is what a full repaint would
                 * have drawn. A window that lit the wrong one, or forgot to
                 * put the one the pointer left back, fails here. */
                probe_repaint("COST-DEMO", "the panel after a hover");

                /* The Theme editor has eight of them in two clusters. */
                app_theme_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                cc = wm_focused();
                ccr = wm_client_rect(cc);
                ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                feed_move(ccr.x0 + 10, ccr.y0 + 10);
                feed_move(ccr.x1 - 60, ccr.y1 - 30);
                moved = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: hovering a Theme editor button "
                         "costs %ld px of %ld (%ld%%) (%s)", moved, ccl,
                         (ccl > 0) ? (moved * 100 / ccl) : 0,
                         (moved > 0 && moved * 10 < ccl) ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the theme editor after a hover");

                /*
                 * ...and one more, driven the same way, because two windows
                 * agreeing proves the helper and not the two windows. The
                 * Viewer's toolbar is two buttons in a 460x320 window, which
                 * is the shape most of them are.
                 */
                app_view_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                cc = wm_focused();
                ccr = wm_client_rect(cc);
                ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                feed_move(ccr.x0 + 30, ccr.y0 + 12);
                feed_move(ccr.x0 + 90, ccr.y0 + 12);
                moved = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: hovering a Viewer button costs "
                         "%ld px of %ld (%ld%%) (%s)", moved, ccl,
                         (ccl > 0) ? (moved * 100 / ccl) : 0,
                         (moved > 0 && moved * 6 < ccl) ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the viewer after a hover");

                /* ...and LEAVING it, which is the other half of the same
                 * gesture: the pointer that crosses a window also leaves it. */
                feed_move(4, 4);
                moved = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: leaving the Viewer with a button "
                         "lit costs %ld px of %ld (%ld%%) (%s)", moved, ccl,
                         (ccl > 0) ? (moved * 100 / ccl) : 0,
                         (moved > 0 && moved * 6 < ccl) ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the viewer after leaving it");

                /*
                 * The Calculator is the hard case, and the reason this check
                 * is here rather than stopping at the Viewer: it is a window
                 * that IS its buttons, a grid of thirty with row and column
                 * spans, so "the rectangle of key N" is not a lookup in a
                 * layout struct the way it is everywhere else. Its hover
                 * path was still repainting whole -- it lives in its own
                 * handler rather than in the message switch, which is
                 * exactly why the first sweep through these files missed it.
                 */
                app_calc_open();
                for (q = 0; q < 24; q++) { sh_run_frame(); }
                cc = wm_focused();
                ccr = wm_client_rect(cc);
                ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                if (!app_calc_key_rect(cc, 2, 0, &b0) ||
                    !app_calc_key_rect(cc, 2, 1, &b1)) {
                    SYS_LOGI("main", "COST-DEMO: no Calculator keys to hover "
                             "(MISMATCH)");
                    b0 = ccr; b1 = ccr;
                }
                feed_move((b0.x0 + b0.x1) / 2, (b0.y0 + b0.y1) / 2);
                feed_move((b1.x0 + b1.x1) / 2, (b1.y0 + b1.y1) / 2);
                moved = sh_last_frame_px();
                SYS_LOGI("main", "COST-DEMO: hovering a Calculator key costs "
                         "%ld px of %ld (%ld%%) (%s)", moved, ccl,
                         (ccl > 0) ? (moved * 100 / ccl) : 0,
                         (moved > 0 && moved * 6 < ccl) ? "OK" : "MISMATCH");
                probe_repaint("COST-DEMO", "the keypad after a hover");

                /*
                 * ...and the open MENU, which is the most expensive thing
                 * the pointer does over a window. A drop-down highlights the
                 * row under it, and every editor here repainted its whole
                 * window on every row the pointer crossed -- so one slide
                 * down a six-item File menu cost six full repaints of a
                 * 660x440 client to move one 100x16 bar.
                 *
                 * The scene walks DOWN the open menu asking the app which
                 * row is lit, rather than assuming a row height: it measures
                 * the frame where the answer changed, so it is measuring a
                 * real transition between two real rows. If it never finds
                 * one it reports a mismatch rather than nothing, because a
                 * menu that stopped opening would otherwise look like a
                 * window that got very cheap.
                 */
                {
                    WmWindow *nw;
                    CRect word;
                    CPoint no;
                    char firstlab[64];
                    const char *lab;
                    int mx, my, yy;
                    long menupx = -1;

                    app_notepad_open();
                    for (q = 0; q < 24; q++) { sh_run_frame(); }
                    nw = wm_focused();
                    no = wm_client_origin(nw);
                    ccr = wm_client_rect(nw);
                    ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                    word = app_notepad_menu_word(nw, 0);      /* File */
                    feed_click(no.x + (word.x0 + word.x1) / 2,
                               no.y + (word.y0 + word.y1) / 2);
                    mx = no.x + word.x0 + 20;   /* past the icon gutter */
                    my = no.y + word.y1;
                    firstlab[0] = '\0';
                    for (yy = 2; yy < 140; yy += 2) {
                        char nowlab[64];
                        feed_move(mx, my + yy);
                        lab = app_notepad_menu_item(nw);
                        if (lab == NULL) { continue; }
                        menu_name(nowlab, (int)sizeof nowlab, lab);
                        if (firstlab[0] == '\0') {
                            sys_strlcpy(firstlab, nowlab, sizeof firstlab);
                            continue;
                        }
                        if (strcmp(nowlab, firstlab) != 0) {
                            menupx = sh_last_frame_px();
                            break;
                        }
                    }
                    SYS_LOGI("main", "COST-DEMO: moving the highlight off "
                             "\"%s\" in Notepad's File menu costs %ld px of "
                             "%ld (%ld%%) (%s)", firstlab, menupx, ccl,
                             (ccl > 0 && menupx > 0) ? (menupx * 100 / ccl) : 0,
                             (menupx > 0 && menupx * 8 < ccl) ? "OK"
                                                             : "MISMATCH");
                    probe_repaint("COST-DEMO",
                                  "the open menu after the highlight moved");

                    /*
                     * ...and the same menu walked with the KEYBOARD, which
                     * is the same highlight moving between the same two
                     * rows and was costing the same full window. The four
                     * office apps share one menu-navigation function, so
                     * this measures the arrangement all four use.
                     */
                    feed_key(PLAT_KEY_DOWN);
                    moved = sh_last_frame_px();
                    SYS_LOGI("main", "COST-DEMO: and Down through it costs "
                             "%ld px of %ld (%ld%%) (%s)", moved, ccl,
                             (ccl > 0) ? (moved * 100 / ccl) : 0,
                             (moved > 0 && moved * 8 < ccl) ? "OK"
                                                            : "MISMATCH");
                    probe_repaint("COST-DEMO", "the menu after a Down key");
                }

                /*
                 * ---- and what SCROLLING costs -----------------------------
                 *
                 * Dragging a scroll thumb is the third continuous gesture in
                 * this desktop, alongside sweeping a toolbar and sliding down
                 * a menu, and it was the last one still repainting whole
                 * windows: nine of them, every editor and every list. The
                 * text moved, so the text well and the bar have to come back
                 * -- but the menu bar, the toolbar and the status line did
                 * not move at all.
                 *
                 * The Log Viewer is the window driven here because it is the
                 * one guaranteed to have something long enough to scroll: by
                 * the time this scene runs, the session log is hundreds of
                 * lines.
                 */
                {
                    WmWindow *lw;
                    CRect bar;
                    lw = NULL;
                    app_logview_open();
                    for (q = 0; q < 24; q++) { sh_run_frame(); }
                    lw = wm_focused();
                    ccr = wm_client_rect(lw);
                    ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                    if (!app_logview_vbar(lw, &bar)) {
                        SYS_LOGI("main", "COST-DEMO: no Log Viewer scroll bar "
                                 "to drag (MISMATCH)");
                        bar = ccr;
                    }
                    /*
                     * Press ON the thumb, which means inside the TRACK: the
                     * last UI_SB_W pixels of the bar are the down arrow, and
                     * a press there steps by a line instead of grabbing
                     * anything. The view is at the bottom when the window
                     * opens, so the thumb is at the track's bottom end.
                     */
                    {
                        int sx = (bar.x0 + bar.x1) / 2;
                        int t0 = bar.y0 + UI_SB_W, t1 = bar.y1 - UI_SB_W;
                        feed_press(sx, t1 - 6);
                        feed_drag(sx, (t0 + t1) / 2);
                        feed_drag(sx, (t0 + t1) / 2 - 8);
                    }
                    moved = sh_last_frame_px();
                    /*
                     * Bounded by what a scroll actually CHANGES -- the well
                     * and the bar -- rather than by a percentage of the
                     * window somebody picked. All the lines moved, so most of
                     * the well is honestly dirty; the toolbar, the path bar
                     * and the status line are not, and they are what this
                     * refuses to pay for. The slack is the mouse pointer,
                     * which is composited over whatever it is on.
                     */
                    {
                        CRect well, moved_rgn;
                        long allowed;
                        if (!app_logview_well(lw, &well)) { well = ccr; }
                        /*
                         * The well and the bar sit side by side and together
                         * span the client, so what moved is one rectangle.
                         *
                         * Grown by a few pixels on the right and bottom
                         * because wm_dirty_add inflates EVERY dirty rectangle
                         * by the drop-shadow margin -- deliberately, so no
                         * call site has to know about shadows -- and the
                         * allowance has to admit that or it is measuring the
                         * shadow policy rather than the repaint. Plus the
                         * mouse pointer, which is composited wherever it is.
                         */
                        moved_rgn = crect_union(&well, &bar);
                        moved_rgn.x1 += 6;
                        moved_rgn.y1 += 6;
                        allowed = (long)crect_w(&moved_rgn) *
                                  (long)crect_h(&moved_rgn) + 4096;
                        SYS_LOGI("main", "COST-DEMO: dragging the Log Viewer's "
                                 "scroll thumb costs %ld px of %ld (%ld%%), "
                                 "against %ld that moved (%s)",
                                 moved, ccl, (ccl > 0) ? (moved * 100 / ccl) : 0,
                                 allowed,
                                 (moved > 0 && moved <= allowed) ? "OK"
                                                                 : "MISMATCH");
                    }
                    feed_release((bar.x0 + bar.x1) / 2,
                                 (bar.y0 + bar.y1) / 2 - 8);
                    probe_repaint("COST-DEMO", "the log after a scroll drag");

                    /*
                     * ...and the ARROW, which is the same gesture at a
                     * different speed: people hold a scroll arrow down, and
                     * it was the half of the scroll path still repainting
                     * the window after the thumb drag was narrowed. The last
                     * UI_SB_W pixels of the bar are the down arrow.
                     */
                    {
                        CRect well2, moved2;
                        long allowed3, released, barpx;
                        int ax2 = (bar.x0 + bar.x1) / 2, ay2 = bar.y1 - 6;
                        /*
                         * BOTH frames, because a click is two of them and
                         * they cost different things. The press scrolls a
                         * line -- the well and the bar. The release only
                         * takes the arrow back from held to idle, and
                         * measuring the pair as one number would have hidden
                         * which half was expensive: it was the release, and
                         * it stayed at 110% of the window for a while after
                         * the press had been narrowed.
                         */
                        feed_press(ax2, ay2);
                        moved = sh_last_frame_px();
                        feed_release(ax2, ay2);
                        released = sh_last_frame_px();
                        if (!app_logview_well(lw, &well2)) { well2 = ccr; }
                        moved2 = crect_union(&well2, &bar);
                        moved2.x1 += 6;
                        moved2.y1 += 6;
                        allowed3 = (long)crect_w(&moved2) *
                                   (long)crect_h(&moved2) + 4096;
                        barpx = (long)(crect_w(&bar) + 6) *
                                (long)(crect_h(&bar) + 6) + 4096;
                        SYS_LOGI("main", "COST-DEMO: pressing its scroll arrow "
                                 "costs %ld px (%ld%%), against %ld that moved "
                                 "(%s)", moved,
                                 (ccl > 0) ? (moved * 100 / ccl) : 0, allowed3,
                                 (moved > 0 && moved <= allowed3) ? "OK"
                                                                  : "MISMATCH");
                        SYS_LOGI("main", "COST-DEMO: ...and letting go costs "
                                 "%ld px (%ld%%), against %ld for the bar "
                                 "alone (%s)", released,
                                 (ccl > 0) ? (released * 100 / ccl) : 0, barpx,
                                 (released > 0 && released <= barpx) ? "OK"
                                                                     : "MISMATCH");
                        probe_repaint("COST-DEMO",
                                      "the log after a scroll arrow");
                    }

                    /*
                     * ---- and the START MENU ------------------------------
                     *
                     * The most-used menu in the system, and the one the
                     * window menus' narrowing never reached: it is a shell
                     * layer rather than a window, so it marks its own
                     * rectangle dirty and that rectangle is the whole panel.
                     * Sliding the pointer down it repaints all of it, once
                     * per row crossed.
                     */
                    {
                        CRect lr;
                        long lpanel, slid;
                        int f3, from_row, to_row;
                        sh_open_launcher(CTRUE);
                        for (f3 = 0; f3 < 24; f3++) { sh_run_frame(); }
                        lr = g_sh.launcher_rect;
                        lpanel = (long)crect_w(&lr) * (long)crect_h(&lr);
                        /*
                         * Down the left column, from one real row to the next
                         * one below it -- and the rows are CHECKED, not
                         * assumed. The first spelling of this walked from
                         * y0+30 to y0+48, and y0+30 is inside the 40-pixel
                         * header: the pointer arrived on a row from nowhere,
                         * so only ever ONE row was marked, and deleting the
                         * marking of the row being LEFT changed neither the
                         * cost nor the repaint. A check whose gesture does
                         * not perform the motion it names cannot fail on the
                         * bug it is aimed at.
                         */
                        feed_move(lr.x0 + 40, lr.y0 + 52);
                        from_row = g_sh.launcher_menu.highlight;
                        feed_move(lr.x0 + 40, lr.y0 + 70);
                        to_row = g_sh.launcher_menu.highlight;
                        slid = sh_last_frame_px();
                        SYS_LOGI("main", "COST-DEMO: the slide really crossed "
                                 "rows: %d -> %d (%s)", from_row, to_row,
                                 (from_row >= 0 && to_row >= 0
                                  && from_row != to_row) ? "OK" : "MISMATCH");
                        SYS_LOGI("main", "COST-DEMO: sliding down the Start "
                                 "menu costs %ld px of its %ld-pixel panel "
                                 "(%ld%%) (%s)", slid, lpanel,
                                 (lpanel > 0) ? (slid * 100 / lpanel) : 0,
                                 (slid > 0 && slid * 3 < lpanel) ? "OK"
                                                                 : "MISMATCH");
                        probe_repaint_rect("COST-DEMO",
                                           "the Start menu after a slide",
                                           &lr);
                        /*
                         * ...and the same menu walked with the ARROW KEYS,
                         * which move exactly the highlight the pointer moves.
                         * There is no reason for the keyboard to cost a panel
                         * where the mouse costs two rows, and until this was
                         * asked it did: the key handler had its own
                         * whole-rectangle marking, a floor below the one the
                         * pointer used, and narrowing one of them silently
                         * leaves the other.
                         */
                        {
                            int was_hl, now_hl;
                            long keyed;
                            feed_key(PLAT_KEY_DOWN);
                            feed_key(PLAT_KEY_DOWN);
                            was_hl = g_sh.launcher_menu.highlight;
                            feed_key(PLAT_KEY_DOWN);
                            now_hl = g_sh.launcher_menu.highlight;
                            keyed = sh_last_frame_px();
                            SYS_LOGI("main", "COST-DEMO: arrowing down the "
                                     "Start menu (%d -> %d) costs %ld px of "
                                     "%ld (%ld%%) (%s)", was_hl, now_hl, keyed,
                                     lpanel,
                                     (lpanel > 0) ? (keyed * 100 / lpanel) : 0,
                                     (was_hl >= 0 && now_hl >= 0
                                      && was_hl != now_hl && keyed > 0
                                      && keyed * 3 < lpanel) ? "OK"
                                                             : "MISMATCH");
                            probe_repaint_rect("COST-DEMO",
                                               "the Start menu after arrowing",
                                               &lr);
                        }
                        /*
                         * ---- and the shadow, painted TWICE ---------------
                         *
                         * Painting a popup once per region rect is only safe
                         * if the rects do not overlap, and cregion_add does
                         * not promise that: it merges an overlapping neighbour
                         * only when the union wastes little area, and appends
                         * it otherwise. A drop shadow is not a colour, it is a
                         * DARKENING -- gfx_drop_shadow reads each pixel, runs
                         * it through a falloff and writes it back -- so a
                         * pixel of shadow inside two region rects gets
                         * darkened twice and comes out too dark. It is the
                         * same read-modify-write the mouse pointer's shadow
                         * turned black on, arrived at from the other side.
                         *
                         * Two crossing strips make the region keep both rects:
                         * their union is enormous next to their areas, so the
                         * merge is declined, and they overlap exactly where
                         * they cross -- placed here on the panel's right
                         * shadow band.
                         */
                        {
                            CRect band, horiz, vert;
                            band = crect_make(lr.x1, lr.y0 + 8, 5,
                                              crect_h(&lr) - 16);
                            horiz = crect_make(lr.x1 - 180, lr.y0 + 40, 190, 8);
                            vert  = crect_make(lr.x1 - 2, lr.y0 + 10, 8,
                                               crect_h(&lr) - 20);
                            sh_mark_dirty(&horiz);
                            sh_mark_dirty(&vert);
                            sh_run_frame();
                            probe_repaint_rect("COST-DEMO",
                                               "the Start menu's shadow under "
                                               "two crossing dirty rects",
                                               &band);
                        }
                        sh_open_launcher(CFALSE);
                        for (f3 = 0; f3 < 12; f3++) { sh_run_frame(); }

                        /*
                         * ---- and the SLIDE-UP, which is where a clip that
                         * replaces rather than intersects shows up ---------
                         *
                         * The panel rises out of the taskbar over five
                         * frames, drawn whole and clipped to the part that
                         * has risen so far. Its columns then set a clip of
                         * their own, and a clip that REPLACES the one already
                         * in force undoes that: the rows get drawn at their
                         * final positions over a desktop the panel has not
                         * reached yet, so a menu that is a third of the way
                         * up shows its whole list floating above itself.
                         *
                         * gfx_set_clip takes what it is given -- it does not
                         * intersect -- so this is a property of every nested
                         * clip in the tree, not a quirk of this one.
                         */
                        if (settings_get()->animations) {
                            /*
                             * Inside the COLUMN band, above the reveal line.
                             * The first spelling watched the top 20 px of the
                             * panel, which is the header -- painted by
                             * another function under another clip, so the
                             * control could not move it and the check could
                             * not fail. Two frames in, the panel has risen
                             * two fifths; the band starts 40 px down; this
                             * strip is comfortably between the two.
                             */
                            CRect above = crect_make(lr.x0, lr.y0 + 50,
                                                     crect_w(&lr), 60);
                            int stale;
                            probe_snap(&above);
                            sh_open_launcher(CTRUE);
                            sh_run_frame();
                            sh_run_frame();
                            stale = probe_diff();
                            SYS_LOGI("main", "COST-DEMO: two frames into the "
                                     "slide-up, nothing is drawn above where "
                                     "the panel has risen to -- %d px (%s)",
                                     stale, (stale == 0) ? "OK" : "MISMATCH");
                            sh_open_launcher(CFALSE);
                            for (f3 = 0; f3 < 12; f3++) { sh_run_frame(); }
                        }
                    }

                    /*
                     * ---- and the DESKTOP CONTEXT MENU ---------------------
                     *
                     * The third menu in the system and the third spelling of
                     * the same rule. The window menus were narrowed through
                     * ui_menu_repaint(), the Start panel through its own
                     * geometry; this one is a plain UiMenu like the window
                     * menus but lives in screen coordinates like the Start
                     * panel, so it fell between the two and kept marking its
                     * whole rectangle per row crossed.
                     */
                    {
                        CRect cr;
                        long cpanel, cslid;
                        int f4, c_from, c_to;
                        sh_context_open_desktop(300, 200);
                        for (f4 = 0; f4 < 12; f4++) { sh_run_frame(); }
                        cr = g_sh.ctx_rect;
                        cpanel = (long)crect_w(&cr) * (long)crect_h(&cr);
                        feed_move(cr.x0 + 20, cr.y0 + 8);
                        c_from = g_sh.ctx_menu.highlight;
                        feed_move(cr.x0 + 20, cr.y0 + 26);
                        c_to = g_sh.ctx_menu.highlight;
                        cslid = sh_last_frame_px();
                        SYS_LOGI("main", "COST-DEMO: sliding down the desktop "
                                 "menu (%d -> %d) costs %ld px of its "
                                 "%ld-pixel panel (%ld%%) (%s)", c_from, c_to,
                                 cslid, cpanel,
                                 (cpanel > 0) ? (cslid * 100 / cpanel) : 0,
                                 (c_from >= 0 && c_to >= 0 && c_from != c_to
                                  && cslid > 0 && cslid * 2 < cpanel)
                                     ? "OK" : "MISMATCH");
                        probe_repaint_rect("COST-DEMO",
                                           "the desktop menu after a slide",
                                           &cr);
                        sh_context_close();
                        for (f4 = 0; f4 < 8; f4++) { sh_run_frame(); }
                    }

                    /*
                     * ---- and what MOVING a window costs -------------------
                     *
                     * Dragging is the most visible gesture a desktop has and
                     * the only one that repaints two places at once: the area
                     * the window left and the area it now covers. There is no
                     * narrowing to be had -- both really did change -- so the
                     * number here is a floor, and it is the number that
                     * decides whether a window follows the pointer or drags
                     * behind it on the machine this is built for.
                     */
                    {
                        WmWindow *dw2;
                        CRect df;
                        long moved3;
                        int f2;
                        app_notepad_open();
                        for (f2 = 0; f2 < 24; f2++) { sh_run_frame(); }
                        dw2 = wm_focused();
                        df = crect_make(120, 90, 400, 280);
                        wm_set_frame(dw2, &df);
                        for (f2 = 0; f2 < 8; f2++) { sh_run_frame(); }
                        feed_press(df.x0 + 100, df.y0 + 6);   /* title bar */
                        feed_drag(df.x0 + 104, df.y0 + 10);
                        feed_drag(df.x0 + 108, df.y0 + 14);
                        moved3 = sh_last_frame_px();
                        feed_release(df.x0 + 108, df.y0 + 14);
                        SYS_LOGI("main", "COST-DEMO: moving a window four "
                                 "pixels costs %ld px of a %ld-pixel frame "
                                 "(%ld%%)", moved3,
                                 (long)crect_w(&df) * (long)crect_h(&df),
                                 (crect_w(&df) > 0)
                                     ? (moved3 * 100 /
                                        ((long)crect_w(&df) *
                                         (long)crect_h(&df))) : 0);

                        /*
                         * ...and the same drag by OUTLINE ([Shell]
                         * DragOutline), which is what the systems this
                         * desktop copies did by default on hardware this
                         * old. The window stays put and a rubber band
                         * follows the pointer; the move happens once, on
                         * release.
                         *
                         * Three separate claims, because the cheap one is
                         * worthless without the other two: it must be much
                         * cheaper, the window must NOT have moved while the
                         * band was out, and it must end up where the band
                         * did -- with nothing left on the desktop where the
                         * band has been.
                         */
                        {
                            WmWindow *ow2;
                            CRect held, landed, watch;
                            long outpx;
                            /*
                             * A FRESH window, not the one just dragged.
                             *
                             * The title bar treats a second press on the same
                             * window inside the double-click window as
                             * maximize -- and these frames run far faster than
                             * a person clicks, so dragging the same window
                             * twice in a row maximized it and every check
                             * below read 800x570 at 0,0. That cost a bisect.
                             */
                            settings_get()->drag_outline = CTRUE;
                            app_notepad_open();
                            for (f2 = 0; f2 < 24; f2++) { sh_run_frame(); }
                            ow2 = wm_focused();
                            wm_set_frame(ow2, &df);
                            for (f2 = 0; f2 < 8; f2++) { sh_run_frame(); }
                            watch = crect_make(df.x0 - 20, df.y0 - 20,
                                               crect_w(&df) + 140,
                                               crect_h(&df) + 140);
                            feed_press(df.x0 + 100, df.y0 + 6);
                            feed_drag(df.x0 + 140, df.y0 + 46);
                            feed_drag(df.x0 + 144, df.y0 + 50);
                            outpx = sh_last_frame_px();
                            held = wm_frame_rect(ow2);
                            SYS_LOGI("main", "COST-DEMO: ...and by OUTLINE it "
                                     "costs %ld px (%ld%% of the frame), a "
                                     "%ldx saving (%s)", outpx,
                                     (crect_w(&df) > 0)
                                         ? (outpx * 100 /
                                            ((long)crect_w(&df) *
                                             (long)crect_h(&df))) : 0,
                                     (outpx > 0) ? (moved3 / outpx) : 0,
                                     (outpx > 0 && outpx * 8 < moved3)
                                         ? "OK" : "MISMATCH");
                            SYS_LOGI("main", "COST-DEMO: ...the window itself "
                                     "has not moved yet (%d,%d) (%s)",
                                     held.x0, held.y0,
                                     (held.x0 == df.x0 && held.y0 == df.y0)
                                         ? "OK" : "MISMATCH");
                            feed_release(df.x0 + 144, df.y0 + 50);
                            for (f2 = 0; f2 < 4; f2++) { sh_run_frame(); }
                            landed = wm_frame_rect(ow2);
                            SYS_LOGI("main", "COST-DEMO: ...and lands where the "
                                     "band was on release (%d,%d -> %d,%d) "
                                     "(%s)", df.x0, df.y0, landed.x0, landed.y0,
                                     (landed.x0 == df.x0 + 44 &&
                                      landed.y0 == df.y0 + 44)
                                         ? "OK" : "MISMATCH");
                            /* No band left on the desktop behind it. */
                            probe_repaint_rect("COST-DEMO",
                                               "the desktop after an outline "
                                               "drag", &watch);

                            /*
                             * ...and RESIZING, which is where the band saves
                             * the most: a resize sends WM_MSG_SIZE, so the
                             * window rebuilds its whole layout and repaints
                             * itself on every motion event, on top of the
                             * two areas the manager marks. The band defers
                             * all of it to the release.
                             *
                             * Grabbed at the bottom-right CORNER, inside the
                             * grip -- a press four pixels in from the edge is
                             * a click in the client area, and would measure a
                             * click.
                             */
                            {
                                CRect r0 = wm_frame_rect(ow2), r1;
                                long fullpx, bandpx;
                                settings_get()->drag_outline = CFALSE;
                                feed_press(r0.x1 - 1, r0.y1 - 1);
                                feed_drag(r0.x1 + 30, r0.y1 + 20);
                                feed_drag(r0.x1 + 34, r0.y1 + 24);
                                fullpx = sh_last_frame_px();
                                feed_release(r0.x1 + 34, r0.y1 + 24);
                                for (f2 = 0; f2 < 6; f2++) { sh_run_frame(); }

                                settings_get()->drag_outline = CTRUE;
                                r1 = wm_frame_rect(ow2);
                                feed_press(r1.x1 - 1, r1.y1 - 1);
                                feed_drag(r1.x1 - 30, r1.y1 - 20);
                                feed_drag(r1.x1 - 34, r1.y1 - 24);
                                bandpx = sh_last_frame_px();
                                SYS_LOGI("main", "COST-DEMO: resizing costs "
                                         "%ld px, and %ld by outline (%ldx) "
                                         "(%s)", fullpx, bandpx,
                                         (bandpx > 0) ? (fullpx / bandpx) : 0,
                                         (bandpx > 0 && bandpx * 4 < fullpx)
                                             ? "OK" : "MISMATCH");
                                {
                                    CRect held2 = wm_frame_rect(ow2);
                                    SYS_LOGI("main", "COST-DEMO: ...and the "
                                             "window is still its old size "
                                             "until the button comes up "
                                             "(%dx%d) (%s)",
                                             crect_w(&held2), crect_h(&held2),
                                             (crect_w(&held2) == crect_w(&r1) &&
                                              crect_h(&held2) == crect_h(&r1))
                                                 ? "OK" : "MISMATCH");
                                }
                                feed_release(r1.x1 - 34, r1.y1 - 24);
                                for (f2 = 0; f2 < 6; f2++) { sh_run_frame(); }
                                {
                                    CRect r2 = wm_frame_rect(ow2);
                                    SYS_LOGI("main", "COST-DEMO: ...and takes "
                                             "the band's size on release "
                                             "(%dx%d -> %dx%d) (%s)",
                                             crect_w(&r1), crect_h(&r1),
                                             crect_w(&r2), crect_h(&r2),
                                             /* The grip was taken one pixel
                                              * inside the corner, so the
                                              * travel is 33 and 23, not 34
                                              * and 24. */
                                             (crect_w(&r2) == crect_w(&r1) - 33
                                              && crect_h(&r2) ==
                                                 crect_h(&r1) - 23)
                                                 ? "OK" : "MISMATCH");
                                }
                                probe_repaint_rect("COST-DEMO",
                                                   "the desktop after an "
                                                   "outline resize", &watch);
                            }
                            settings_get()->drag_outline = CFALSE;
                        }
                    }

                    /*
                     * ---- and what NOTHING costs ---------------------------
                     *
                     * The cheapest frame in a desktop is the one where the
                     * user is reading. Every measurement above is about a
                     * gesture; this one is about the seconds between them,
                     * which is most of the time a machine is switched on and
                     * all of the time its fan is the loudest thing in the
                     * room.
                     *
                     * With windows OPEN, because that is where it goes wrong:
                     * a window that invalidates itself on every WM_MSG_TIMER
                     * whether or not its picture changed costs the same as a
                     * window somebody is typing in, forever, and nothing
                     * about it looks wrong on screen.
                     */
                    {
                        long idle = 0;
                        int f;
                        feed_move(4, 4);
                        for (f = 0; f < 30; f++) { sh_run_frame(); }
                        for (f = 0; f < 20; f++) {
                            sh_run_frame();
                            idle += sh_last_frame_px();
                        }
                        SYS_LOGI("main", "COST-DEMO: twenty idle frames with "
                                 "%d window(s) open composite %ld px in total "
                                 "(%s)", wm_window_count(), idle,
                                 (idle < 8192) ? "OK" : "MISMATCH");
                        /*
                         * Zero, and that is not a tautology: marking the
                         * taskbar dirty on every frame takes this to 480,195,
                         * which is what one thing repainting for no reason
                         * costs over twenty frames. The last window that did
                         * it was an unfocused Console blinking a caret it had
                         * no business drawing -- see --focus-demo.
                         */
                    }

                    /*
                     * ...and idle with a MENU OPEN, which is the same question
                     * asked where the answer used to be different.
                     *
                     * A menu is open for as long as somebody is deciding, and
                     * deciding is reading: the frames while a Start menu hangs
                     * there are idle frames in every sense except that the
                     * shell used to paint the whole panel through every one of
                     * them. Twenty of those cost 2,759,460 pixels -- nearly
                     * six times what the worst window ever did -- and the
                     * check above could not see it, because it ran with no
                     * menu open. So it is asked again, with one.
                     */
                    {
                        long midle = 0;
                        int f;
                        sh_open_launcher(CTRUE);
                        for (f = 0; f < 30; f++) { sh_run_frame(); }
                        for (f = 0; f < 20; f++) {
                            sh_run_frame();
                            midle += sh_last_frame_px();
                        }
                        SYS_LOGI("main", "COST-DEMO: twenty idle frames with "
                                 "the Start menu OPEN composite %ld px in "
                                 "total (%s)", midle,
                                 (midle < 8192) ? "OK" : "MISMATCH");
                        probe_repaint("COST-DEMO",
                                      "the desktop under an idle open menu");
                        sh_open_launcher(CFALSE);
                        for (f = 0; f < 12; f++) { sh_run_frame(); }
                    }

                    /*
                     * ...and idle under a TOOLTIP, which is the same bug in
                     * the overlay that is up the longest.
                     *
                     * A tooltip appears BECAUSE the pointer stopped moving and
                     * stays until it moves again, so "a tooltip is showing" and
                     * "the desktop is idle" are very nearly the same sentence.
                     * It is shown directly here rather than hovered into
                     * existence: the hover DELAY belongs to --taskbar-demo, and
                     * what is being measured is what an open one costs per
                     * frame. That it is still open at the end is checked, so
                     * this cannot quietly become a measurement of nothing.
                     */
                    {
                        long tidle = 0;
                        int f;
                        cbool still;
                        sh_tooltip_show("Idle cost of a tooltip", 200,
                                        g_sh.taskbar_rect.y0);
                        for (f = 0; f < 10; f++) { sh_run_frame(); }
                        for (f = 0; f < 20; f++) {
                            sh_run_frame();
                            tidle += sh_last_frame_px();
                        }
                        still = sh_tooltip_is_open();
                        SYS_LOGI("main", "COST-DEMO: twenty idle frames under "
                                 "an open tooltip composite %ld px in total, "
                                 "and it is still up: %d (%s)", tidle,
                                 (int)still,
                                 (still && tidle < 8192) ? "OK" : "MISMATCH");
                        probe_repaint("COST-DEMO",
                                      "the desktop under an idle tooltip");
                        sh_tooltip_hide();
                        for (f = 0; f < 6; f++) { sh_run_frame(); }
                    }

                    /*
                     * ...and idle under the ALT+TAB panel, the last overlay
                     * that was doing it.
                     *
                     * This one lingers 900 ms after the last press -- about
                     * fifty-four frames -- and it lingers precisely so that
                     * somebody can READ it and decide whether to press again.
                     * Every one of those frames is a frame where nothing is
                     * happening.
                     */
                    {
                        long sidle = 0;
                        int f;
                        cbool up;
                        feed_key(PLAT_KEY_NEXTWIN);
                        for (f = 0; f < 4; f++) { sh_run_frame(); }
                        for (f = 0; f < 20; f++) {
                            sh_run_frame();
                            sidle += sh_last_frame_px();
                        }
                        up = sh_switch_showing();
                        SYS_LOGI("main", "COST-DEMO: twenty idle frames under "
                                 "the Alt+Tab panel composite %ld px in "
                                 "total, and it is still up: %d (%s)", sidle,
                                 (int)up,
                                 (up && sidle < 8192) ? "OK" : "MISMATCH");
                        probe_repaint("COST-DEMO",
                                      "the desktop under an idle Alt+Tab "
                                      "panel");
                        /*
                         * And the press that MOVES the highlight still costs
                         * what it has to. A panel that repaints only when the
                         * region says so is worth nothing if the region stops
                         * saying so on the one frame the picture changes --
                         * which is the frame somebody is looking at.
                         */
                        {
                            long pressed;
                            feed_key(PLAT_KEY_NEXTWIN);
                            pressed = sh_last_frame_px();
                            SYS_LOGI("main", "COST-DEMO: ...and the press that "
                                     "moves its highlight costs %ld px (%s)",
                                     pressed,
                                     (pressed > 0) ? "OK" : "MISMATCH");
                            probe_repaint("COST-DEMO",
                                          "the Alt+Tab panel after a press");
                        }
                    }

                    /*
                     * ...and the same drag in an EDITOR, because the two
                     * windows narrow to different rectangles -- a list well
                     * and a text well with a toolbar, a menu bar and a
                     * status line around it -- and one of them agreeing
                     * proves one of them. The page needs to be longer than
                     * the window before there is a thumb to grab, which is
                     * what the newlines are for.
                     */
                    {
                        WmWindow *ew2;
                        CRect nbar, nwell, nmoved;
                        long allowed2;
                        int k3;
                        app_notepad_open();
                        for (q = 0; q < 24; q++) { sh_run_frame(); }
                        ew2 = wm_focused();
                        for (k3 = 0; k3 < 60; k3++) { feed_key(PLAT_KEY_ENTER); }
                        ccr = wm_client_rect(ew2);
                        ccl = (long)crect_w(&ccr) * (long)crect_h(&ccr);
                        if (!app_notepad_scroll_rects(ew2, &nbar, &nwell)) {
                            SYS_LOGI("main", "COST-DEMO: no Notepad scroll bar "
                                     "to drag (MISMATCH)");
                            nbar = ccr; nwell = ccr;
                        }
                        {
                            int sx = (nbar.x0 + nbar.x1) / 2;
                            int t0 = nbar.y0 + UI_SB_W, t1 = nbar.y1 - UI_SB_W;
                            feed_press(sx, t1 - 6);
                            feed_drag(sx, (t0 + t1) / 2);
                            feed_drag(sx, (t0 + t1) / 2 - 8);
                            moved = sh_last_frame_px();
                            feed_release(sx, (t0 + t1) / 2 - 8);
                        }
                        nmoved = crect_union(&nwell, &nbar);
                        nmoved.x1 += 6;
                        nmoved.y1 += 6;
                        allowed2 = (long)crect_w(&nmoved) *
                                   (long)crect_h(&nmoved) + 4096;
                        SYS_LOGI("main", "COST-DEMO: dragging Notepad's scroll "
                                 "thumb costs %ld px of %ld (%ld%%), against "
                                 "%ld that moved (%s)", moved, ccl,
                                 (ccl > 0) ? (moved * 100 / ccl) : 0, allowed2,
                                 (moved > 0 && moved <= allowed2) ? "OK"
                                                                  : "MISMATCH");
                        probe_repaint("COST-DEMO",
                                      "the page after a scroll drag");
                    }
                }
            }
        }

        if (opt->wheel_demo) {
            /*
             * The mouse wheel, which until now did nothing anywhere in this
             * desktop: the platform seam had no event for it, so a wheel mouse
             * was a two-button mouse.
             *
             * Three separate claims, and the middle one is the interesting
             * one:
             *
             *   - a notch scrolls by exactly UI_WHEEL_LINES, and turning it
             *     back returns to precisely where it started (no drift);
             *   - it goes to the window under the POINTER, proved by
             *     scrolling a window that does NOT have focus while another
             *     one does -- routing by focus would leave it still;
             *   - and it does not STEAL that focus, because reading
             *     something is not choosing it.
             *
             * The two windows are moved apart deliberately. Both open near
             * the middle of the screen, so a scene that trusted the default
             * placement would be pointing at whichever one happened to be on
             * top and proving nothing about routing at all.
             */
            WmWindow *lw, *nw4;
            CRect lframe, nframe, well;
            int q, t0, t1, t2, tmax;

            app_logview_open();
            lw = wm_focused();
            /*
             * Deliberately SHORT. The Log Viewer opens scrolled to the end,
             * and this scene runs early in a fresh session, so a tall window
             * holds the entire log and there is nothing to scroll -- which is
             * exactly how the first version of this passed its clamp checks
             * and proved nothing. Six rows of a twenty-line log is a window
             * with a real thumb in it.
             */
            lframe = crect_make(8, 40, 360, 140);
            wm_set_frame(lw, &lframe);

            app_notepad_open();
            nw4 = wm_focused();
            nframe = crect_make(400, 40, 360, 300);
            wm_set_frame(nw4, &nframe);
            for (q = 0; q < 24; q++) { sh_run_frame(); }

            SYS_LOGI("main", "WHEEL-DEMO: two windows apart, Notepad focused "
                     "(%s)", (wm_focused() == nw4) ? "OK" : "MISMATCH");

            if (!app_logview_well(lw, &well)) {
                SYS_LOGI("main", "WHEEL-DEMO: no Log Viewer well (MISMATCH)");
                well = lframe;
            }
            /*
             * ...and there really is something to scroll. Without this the
             * whole scene passes on a window whose content fits: every
             * "nothing moved" check is trivially true and the one that must
             * move is the only one that fails.
             */
            SYS_LOGI("main", "WHEEL-DEMO: the log is longer than the window "
                     "(opened at line %d) (%s)", app_logview_top(lw),
                     (app_logview_top(lw) > UI_WHEEL_LINES) ? "OK"
                                                            : "MISMATCH");
            /* Park the pointer on the LOG VIEWER, which is not focused. */
            {
                int wx = (well.x0 + well.x1) / 2;
                int wy = (well.y0 + well.y1) / 2;

                t0 = app_logview_top(lw);
                feed_wheel(wx, wy, 1);        /* away from the user: back up */
                t1 = app_logview_top(lw);
                SYS_LOGI("main", "WHEEL-DEMO: one notch over an UNFOCUSED "
                         "window scrolled it %d lines (%s)", t0 - t1,
                         (t0 - t1 == UI_WHEEL_LINES) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "WHEEL-DEMO: ...and the focus stayed where it "
                         "was (%s)",
                         (wm_focused() == nw4) ? "OK" : "MISMATCH");

                feed_wheel(wx, wy, -1);       /* and back */
                t2 = app_logview_top(lw);
                SYS_LOGI("main", "WHEEL-DEMO: turning it back lands on the "
                         "same line (%d -> %d -> %d) (%s)", t0, t1, t2,
                         (t2 == t0) ? "OK" : "MISMATCH");

                /* What it drew is what a full repaint would have drawn. */
                probe_repaint_rect("WHEEL-DEMO", "the log after a wheel notch",
                                   &lframe);

                /* All the way to the top, then one more: clamped, and the
                 * extra notch must not cost a frame. A wheel held at the end
                 * of a document that repainted anyway would be the most
                 * expensive idle in the system. */
                feed_wheel(wx, wy, 9999);
                tmax = app_logview_top(lw);
                feed_wheel(wx, wy, 1);
                SYS_LOGI("main", "WHEEL-DEMO: at the top it clamps to line 0 "
                         "and the next notch draws %ld px (%s)",
                         sh_last_frame_px(),
                         (tmax == 0 && app_logview_top(lw) == 0 &&
                          sh_last_frame_px() < 4096) ? "OK" : "MISMATCH");

                /*
                 * Down to the far end, and then the desktop notch is turned
                 * the OTHER way -- away from the user, back up, where there
                 * is room to move.
                 *
                 * That direction is the whole check. Sitting at the bottom
                 * and feeding another notch DOWN cannot move anything even
                 * if the wheel is delivered, so it would pass whether the
                 * routing worked or not: a control that cannot fail. This
                 * one has three lines of travel available and refuses to
                 * take them.
                 */
                feed_wheel(wx, wy, -4);
                t0 = app_logview_top(lw);
                feed_wheel(4, 4, 1);
                SYS_LOGI("main", "WHEEL-DEMO: a notch over the desktop moved "
                         "nothing (%d -> %d) (%s)", t0, app_logview_top(lw),
                         (app_logview_top(lw) == t0) ? "OK" : "MISMATCH");
            }

            /*
             * ...and a second window, of a different shape, asked the same
             * question WITHOUT a top-line getter of its own.
             *
             * Nineteen windows answer the wheel and only one of them is
             * driven by the checks above; a lint keeps the rest from being
             * forgotten, but a lint only proves the message is handled, not
             * that handling it does anything. Disk Usage is a list beside a
             * treemap -- neither a document nor a log -- and what a person
             * sees is the only thing being asked here: the rows under the
             * pointer are different pixels than they were.
             */
            {
                WmWindow *dw;
                CRect dframe, list;
                int moved_px, still_px;

                app_diskuse_open();
                dw = wm_focused();
                /* Short, for the same reason the Log Viewer is: a tall list
                 * holds the whole folder and cannot scroll. */
                dframe = crect_make(120, 200, 420, 150);
                wm_set_frame(dw, &dframe);
                for (q = 0; q < 24; q++) { sh_run_frame(); }

                /* The row strip, taken from the frame rather than from the
                 * app's layout -- the point is what is on the screen. */
                list = crect_make(dframe.x0 + 8, dframe.y0 + 60, 170, 60);

                probe_snap(&list);
                feed_wheel(list.x0 + 40, list.y0 + 20, -1);
                moved_px = probe_diff();
                SYS_LOGI("main", "WHEEL-DEMO: a notch over Disk Usage's list "
                         "redrew %d px of it (%s)", moved_px,
                         (moved_px > 0) ? "OK" : "MISMATCH");

                /*
                 * ...and a notch turned over its TREEMAP still scrolls the
                 * LIST. The wheel is delivered to the window, not to the
                 * control under the pointer, and this window has exactly one
                 * thing that scrolls -- so pointing at its other half is not
                 * a reason for the wheel to do nothing. A flick large enough
                 * to run the list to its top, so the answer does not depend
                 * on how many rows were left.
                 */
                probe_snap(&list);
                feed_wheel(dframe.x1 - 40, dframe.y0 + 80, 9999);
                still_px = probe_diff();
                SYS_LOGI("main", "WHEEL-DEMO: ...and a flick that ran the list "
                         "to its top changed %d px more (%s)", still_px,
                         (still_px > 0) ? "OK" : "MISMATCH");
                probe_repaint_rect("WHEEL-DEMO", "Disk Usage after a wheel",
                                   &dframe);
            }
        }

        if (opt->stale_demo) {
            /*
             * Every window, asked the same question: after you reacted to
             * something, does the screen show what a full repaint would?
             *
             * This is the other half of the repaint work. Narrowing a repaint
             * is the optimisation that pays in speed and bills in STALE
             * PIXELS: a region that missed part of what changed leaves last
             * second's image on screen, no test of the drawing code sees it,
             * and it looks like a rendering glitch rather than a bug in the
             * invalidate. Three of those turned up one at a time this week --
             * a scroll bar left drawn as though it were still held in three
             * apps, and a status line rewritten by a key the menu had
             * already eaten -- and each was found only because somebody
             * happened to be measuring that one window.
             *
             * So: drive the three things a pointer and a keyboard can do
             * without committing to anything (hover, wheel, arrows), then
             * force a full repaint and compare. Every window in the Start
             * menu, driven through the Start menu for the reason
             * --launch-demo gives: a list of openers kept in this file is a
             * list of the apps somebody remembered.
             *
             * Nothing here CLICKS. A click in an unknown window saves a file,
             * opens a dialog or ends the session, and a sweep that has to
             * know which is which is a sweep that stops covering the app
             * somebody adds next.
             */
            static const int NO_WINDOW3[] = {
                SH_CMD_CAPTURE, SH_CMD_EXIT_TO_DOS, SH_CMD_RESTART_SHELL,
                SH_CMD_REFRESH, SH_CMD_ARRANGE, SH_CMD_SHOWDESKTOP
            };
            /*
             * ...and the one window this sweep must not CLICK inside.
             *
             * The Shut Down dialog's OK ends the session, which would end the
             * scene half way through with most of the windows unchecked and
             * nothing saying so. It is still opened, hovered, scrolled and
             * keyed like the rest -- only the click phase skips it.
             */
            static const int NO_CLICK3[] = { SH_CMD_SHUTDOWN };
            int m, tried = 0, dirty = 0, skipped = 0, worst = 0;
            int clicked_away = 0;
            char worst_name[64];
            worst_name[0] = '\0';
            feed_move(4, 4);

            for (m = 0; m < 2; m++) {
                const UiMenu *menu = (m == 0) ? &g_sh.launcher_menu
                                              : &g_sh.launcher_menu2;
                int it;
                for (it = 0; it < menu->count; it++) {
                    int id = menu->items[it].id;
                    const char *name = menu->items[it].label;
                    WmWindow *w;
                    CRect want, fr;
                    int before, k, phase, worst_here = 0, worst_phase = -1;
                    CRect worst_box = crect_make(0, 0, 0, 0);
                    CRect keep_box = crect_make(0, 0, 0, 0);
                    cbool skip = CFALSE;
                    unsigned q;

                    if (id == UI_MENU_SEPARATOR_ID) { continue; }
                    for (q = 0; q < sizeof(NO_WINDOW3)/sizeof(NO_WINDOW3[0]); q++) {
                        if (id == NO_WINDOW3[q]) { skip = CTRUE; }
                    }
                    if (skip) { continue; }

                    before = wm_window_count();
                    sh_dispatch_command(id);
                    for (k = 0; k < 3; k++) { sh_run_frame(); }
                    if (wm_window_count() <= before) { continue; }
                    w = wm_focused();
                    if (w == NULL) { continue; }

                    want = crect_make(170, 110, 420, 300);
                    wm_set_frame(w, &want);
                    for (k = 0; k < 24; k++) { sh_run_frame(); }
                    fr = wm_frame_rect(w);

                    /*
                     * Does it move on its OWN? A visualiser, a graph, a
                     * blinking caret and a second hand all differ between two
                     * frames on purpose, and comparing them proves nothing
                     * about invalidation.
                     *
                     * Asked rather than looked up: wm_is_animated only knows
                     * about the manager's own open/close animation, and a
                     * hardcoded list of the restless windows is a list that
                     * goes stale. Two settle frames, a snapshot, four more
                     * frames, and a comparison -- anything that differs with
                     * no input at all is running its own clock.
                     */
                    sh_invalidate_all();
                    sh_run_frame();
                    probe_snap(&fr);
                    for (k = 0; k < 4; k++) { sh_run_frame(); }
                    if (probe_diff() > 0) {
                        skipped++;
                        wm_destroy(w);
                        for (k = 0; k < 2; k++) { sh_run_frame(); }
                        continue;
                    }
                    tried++;

                    for (phase = 0; phase < 4; phase++) {
                        int n, cx = (fr.x0 + fr.x1) / 2;
                        int cy = (fr.y0 + fr.y1) / 2;
                        if (phase == 3) {
                            cbool noclick = CFALSE;
                            for (q = 0; q < sizeof(NO_CLICK3)/sizeof(NO_CLICK3[0]); q++) {
                                if (id == NO_CLICK3[q]) { noclick = CTRUE; }
                            }
                            if (noclick) { continue; }
                        }
                        switch (phase) {
                        case 0:
                            /* A sweep across the client, corner to corner:
                             * whatever lights under the pointer, lights. */
                            for (k = 1; k <= 6; k++) {
                                feed_move(fr.x0 + (crect_w(&fr) * k) / 7,
                                          fr.y0 + (crect_h(&fr) * k) / 7);
                            }
                            feed_move(cx, cy);
                            break;
                        case 1:
                            feed_wheel(cx, cy, -2);
                            feed_wheel(cx, cy, 1);
                            break;
                        case 2:
                            feed_key(PLAT_KEY_DOWN);
                            feed_key(PLAT_KEY_DOWN);
                            feed_key(PLAT_KEY_UP);
                            break;
                        default:
                            /*
                             * A CLICK, which is where most state changes
                             * live and therefore where most of "changed
                             * without invalidating" lives.
                             *
                             * One press-and-release a quarter of the way
                             * across and a third of the way down -- a
                             * toolbar row in most of these windows and a
                             * document in the rest. Unguarded it would be
                             * reckless: a click can open a dialog, close the
                             * window or save a file. Files are harmless
                             * (this runs in a throwaway home), and the other
                             * two are handled below by watching the window
                             * count and giving up on this window rather than
                             * by a list of which click does what -- a list
                             * like that stops covering the app somebody adds
                             * next.
                             */
                            feed_click(fr.x0 + crect_w(&fr) / 4,
                                       fr.y0 + crect_h(&fr) / 3);
                            break;
                        }
                        if (phase == 3 && wm_window_count() != before + 1) {
                            /* It opened something, or closed itself. Either
                             * way the frame under the probe is not the window
                             * any more. Counted, not quietly dropped. */
                            clicked_away++;
                            break;
                        }
                        /* Settle whatever the input started, WITHOUT
                         * invalidating: a window that queued its repaint for
                         * the next frame has had it by now, and one that
                         * queued nothing is exactly the case being looked
                         * for. */
                        for (k = 0; k < 3; k++) { sh_run_frame(); }
                        probe_snap(&fr);
                        sh_invalidate_all();
                        sh_run_frame();
                        n = probe_diff_box(&worst_box);
                        if (n > worst_here) {
                            worst_here = n;
                            worst_phase = phase;
                            keep_box = worst_box;
                        }
                    }

                    if (worst_here > 0) {
                        static const char *const PHASE[4] = {
                            "a pointer sweep", "a wheel notch", "arrow keys",
                            "a click"
                        };
                        dirty++;
                        if (worst_here > worst) {
                            worst = worst_here;
                            sys_strlcpy(worst_name, (name != NULL) ? name : "?",
                                        sizeof worst_name);
                        }
                        SYS_LOGI("main", "STALE-DEMO: '%s' left %d px that a "
                                 "full repaint disagreed with, after %s, "
                                 "within %d,%d..%d,%d of its frame", name,
                                 worst_here,
                                 (worst_phase >= 0) ? PHASE[worst_phase] : "?",
                                 keep_box.x0, keep_box.y0, keep_box.x1,
                                 keep_box.y1);
                    }
                    wm_destroy(w);
                    for (k = 0; k < 2; k++) { sh_run_frame(); }
                    feed_move(4, 4);
                }
            }
            SYS_LOGI("main", "STALE-DEMO: %d window(s) hovered, scrolled, "
                     "keyed and clicked; %d left stale pixels (worst: %s, "
                     "%d px); %d skipped for animating on their own; %d "
                     "opened or closed something on the click (%s)", tried,
                     dirty, (worst_name[0] != '\0') ? worst_name : "none",
                     worst, skipped, clicked_away,
                     (tried > 10 && dirty == 0) ? "OK" : "MISMATCH");
        }

        if (opt->shrink_demo) {
            /*
             * Every window stays inside itself when dragged down to the
             * smallest size the manager allows.
             *
             * WM_MIN_W x WM_MIN_H is not a hypothetical: it is exactly what a
             * user gets by dragging a corner in as far as it goes, and it is
             * where layout arithmetic written for a comfortable window goes
             * negative. A rectangle whose width comes out below zero does not
             * fail -- it draws somewhere else.
             *
             * What is checked is not containment. The manager sets a clip
             * before it sends WM_MSG_PAINT, so "did it paint outside its
             * window" cannot fail through the ordinary path -- a check that
             * cannot fail is not a check, and the first version of this scene
             * was one.
             *
             * What CAN fail, and does, is this: repainting a small PART of a
             * window must leave the window looking exactly as it did. The
             * screen is settled by a full repaint the instant before, so
             * every pixel that moves afterwards -- inside the frame or out --
             * was painted by something that ignored the region it was given.
             * That is the shape of the clip bug gfx_clip_narrow exists for,
             * and at this size it is at its worst: a toolbar laid out for a
             * comfortable window has buttons whose rectangles are past the
             * edge, so anything that widens the clip back to its own
             * rectangle paints them over the rest of the window.
             *
             * Driven through the Start menu rather than a list of openers
             * kept here, for the reason --launch-demo gives: a list in this
             * file is a list of the apps somebody remembered.
             */
            static const int NO_WINDOW2[] = {
                SH_CMD_CAPTURE, SH_CMD_EXIT_TO_DOS, SH_CMD_RESTART_SHELL,
                SH_CMD_REFRESH, SH_CMD_ARRANGE, SH_CMD_SHOWDESKTOP
            };
            /* Room around the frame to watch. Wide enough to catch a layout
             * that went a little negative, which is what this looks like. */
#define SHRINK_HALO 100
            int m, tried = 0, leaked = 0, worst = 0, skipped = 0;
            char worst_name[64];
            worst_name[0] = '\0';

            /* Parked in a corner and left there: a cursor that moves is a
             * pixel that changes for reasons of its own. */
            feed_move(4, 4);

            for (m = 0; m < 2; m++) {
                const UiMenu *menu = (m == 0) ? &g_sh.launcher_menu
                                              : &g_sh.launcher_menu2;
                int it;
                for (it = 0; it < menu->count; it++) {
                    int id = menu->items[it].id;
                    const char *name = menu->items[it].label;
                    WmWindow *w;
                    CRect want, fr, halo;
                    int before, x, y, n = 0, k;
                    cbool skip = CFALSE;
                    unsigned q;
                    GfxSurface *bb;

                    if (id == UI_MENU_SEPARATOR_ID) { continue; }
                    for (q = 0; q < sizeof(NO_WINDOW2)/sizeof(NO_WINDOW2[0]); q++) {
                        if (id == NO_WINDOW2[q]) { skip = CTRUE; }
                    }
                    if (skip) { continue; }

                    before = wm_window_count();
                    sh_dispatch_command(id);
                    for (k = 0; k < 3; k++) { sh_run_frame(); }
                    if (wm_window_count() <= before) { continue; }
                    w = wm_focused();
                    if (w == NULL) { continue; }
                    /*
                     * A window that repaints itself every frame -- the Media
                     * Player's visualiser, the Benchmark's bars, the About
                     * banner's sheen -- cannot be compared across two frames,
                     * because between them it has moved on purpose. Skipped
                     * and COUNTED: a check that quietly drops the awkward
                     * cases reads as covering everything.
                     */
                    if (wm_is_animated(w)) {
                        skipped++;
                        wm_destroy(w);
                        for (k = 0; k < 2; k++) { sh_run_frame(); }
                        continue;
                    }
                    tried++;

                    /* Down to the corner-drag minimum, well clear of the
                     * taskbar and the screen edges so the halo is real
                     * desktop rather than clamped away. */
                    want = crect_make(200, 150, WM_MIN_W, WM_MIN_H);
                    wm_set_frame(w, &want);
                    /* Settle: the open animation draws outlines over the
                     * scene, and they are somebody else's pixels. */
                    for (k = 0; k < 10; k++) { sh_run_frame(); }
                    sh_invalidate_all();
                    sh_run_frame();

                    fr = wm_frame_rect(w);
                    halo = crect_inset(&fr, -SHRINK_HALO);
                    bb = plat_backbuffer();
                    for (y = halo.y0; y < halo.y1; y++) {
                        for (x = halo.x0; x < halo.x1; x++) {
                            int ix = x - halo.x0, iy = y - halo.y0;
                            if (ix < 0 || iy < 0 || ix >= 640 || iy >= 480) {
                                continue;
                            }
                            g_rp_snap[iy * 640 + ix] = gfx_get_pixel(bb, x, y);
                        }
                    }
                    /*
                     * A SMALL region, not the whole frame, and that is the
                     * point. Repainting the whole window cannot escape it --
                     * the manager's clip is the frame. Repainting a corner of
                     * it can, if anything inside sets the clip to its own
                     * rectangle instead of narrowing the one in force: at
                     * this size a toolbar laid out for a comfortable window
                     * has buttons whose rectangles are past the right edge,
                     * so their labels land on the desktop. That is the shape
                     * of the bug gfx_clip_narrow exists for, and this is 29
                     * windows standing guard over it.
                     */
                    {
                        CRect bit = crect_make(fr.x0 + 20, fr.y0 + 20, 24, 18);
                        wm_invalidate(w, &bit);
                    }
                    sh_run_frame();
                    bb = plat_backbuffer();
                    for (y = halo.y0; y < halo.y1; y++) {
                        for (x = halo.x0; x < halo.x1; x++) {
                            int ix = x - halo.x0, iy = y - halo.y0;
                            if (ix < 0 || iy < 0 || ix >= 640 || iy >= 480) {
                                continue;
                            }
                            if (g_rp_snap[iy * 640 + ix] !=
                                gfx_get_pixel(bb, x, y)) { n++; }
                        }
                    }
                    if (n > 0) {
                        leaked++;
                        if (n > worst) {
                            worst = n;
                            sys_strlcpy(worst_name, (name != NULL) ? name : "?",
                                        sizeof worst_name);
                        }
                        SYS_LOGI("main", "SHRINK-DEMO: '%s' changed %d px it "
                                 "was not asked to repaint, at %dx%d", name, n,
                                 WM_MIN_W, WM_MIN_H);
                    }
                    wm_destroy(w);
                    for (k = 0; k < 2; k++) { sh_run_frame(); }
                }
            }
            SYS_LOGI("main", "SHRINK-DEMO: %d window(s) dragged down to "
                     "%dx%d and repainted a corner; %d of them redrew "
                     "something else as well (%d self-animating skipped)%s%s "
                     "(%s)",
                     tried, WM_MIN_W, WM_MIN_H, leaked, skipped,
                     (worst > 0) ? " -- worst: " : "",
                     (worst > 0) ? worst_name : "",
                     (tried > 20 && leaked == 0) ? "OK" : "MISMATCH");
#undef SHRINK_HALO
        }

        if (opt->focus_demo) {
            /*
             * A window that does not have the keyboard stops saying it does.
             *
             * Everything that draws a SELECTION drew it the same whether the
             * keyboard was there or not: the File Manager's highlighted row
             * stayed a live blue after you clicked away, and a selected
             * desktop icon kept its accent plate and white ring behind an
             * open window -- while the arrow keys, which reach the desktop
             * only when no window is focused, were going somewhere else
             * entirely. Windows greyed an unfocused selection for exactly
             * this reason.
             *
             * Read off the backbuffer: the assertion is about what is drawn,
             * and "the flag is set" would have passed the whole time
             * ui_list_draw took a 'focused' argument and had CASTALIA_UNUSED
             * on it.
             */
            WmWindow *fw, *cw2;
            GfxSurface *bb;
            CRect box;
            int live_px, dim_px;

            /* ---- the desktop's own selection --------------------------- */
            /* Click the Documents icon, the way --icon-sel does. The box is
             * to the LEFT of anything opened below, so the count measures the
             * icon rather than what covers it. */
            feed_click(60, 120);
            box = crect_make(12, 84, 110, 110);
            bb = plat_backbuffer();
            live_px = demo_count_px(bb, &box, ui_palette()->accent);
            SYS_LOGI("main", "FOCUS-DEMO: the selected desktop icon is on its "
                     "accent plate (%d px) (%s)", live_px,
                     (live_px > 40) ? "OK" : "MISMATCH");

            app_calc_open();
            sh_run_frame();
            sh_run_frame();
            bb = plat_backbuffer();
            dim_px = demo_count_px(bb, &box, ui_palette()->accent);
            SYS_LOGI("main", "FOCUS-DEMO: opening a window takes the arrows "
                     "away and the plate goes quiet (%d px) (%s)", dim_px,
                     (dim_px == 0) ? "OK" : "MISMATCH");

            /* ---- and a list inside a window ---------------------------- */
            cw2 = wm_focused();
            app_fileman_open();
            sh_run_frame();
            fw = wm_focused();
            feed_key(PLAT_KEY_HOME);
            feed_key(PLAT_KEY_DOWN);
            {
                CRect rr = app_fileman_row_rect(fw, 1);
                CColor a1, a2, a3;
                int sx, sy;
                if (crect_w(&rr) <= 0) {
                    SYS_LOGI("main", "FOCUS-DEMO: no row to look at "
                             "(MISMATCH)");
                } else {
                    /* Right-hand end of the band: clear of the icon, the
                     * name, and of the window that takes the focus below. */
                    sx = rr.x1 - 30;
                    sy = (rr.y0 + rr.y1) / 2;
                    bb = plat_backbuffer();
                    a1 = gfx_get_pixel(bb, sx, sy);
                    wm_focus(cw2);
                    sh_run_frame();
                    bb = plat_backbuffer();
                    a2 = gfx_get_pixel(bb, sx, sy);
                    SYS_LOGI("main", "FOCUS-DEMO: clicking away changed the "
                             "selected row by %d (%s)", colour_gap(a1, a2),
                             (colour_gap(a1, a2) >= 60) ? "OK" : "MISMATCH");
                    /* ...and coming back restores it exactly. A treatment
                     * that dimmed and never came back would pass the check
                     * above. */
                    wm_focus(fw);
                    sh_run_frame();
                    bb = plat_backbuffer();
                    a3 = gfx_get_pixel(bb, sx, sy);
                    SYS_LOGI("main", "FOCUS-DEMO: coming back restores it "
                             "(%d from where it started) (%s)",
                             colour_gap(a1, a3),
                             (colour_gap(a1, a3) == 0) ? "OK" : "MISMATCH");
                }
            }

            /* ---- a MINIMIZED window stops taking the keyboard -----------
             *
             * Key events go to the focus, and nothing asked whether the focus
             * was still on the screen. Minimizing the editor you were typing
             * in left it holding the keyboard: you carried on typing, nothing
             * moved, and the text was going into a window that was not
             * there. Restoring it showed what you had typed.
             *
             * Checked through the DOCUMENT rather than through the focus
             * pointer, because "g_wm.focus changed" is not the complaint --
             * the complaint is where the letters went.
             */
            {
                WmWindow *typed, *other;
                char before5[64];
                int k5;

                app_notepad_open();
                for (k5 = 0; k5 < 20; k5++) { sh_run_frame(); }
                typed = wm_focused();
                feed_char('h'); feed_char('i');
                sys_strlcpy(before5, app_notepad_text(typed), sizeof before5);

                /* A second window, so there is somewhere for the keyboard to
                 * go. With nothing else open the focus goes nowhere, which is
                 * also correct and would pass this check for the wrong
                 * reason. */
                app_calc_open();
                for (k5 = 0; k5 < 20; k5++) { sh_run_frame(); }
                other = wm_focused();
                wm_focus(typed);
                for (k5 = 0; k5 < 4; k5++) { sh_run_frame(); }

                wm_minimize(typed);
                for (k5 = 0; k5 < 6; k5++) { sh_run_frame(); }
                feed_char('X'); feed_char('Y'); feed_char('Z');
                for (k5 = 0; k5 < 3; k5++) { sh_run_frame(); }
                SYS_LOGI("main", "FOCUS-DEMO: typing after minimizing does not "
                         "reach the minimized window (\"%s\") (%s)",
                         app_notepad_text(typed),
                         (strcmp(app_notepad_text(typed), before5) == 0)
                             ? "OK" : "MISMATCH");
                SYS_LOGI("main", "FOCUS-DEMO: ...the keyboard went to what is "
                         "still on screen instead (%s)",
                         (wm_focused() == other) ? "OK" : "MISMATCH");

                /*
                 * ...and the same through SHOW DESKTOP, which hides every
                 * window at once with a loop of its own and never called
                 * wm_minimize. A fix that lived in the minimize would have
                 * covered the caption button and not the taskbar's first
                 * Quick Launch icon; this is why it lives in wm_show, which
                 * is the one place a window is hidden.
                 */
                wm_show(typed, CTRUE);
                for (k5 = 0; k5 < 8; k5++) { sh_run_frame(); }
                sh_dispatch_command(SH_CMD_SHOWDESKTOP);
                for (k5 = 0; k5 < 8; k5++) { sh_run_frame(); }
                feed_char('Q'); feed_char('W');
                for (k5 = 0; k5 < 3; k5++) { sh_run_frame(); }
                SYS_LOGI("main", "FOCUS-DEMO: ...and Show Desktop takes it "
                         "away too (\"%s\") (%s)", app_notepad_text(typed),
                         (strcmp(app_notepad_text(typed), before5) == 0)
                             ? "OK" : "MISMATCH");
                wm_destroy(other);
                wm_destroy(typed);
                for (k5 = 0; k5 < 24; k5++) { sh_run_frame(); }
            }

            /* ---- clicking a window BEHIND another brings it forward -----
             *
             * The manager used to mark a window's whole frame dirty on every
             * mouse-down, raised or not -- so this worked for the wrong
             * reason, and every click in an already-front window paid a full
             * repaint for it. wm_bring_to_front invalidates only when the
             * window really moved in the z-order now, which is correct and
             * cheaper and would be silently wrong if the raise itself broke.
             *
             * Checked through the PIXELS in the overlap, not through the
             * z-order list: "the array was reordered" would pass with
             * nothing redrawn, which is exactly the failure this guards.
             */
            {
                WmWindow *back, *front;
                CRect bf, ff, over;
                int changed;

                app_calc_open();
                for (live_px = 0; live_px < 20; live_px++) { sh_run_frame(); }
                back = wm_focused();
                bf = crect_make(120, 120, 300, 220);
                wm_set_frame(back, &bf);

                /*
                 * TWO STILL WINDOWS. The About box was the front one here at
                 * first, and its banner animates its sheen every frame -- so
                 * the overlap changed on its own and the check below passed
                 * with the raise's invalidate deleted. A check that cannot
                 * fail is not a check.
                 */
                app_notepad_open();
                for (live_px = 0; live_px < 20; live_px++) { sh_run_frame(); }
                front = wm_focused();
                ff = crect_make(240, 180, 300, 220);   /* over its lower right */
                wm_set_frame(front, &ff);
                for (live_px = 0; live_px < 20; live_px++) { sh_run_frame(); }

                over = crect_make(250, 190, 120, 80);  /* inside both */
                probe_snap(&over);
                /* A point on the BACK window that the front one does not
                 * cover: its top-left corner, well inside the title bar. */
                feed_click(bf.x0 + 40, bf.y0 + 6);
                for (live_px = 0; live_px < 6; live_px++) { sh_run_frame(); }
                changed = probe_diff();
                SYS_LOGI("main", "FOCUS-DEMO: clicking a window behind another "
                         "raises it -- %d px of the overlap were redrawn, and "
                         "it has the keyboard (%s)", changed,
                         (changed > 0 && wm_focused() == back && back != front)
                             ? "OK" : "MISMATCH");

                /*
                 * ...and the RAISE on its own, which the check above cannot
                 * isolate: a click also moves the keyboard, and wm_focus
                 * repaints both windows whole, so the overlap comes back
                 * either way. Deleting the raise's own invalidate leaves that
                 * check green.
                 *
                 * So: give the BACK window the keyboard first and let that
                 * settle, then raise it with nothing else happening. The
                 * overlap must still be redrawn -- a z-order array quietly
                 * reordered under an unchanged screen is the failure.
                 */
                wm_focus(front);
                for (live_px = 0; live_px < 6; live_px++) { sh_run_frame(); }
                wm_bring_to_front(front);
                for (live_px = 0; live_px < 6; live_px++) { sh_run_frame(); }
                wm_focus(back);
                for (live_px = 0; live_px < 6; live_px++) { sh_run_frame(); }
                probe_snap(&over);
                wm_bring_to_front(back);      /* focus already here */
                for (live_px = 0; live_px < 6; live_px++) { sh_run_frame(); }
                changed = probe_diff();
                SYS_LOGI("main", "FOCUS-DEMO: ...and raising it with the "
                         "keyboard already there still redraws the overlap "
                         "(%d px) (%s)", changed,
                         (changed > 0) ? "OK" : "MISMATCH");
                wm_destroy(front);
                wm_destroy(back);
                for (live_px = 0; live_px < 3; live_px++) { sh_run_frame(); }
            }

            /* ---- and the CARET, which is the same promise ---------------
             *
             * A caret says "what you type lands here". Every editor drew one
             * whether or not it had the keyboard, so two windows could make
             * that promise at once and only one of them was telling the
             * truth. The Console went further and BLINKED its one while
             * unfocused, which also meant invalidating a rectangle twice a
             * second forever, in every console anybody had left open: with
             * that fixed, twenty idle frames with eleven windows open
             * composite nothing at all.
             *
             * Sampled where the caret is, not where the window is: an empty
             * document puts it at the first column of the first row, four
             * pixels into the text well.
             */
            {
                WmWindow *nw5, *ow5;
                CRect nbar, nwell;
                CColor lit = 0UL, dark = 0UL, back = 0UL;
                int cx5 = 0, cy5 = 0;

                app_notepad_open();
                for (live_px = 0; live_px < 20; live_px++) { sh_run_frame(); }
                nw5 = wm_focused();
                if (!app_notepad_scroll_rects(nw5, &nbar, &nwell)) {
                    SYS_LOGI("main", "FOCUS-DEMO: no Notepad text well "
                             "(MISMATCH)");
                } else {
                    cx5 = nwell.x0 + 4;
                    cy5 = nwell.y0 + 5;
                    bb = plat_backbuffer();
                    lit = gfx_get_pixel(bb, cx5, cy5);
                    /* A pixel one column to the right is the page itself,
                     * and is what the caret's own column must look like once
                     * the window is not the one being typed into. */
                    back = gfx_get_pixel(bb, cx5 + 3, cy5);
                    SYS_LOGI("main", "FOCUS-DEMO: the focused editor shows a "
                             "caret (%d from the page beside it) (%s)",
                             colour_gap(lit, back),
                             (colour_gap(lit, back) > 100) ? "OK"
                                                           : "MISMATCH");
                    /* Something else takes the keyboard. */
                    app_calc_open();
                    for (live_px = 0; live_px < 20; live_px++) { sh_run_frame(); }
                    ow5 = wm_focused();
                    bb = plat_backbuffer();
                    dark = gfx_get_pixel(bb, cx5, cy5);
                    SYS_LOGI("main", "FOCUS-DEMO: ...and stops showing one the "
                             "moment it is not the window being typed into "
                             "(%d from the page) (%s)", colour_gap(dark, back),
                             (ow5 != nw5 && colour_gap(dark, back) == 0)
                                 ? "OK" : "MISMATCH");
                    /* ...and it comes back. A caret that went away and stayed
                     * away would pass the check above. */
                    wm_focus(nw5);
                    for (live_px = 0; live_px < 3; live_px++) { sh_run_frame(); }
                    bb = plat_backbuffer();
                    SYS_LOGI("main", "FOCUS-DEMO: ...and comes back with the "
                             "keyboard (%d from where it started) (%s)",
                             colour_gap(lit, gfx_get_pixel(bb, cx5, cy5)),
                             (colour_gap(lit, gfx_get_pixel(bb, cx5, cy5)) == 0)
                                 ? "OK" : "MISMATCH");
                    /*
                     * ...and the SELECTION beside it, which is the same
                     * promise in a different shape: an accent-blue band says
                     * "the arrow keys move this", and in a window nobody is
                     * typing into that is false. The File Manager's rows and
                     * every list control already went quiet; the three
                     * editors did not, so four windows disagreed with the
                     * fifth about what unfocused looks like -- by drawing
                     * nothing at all.
                     *
                     * Selected with the keyboard while the editor still has
                     * it: Shift+End takes the first row, so the band lands
                     * on a column that had page under it a moment ago.
                     */
                    wm_focus(nw5);
                    for (live_px = 0; live_px < 3; live_px++) { sh_run_frame(); }
                    feed_char('X');
                    feed_char('Y');
                    {
                        PlatEvent es;
                        CColor band_lit, band_quiet;
                        memset(&es, 0, sizeof es);
                        es.type = PLAT_EV_KEY_DOWN;
                        es.key = PLAT_KEY_HOME;
                        plat_host_push_event(&es); sh_run_frame();
                        es.key = PLAT_KEY_END; es.mods = PLAT_MOD_SHIFT;
                        plat_host_push_event(&es); sh_run_frame();
                        bb = plat_backbuffer();
                        band_lit = gfx_get_pixel(bb, cx5 + 2, cy5);
                        app_calc_open();
                        for (live_px = 0; live_px < 20; live_px++) {
                            sh_run_frame();
                        }
                        bb = plat_backbuffer();
                        band_quiet = gfx_get_pixel(bb, cx5 + 2, cy5);
                        SYS_LOGI("main", "FOCUS-DEMO: a selection goes quiet "
                                 "when the window loses the keyboard (%d) (%s)",
                                 colour_gap(band_lit, band_quiet),
                                 (colour_gap(band_lit, band_quiet) > 60)
                                     ? "OK" : "MISMATCH");
                    }
                }
            }
        }

        if (opt->hotbtn_demo) {
            /*
             * A button under the pointer looks different, a button being held
             * looks pressed, and a button the pointer has LEFT goes back.
             *
             * ui_draw_button has drawn all three treatments since it was
             * written; almost nothing ever asked for any but the first. The
             * Calculator is the clearest case in the system -- a window that
             * is nothing but buttons -- and every one of its keys was drawn
             * UI_BTN_NORMAL, so pressing one changed the number in the
             * display and nothing else moved.
             *
             * Read off the BACKBUFFER, because the assertion is about pixels:
             * "the state field says HOVER" would have passed the whole time
             * the state field was never consulted. The pointer sits at each
             * key's centre and the sample is taken up and left of it, since
             * the cursor is drawn down and right of its hotspot and would
             * otherwise be what the check measures.
             */
            WmWindow *cw;
            CRect hot, cold;
            GfxSurface *bb;
            int ok = 0;

            app_calc_open();
            /*
             * ...after the open animation has finished, not during it. A
             * window that is still growing marks its whole bounds dirty
             * every frame, so anything measured here is measured against a
             * full repaint that nobody asked for: the release check below
             * passed with the release repaint deleted, purely because the
             * animation was still redrawing the key. Two measurements in
             * the cost scene were lost to the same thing.
             */
            {
                int q;
                for (q = 0; q < 24; q++) { sh_run_frame(); }
            }
            cw = wm_focused();
            ok = (cw != NULL &&
                  app_calc_key_rect(cw, 1, 1, &hot) &&
                  app_calc_key_rect(cw, 0, 0, &cold));
            SYS_LOGI("main", "HOTBTN-DEMO: found two keys to compare (%s)",
                     ok ? "OK" : "MISMATCH");
            if (ok) {
                CColor face0, faceh, face2, edgeh, edgep, ctl0, ctl1;
                /*
                 * The pointer goes to the key's bottom-right CORNER and the
                 * sample is taken from its middle. The cursor is drawn down
                 * and to the right of its hotspot, so from there it hangs off
                 * the key entirely -- park it in the middle instead and the
                 * check measures the mouse pointer.
                 */
                int hx = hot.x1 - 3, hy = hot.y1 - 3;
                int sx = (hot.x0 + hot.x1) / 2, sy = (hot.y0 + hot.y1) / 2;
                int cx = (cold.x0 + cold.x1) / 2;
                int cy = (cold.y0 + cold.y1) / 2;

                /* Pointer parked off the keypad entirely -- inside the
                 * window, so it is this window being asked, but on the strip
                 * above the first row of keys. */
                feed_move(hot.x0, hot.y0 - 20);
                bb = plat_backbuffer();
                face0 = gfx_get_pixel(bb, sx, sy);
                ctl0  = gfx_get_pixel(bb, cx, cy);

                /* ...and now on the key. */
                feed_move(hx, hy);
                bb = plat_backbuffer();
                faceh = gfx_get_pixel(bb, sx, sy);
                edgeh = gfx_get_pixel(bb, hot.x0 + 1, hot.y0 + 1);
                ctl1  = gfx_get_pixel(bb, cx, cy);
                SYS_LOGI("main", "HOTBTN-DEMO: the key under the pointer "
                         "changed by %d (%s)", colour_gap(face0, faceh),
                         (colour_gap(face0, faceh) >= 40) ? "OK" : "MISMATCH");
                /* ...and its neighbour did NOT. A treatment applied to the
                 * whole window would pass the check above and be wrong. */
                SYS_LOGI("main", "HOTBTN-DEMO: the key beside it did not, by "
                         "%d (%s)", colour_gap(ctl0, ctl1),
                         (colour_gap(ctl0, ctl1) == 0) ? "OK" : "MISMATCH");

                /* Held down: the bevel flips, so the lit top-left corner
                 * goes dark. The FACE is unchanged by a press, which is why
                 * this samples the edge and the check above samples the
                 * middle. */
                {
                    PlatEvent e;
                    memset(&e, 0, sizeof e);
                    e.type = PLAT_EV_MOUSE_DOWN;
                    e.mouse_x = hx; e.mouse_y = hy;
                    e.buttons = PLAT_MB_LEFT;
                    plat_host_set_mouse(hx, hy, PLAT_MB_LEFT);
                    plat_host_push_event(&e);
                    sh_run_frame();
                }
                bb = plat_backbuffer();
                edgep = gfx_get_pixel(bb, hot.x0 + 1, hot.y0 + 1);
                SYS_LOGI("main", "HOTBTN-DEMO: holding it sank the bevel, by "
                         "%d (%s)", colour_gap(edgeh, edgep),
                         (colour_gap(edgeh, edgep) >= 24) ? "OK" : "MISMATCH");
                {
                    PlatEvent e;
                    memset(&e, 0, sizeof e);
                    e.type = PLAT_EV_MOUSE_UP;
                    e.mouse_x = hx; e.mouse_y = hy;
                    plat_host_set_mouse(hx, hy, 0);
                    plat_host_push_event(&e);
                    sh_run_frame();
                }
                /*
                 * Letting go with the pointer still ON the key raises the
                 * bevel again -- it is hovered now, not held -- and the
                 * release repaints that key alone rather than the window.
                 * A release that narrowed to the wrong rectangle leaves the
                 * key looking pressed with nothing holding it, which is
                 * what this samples: the same edge pixel as the two checks
                 * above, back where hovering put it.
                 */
                {
                    CColor edgeu;
                    bb = plat_backbuffer();
                    edgeu = gfx_get_pixel(bb, hot.x0 + 1, hot.y0 + 1);
                    SYS_LOGI("main", "HOTBTN-DEMO: letting go raised it again "
                             "(%d from hovered) (%s)", colour_gap(edgeh, edgeu),
                             (colour_gap(edgeh, edgeu) == 0) ? "OK" : "MISMATCH");
                }

                /*
                 * And the pointer LEAVING the window puts it back. This is
                 * the one that needed the window manager: a window is only
                 * ever told where the pointer IS, and once it is over
                 * something else that message goes to somebody else -- so a
                 * lit button stayed lit with the pointer nowhere near it.
                 */
                feed_move(4, 4);         /* the desktop, well outside */
                bb = plat_backbuffer();
                face2 = gfx_get_pixel(bb, sx, sy);
                SYS_LOGI("main", "HOTBTN-DEMO: leaving the window put the key "
                         "back (%d from where it started) (%s)",
                         colour_gap(face0, face2),
                         (colour_gap(face0, face2) == 0) ? "OK" : "MISMATCH");
            }

            /*
             * ...and the same thing on a toolbar, which repaints only the
             * STRIP the buttons are in rather than the whole window -- ten
             * 660x28 redraws instead of ten 660x440 ones while the pointer
             * crosses it.
             *
             * A partial repaint is the cheap answer that is easy to get
             * subtly wrong, so this checks the expensive property directly:
             * what the strip looks like after the partial repaint must be
             * exactly what a full one would have drawn, to the pixel.
             */
            {
                WmWindow *fw;
                CRect b0, b1;
                fw = NULL;
                app_fileman_open();
                {   /* let the open animation finish before measuring */
                    int q;
                    for (q = 0; q < 8; q++) { sh_run_frame(); }
                }
                fw = wm_focused();
                b0 = app_fileman_btn_rect(fw, 0);
                b1 = app_fileman_btn_rect(fw, 3);
                if (crect_w(&b0) <= 0 || crect_w(&b1) <= 0) {
                    SYS_LOGI("main", "HOTBTN-DEMO: no toolbar to hover "
                             "(MISMATCH)");
                } else {
                    CColor off, on;
                    /* Inside the bevel and outside the 16x16 icon: a
                     * 24-wide icon button shows its face in the ring around
                     * the glyph, and the middle is the glyph. */
                    int bx = b1.x0 + 4, by = (b1.y0 + b1.y1) / 2;
                    feed_move(b0.x0 - 8, b0.y0 + 4);   /* beside the buttons */
                    off = gfx_get_pixel(plat_backbuffer(), bx, by);
                    feed_move(b1.x1 - 3, b1.y1 - 3);   /* onto the fourth    */
                    on = gfx_get_pixel(plat_backbuffer(), bx, by);
                    SYS_LOGI("main", "HOTBTN-DEMO: the toolbar button under "
                             "the pointer changed by %d (%s)",
                             colour_gap(off, on),
                             (colour_gap(off, on) >= 40) ? "OK" : "MISMATCH");
                    {
                        CRect strip = crect_make(b0.x0 - 4, b0.y0 - 4,
                                                 (b1.x1 - b0.x0) + 8,
                                                 (b1.y1 - b0.y0) + 8);
                        probe_repaint_rect("HOTBTN-DEMO",
                                           "the toolbar strip after a partial "
                                           "hover repaint", &strip);
                    }
                }
            }

            /*
             * Notepad's toolbar is the same code again, and "the same code
             * again" is where the sibling gets forgotten -- the version of
             * this that shipped invalidated a CLIENT rectangle against a
             * SCREEN frame, which intersects to nothing and repaints
             * nothing. It looked exactly like a hover that was not wired up.
             */
            {
                WmWindow *nw;
                CRect nb;
                app_notepad_open();
                {
                    int q;
                    for (q = 0; q < 8; q++) { sh_run_frame(); }
                }
                nw = wm_focused();
                /*
                 * A window BEHIND this one must not paint into it.
                 *
                 * The Calculator is under Notepad here. Moving the pointer
                 * repaints a small region -- the two cursor rectangles -- and
                 * every window overlapping it redraws, back to front. That is
                 * correct, and it still put 385 pixels of the Calculator's
                 * key LABELS through Notepad's empty white page, because
                 * ui_draw_button set the clip to its own button rather than
                 * narrowing the window manager's, and gfx_set_clip replaces.
                 * Anything drawn after that was clipped to a rectangle that
                 * knew nothing about which window it belonged to.
                 *
                 * Two moves over the text area, which invalidate nothing of
                 * their own, and then the same question as above: is what is
                 * on screen what a full repaint would have drawn.
                 */
                {
                    CRect cr9 = wm_client_rect(nw);
                    feed_move(cr9.x0 + 40, cr9.y1 - 40);
                    feed_move(cr9.x0 + 60, cr9.y1 - 60);
                    probe_repaint("HOTBTN-DEMO",
                                  "a window behind this one painted nothing "
                                  "into it");
                }
                nb = app_notepad_btn_rect(nw, 1);      /* Open */
                if (crect_w(&nb) <= 0) {
                    SYS_LOGI("main", "HOTBTN-DEMO: no Notepad toolbar to "
                             "hover (MISMATCH)");
                } else {
                    CColor off, on;
                    int bx = nb.x0 + 4, by = (nb.y0 + nb.y1) / 2;
                    feed_move(nb.x0 - 8, by);
                    off = gfx_get_pixel(plat_backbuffer(), bx, by);
                    feed_move(nb.x1 - 3, nb.y1 - 3);
                    on = gfx_get_pixel(plat_backbuffer(), bx, by);
                    SYS_LOGI("main", "HOTBTN-DEMO: Notepad's toolbar button "
                             "changed by %d (%s)", colour_gap(off, on),
                             (colour_gap(off, on) >= 40) ? "OK" : "MISMATCH");
                    {
                        CRect strip = crect_make(nb.x0 - 40, nb.y0 - 4,
                                                 crect_w(&nb) + 80,
                                                 crect_h(&nb) + 8);
                        probe_repaint_rect("HOTBTN-DEMO",
                                           "Notepad's toolbar strip after a "
                                           "partial hover repaint", &strip);
                    }
                }
            }
        }

        if (opt->renmany_demo) {
            /*
             * `ren *.TXT *.BAK` renames a folder -- and a batch that would
             * lose a file renames nothing at all.
             *
             * tests/test_ren.c drives the pattern rules over every shape that
             * goes wrong. What it cannot reach is the part that touches a
             * filesystem: that the pattern selects the files actually on the
             * disk, that the renames happen, and -- the one that matters --
             * that a batch which would put two files on one name leaves the
             * folder exactly as it found it rather than doing the safe half.
             */
            char home[CASTALIA_MAX_PATH];
            char probe[CASTALIA_MAX_PATH];
            WmWindow *cw;
            int k;

            sys_strlcpy(home, sys_home(), sizeof(home));
            {
                static const char *const MADE[4] = {
                    "REPORT1.TXT", "REPORT2.TXT", "REPORT3.TXT", "KEEP.DAT"
                };
                for (k = 0; k < 4; k++) {
                    sys_snprintf(probe, sizeof(probe), "%s/%s", home, MADE[k]);
                    feed_write_text(probe, MADE[k]);
                }
            }

            app_console_open();
            sh_run_frame();
            cw = wm_focused();

            /* ---- the batch that works ---------------------------------- */
            demo_console_cmd("ren *.TXT *.BAK");
            {
                int renamed = 0, left = 0;
                for (k = 1; k <= 3; k++) {
                    sys_snprintf(probe, sizeof(probe), "%s/REPORT%d.BAK", home, k);
                    if (plat_file_size(probe) >= 0) { renamed++; }
                    sys_snprintf(probe, sizeof(probe), "%s/REPORT%d.TXT", home, k);
                    if (plat_file_size(probe) >= 0) { left++; }
                }
                sys_snprintf(probe, sizeof(probe), "%s/KEEP.DAT", home);
                SYS_LOGI("main", "RENMANY-DEMO: 'ren *.TXT *.BAK' renamed %d of "
                         "3, left %d behind, and did not touch KEEP.DAT (%s)",
                         renamed, left,
                         (renamed == 3 && left == 0 &&
                          plat_file_size(probe) >= 0) ? "OK" : "MISMATCH");
            }
            /* ...and the CONTENTS moved with the names, not just the names. */
            {
                char got[64];
                sys_snprintf(probe, sizeof(probe), "%s/REPORT2.BAK", home);
                feed_read_text(probe, got, sizeof(got));
                SYS_LOGI("main", "RENMANY-DEMO: REPORT2.BAK still holds what "
                         "REPORT2.TXT held (\"%s\") (%s)", demo_oneline(got),
                         (strcmp(got, "REPORT2.TXT") == 0) ? "OK" : "MISMATCH");
            }

            /* ---- the batch that must do NOTHING ------------------------ */
            /*
             * Every one of these results is a legal 8.3 name and none is
             * refused on its own. The star sits past the end of a
             * seven-character base, so all three files come out as
             * ARCHIVED.BAK -- an instruction to delete the folder one
             * overwrite at a time.
             */
            demo_console_cmd("ren *.BAK ARCHIVED*");
            {
                int still = 0;
                for (k = 1; k <= 3; k++) {
                    sys_snprintf(probe, sizeof(probe), "%s/REPORT%d.BAK", home, k);
                    if (plat_file_size(probe) >= 0) { still++; }
                }
                sys_snprintf(probe, sizeof(probe), "%s/ARCHIVED.BAK", home);
                SYS_LOGI("main", "RENMANY-DEMO: a batch that would collide "
                         "renamed nothing -- %d of 3 still there, no "
                         "ARCHIVED.BAK (%s)", still,
                         (still == 3 && plat_file_size(probe) < 0)
                             ? "OK" : "MISMATCH -- it did the safe half");
            }
            /* ...and it SAID why, naming a file. A refusal nobody can read is
             * indistinguishable from nothing happening. */
            {
                int i2, said = 0;
                for (i2 = 0; i2 < app_console_line_count(cw); i2++) {
                    const char *ln = app_console_line(cw, i2);
                    if (demo_contains(ln, "ARCHIVED.BAK") &&
                        demo_contains(ln, "Nothing renamed")) { said = 1; }
                }
                SYS_LOGI("main", "RENMANY-DEMO: it said which name two files "
                         "would share (%s)", said ? "OK" : "MISMATCH");
            }

            /* ---- and one that would land on a file already there -------- */
            /*
             * Both spellings, because they are different code. A single-file
             * `ren A B` went straight to plat_file_rename, which overwrites
             * without complaint -- it destroyed KEEP.DAT and said "Renamed to
             * 'KEEP.DAT'." The wildcard form plans first and refuses.
             */
            demo_console_cmd("ren REPORT1.BAK KEEP.DAT");   /* one file  */
            {
                char got[64];
                sys_snprintf(probe, sizeof(probe), "%s/KEEP.DAT", home);
                feed_read_text(probe, got, sizeof(got));
                sys_snprintf(probe, sizeof(probe), "%s/REPORT1.BAK", home);
                SYS_LOGI("main", "RENMANY-DEMO: 'ren A B' would not overwrite "
                         "an existing B -- KEEP.DAT still holds \"%s\" and "
                         "REPORT1.BAK is still there (%s)", demo_oneline(got),
                         (strcmp(got, "KEEP.DAT") == 0 &&
                          plat_file_size(probe) >= 0) ? "OK" : "MISMATCH");
            }
            demo_console_cmd("ren REPORT1.BA? KEEP.DAT");   /* a batch   */
            {
                char got[64];
                sys_snprintf(probe, sizeof(probe), "%s/KEEP.DAT", home);
                feed_read_text(probe, got, sizeof(got));
                sys_snprintf(probe, sizeof(probe), "%s/REPORT1.BAK", home);
                SYS_LOGI("main", "RENMANY-DEMO: nor would a batch -- KEEP.DAT "
                         "still holds \"%s\" and REPORT1.BAK is still there "
                         "(%s)", demo_oneline(got),
                         (strcmp(got, "KEEP.DAT") == 0 &&
                          plat_file_size(probe) >= 0) ? "OK" : "MISMATCH");
            }
            /* ...and a rename that is only a change of case still works, which
             * the guard above could easily have broken. */
            demo_console_cmd("ren KEEP.DAT keep.dat");
            {
                int i2, refused = 0;
                for (i2 = 0; i2 < app_console_line_count(cw); i2++) {
                    if (demo_contains(app_console_line(cw, i2),
                                      "'keep.dat' already exists")) { refused = 1; }
                }
                SYS_LOGI("main", "RENMANY-DEMO: renaming a file to its own name "
                         "in another case is not refused as a clash (%s)",
                         refused ? "MISMATCH" : "OK");
            }

            /* ---- copy has the same hole in the command next door -------- */
            /*
             * `copy A B` opened B with "wb", which truncates whatever is
             * there, and then said "1 file copied to 'B'." The same shape as
             * `ren` -- found by asking where else it lived rather than by
             * hitting it.
             */
            demo_console_cmd("copy REPORT2.BAK keep.dat");
            {
                char got[64];
                sys_snprintf(probe, sizeof(probe), "%s/keep.dat", home);
                feed_read_text(probe, got, sizeof(got));
                /* keep.dat, exactly as it is spelled on the disk by now: the
                 * case-rename check above lowercased it, and on a
                 * case-sensitive host "KEEP.DAT" is a different file that the
                 * copy would happily create -- passing this check while
                 * proving nothing. */
                SYS_LOGI("main", "RENMANY-DEMO: 'copy A B' would not overwrite "
                         "an existing B either -- it still holds \"%s\" (%s)",
                         demo_oneline(got),
                         (strcmp(got, "KEEP.DAT") == 0) ? "OK" : "MISMATCH");
            }
            /* ...and a copy to a NEW name still works, so the guard is a
             * guard and not a wall. */
            demo_console_cmd("copy REPORT2.BAK FRESH.BAK");
            {
                char got[64];
                sys_snprintf(probe, sizeof(probe), "%s/FRESH.BAK", home);
                feed_read_text(probe, got, sizeof(got));
                SYS_LOGI("main", "RENMANY-DEMO: ...but a copy to a new name "
                         "still works (\"%s\") (%s)", demo_oneline(got),
                         (strcmp(got, "REPORT2.TXT") == 0) ? "OK" : "MISMATCH");
            }

            /* ---- a pattern matching nothing says so --------------------- */
            demo_console_cmd("ren *.ZZZ *.YYY");
            {
                int i2, said = 0;
                for (i2 = 0; i2 < app_console_line_count(cw); i2++) {
                    if (demo_contains(app_console_line(cw, i2),
                                      "No files match")) { said = 1; }
                }
                SYS_LOGI("main", "RENMANY-DEMO: a pattern matching nothing says "
                         "so rather than reporting 0 renamed (%s)",
                         said ? "OK" : "MISMATCH");
            }

            /*
             * ...and the same thing from the File Manager, driven the way a
             * person reaches it: right-click the empty area of the folder,
             * pick "Rename Many...", answer both prompts.
             *
             * The console and the window go through one ren_batch.c, so this
             * is not re-testing the rules -- it is testing that the menu item
             * is wired, that the two prompts hand their answers to it in the
             * right order, and that the listing afterwards shows the result.
             */
            {
                WmWindow *fw;
                int k2, step, seen = 0, reached = 0;

                app_fileman_open_path(home);
                for (k2 = 0; k2 < 6; k2++) { sh_run_frame(); }
                fw = wm_focused();

                /* Driven with the keyboard, and the menu walked BY NAME --
                 * so this does not break the day the menu gains an entry, and
                 * an entry that is never reached fails here rather than
                 * silently clicking whatever moved into its place. */
                feed_key(PLAT_KEY_END);        /* land on a file */
                feed_key(PLAT_KEY_F10);        /* its menu       */
                for (step = 0; step < 16; step++) {
                    const char *lab = app_fileman_ctx_item(fw);
                    if (lab != NULL && strcmp(lab, "Rename Many...") == 0) {
                        reached = 1;
                        break;
                    }
                    feed_key(PLAT_KEY_DOWN);
                }
                SYS_LOGI("main", "RENMANY-DEMO: the arrows reach Rename Many "
                         "on the item menu (%s)", reached ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ENTER);
                for (k2 = 0; k2 < 3; k2++) { sh_run_frame(); }

                demo_prompt_fill("*.BAK");
                demo_prompt_fill("DONE*.TXT");

                /*
                 * DONERT1, not DONE1. "DONE" is four literals, so the star
                 * sits at position 4 and copies "RT1" out of "REPORT1" -- the
                 * positional rule again, and the reason a batch is planned
                 * before it runs rather than trusted to look right.
                 */
                for (k2 = 1; k2 <= 3; k2++) {
                    sys_snprintf(probe, sizeof(probe), "%s/DONERT%d.TXT", home, k2);
                    if (plat_file_size(probe) >= 0) { seen++; }
                }
                SYS_LOGI("main", "RENMANY-DEMO: the File Manager renamed %d of "
                         "3 through its own menu and prompts -- \"%s\" (%s)",
                         seen, demo_oneline(app_fileman_status(fw)),
                         (seen == 3) ? "OK" : "MISMATCH");

                /*
                 * ...and F2, which renames ONE file, must not overwrite
                 * another either. It went straight to plat_file_rename, so
                 * typing the name of the file next to this one destroyed it
                 * and the status line said "Renamed to 'X'".
                 */
                {
                    char kept[64];
                    int got_menu = 0;
                    /* END lands on keep.dat, which sorts last; one UP is the
                     * file above it. HOME lands on "..", whose menu is the
                     * folder's rather than an item's. */
                    feed_key(PLAT_KEY_END);
                    feed_key(PLAT_KEY_UP);
                    feed_key(PLAT_KEY_F10);
                    for (step = 0; step < 16; step++) {
                        const char *lab = app_fileman_ctx_item(fw);
                        if (lab != NULL && strcmp(lab, "Rename") == 0) {
                            got_menu = 1;
                            break;
                        }
                        feed_key(PLAT_KEY_DOWN);
                    }
                    feed_key(PLAT_KEY_ENTER);
                    demo_prompt_fill("keep.dat");
                    sys_snprintf(probe, sizeof(probe), "%s/keep.dat", home);
                    feed_read_text(probe, kept, sizeof(kept));
                    /*
                     * The STATUS is checked as well as the file, because a
                     * rename that never happened also leaves the file alone.
                     * Only the refusal message distinguishes "it was stopped"
                     * from "the scene missed the menu".
                     */
                    SYS_LOGI("main", "RENMANY-DEMO: renaming one file onto an "
                             "existing name is refused -- keep.dat still holds "
                             "\"%s\", status \"%s\" (%s)", demo_oneline(kept),
                             demo_oneline(app_fileman_status(fw)),
                             (got_menu && strcmp(kept, "KEEP.DAT") == 0 &&
                              demo_contains(app_fileman_status(fw),
                                            "already exists"))
                                 ? "OK" : "MISMATCH");
                }
            }
        }
        if (opt->compare_demo) {
            /*
             * File Compare, end to end: two real files on disk, opened through
             * the app, and the answer read back off the window.
             *
             * tests/test_diff.c already drives the comparison itself over
             * every shape that goes wrong. What it cannot reach is the part
             * that touches a filesystem and a window -- that the two paths
             * handed in are the two files actually read, that the report is
             * built from them, and that it says so on screen.
             */
            char home[CASTALIA_MAX_PATH];
            char pa[CASTALIA_MAX_PATH], pb[CASTALIA_MAX_PATH];
            char got[160];
            int rows, i;
            int saw_minus = 0, saw_plus = 0;

            sys_strlcpy(home, sys_home(), sizeof(home));
            sys_snprintf(pa, sizeof(pa), "%s/CMPA.TXT", home);
            sys_snprintf(pb, sizeof(pb), "%s/CMPB.TXT", home);
            /* One changed line and one inserted line, far enough apart to be
             * two separate differences. */
            feed_write_text(pa, "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\n");
            feed_write_text(pb,
                "alpha\nBETA CHANGED\ngamma\ndelta\nNEW LINE\nepsilon\nzeta\n");

            app_compare_open(pa, pb);
            sh_run_frame();

            rows = app_compare_rows();
            app_compare_status(got, sizeof(got));
            SYS_LOGI("main", "COMPARE-DEMO: the window opened and read both "
                     "files -- \"%s\" (%s)", got,
                     (rows > 0 && got[0] != '\0') ? "OK" : "MISMATCH");

            /* The CONTENT of the changed lines, from both sides. A report that
             * counted the differences correctly and showed the wrong text
             * would pass a count check and be useless. */
            for (i = 0; i < rows; i++) {
                if (!app_compare_row(i, got, sizeof(got))) { continue; }
                if (got[0] == '-' && demo_contains(got, "beta")) { saw_minus++; }
                if (got[0] == '+' && demo_contains(got, "BETA CHANGED")) {
                    saw_plus++;
                }
                if (got[0] == '+' && demo_contains(got, "NEW LINE")) {
                    saw_plus++;
                }
            }
            SYS_LOGI("main", "COMPARE-DEMO: the changed line is shown from "
                     "both files (%d old, %d new) (%s)", saw_minus, saw_plus,
                     (saw_minus >= 1 && saw_plus >= 2) ? "OK" : "MISMATCH");

            /* Two identical files say so rather than showing an empty list a
             * person has to interpret. */
            feed_write_text(pb, "alpha\nbeta\ngamma\ndelta\nepsilon\nzeta\n");
            app_compare_open(pa, pb);
            sh_run_frame();
            app_compare_status(got, sizeof(got));
            SYS_LOGI("main", "COMPARE-DEMO: identical files say so (\"%s\") "
                     "(%s)", got,
                     (app_compare_rows() == 0 &&
                      demo_contains(got, "identical")) ? "OK" : "MISMATCH");
            /*
             * ---- and the three buttons can be PRESSED -------------------
             *
             * They could not. The click handler offset each button to screen
             * coordinates and then tested the pointer -- which arrives in
             * client coordinates -- against it, so the rectangle it compared
             * against was a whole window's offset away from the button. None
             * of the three could be hit, and clicks in the middle of the diff
             * list opened a file dialog instead.
             *
             * Everything above passed the entire time, because it opens the
             * window with both paths already chosen and never presses
             * anything. That is the gap this closes: a window driven only
             * through its API is a window whose buttons are never tested.
             */
            {
                WmWindow *cwin = wm_focused();
                CRect ba = app_compare_btn_rect(cwin, 0);
                if (crect_w(&ba) <= 0) {
                    SYS_LOGI("main", "COMPARE-DEMO: no File 1 button "
                             "(MISMATCH)");
                } else {
                    feed_click((ba.x0 + ba.x1) / 2, (ba.y0 + ba.y1) / 2);
                    SYS_LOGI("main", "COMPARE-DEMO: pressing 'File 1...' "
                             "opened the file dialog (%s)",
                             wm_has_modal() ? "OK" : "MISMATCH");
                    feed_key(PLAT_KEY_ESC);
                    /*
                     * ...and the place the SHIFTED rectangle used to sit
                     * opens nothing. That is the other half of the same bug:
                     * the old test compared a screen rectangle against a
                     * client-coordinate pointer, so a click well down in the
                     * diff list -- wherever client (button + origin) landed
                     * -- came back as "File 1...".
                     *
                     * The point is computed, not guessed: a click at screen x
                     * arrives as client x - origin.x, so the one that used to
                     * hit is at 2*origin + the button's own offset.
                     */
                    {
                        CPoint co = wm_client_origin(cwin);
                        CRect cr3 = wm_client_rect(cwin);
                        int gx = 2 * co.x + (ba.x0 - co.x) + 20;
                        int gy = 2 * co.y + (ba.y0 - co.y) + 9;
                        if (gx > cr3.x1 - 2 || gy > cr3.y1 - 2) {
                            SYS_LOGI("main", "COMPARE-DEMO: the old ghost "
                                     "button would fall outside the window "
                                     "(MISMATCH)");
                        } else {
                            feed_click(gx, gy);
                            SYS_LOGI("main", "COMPARE-DEMO: clicking where the "
                                     "shifted rectangle used to be opens "
                                     "nothing (%s)",
                                     wm_has_modal() ? "MISMATCH" : "OK");
                        }
                    }
                }
            }

            /* Left showing the real comparison for the screenshot -- the
             * identical pass above overwrote B, so put the difference back. */
            feed_write_text(pb,
                "alpha\nBETA CHANGED\ngamma\ndelta\nNEW LINE\nepsilon\nzeta\n");
            app_compare_open(pa, pb);
            sh_run_frame();
        }
        if (opt->bigfile_demo) {
            /*
             * A file too big for the editor must come back whole, or not at
             * all.
             *
             * Notepad holds 16K. It read as much as fit, said "Opened FILE.TXT
             * (16383 bytes)" -- which reads as the file's size, because for
             * every file that fits it IS the file's size -- and Save wrote
             * that buffer back over the original. Everything past 16K was
             * gone, and nothing at any point said so. It is the Recycle Bin
             * bug again: destroys data while announcing success.
             *
             * Every other reader in the system already gets this right. The
             * console's fc and the File Compare window REFUSE a file over
             * their limit and say its size; the Viewer, which cannot write,
             * still prints "(truncated)". The one program that writes back was
             * the one that said nothing.
             *
             * So the scene drives the whole path a person would: a 20K file,
             * opened, then Ctrl+S and Enter on the pre-filled name -- which is
             * the original file. Then it asks the filesystem, not the status
             * line, how big that file is now.
             */
            char home[CASTALIA_MAX_PATH];
            char path[CASTALIA_MAX_PATH];
            char st[96];
            long before, after;
            int held;
            WmWindow *nw;

            sys_strlcpy(home, sys_home(), sizeof(home));
            sys_snprintf(path, sizeof(path), "%s/BIG.TXT", home);
            {
                PlatFile *f = plat_fopen(path, "wb");
                if (f != NULL) {
                    char line[24];
                    int i;
                    /* 1000 x 20 bytes, then a marked tail: the loss is
                     * something you can look at rather than infer. */
                    for (i = 0; i < 1000; i++) {
                        sys_snprintf(line, sizeof(line), "line %04d payload\n", i);
                        plat_fwrite(f, line, sys_strnlen(line, 24u));
                    }
                    plat_fwrite(f, "TAIL-MARKER\n", 12u);
                    plat_fclose(f);
                }
            }
            before = plat_file_size(path);

            app_notepad_open_file(path);
            sh_run_frame();
            nw = wm_focused();
            held = (int)strlen(app_notepad_text(nw));
            app_notepad_status(nw, st, sizeof(st));

            /*
             * Check 1: a file it cannot hold is REFUSED, and the status line
             * says so. Holding a prefix and calling it the file is the failure
             * -- what makes it a failure is that the message is the same one a
             * successful open gives.
             */
            SYS_LOGI("main", "BIGFILE-DEMO: %ld-byte file, editor holds %d, "
                     "status \"%s\" (%s)", before, held, st,
                     (held == 0 && demo_contains(st, "too large")) ? "OK"
                                                                  : "MISMATCH");

            /* Ctrl+S, then Enter on the pre-filled name -- which is BIG.TXT
             * itself. This is the keystroke that used to destroy the tail. */
            { PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
              ke.key = 19; ke.ch = 's'; ke.mods = PLAT_MOD_CTRL;
              plat_host_push_event(&ke); }
            sh_run_frame();
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = '\n'; plat_host_push_event(&en); }
            sh_run_frame();
            after = plat_file_size(path);

            /*
             * Check 2: the file on disk is the size it was. This is the one
             * that matters -- check 1 is about the message, this is about the
             * bytes.
             */
            SYS_LOGI("main", "BIGFILE-DEMO: after open+save the file is %ld "
                     "bytes, was %ld (%s)", after, before,
                     (after == before) ? "OK" : "MISMATCH -- lost the tail");

            /* ...and the marked tail is still the tail. A file that is the
             * right LENGTH and the wrong bytes would pass check 2. */
            {
                PlatFile *f = plat_fopen(path, "rb");
                char tail[16];
                cu32 got = 0;
                tail[0] = '\0';
                if (f != NULL) {
                    if (plat_fseek(f, before - 12L) == CE_OK) {
                        got = plat_fread(f, tail, 12u);
                    }
                    plat_fclose(f);
                }
                if (got > 15u) { got = 15u; }
                tail[got] = '\0';
                SYS_LOGI("main", "BIGFILE-DEMO: the last line is still there "
                         "(\"%s\") (%s)", demo_oneline(tail),
                         demo_contains(tail, "TAIL-MARKER") ? "OK" : "MISMATCH");
            }

            /*
             * ...and a file that DOES fit still opens and still saves, so the
             * refusal above is a limit and not a wall. A guard that turned
             * every open into a refusal would pass all three checks above.
             */
            {
                char small[CASTALIA_MAX_PATH];
                char back[64];
                sys_snprintf(small, sizeof(small), "%s/SMALL.TXT", home);
                feed_write_text(small, "kept\n");
                app_notepad_open_file(small);
                sh_run_frame();
                nw = wm_focused();
                held = (int)strlen(app_notepad_text(nw));
                app_notepad_status(nw, st, sizeof(st));
                { PlatEvent ke; memset(&ke, 0, sizeof(ke));
                  ke.type = PLAT_EV_KEY_DOWN; ke.key = 19; ke.ch = 's';
                  ke.mods = PLAT_MOD_CTRL; plat_host_push_event(&ke); }
                sh_run_frame();
                { PlatEvent en; memset(&en, 0, sizeof(en));
                  en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                  en.ch = '\n'; plat_host_push_event(&en); }
                sh_run_frame();
                feed_read_text(small, back, sizeof(back));
                SYS_LOGI("main", "BIGFILE-DEMO: a file that fits still opens "
                         "(%d bytes, \"%s\") and saves (\"%s\") (%s)",
                         held, st, demo_oneline(back),
                         (held == 5 && strcmp(back, "kept\n") == 0) ? "OK"
                                                                   : "MISMATCH");
            }

            /*
             * Save As asks before REPLACING a file.
             *
             * Every app saves through one shared dialog, so this was true of
             * Notepad, Paint, CastaliaWrite, CastaliaSheet and the Theme
             * Editor at once: type the name of something already there and it
             * was gone. The most ordinary overwrite a desktop has, and the
             * one none of the other guards covered.
             */
            {
                char keep[CASTALIA_MAX_PATH];
                char donor[CASTALIA_MAX_PATH];
                char got[64];
                int k3, pass;

                sys_snprintf(keep, sizeof(keep), "%s/PRECIOUS.TXT", home);
                /* A donor with KNOWN contents, so "it was replaced" can be
                 * checked as "it now holds the other file" rather than merely
                 * "it is different" -- which is also true of a file that was
                 * emptied, or deleted, or never written at all. */
                sys_snprintf(donor, sizeof(donor), "%s/DONOR.TXT", home);

                /*
                 * Two independent passes, each from a clean desktop. Reusing
                 * one dialog does not work and is worth saying why: after the
                 * first answer the Save As dialog is still standing, so the
                 * next Ctrl+S goes to IT rather than to the editor, and the
                 * second pass silently drives the first pass's dialog.
                 */
                for (pass = 0; pass < 3; pass++) {
                    while (wm_window_count() > 0) {
                        WmWindow *dw7 = wm_window_at(0);
                        if (dw7 == NULL) { break; }
                        wm_destroy(dw7);
                    }
                    for (k3 = 0; k3 < 3; k3++) { sh_run_frame(); }
                    feed_write_text(keep, "do not lose me\n");
                    feed_write_text(donor, "donor text\n");

                    app_notepad_open_file(donor);
                    for (k3 = 0; k3 < 4; k3++) { sh_run_frame(); }
                    { PlatEvent ke; memset(&ke, 0, sizeof(ke));
                      ke.type = PLAT_EV_KEY_DOWN; ke.key = 19; ke.ch = 's';
                      ke.mods = PLAT_MOD_CTRL; plat_host_push_event(&ke); }
                    for (k3 = 0; k3 < 3; k3++) { sh_run_frame(); }
                    /*
                     * Pass 2 HIGHLIGHTS a row in the listing first. Enter
                     * accepts the highlighted row, replacing whatever is in
                     * the field with that row's name -- so typing a new name
                     * and pressing Enter wrote over the highlighted file
                     * instead of the one you named. Typing deselects now, and
                     * this is what says so.
                     */
                    if (pass == 2) {
                        feed_key(PLAT_KEY_DOWN);
                        for (k3 = 0; k3 < 2; k3++) { sh_run_frame(); }
                    }
                    demo_prompt_fill("PRECIOUS.TXT");
                    for (k3 = 0; k3 < 3; k3++) { sh_run_frame(); }
                    feed_key((pass == 0) ? PLAT_KEY_ESC : PLAT_KEY_ENTER);
                    for (k3 = 0; k3 < 4; k3++) { sh_run_frame(); }
                    feed_read_text(keep, got, sizeof(got));
                    if (pass == 0) {
                        SYS_LOGI("main", "BIGFILE-DEMO: Save As over an "
                                 "existing file asks, and no keeps it "
                                 "(\"%s\") (%s)", demo_oneline(got),
                                 (strcmp(got, "do not lose me\n") == 0)
                                     ? "OK" : "MISMATCH");
                    } else if (pass == 1) {
                        SYS_LOGI("main", "BIGFILE-DEMO: ...and yes replaces it "
                                 "with what the editor held (\"%s\") (%s)",
                                 demo_oneline(got),
                                 (strcmp(got, "donor text\n") == 0)
                                     ? "OK" : "MISMATCH");
                    } else {
                        SYS_LOGI("main", "BIGFILE-DEMO: a typed name beats a "
                                 "highlighted row -- PRECIOUS.TXT is what got "
                                 "written (\"%s\") (%s)", demo_oneline(got),
                                 (strcmp(got, "donor text\n") == 0)
                                     ? "OK" : "MISMATCH -- it wrote the row");
                    }
                }
                while (wm_window_count() > 0) {
                    WmWindow *dw8 = wm_window_at(0);
                    if (dw8 == NULL) { break; }
                    wm_destroy(dw8);
                }
                for (k3 = 0; k3 < 3; k3++) { sh_run_frame(); }
            }

            /*
             * The same question of the other two editors that write back.
             *
             * The Sheet is 48 x 16 and opens CSVs that come from anywhere;
             * Write holds 4095 characters. Both loaded a prefix, said
             * "Opened", and saved the prefix over the file. Both now show
             * what fits and refuse to ADOPT the path, so Ctrl+S offers a new
             * name (BOOK1.CSV / DOC1.CWD) instead of overwriting.
             *
             * Both live in DOCS, which is where their file dialogs open.
             */
            {
                char docs[CASTALIA_MAX_PATH];
                char csv[CASTALIA_MAX_PATH], doc[CASTALIA_MAX_PATH];
                long csv_before, doc_before;
                int k2;

                sys_snprintf(docs, sizeof(docs), "%s/DOCS", home);
                plat_mkdir(docs);
                sys_snprintf(csv, sizeof(csv), "%s/BIG.CSV", docs);
                sys_snprintf(doc, sizeof(doc), "%s/BIG.CWD", docs);
                {
                    PlatFile *f = plat_fopen(csv, "wb");
                    if (f != NULL) {
                        char row[16];
                        int i;
                        /* 120 rows: two and a half times the grid. */
                        for (i = 0; i < 120; i++) {
                            sys_snprintf(row, sizeof(row), "r%03d,%d\n", i, i);
                            plat_fwrite(f, row, sys_strnlen(row, 16u));
                        }
                        plat_fclose(f);
                    }
                    f = plat_fopen(doc, "wb");
                    if (f != NULL) {
                        char chunk[64];
                        int i;
                        plat_fwrite(f, "CWRITE1\n.P 0\n", 13u);
                        for (i = 0; i < 64; i++) { chunk[i] = 'w'; }
                        /* 6400 characters against a 4095 model. */
                        for (i = 0; i < 100; i++) { plat_fwrite(f, chunk, 64u); }
                        plat_fclose(f);
                    }
                }
                csv_before = plat_file_size(csv);
                doc_before = plat_file_size(doc);

                app_sheet_open_file(csv);
                sh_run_frame();
                for (k2 = 0; k2 < 2; k2++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN; ke.key = 19; ke.ch = 's';
                    ke.mods = PLAT_MOD_CTRL; plat_host_push_event(&ke);
                    sh_run_frame();
                    memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                    ke.key = PLAT_KEY_ENTER; ke.ch = '\n';
                    plat_host_push_event(&ke);
                    sh_run_frame();
                    if (k2 == 0) {
                        SYS_LOGI("main", "BIGFILE-DEMO: a 120-row CSV is not "
                                 "saved back as 48 -- %ld bytes, was %ld (%s)",
                                 plat_file_size(csv), csv_before,
                                 (plat_file_size(csv) == csv_before)
                                     ? "OK" : "MISMATCH -- lost the rows");
                        app_write_open_file(doc);
                        sh_run_frame();
                    }
                }
                SYS_LOGI("main", "BIGFILE-DEMO: a document too long for Write "
                         "is not saved back short -- %ld bytes, was %ld (%s)",
                         plat_file_size(doc), doc_before,
                         (plat_file_size(doc) == doc_before)
                             ? "OK" : "MISMATCH -- lost the tail");
            }

            /*
             * Paint, whose version of this is the most expensive: pictures.
             *
             * The canvas was a fixed 420x260, so a bigger image landed at the
             * top-left, the rest was dropped, and Save wrote 420x260 back
             * over the file. The system's own screen capture writes 800x600
             * BMPs -- opening your own screenshot and pressing Ctrl+S threw
             * away three quarters of it.
             *
             * Two pictures: one inside the canvas budget, which must arrive
             * WHOLE and at its own size, and one over it, which must leave
             * the file alone.
             */
            {
                char small_bmp[CASTALIA_MAX_PATH];
                char huge_bmp[CASTALIA_MAX_PATH];
                long huge_before;
                GfxSurface *made, *back;
                int k2;

                sys_snprintf(small_bmp, sizeof(small_bmp), "%s/PIC.BMP", home);
                sys_snprintf(huge_bmp, sizeof(huge_bmp), "%s/SHOT.BMP", home);
                made = gfx_surface_new(400, 240);   /* inside the canvas */
                if (made != NULL) {
                    CRect all = crect_make(0, 0, 400, 240);
                    CRect mark = crect_make(370, 210, 30, 30);
                    gfx_fill_rect(made, &all, 0x0000FF00UL);
                    /* A mark in the far corner: a round trip that lost the
                     * edge would still be a picture, just a smaller one. */
                    gfx_fill_rect(made, &mark, 0x00FF0000UL);
                    gfx_bmp_save(made, small_bmp);
                    gfx_surface_free(made);
                }
                made = gfx_surface_new(800, 600);   /* a screen capture */
                if (made != NULL) {
                    CRect all = crect_make(0, 0, 800, 600);
                    gfx_fill_rect(made, &all, 0x000000FFUL);
                    gfx_bmp_save(made, huge_bmp);
                    gfx_surface_free(made);
                }
                huge_before = plat_file_size(huge_bmp);

                app_paint_open_file(small_bmp);
                for (k2 = 0; k2 < 4; k2++) { sh_run_frame(); }
                /* Saved back out and read off the disk: a picture that fits
                 * survives the round trip whole, corner included. */
                { PlatEvent ke; memset(&ke, 0, sizeof(ke));
                  ke.type = PLAT_EV_KEY_DOWN; ke.key = 19; ke.ch = 's';
                  ke.mods = PLAT_MOD_CTRL; plat_host_push_event(&ke); }
                sh_run_frame();
                { PlatEvent en; memset(&en, 0, sizeof(en));
                  en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                  en.ch = '\n'; plat_host_push_event(&en); }
                sh_run_frame();
                back = gfx_bmp_load(small_bmp);
                if (back == NULL) {
                    SYS_LOGW("main", "BIGFILE-DEMO: PIC.BMP did not reload "
                             "(MISMATCH)");
                } else {
                    CColor corner =
                        back->pixels[235 * back->pitch + 390] & 0xFFFFFFUL;
                    SYS_LOGI("main", "BIGFILE-DEMO: a picture that fits saves "
                             "back whole (%dx%d, far corner %06lX) (%s)",
                             back->w, back->h, (unsigned long)corner,
                             (back->w == 400 && back->h == 240 &&
                              corner == 0x00FF0000UL) ? "OK" : "MISMATCH");
                    gfx_surface_free(back);
                }

                /* ...and one too big for any canvas leaves its file alone. */
                app_paint_open_file(huge_bmp);
                for (k2 = 0; k2 < 4; k2++) { sh_run_frame(); }
                { PlatEvent ke; memset(&ke, 0, sizeof(ke));
                  ke.type = PLAT_EV_KEY_DOWN; ke.key = 19; ke.ch = 's';
                  ke.mods = PLAT_MOD_CTRL; plat_host_push_event(&ke); }
                sh_run_frame();
                { PlatEvent en; memset(&en, 0, sizeof(en));
                  en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                  en.ch = '\n'; plat_host_push_event(&en); }
                sh_run_frame();
                SYS_LOGI("main", "BIGFILE-DEMO: an 800x600 capture is not "
                         "saved back cropped -- %ld bytes, was %ld (%s)",
                         plat_file_size(huge_bmp), huge_before,
                         (plat_file_size(huge_bmp) == huge_before)
                             ? "OK" : "MISMATCH -- cropped the picture");

                /*
                 * ...and what all of this COSTS, measured rather than argued.
                 *
                 * Three Paint windows are open by now, and the last of them
                 * gets its undo ring filled -- PC_UNDO_LEVELS whole canvases,
                 * which is where Paint's memory actually goes. That is a
                 * session a person can have, and it has to stay inside the
                 * hard budget.
                 *
                 * It did not. On the code this scene was written against the
                 * peak was 8523 KB of 8192, over budget and unmeasured: the
                 * mem-check scene opens each app once and closes it, so it
                 * never held three canvases at once. Shrinking the canvas to
                 * the picture (rather than padding every picture out to
                 * 420x260) is what brought it back inside, and the margin is
                 * thin -- this check is the thing that will say so when the
                 * next surface is added.
                 */
                {
                    char cap_bmp[CASTALIA_MAX_PATH];
                    const int CANX = 52 + 5, CANY = 16 + 5;
                    cu32 peak_kb;
                    CPoint o2;
                    WmWindow *pw2;
                    int d;

                    sys_snprintf(cap_bmp, sizeof(cap_bmp), "%s/CAP.BMP", home);
                    made = gfx_surface_new(500, 400);   /* over the ceiling */
                    if (made != NULL) {
                        CRect all = crect_make(0, 0, 500, 400);
                        gfx_fill_rect(made, &all, 0x00808080UL);
                        gfx_bmp_save(made, cap_bmp);
                        gfx_surface_free(made);
                    }
                    app_paint_open_file(cap_bmp);
                    for (k2 = 0; k2 < 4; k2++) { sh_run_frame(); }
                    pw2 = wm_focused();
                    o2 = wm_client_origin(pw2);
                    /* Four strokes fills the undo ring: PC_UNDO_LEVELS full
                     * canvases, which is the part that multiplies. */
                    for (d = 0; d < PC_UNDO_LEVELS + 1; d++) {
                        PlatEvent me;
                        memset(&me, 0, sizeof(me));
                        me.type = PLAT_EV_MOUSE_DOWN;
                        me.buttons = PLAT_MB_LEFT;
                        me.mouse_x = o2.x + CANX + 20 + d * 8;
                        me.mouse_y = o2.y + CANY + 20;
                        plat_host_push_event(&me);
                        me.type = PLAT_EV_MOUSE_MOVE;
                        me.mouse_y = o2.y + CANY + 120;
                        plat_host_push_event(&me);
                        me.type = PLAT_EV_MOUSE_UP; me.buttons = 0;
                        plat_host_push_event(&me);
                        sh_run_frame();
                    }
                    peak_kb = (cu32)(sys_mem_peak_bytes() / 1024u);
                    SYS_LOGI("main", "BIGFILE-DEMO: three Paint windows with a "
                             "full undo ring peak at %lu KB of the %d KB "
                             "budget (%s)", (unsigned long)peak_kb,
                             CASTALIA_BUDGET_KB,
                             (peak_kb <= (cu32)CASTALIA_BUDGET_KB)
                                 ? "OK" : "MISMATCH -- over budget");
                }
            }
        }
        if (opt->lscroll_demo) {
            /*
             * A Start menu column too tall for the screen still reaches its
             * last entry.
             *
             * --launch-demo proves the SHIPPED lists fit at 640x480 and fails
             * if they stop fitting, which keeps somebody from adding a
             * fifteenth program without noticing. It cannot cover the case
             * that actually happens on somebody else's machine: .CAPP add-ons
             * are found at run time, one row each, and no build-time check
             * can count them. The column used to respond by simply not
             * drawing the overflow -- rows that were not there and not
             * clickable, which looks exactly like rows that were never added.
             *
             * Run in a short screen so the left column has to scroll. Every
             * claim below is about the LAST entry, because the last entry is
             * the one that used to disappear.
             */
            CRect lr, bar;
            int last, last_cmd, before, after, k;
            cbool has_bar;
            const UiMenu *lm = &g_sh.launcher_menu;

            sh_open_launcher(CTRUE);
            for (k = 0; k < 24; k++) { sh_run_frame(); }
            lr = g_sh.launcher_rect;
            last = lm->count - 1;
            last_cmd = (last >= 0) ? lm->items[last].id : -1;
            has_bar = sh_launcher_bar_rect(0, &bar);

            /* The premise. Without an overflowing column the rest of this
             * scene measures a menu that fits, and passes on anything. */
            SYS_LOGI("main", "LSCROLL-DEMO: the left column overflows and "
                     "carries a scroll bar (%s)",
                     (has_bar && sh_launcher_clamped()) ? "OK" : "MISMATCH");

            /*
             * The bar does not eat the labels.
             *
             * lr_col_width sizes a column to its longest label plus fourteen
             * pixels of right padding; a fifteen-pixel bar dropped INSIDE
             * that eats the padding and then a pixel of the last glyph. Only
             * the longest entry shows it, and only on the machines where
             * enough add-ons were installed to make the column scroll --
             * which is to say, never on the machine it was written on.
             *
             * Asked in pixels rather than arithmetic: find the rightmost ink
             * on the widest row and require it left of the bar. The widest
             * row is found the same way the layout finds it, by measuring
             * every label, because hard-coding "Clock, Calendar & Agenda"
             * would go quietly wrong the day a longer one is added.
             */
            {
                int widest = -1, wmax = 0, i2;
                for (i2 = 0; i2 < lm->count; i2++) {
                    int tw = gfx_text_width(GFX_FONT_SYSTEM,
                                            lm->items[i2].label);
                    if (tw > wmax) { wmax = tw; widest = i2; }
                }
                if (widest < 0 || !has_bar) {
                    SYS_LOGI("main", "LSCROLL-DEMO: no widest row or no bar to "
                             "test it against (MISMATCH)");
                } else {
                    /* Put that row on screen, then read its pixels. */
                    CRect rr;
                    int ink = -1, span = 0, y2, x2;
                    GfxSurface *bb;
                    for (i2 = 0; i2 < lm->count * 2 + 4; i2++) {
                        feed_key(PLAT_KEY_DOWN);
                        if (lm->highlight == widest) { break; }
                    }
                    for (i2 = 0; i2 < 4; i2++) { sh_run_frame(); }
                    rr = crect_make(0, 0, 0, 0);
                    bb = plat_backbuffer();
                    /* The row is highlighted, so its ink is the accent text
                     * on the accent band: anything that is not the band. */
                    for (y2 = lr.y0; y2 < lr.y1; y2++) {
                        if (sh_launcher_command_at(lr.x0 + 20, y2)
                            == lm->items[widest].id) {
                            rr = crect_make(lr.x0, y2, bar.x1 - lr.x0, 1);
                            break;
                        }
                    }
                    if (crect_h(&rr) == 1) {
                        /*
                         * Inside the highlight band and STOPPING at the bar:
                         * the bar's own pixels differ from the band too, and
                         * scanning into it finds the bar and calls it ink --
                         * which is what the first spelling of this did, and
                         * it reported the label ending fourteen pixels to the
                         * right of a bar it was supposedly clear of.
                         *
                         * And across the whole row rather than one scanline.
                         * The second spelling read the row's TOP line, where
                         * a row that is eighteen pixels tall and a font that
                         * is eight have nothing to say to each other: it
                         * found two pixels of the band's own edge and called
                         * a full label a clipped one.
                         */
                        int dy;
                        for (dy = 0; dy < 18; dy++) {
                            CColor band_px;
                            int first = -1, last = -1, y3 = rr.y0 + dy;
                            if (y3 >= lr.y1) { break; }
                            band_px = gfx_get_pixel(bb, rr.x0 + 3, y3);
                            for (x2 = rr.x0 + 3; x2 < bar.x0; x2++) {
                                if (gfx_get_pixel(bb, x2, y3) != band_px) {
                                    if (first < 0) { first = x2; }
                                    last = x2;
                                }
                            }
                            if (first >= 0 && last - first + 1 > span) {
                                span = last - first + 1;
                                ink = first;   /* the text's ORIGIN */
                            }
                        }
                    }
                    /*
                     * The claim, stated so that it can see ONE PIXEL,
                     * because one pixel is exactly the size of the bug.
                     *
                     * lr_col_width sizes the column to LR_TEXT_X + the
                     * longest label + LR_COL_PAD_R. Dropping a 15-pixel bar
                     * inside that leaves the label 1 pixel short of its own
                     * last column of glyph -- it eats the 14 of padding and
                     * then one more. So: the bar must begin at or after
                     * where the longest label ENDS, and where it ends is
                     * measured from the drawn pixels (its origin) plus what
                     * the font says it is wide.
                     *
                     * Two spellings of this could not fail. The first
                     * scanned past the bar and found the BAR, reporting a
                     * label that ended to the right of the thing it was
                     * clear of. The second measured the ink SPAN, which is
                     * bounded by the highlight band's own edge rather than
                     * by the glyphs, so it read about the same number
                     * whatever the text did.
                     */
                    SYS_LOGI("main", "LSCROLL-DEMO: the longest label ('%s') "
                             "runs %d..%d and the bar starts at %d -- not one "
                             "pixel of it is under the bar (%s)",
                             lm->items[widest].label, ink, ink + wmax, bar.x0,
                             (ink >= 0 && bar.x0 >= ink + wmax)
                                 ? "OK" : "MISMATCH");
                }
                /* And no highlight, so the reachability walk below reads a
                 * plain column. */
                g_sh.launcher_menu.highlight = -1;
                sh_run_frame();
            }

            /* Walk the column from top to bottom asking what each pixel row
             * would launch, and count how many distinct entries answer. */
            {
                int seen_last = 0, y;
                for (y = lr.y0; y < lr.y1; y++) {
                    if (sh_launcher_command_at(lr.x0 + 20, y) == last_cmd) {
                        seen_last = 1;
                        break;
                    }
                }
                before = seen_last;
            }
            SYS_LOGI("main", "LSCROLL-DEMO: at rest the last entry ('%s') is "
                     "off the bottom -- not reachable (%s)",
                     (last >= 0) ? lm->items[last].label : "?",
                     (before == 0) ? "OK" : "MISMATCH");

            /* Wheel it down. Enough notches to reach the end whatever the
             * screen turns out to be; the offset clamps itself. */
            for (k = 0; k < 40; k++) {
                feed_wheel(lr.x0 + 20, lr.y0 + 60, -1);
            }
            {
                int seen_last = 0, y;
                for (y = lr.y0; y < lr.y1; y++) {
                    if (sh_launcher_command_at(lr.x0 + 20, y) == last_cmd) {
                        seen_last = 1;
                        break;
                    }
                }
                after = seen_last;
            }
            SYS_LOGI("main", "LSCROLL-DEMO: after the wheel it IS reachable "
                     "-- a click there launches it (%s)",
                     (after == 1) ? "OK" : "MISMATCH");
            probe_repaint_rect("LSCROLL-DEMO",
                               "the Start menu after scrolling", &lr);

            /*
             * And the keyboard gets there too. Arrowing down past the bottom
             * of a scrolled column used to leave the highlight somewhere
             * nobody could see, which is worse than no highlight: Enter then
             * launches whatever it landed on.
             */
            for (k = 0; k < 40; k++) { feed_wheel(lr.x0 + 20, lr.y0 + 60, 1); }
            {
                int hi = -1;
                cbool on_screen = CFALSE;
                /*
                 * Down until it LANDS on the last row, not a fixed number of
                 * presses: ui_menu_step wraps and skips separators, so
                 * "count + 2 presses" lands wherever it lands. The first
                 * spelling of this pressed count + 2 and reported "arrowing
                 * to row 1 scrolls it into view" -- true, and nothing to do
                 * with the row that was the point.
                 */
                for (k = 0; k < lm->count * 2 + 4; k++) {
                    feed_key(PLAT_KEY_DOWN);
                    if (lm->highlight == last) { break; }
                }
                hi = lm->highlight;
                if (hi >= 0) {
                    int y;
                    for (y = lr.y0; y < lr.y1; y++) {
                        if (sh_launcher_command_at(lr.x0 + 20, y)
                            == lm->items[hi].id) {
                            on_screen = CTRUE;
                            break;
                        }
                    }
                }
                SYS_LOGI("main", "LSCROLL-DEMO: arrowing down lands on the "
                         "last row (%d of %d) and scrolls it into view (%s)",
                         hi, last,
                         (hi == last && on_screen) ? "OK" : "MISMATCH");
            }
            probe_repaint_rect("LSCROLL-DEMO",
                               "the Start menu after arrowing to the end",
                               &lr);
            sh_open_launcher(CFALSE);
            for (k = 0; k < 8; k++) { sh_run_frame(); }
        }
        if (opt->launch_demo) {
            /*
             * Every entry on the Start menu opens something.
             *
             * Nothing checked this, and it could not have: each app's scene
             * calls its app_*_open() directly, which is the right thing for
             * testing the app and says nothing at all about whether the menu
             * entry reaches it. The command ids are hand-assigned numbers in
             * shell.h and the dispatcher answers a RANGE of them before the
             * table -- so an id that drifts into that range stops working
             * while every other check stays green.
             *
             * Which is exactly what had happened: the window menu claims
             * 137..150, and FreeCell and Reversi had been given 145 and 146.
             */
            int fails = 0, tried = 0, over = 0;
            int m;
            /* Commands that deliberately open no window: they act on the
             * session or the desktop. Listed by name so that a NEW command
             * which opens nothing fails this check rather than being quietly
             * covered by a pattern. */
            static const int NO_WINDOW[] = {
                SH_CMD_CAPTURE, SH_CMD_EXIT_TO_DOS, SH_CMD_RESTART_SHELL,
                SH_CMD_REFRESH, SH_CMD_ARRANGE, SH_CMD_SHOWDESKTOP
            };
            /*
             * ...and the menu FITS. Rows past the work area are not drawn and
             * not clickable -- an entry you cannot click is indistinguishable
             * from one that was never added, which is the same failure that
             * hid FreeCell and Reversi, reached from the other side.
             *
             * This scene runs at 640x480, the smallest mode this supports, so
             * the ceiling is the real one. Adding one item too many to the
             * Start menu should break the build rather than the menu.
             */
            sh_open_launcher(CTRUE);
            sh_run_frame();
            SYS_LOGI("main", "LAUNCH-DEMO: the Start menu fits the work area "
                     "at %dx%d (%s)", info.width, info.height,
                     sh_launcher_clamped() ? "MISMATCH" : "OK");
            sh_open_launcher(CFALSE);
            sh_run_frame();
            for (m = 0; m < 2; m++) {
                const UiMenu *menu = (m == 0) ? &g_sh.launcher_menu
                                              : &g_sh.launcher_menu2;
                int it;
                for (it = 0; it < menu->count; it++) {
                    int id = menu->items[it].id;
                    int before, after, guard;
                    cbool skip = CFALSE;
                    unsigned q;
                    if (id == UI_MENU_SEPARATOR_ID) { continue; }
                    for (q = 0; q < sizeof(NO_WINDOW)/sizeof(NO_WINDOW[0]); q++) {
                        if (id == NO_WINDOW[q]) { skip = CTRUE; }
                    }
                    if (skip) { continue; }
                    tried++;
                    before = wm_window_count();
                    sh_dispatch_command(id);
                    sh_run_frame();
                    after = wm_window_count();
                    if (after <= before) {
                        fails++;
                        SYS_LOGI("main", "LAUNCH-DEMO: '%s' (id %d) opened "
                                 "nothing (MISMATCH)",
                                 menu->items[it].label, id);
                    } else {
                        /*
                         * ...and it FITS. A window taller than the work area
                         * puts its own bottom under the taskbar, and what
                         * lives at the bottom of a window is the status bar --
                         * so the app looks fine and has quietly lost the row
                         * that answers "what is selected" or "what did that
                         * do". Several apps size themselves from a constant
                         * and only some of them clamp it.
                         */
                        WmWindow *nw = wm_focused();
                        if (nw != NULL) {
                            CRect fr = wm_frame_rect(nw);
                            /* Both axes. The File Manager asks for 660 px of
                             * width, which does not fit a 640 px screen at
                             * all -- checking only the height would have said
                             * it was fine once its bottom was in order. */
                            int dy = fr.y1 - g_sh.taskbar_rect.y0;
                            int dx = fr.x1 - g_sh.screen_w;
                            if (dy > 0 || dx > 0 || fr.y0 < 0 || fr.x0 < 0) {
                                over++;
                                SYS_LOGI("main", "LAUNCH-DEMO: '%s' is %d px "
                                         "below and %d px right of the work "
                                         "area (MISMATCH)",
                                         menu->items[it].label,
                                         (dy > 0) ? dy : 0, (dx > 0) ? dx : 0);
                            }
                        }
                    }
                    /* Back to where we started, so the next entry is measured
                     * against a clean desktop rather than a pile. */
                    for (guard = 0; guard < 8 && wm_window_count() > before;
                         guard++) {
                        WmWindow *top = wm_focused();
                        if (top == NULL) { break; }
                        wm_destroy(top);
                        sh_run_frame();
                    }
                }
            }
            SYS_LOGI("main", "LAUNCH-DEMO: %d Start menu entries, %d opened "
                     "nothing (%s)", tried, fails,
                     (tried > 20 && fails == 0) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "LAUNCH-DEMO: %d window(s) outside the work "
                     "area at %dx%d (%s)", over, info.width, info.height,
                     (over == 0) ? "OK" : "MISMATCH");

            /*
             * The Start menu is not the only way in. The desktop icons, the
             * Quick Launch strip and the desktop's own context menu dispatch
             * the same ids through the same door, and an id that stops working
             * stops working for all of them at once -- but a surface can also
             * carry a command that appears NOWHERE on the Start menu, and that
             * one has nothing else covering it. The Recycle Bin is exactly
             * that: reachable only from its desktop icon and the context menu.
             */
            {
                /* Every command any of those three surfaces raises, taken from
                 * the source of each rather than guessed. */
                static const int SURFACE[] = {
                    /* desktop icons */
                    SH_CMD_SYSINFO, SH_CMD_FILEMANAGER, SH_CMD_CONTROLCENTER,
                    SH_CMD_LOGVIEWER, SH_CMD_MEDIA, SH_CMD_CLOCK,
                    SH_CMD_RECYCLEBIN,
                    /* Quick Launch */
                    SH_CMD_NOTEPAD, SH_CMD_MINES,
                    /* desktop context menu */
                    SH_CMD_TASKMANAGER, SH_CMD_DISPLAYPROPS, SH_CMD_ABOUT
                };
                static const char *const SURFACE_NAME[] = {
                    "This Machine", "Documents", "Control Center",
                    "Log Viewer", "Media Player", "Clock", "Recycle Bin",
                    "Quick Launch Notepad", "Quick Launch Mines",
                    "Task Manager", "Properties", "About"
                };
                int q, sfails = 0;
                for (q = 0; q < (int)(sizeof(SURFACE)/sizeof(SURFACE[0])); q++) {
                    int before = wm_window_count(), after, guard;
                    sh_dispatch_command(SURFACE[q]);
                    sh_run_frame();
                    after = wm_window_count();
                    if (after <= before) {
                        sfails++;
                        SYS_LOGI("main", "LAUNCH-DEMO: '%s' (id %d) opened "
                                 "nothing (MISMATCH)",
                                 SURFACE_NAME[q], SURFACE[q]);
                    }
                    for (guard = 0; guard < 8 && wm_window_count() > before;
                         guard++) {
                        WmWindow *top = wm_focused();
                        if (top == NULL) { break; }
                        wm_destroy(top);
                        sh_run_frame();
                    }
                }
                SYS_LOGI("main", "LAUNCH-DEMO: %d desktop / Quick Launch / "
                         "context-menu commands, %d opened nothing (%s)",
                         q, sfails, (sfails == 0) ? "OK" : "MISMATCH");
            }
        }
        if (opt->vale_demo) {
            /*
             * The castle on the wallpaper is lit from where the sun actually
             * is, and its stone is graded.
             *
             * Both were wrong. Every mass was a flat fill with one darker line
             * down its RIGHT edge -- while the sun in this same scene sits at
             * 78% of the width and the castle at 30%, so the light comes from
             * the castle's right and it was shading the lit faces. Flat fill
             * was the other half: the sky, the hills and the haze are all
             * gradients, and the one flat object read as grey cardboard
             * propped on a painting.
             *
             * Neither the sun's position nor the castle's is passed in here.
             * Both are FOUND in the pixels -- the sun as the warmest column of
             * sky, the castle as the bounding box of stone-coloured pixels --
             * so this keeps testing the relationship if either one moves,
             * which is the whole reason the two drifted apart to begin with.
             */
            const GfxSurface *bb;
            int w, h, x, y;
            long warm_best = -0x7FFFFFFFL;
            int sun_x = -1;
            int bx0 = 1 << 30, bx1 = -1, by0 = 1 << 30, by1 = -1;
            long lsum = 0, rsum = 0, tsum = 0, bsum = 0;
            int ln = 0, rn = 0, tn = 0, bn = 0;

            sh_run_frame();
            /* The WALLPAPER, not the screen: the desktop icons are composited
             * over it, and the first version of this scene found its "sun"
             * inside the Documents folder icon -- the warmest thing on the
             * left-hand side. */
            bb = sh_desktop_background();
            if (bb == NULL) {
                SYS_LOGI("main", "VALE-DEMO: no cached background to measure "
                         "(MISMATCH)");
                goto vale_done;
            }
            w = bb->w; h = bb->h;

            /* The sun is the WARMEST part of the sky, not the brightest: the
             * clouds are pure white and would win a brightness contest, but
             * they are neutral and the sun glow is amber. */
            for (x = 0; x < w; x++) {
                long warm = 0;
                for (y = 4; y < h * 30 / 100; y++) {
                    CColor c = gfx_get_pixel(bb, x, y);
                    warm += GFX_R(c) - GFX_B(c);
                }
                if (warm > warm_best) { warm_best = warm; sun_x = x; }
            }

            /* The castle is the stone: warm grey (R > G > B, and not saturated
             * enough to be the gold pennant), and light enough not to be the
             * flagpole, the windows or the gate. */
            for (y = h * 40 / 100; y < h; y++) {
                for (x = 0; x < w; x++) {
                    CColor c = gfx_get_pixel(bb, x, y);
                    int r = GFX_R(c), g = GFX_G(c), b = GFX_B(c);
                    int luma = (r * 30 + g * 59 + b * 11) / 100;
                    if (!(r > g && g > b && (r - b) < 60 && luma > 70)) {
                        continue;
                    }
                    if (x < bx0) { bx0 = x; }
                    if (x > bx1) { bx1 = x; }
                    if (y < by0) { by0 = y; }
                    if (y > by1) { by1 = y; }
                }
            }

            {
                /* No stone at all leaves the sentinels untouched, and
                 * bx1 - bx0 then prints as a vast negative number that reads
                 * like a different bug. Report the honest zero. */
                cbool found = (bx1 >= bx0 && by1 >= by0) ? CTRUE : CFALSE;
                int cw = found ? (bx1 - bx0 + 1) : 0;
                int ch = found ? (by1 - by0 + 1) : 0;
                SYS_LOGI("main", "VALE-DEMO: there is a castle on the hill "
                         "(%d x %d px of stone) (%s)", cw, ch,
                         (cw > 40 && ch > 25) ? "OK" : "MISMATCH");
                if (!found) { goto vale_done; }
            }

            if (bx1 > bx0 && by1 > by0) {
                int span = bx1 - bx0 + 1;
                int hspan = by1 - by0 + 1;
                for (y = by0; y <= by1; y++) {
                    for (x = bx0; x <= bx1; x++) {
                        CColor c = gfx_get_pixel(bb, x, y);
                        int r = GFX_R(c), g = GFX_G(c), b = GFX_B(c);
                        int luma = (r * 30 + g * 59 + b * 11) / 100;
                        if (!(r > g && g > b && (r - b) < 60 && luma > 70)) {
                            continue;
                        }
                        if (x < bx0 + span / 8)      { lsum += luma; ln++; }
                        if (x > bx1 - span / 8)      { rsum += luma; rn++; }
                        if (y < by0 + hspan / 4)     { tsum += luma; tn++; }
                        if (y > by1 - hspan / 4)     { bsum += luma; bn++; }
                    }
                }
            }

            /* Lit on the side the sun is on. */
            {
                int lavg = (ln > 0) ? (int)(lsum / ln) : 0;
                int ravg = (rn > 0) ? (int)(rsum / rn) : 0;
                int cxx  = (bx0 + bx1) / 2;
                cbool sun_right = (sun_x > cxx) ? CTRUE : CFALSE;
                cbool lit_right = (ravg > lavg) ? CTRUE : CFALSE;
                SYS_LOGI("main", "VALE-DEMO: sun at x=%d, castle at x=%d; "
                         "stone is brighter on the %s (L%d R%d) (%s)",
                         sun_x, cxx, lit_right ? "right" : "left", lavg, ravg,
                         (ln > 0 && rn > 0 && sun_right == lit_right)
                             ? "OK" : "MISMATCH");
            }

            /*
             * Drawing only the visible part of each hill layer produces the
             * SAME PICTURE as drawing all of it.
             *
             * Each layer now stops where the layer in front of it starts,
             * instead of filling to the bottom of the screen and being painted
             * over -- about three hundred thousand pixels of an 800x600 bake
             * that nobody ever saw, on every boot. The only risk is a limit
             * that stops a layer too early and punches a hole.
             *
             * This bakes the background both ways and compares every pixel.
             * Two earlier versions tried to RECOGNISE a hole instead -- "no
             * sky below the skyline" -- and both were wrong about a wallpaper
             * that had not changed at all: the first found its skyline in the
             * clouds, which are near-white and so not sky-blue, and the second
             * found it in the sun's glow, which is warm enough that green ends
             * up its largest channel. The property worth checking was never
             * "what does a hole look like" but "is this the same picture", and
             * that one needs no heuristic.
             */
            {
                cu32 fast, slow;
                fast = sh_desktop_cache_sum();
                sh_desktop_set_hill_limits(CFALSE);
                sh_desktop_build_cache();
                slow = sh_desktop_cache_sum();
                sh_desktop_set_hill_limits(CTRUE);
                sh_desktop_build_cache();
                SYS_LOGI("main", "VALE-DEMO: the hills painted short and the "
                         "hills painted whole are the same picture "
                         "(%08lX vs %08lX) (%s)",
                         (unsigned long)fast, (unsigned long)slow,
                         (fast != 0u && fast == slow) ? "OK" : "MISMATCH");
            }

            /* Graded down its height, like everything else in the scene. */
            {
                int tavg = (tn > 0) ? (int)(tsum / tn) : 0;
                int bavg = (bn > 0) ? (int)(bsum / bn) : 0;
                SYS_LOGI("main", "VALE-DEMO: the stone is graded, not flat "
                         "(top %d vs base %d) (%s)", tavg, bavg,
                         (tn > 0 && bn > 0 && tavg - bavg > 12)
                             ? "OK" : "MISMATCH");
            }
        vale_done: ;
        }
        if (opt->orb_demo) {
            /*
             * The Start button is a BUTTON.
             *
             * It stopped being one silently. In glossy mode it is a sphere
             * that overhangs the taskbar's top edge, and it was drawn by
             * handing the whole 38px square to sh_logo_draw_plated -- which
             * puts a pale disc behind the PROCEDURAL mark. The moment the mark
             * became a pack image that call started drawing the image alone
             * (an image is a finished mark and needs no disc behind it on the
             * launcher's header, which is what the plate was for). So the
             * taskbar had no button at all: bare artwork at button size,
             * floating over the wallpaper with a hard clipped edge, and
             * hover/pressed were a picture changing colour.
             *
             * Nothing caught it because nothing here had ever asked what the
             * Start button LOOKS like. These four checks do, off the composited
             * back buffer, which is the only place the answer exists.
             */
            GfxSurface *bb;
            CRect b;
            int cx, cy, R, i;
            int ring_up = 0;
            long sum_up = 0, sum_down = 0;
            /* Sixteen points around a ring at 80% of the radius: outside the
             * mark (which occupies the middle 58%) and inside the rim, so
             * every one of them is the sphere's body and nothing else. */
            static const int COS16[16] = { 100, 92, 71, 38, 0, -38, -71, -92,
                                           -100, -92, -71, -38, 0, 38, 71, 92 };
            static const int SIN16[16] = { 0, 38, 71, 92, 100, 92, 71, 38,
                                           0, -38, -71, -92, -100, -92, -71, -38 };

            sh_run_frame();
            /* There is no orb in 16/256-colour or safe mode -- the Start
             * button is a bevelled, labelled rectangle there, a different
             * drawing with different checks to make. The demo harness runs
             * this scene in truecolor, where the orb is what exists. */
            if (!sh_glossy()) {
                SYS_LOGI("main", "ORB-DEMO: flat mode draws a labelled button, "
                         "not an orb -- nothing here to check");
                goto orb_done;
            }
            b = g_sh.launcher_button;
            cx = b.x0 + crect_w(&b) / 2;
            cy = b.y0 + crect_h(&b) / 2;
            R  = (crect_w(&b) * 80) / 200;      /* 80% of half the width */
            bb = plat_backbuffer();

            /*
             * 1. There is a disc there. Each ring point is compared with the
             * pixel at the SAME HEIGHT far to the right -- taskbar below the
             * bar's top edge, wallpaper above it -- because that is exactly
             * what showed through when there was no disc.
             */
            for (i = 0; i < 16; i++) {
                int px = cx + (COS16[i] * R) / 100;
                int py = cy + (SIN16[i] * R) / 100;
                int gap = colour_gap(gfx_get_pixel(bb, px, py),
                                     gfx_get_pixel(bb, 400, py));
                sum_up += gap;
                if (gap > 60) { ring_up++; }
            }
            SYS_LOGI("main", "ORB-DEMO: the Start button is a disc, not bare "
                     "artwork -- %d/16 ring points differ from the bar behind "
                     "them (%s)", ring_up, (ring_up >= 14) ? "OK" : "MISMATCH");

            /*
             * 2. The overhang is not a stray pixel. With R = d/2 the topmost
             * and leftmost points sit exactly on the boundary while the
             * opposite sides fall a pixel short, and the circle grows a nub at
             * 12 and 9 o'clock that reads as dirt on the wallpaper.
             */
            {
                int nub = colour_gap(gfx_get_pixel(bb, cx, b.y0),
                                     gfx_get_pixel(bb, cx, b.y0 - 2));
                SYS_LOGI("main", "ORB-DEMO: no stray pixel above the sphere "
                         "(gap %d) (%s)", nub, (nub < 40) ? "OK" : "MISMATCH");
            }

            /* 3. Pressed looks pressed. Same ring, menu open. */
            sh_open_launcher(CTRUE);
            sh_run_frame();
            sh_run_frame();
            bb = plat_backbuffer();
            for (i = 0; i < 16; i++) {
                int px = cx + (COS16[i] * R) / 100;
                int py = cy + (SIN16[i] * R) / 100;
                int gap = colour_gap(gfx_get_pixel(bb, px, py),
                                     gfx_get_pixel(bb, 400, py));
                sum_down += gap;
            }
            {
                long d = (sum_up > sum_down) ? (sum_up - sum_down)
                                             : (sum_down - sum_up);
                SYS_LOGI("main", "ORB-DEMO: pressed differs from resting "
                         "(%ld vs %ld over the ring) (%s)", sum_up, sum_down,
                         (d > 300) ? "OK" : "MISMATCH");
            }

            /*
             * 4. ...and it is still a whole sphere while pressed. The launcher
             * panel's bottom edge lands on the taskbar and is composited after
             * it, so an open menu covered the overhang and sliced the top off
             * the orb. The check is the ring again: the four points above the
             * bar's top edge are the ones the panel used to eat.
             */
            /*
             * ...and it is still a WHOLE sphere while pressed. The launcher
             * panel's bottom edge lands on the taskbar and is composited after
             * it, so an open menu covered the overhang and sliced the top off
             * the orb.
             *
             * The comparison is against the panel BESIDE the orb at the same
             * height, not against the wallpaper: when the panel was winning,
             * the top of the button was simply more panel, and any check that
             * only asked "does this differ from the desktop behind it" said
             * yes to that too. It did -- the first version of this check
             * passed against the bug it was written for.
             */
            {
                int inside  = gfx_get_pixel(bb, cx, b.y0 + 3);
                int beside  = gfx_get_pixel(bb, b.x0 + crect_w(&b) + 8,
                                            b.y0 + 3);
                int gap = colour_gap(inside, beside);
                SYS_LOGI("main", "ORB-DEMO: the sphere survives an open Start "
                         "menu -- its overhang is the orb, not more menu "
                         "(gap %d) (%s)", gap, (gap > 60) ? "OK" : "MISMATCH");
            }
            sh_open_launcher(CFALSE);
            sh_run_frame();
        orb_done: ;
        }
        if (opt->trash_demo) {
            /*
             * Delete NOTES.TXT from two different folders, then put both back.
             *
             * This is a bug that shipped. The bin is one flat folder, so the
             * second delete found the slot taken and cleared it with
             * plat_file_remove -- destroying the first file, permanently,
             * while the status line said "Moved to Trash" and Help promised
             * that deleting "moves to the Recycle Bin rather than destroying
             * anything". A data-loss path that announces success.
             *
             * Everything below goes through the real File Manager: Delete on
             * the keyboard, its confirm dialog, and Restore off the context
             * menu F10 opens. tests/test_trash.c already covers the naming
             * and the index as strings; what only a running system can answer
             * is whether the two ever meet -- and the contents are compared,
             * not just the names, because the whole failure was one file
             * wearing another's name.
             */
            char home[CASTALIA_MAX_PATH];
            char da[CASTALIA_MAX_PATH], db[CASTALIA_MAX_PATH];
            char fa[CASTALIA_MAX_PATH], fb[CASTALIA_MAX_PATH];
            char trash[CASTALIA_MAX_PATH], tmp[CASTALIA_MAX_PATH];
            static const char *TEXT_A = "this one lived in TRSHA";
            static const char *TEXT_B = "and this one lived in TRSHB";
            int step;

            sys_strlcpy(home, sys_home(), sizeof(home));
            sys_snprintf(da, sizeof(da), "%s/TRSHA", home);
            sys_snprintf(db, sizeof(db), "%s/TRSHB", home);
            sys_snprintf(fa, sizeof(fa), "%s/NOTES.TXT", da);
            sys_snprintf(fb, sizeof(fb), "%s/NOTES.TXT", db);
            sys_home_path(trash, (cu32)sizeof(trash), "TRASH");
            plat_mkdir(trash);
            plat_mkdir(da);
            plat_mkdir(db);

            /* Start from a known bin: leftovers from an earlier run would
             * shift every row this check counts on. */
            sys_snprintf(tmp, sizeof(tmp), "%s/NOTES.TXT", trash);
            plat_file_remove(tmp);
            sys_snprintf(tmp, sizeof(tmp), "%s/NOTES~1.TXT", trash);
            plat_file_remove(tmp);
            sys_snprintf(tmp, sizeof(tmp), "%s/TRASH.IDX", trash);
            plat_file_remove(tmp);
            feed_write_text(fa, TEXT_A);
            feed_write_text(fb, TEXT_B);

            /* Delete both, each from its own folder. Row 0 is "..", row 1 is
             * the only file there. */
            for (step = 0; step < 2; step++) {
                app_fileman_open_path(step == 0 ? da : db);
                sh_run_frame();
                feed_key(PLAT_KEY_HOME);
                feed_key(PLAT_KEY_DOWN);
                feed_key(PLAT_KEY_DELETE);
                feed_key(PLAT_KEY_ENTER);      /* Yes, move it to Trash */
                wm_destroy(wm_focused());
                sh_run_frame();
            }

            /*
             * THE check. Two files went in; two files must be in there, and
             * the first one must still say what it said. Under the old code
             * the second delete removed the first, so the bin held one file
             * reading "TRSHB" and TRSHA's copy was gone for good.
             */
            {
                char pa[CASTALIA_MAX_PATH], pb[CASTALIA_MAX_PATH];
                char got[64];
                sys_snprintf(pa, sizeof(pa), "%s/NOTES.TXT", trash);
                sys_snprintf(pb, sizeof(pb), "%s/NOTES~1.TXT", trash);
                SYS_LOGI("main", "TRASH-DEMO: both deletes kept their own "
                         "slot (%s)",
                         (plat_file_size(pa) > 0 && plat_file_size(pb) > 0)
                             ? "OK" : "MISMATCH");
                feed_read_text(pa, got, sizeof(got));
                SYS_LOGI("main", "TRASH-DEMO: the first file survived the "
                         "second delete (%s)",
                         (strcmp(got, TEXT_A) == 0) ? "OK" : "MISMATCH");
            }
            SYS_LOGI("main", "TRASH-DEMO: the desktop bin shows full (%s)",
                     sh_recyclebin_full() ? "OK" : "MISMATCH");

            /* Now restore both, through the context menu. The bin is opened
             * fresh each time: after the first restore the remaining item is
             * row 1 again. */
            plat_file_remove(fa);
            plat_file_remove(fb);
            for (step = 0; step < 2; step++) {
                app_fileman_open_path(trash);
                sh_run_frame();
                feed_key(PLAT_KEY_HOME);
                feed_key(PLAT_KEY_DOWN);
                /* F10 opens the menu with the highlight already on its first
                 * item, "Open"; one Down reaches "Restore". */
                feed_key(PLAT_KEY_F10);
                feed_key(PLAT_KEY_DOWN);
                feed_key(PLAT_KEY_ENTER);
                wm_destroy(wm_focused());
                sh_run_frame();
            }

            /*
             * Each file back in ITS OWN folder, under ITS OWN name, with ITS
             * OWN bytes. The name matters as much as the folder: the second
             * one was stored as NOTES~1.TXT, and handing that back is not a
             * restore.
             */
            {
                char ga[64], gb[64];
                feed_read_text(fa, ga, sizeof(ga));
                feed_read_text(fb, gb, sizeof(gb));
                SYS_LOGI("main", "TRASH-DEMO: TRSHA/NOTES.TXT is back, and it "
                         "is TRSHA's (%s)",
                         (strcmp(ga, TEXT_A) == 0) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "TRASH-DEMO: TRSHB/NOTES.TXT is back, and it "
                         "is TRSHB's (%s)",
                         (strcmp(gb, TEXT_B) == 0) ? "OK" : "MISMATCH");
            }
            /* The bin is empty again -- and its own index file must not read
             * as a leftover item, or the icon stays full forever. */
            SYS_LOGI("main", "TRASH-DEMO: the desktop bin reads empty once "
                     "everything is back (%s)",
                     sh_recyclebin_full() ? "MISMATCH" : "OK");
            {
                static char idx[4096];
                sys_snprintf(tmp, sizeof(tmp), "%s/TRASH.IDX", trash);
                feed_read_text(tmp, idx, sizeof(idx));
                SYS_LOGI("main", "TRASH-DEMO: the index forgot both items "
                         "(%s)",
                         (trash_idx_count(idx) == 0) ? "OK" : "MISMATCH");
            }

            plat_file_remove(fa);
            plat_file_remove(fb);
            plat_dir_remove(da);
            plat_dir_remove(db);
            sys_snprintf(tmp, sizeof(tmp), "%s/TRASH.IDX", trash);
            plat_file_remove(tmp);
        }
        if (opt->screen_check) {
            /* Used by the abuse suite. Booting without crashing is the easy
             * half of surviving a corrupt config; the half that matters is
             * whether what came up is a desktop or a blank screen that failed
             * quietly. Two checks, and only two, because a check that cannot
             * fail is worse than no check:
             *
             *   - the desktop band has real colour variety. Confirmed able to
             *     fail: disabling sh_desktop_paint makes it report MISMATCH.
             *   - the video mode is inside the range the system claims to
             *     support. This is the one an INI full of absurd numbers
             *     would break, and it is aimed at the DOS backend, which
             *     negotiates modes; the host backend always returns
             *     800x600x32, so nothing here can make it fail on the host.
             *
             * A third check lived here briefly -- "was a taskbar drawn",
             * by comparing the colours along the bottom against the desktop
             * above. It cannot work: the desktop is a vertical gradient, so
             * the bottom band differs from the top whether a taskbar was
             * drawn or not, and it passed with taskbar painting disabled.
             */
            GfxSurface *bb;
            PlatVideoInfo vi;
            int x, y, seen = 0;
            CColor pal[64];
            /* Draw first: the scenes run before the frame loop, so sampling
             * here without this would measure an empty buffer. */
            sh_run_frame();
            sh_run_frame();
            bb = plat_backbuffer();
            plat_video_info(&vi);

            for (y = 4; y < vi.height - 40; y += 7) {
                for (x = 4; x < vi.width - 4; x += 7) {
                    CColor c = gfx_get_pixel(bb, x, y);
                    int k, dup = 0;
                    for (k = 0; k < seen; k++) {
                        if (pal[k] == c) { dup = 1; break; }
                    }
                    if (!dup && seen < 64) { pal[seen++] = c; }
                }
            }
            SYS_LOGI("main", "SCREEN-CHECK: %dx%dx%d, %d distinct colours",
                     vi.width, vi.height, vi.bpp, seen);
            SYS_LOGI("main", "SCREEN-CHECK: the desktop was painted (%s)",
                     (seen >= 6) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "SCREEN-CHECK: the mode is one we support (%s)",
                     (vi.width >= 320 && vi.width <= 1600 &&
                      vi.height >= 200 && vi.height <= 1200 &&
                      (vi.bpp == 8 || vi.bpp == 16 || vi.bpp == 32))
                         ? "OK" : "MISMATCH");
        }
        if (opt->cpu_demo) {
            /* The decoding tables are unit-tested against made-up numbers.
             * What that cannot cover is the seam: the real read going through
             * the real decoder and coming out fit to put in a window.
             *
             * What this checks, and each was confirmed able to fail: the name
             * is non-empty and printable (breaking cpu_describe to write
             * nothing fails it), the feature list is never blank, and a
             * RECOGNISED vendor's label reaches the displayed name (dropping
             * the vendor from cpu_describe fails it).
             *
             * What it cannot check, and does not pretend to: whether the read
             * itself was correct. Reading the vendor out of the wrong register
             * produces an unrecognised vendor, which is indistinguishable from
             * genuinely running on a chip nobody has heard of -- so it takes
             * the numeric-fallback branch and passes, correctly. Verifying the
             * read would need a known-good answer this program has no way to
             * obtain about the machine it is running on.
             */
            PlatCpuId cid;
            CpuInfo cpu;
            char name[64], feat[64];
            const char *label;
            int k, printable = 1;

            plat_cpu_id(&cid);
            cpu.has_cpuid = cid.has_cpuid;
            sys_strlcpy(cpu.vendor, cid.vendor, sizeof(cpu.vendor));
            cpu.family = cid.family;
            cpu.model = cid.model;
            cpu.stepping = cid.stepping;
            cpu.features = cid.features;
            cpu_describe(&cpu, name, sizeof(name));
            cpu_feature_list(cpu.features, feat, sizeof(feat));
            label = cpu_vendor_label(cid.vendor);

            SYS_LOGI("main", "CPU-DEMO: '%s' [%s] (vendor '%s', %d/%d/%d)",
                     name, feat, cid.vendor, cid.family, cid.model,
                     cid.stepping);
            for (k = 0; name[k] != '\0'; k++) {
                if (name[k] < 0x20 || name[k] > 0x7E) { printable = 0; }
            }
            SYS_LOGI("main", "CPU-DEMO: the name is fit to display (%s)",
                     (name[0] != '\0' && printable) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "CPU-DEMO: the feature list is never empty (%s)",
                     (feat[0] != '\0') ? "OK" : "MISMATCH");
            /* Two different assertions, because the interesting seam differs.
             * When the vendor was RECOGNISED, the friendly label ("Intel")
             * has to reach the displayed name -- and "recognised" means the
             * label differs from the raw string, since an unrecognised vendor
             * is echoed verbatim and "the label appears in the name" would
             * then be true by construction rather than by working.
             * When it was not recognised, the name must fall back to the
             * numbers, so it has to say "family". */
            if (cid.has_cpuid && sys_stricmp(label, cid.vendor) != 0 &&
                sys_stricmp(label, "Unknown") != 0) {
                int found = 0, li = 0;
                for (k = 0; name[k] != '\0'; k++) {
                    li = (name[k] == label[li]) ? li + 1 : 0;
                    if (label[li] == '\0') { found = 1; break; }
                }
                SYS_LOGI("main", "CPU-DEMO: the vendor label '%s' reached the "
                         "name (%s)", label, found ? "OK" : "MISMATCH");
            } else if (cid.has_cpuid) {
                int fam = 0, li = 0;
                static const char *WANT = "family";
                for (k = 0; name[k] != '\0'; k++) {
                    li = (name[k] == WANT[li]) ? li + 1 : 0;
                    if (WANT[li] == '\0') { fam = 1; break; }
                }
                SYS_LOGI("main", "CPU-DEMO: an unknown vendor falls back to "
                         "the numbers (%s)", fam ? "OK" : "MISMATCH");
            } else {
                SYS_LOGI("main", "CPU-DEMO: no CPUID here, and it says so (%s)",
                         (name[0] != '\0') ? "OK" : "MISMATCH");
            }
        }
        if (opt->switch_demo) {
            /* The property worth checking is the one z-order cycling does not
             * have: Alt+Tab goes to the window you were just in, and pressing
             * it again brings you back. Three windows, so a rotation and an
             * MRU walk visibly disagree -- with z-order the second press would
             * land on the third window, and it does: replacing the MRU step
             * with wm_cycle_focus makes the second check below fail.
             */
            PlatEvent e;
            const char *a, *b, *c;
            app_calc_open();    sh_run_frame();
            app_notepad_open(); sh_run_frame();
            app_fileman_open(); sh_run_frame();
            c = wm_title(wm_focused());
            memset(&e, 0, sizeof(e)); e.type = PLAT_EV_KEY_DOWN;
            e.key = PLAT_KEY_NEXTWIN; e.ch = 0;
            plat_host_push_event(&e); sh_run_frame();
            b = wm_title(wm_focused());
            SYS_LOGI("main", "SWITCH-DEMO: '%s' -> '%s'", c, b);
            SYS_LOGI("main", "SWITCH-DEMO: one press leaves the window it "
                     "started on (%s)",
                     (b != NULL && c != NULL && strcmp(b, c) != 0) ? "OK"
                                                                   : "MISMATCH");
            plat_host_push_event(&e); sh_run_frame();
            a = wm_title(wm_focused());
            SYS_LOGI("main", "SWITCH-DEMO: pressing it again returns to '%s' "
                     "(%s)", c,
                     (a != NULL && c != NULL && strcmp(a, c) == 0) ? "OK"
                                                                   : "MISMATCH");
            /* Shift walks the other way, and "the other way" is worth being
             * precise about. Because each press commits immediately there is
             * no held-open switching session to step backwards through, so
             * Shift from the front wraps to the LEAST recently used window --
             * and repeating it rotates through every window in a stable order.
             * That is the useful complement to plain Alt+Tab, which toggles
             * between the last two. My first version of this check asserted
             * that Shift undid the previous press; that is what a session
             * model would do, not this one, and the code was right.
             *
             * This checks the SHELL's handling of the modifier, which is all
             * it can: whether a real BIOS reports Shift+Alt+Tab as scan 0xA5
             * with the shift flag set is a question about hardware nobody here
             * has, and src/platform/dos/keyboard.c says so where the scan code
             * is mapped. */
            {
                const char *oldest = wm_title(wm_focused());
                const char *seen[3];
                PlatEvent se;
                int k, distinct = 1;
                memset(&se, 0, sizeof(se));
                se.type = PLAT_EV_KEY_DOWN;
                se.key = PLAT_KEY_NEXTWIN;
                se.mods = PLAT_MOD_SHIFT;
                for (k = 0; k < 3; k++) {
                    plat_host_push_event(&se);
                    sh_run_frame();
                    seen[k] = wm_title(wm_focused());
                }
                /* Three presses over three windows must visit three different
                 * ones and end back where it began -- a rotation, not a
                 * toggle. */
                if (seen[0] == NULL || seen[1] == NULL || seen[2] == NULL ||
                    strcmp(seen[0], seen[1]) == 0 ||
                    strcmp(seen[1], seen[2]) == 0 ||
                    strcmp(seen[0], seen[2]) == 0) { distinct = 0; }
                SYS_LOGI("main", "SWITCH-DEMO: Shift rotates '%s' -> '%s' -> "
                         "'%s' -> '%s'", oldest, seen[0], seen[1], seen[2]);
                SYS_LOGI("main", "SWITCH-DEMO: Shift visits every window and "
                         "comes back (%s)",
                         (distinct && oldest != NULL &&
                          strcmp(seen[2], oldest) == 0) ? "OK" : "MISMATCH");
            }

            /* And the panel is on screen while this is happening.
             *
             * Counting "pale" pixels around the middle of the screen does NOT
             * work, and the first version of this check did exactly that: the
             * region sits over the File Manager's white file list, so it
             * passed with the panel drawing disabled entirely. What only the
             * panel produces is its selected row -- a wide solid band of the
             * accent colour, where the panel puts it and nowhere else. */
            {
                GfxSurface *bb = plat_backbuffer();
                const UiPalette *pal = ui_palette();
                PlatVideoInfo vi;
                int x, y, best = 0;
                plat_video_info(&vi);
                for (y = vi.height / 2 - 70; y < vi.height / 2 + 40; y++) {
                    int run = 0;
                    for (x = (vi.width - 236) / 2; x < (vi.width + 236) / 2; x++) {
                        if (gfx_get_pixel(bb, x, y) == pal->accent) {
                            run++;
                            if (run > best) { best = run; }
                        } else {
                            run = 0;
                        }
                    }
                }
                SYS_LOGI("main", "SWITCH-DEMO: the panel's selected row is "
                         "drawn (%d px wide) (%s)", best,
                         (best > 180) ? "OK" : "MISMATCH");
            }
        }
        if (opt->sysmenu_keep) {
            /* Just the picture: a window with its menu up, for the docs.
             * The window-open zoom draws an outline over everything while it
             * runs, so let it finish first -- otherwise the picture catches a
             * rectangle mid-flight across the menu and looks like a bug. */
            int f2;
            app_notepad_open();
            for (f2 = 0; f2 < 16; f2++) { sh_run_frame(); }
            feed_key(PLAT_KEY_SYSMENU);
            feed_key(PLAT_KEY_DOWN);  /* land on an item, so the picture shows
                                       * the selection too */
        }
        if (opt->deskkeys_demo) {
            /*
             * The desktop icons, with no mouse.
             *
             * They were reachable by clicking and in no other way, on a system
             * whose own hardware verification was carried out with a keyboard
             * alone -- so This Machine, Documents, the Control Center, the Log
             * Viewer, the Media Player, the Clock and the Recycle Bin could
             * only be got at through the launcher.
             *
             * The checks that matter are the routing ones. Arrows have to
             * reach the desktop ONLY when nothing else wants them, and the
             * easiest way to get this wrong is to steal them from a window.
             */
            int sel0, sel1, sel2;

            feed_key(PLAT_KEY_DOWN);
            sel0 = sh_desktop_selected();
            SYS_LOGI("main", "DESKKEYS-DEMO: first arrow selects icon %d", sel0);
            SYS_LOGI("main", "DESKKEYS-DEMO: an arrow on a bare desktop selects "
                     "an icon (%s)", (sel0 == 0) ? "OK" : "MISMATCH");

            feed_key(PLAT_KEY_DOWN);
            sel1 = sh_desktop_selected();
            feed_key(PLAT_KEY_UP);
            sel2 = sh_desktop_selected();
            SYS_LOGI("main", "DESKKEYS-DEMO: down then up walked %d -> %d -> "
                     "%d", sel0, sel1, sel2);
            SYS_LOGI("main", "DESKKEYS-DEMO: down moves on and up comes back "
                     "(%s)",
                     (sel1 == sel0 + 1 && sel2 == sel0) ? "OK" : "MISMATCH");

            /* The default layout is one column, so Left and Right have
             * nowhere to go and must leave the selection alone rather than
             * jumping to whatever is next in the array. */
            feed_key(PLAT_KEY_RIGHT);
            feed_key(PLAT_KEY_LEFT);
            SYS_LOGI("main", "DESKKEYS-DEMO: sideways in a single column does "
                     "nothing (%s)",
                     (sh_desktop_selected() == sel2) ? "OK" : "MISMATCH");

            /* Enter opens the selected icon. Icon 0 is This Machine. */
            feed_key(PLAT_KEY_ENTER);
            {
                WmWindow *opened = wm_focused();
                const char *t = (opened != NULL) ? wm_title(opened) : "(none)";
                SYS_LOGI("main", "DESKKEYS-DEMO: Enter opened '%s'", t);
                SYS_LOGI("main", "DESKKEYS-DEMO: Enter opens the selected icon "
                         "(%s)",
                         (opened != NULL && strstr(t, "System") != NULL)
                             ? "OK" : "MISMATCH");
            }

            /*
             * And now the routing check that stops this being a bug: with a
             * window focused, the arrows belong to the WINDOW. If the desktop
             * kept taking them, every list and text field in the system would
             * lose its arrow keys the moment the desktop had a selection.
             */
            {
                int before = sh_desktop_selected();
                feed_key(PLAT_KEY_DOWN);
                feed_key(PLAT_KEY_DOWN);
                SYS_LOGI("main", "DESKKEYS-DEMO: with a window up the desktop "
                         "selection stayed at %d (%s)", sh_desktop_selected(),
                         (sh_desktop_selected() == before) ? "OK" : "MISMATCH");
            }
            /* ...and once the window closes, they come back to the desktop. */
            feed_key(PLAT_KEY_CLOSE);
            {
                int before = sh_desktop_selected();
                feed_key(PLAT_KEY_DOWN);
                SYS_LOGI("main", "DESKKEYS-DEMO: closing it gives the arrows "
                         "back (%d -> %d) (%s)", before, sh_desktop_selected(),
                         (sh_desktop_selected() == before + 1)
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->mousekey_demo) {


            /*
             * Mouse and Keyboard: the last two Control Center categories.
             *
             * A settings panel is easy to write and easy to leave decorative,
             * so the checks are about whether anything downstream CHANGED.
             * For the double-click window that is measurable: set it to its
             * fastest, then click twice 300 ms apart. Under the old
             * hard-coded 400 ms that was a double-click; under a setting the
             * desktop actually reads, it is two single clicks and nothing
             * opens. A panel wired to nothing passes every other check.
             */
            WmWindow *cw2;
            CRect rr;
            CPoint og;
            int before, i;

            {
                /*
                 * Every category, checked for CONTAINMENT.
                 *
                 * The Screen Saver caption was 45 characters in a panel that fits
                 * 42: it ran through the panel's etched border and out over the
                 * window frame. gfx_draw_text neither fits nor clips -- the same
                 * hole that once let every button in the system paint outside
                 * itself -- and nothing noticed for as long as the panel has
                 * existed, because no check in this tree looks at a panel's
                 * pixels and the overflow is off at the right-hand edge where
                 * the eye does not go.
                 *
                 * So: draw each of the ten panels and require the four columns
                 * just OUTSIDE each one to be untouched. Everything a panel draws
                 * -- text, previews, radio rows -- belongs inside it.
                 */
                int cat2, dirty_cat = -1, dirty_px = 0;
                for (cat2 = 0; cat2 < 10; cat2++) {
                    WmWindow *sw2;
                    CRect pan;
                    GfxSurface *bb2;
                    CPoint po;
                    int q, xx, yy, bad = 0;
                    CColor face;
                    app_control_open_cat(cat2);
                    for (q = 0; q < 8; q++) { sh_run_frame(); }
                    sw2 = wm_focused();
                    bb2 = plat_backbuffer();
                    if (sw2 != NULL && bb2 != NULL &&
                        app_control_panel_rect(sw2, &pan)) {
                        po = wm_client_origin(sw2);
                        /* The window's own face, sampled from a gap below the
                         * panel rather than assumed from the theme. */
                        face = gfx_get_pixel(bb2, po.x + pan.x1 + 3,
                                             po.y + pan.y1 + 3);
                        for (yy = po.y + pan.y0; yy <= po.y + pan.y1; yy++) {
                            for (xx = po.x + pan.x1 + 1;
                                 xx <= po.x + pan.x1 + 4; xx++) {
                                if (gfx_get_pixel(bb2, xx, yy) != face) { bad++; }
                            }
                        }
                    }
                    if (bad > dirty_px) { dirty_px = bad; dirty_cat = cat2; }
                    while (wm_window_count() > 0) {
                        WmWindow *dw2 = wm_window_at(0);
                        if (dw2 == NULL) { break; }
                        wm_destroy(dw2);
                    }
                    for (q = 0; q < 3; q++) { sh_run_frame(); }
                }
                SYS_LOGI("main", "MOUSEKEY-DEMO: %d pixel(s) painted outside a "
                         "settings panel (worst category %d) (%s)",
                         dirty_px, dirty_cat,
                         (dirty_px == 0) ? "OK" : "MISMATCH");
            }

            /*
             * First, the same panel with NO MOUSE.
             *
             * The Control Center could browse its categories from the
             * keyboard and change nothing: all fifteen of its controls
             * answered the pointer alone. That is the worst app in the suite
             * to have that problem, because the Mouse and Keyboard panels
             * are inside it -- the place you would go to sort out a mouse is
             * the place you could not reach without one.
             *
             * Tab moves into the panel, Down walks it, Enter chooses.
             */
            {
                WmWindow *kw;
                int chosen;
                app_control_open_cat(6);        /* Keyboard */
                for (i = 0; i < 6; i++) { sh_run_frame(); }
                kw = wm_focused();
                /*
                 * Down to "Shortest (250 ms)", the FOURTH row -- deliberately
                 * not the third.
                 *
                 * The third is "Short (500 ms)", which is also the built-in
                 * default, so the first version of this check asserted 500
                 * and passed with the whole Tab handler removed: the arrows
                 * had merely walked the category list and Enter had pressed
                 * Apply, leaving the delay at the value it already had. A
                 * check whose expected value is the default is not a check.
                 */
                feed_key(PLAT_KEY_TAB);         /* into the panel      */
                feed_key(PLAT_KEY_DOWN);        /* second row          */
                feed_key(PLAT_KEY_DOWN);        /* third               */
                feed_key(PLAT_KEY_DOWN);        /* fourth: Shortest    */
                feed_key(PLAT_KEY_ENTER);       /* choose it           */
                sh_run_frame();
                chosen = app_control_key_delay(kw);
                SYS_LOGI("main", "MOUSEKEY-DEMO: keyboard-only pick set the "
                         "repeat delay to %d ms, want 250 and not the 500 it "
                         "starts at (%s)", chosen,
                         (chosen == 250) ? "OK" : "MISMATCH");
                /*
                 * Destroyed outright, not closed with Alt+F4: the Control
                 * Center does not act on WM_MSG_CLOSE, so the window stayed
                 * up and the next app_control_open_cat reused it -- still on
                 * the Keyboard panel. The Mouse checks below then clicked
                 * Keyboard rows and reported the double-click window
                 * unchanged, which looked exactly like the panel being
                 * broken rather than the scene being wrong.
                 */
                while (wm_window_count() > 0) {
                    WmWindow *dw = wm_window_at(0);
                    if (dw == NULL) { break; }
                    wm_destroy(dw);
                }
                for (i = 0; i < 4; i++) { sh_run_frame(); }
            }

            app_control_open_cat(5);            /* Mouse */
            sh_run_frame();
            cw2 = wm_focused();
            og = wm_client_origin(cw2);
            /* "Fast (250 ms)" is the fourth row of the group. */
            rr = crect_make(0, 0, 0, 0);
            for (i = 0; i < 4; i++) {
                CRect row;
                if (!app_control_row_rect(cw2, i + 2, &row)) { continue; }
                if (i == 3) { rr = row; }
            }
            feed_click(og.x + rr.x0 + 10, og.y + (rr.y0 + rr.y1) / 2);
            feed_key(PLAT_KEY_ENTER);           /* OK: apply and close */
            sh_run_frame();
            SYS_LOGI("main", "MOUSEKEY-DEMO: the Mouse panel set the "
                     "double-click window to %d ms (%s)",
                     settings_get()->dblclick_ms,
                     (settings_get()->dblclick_ms == 250) ? "OK" : "MISMATCH");

            /* Two clicks 300 ms apart on a desktop icon. Too slow to be a
             * double-click now, and comfortably fast enough under the old
             * constant -- which is what makes this a test of the wiring. */
            before = wm_window_count();
            if (sh_desktop_icon_rect(0, &rr)) {
                int cx = (rr.x0 + rr.x1) / 2, cy = (rr.y0 + rr.y1) / 2;
                feed_click(cx, cy);
                plat_sleep_ms(300);
                sh_run_frame();
                feed_click(cx, cy);
                SYS_LOGI("main", "MOUSEKEY-DEMO: 300 ms apart is no longer a "
                         "double-click (%d windows -> %d) (%s)",
                         before, wm_window_count(),
                         (wm_window_count() == before) ? "OK" : "MISMATCH");
                /* ...and back to back it still opens, so the check above is
                 * not just "clicking an icon does nothing". */
                feed_click(cx, cy);
                feed_click(cx, cy);
                SYS_LOGI("main", "MOUSEKEY-DEMO: back to back still opens it "
                         "(%d windows) (%s)", wm_window_count(),
                         (wm_window_count() > before) ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_CLOSE);
            }

            /* Keyboard: the rate has to reach the platform, which is the only
             * place it can mean anything -- nothing above plat_ is in the
             * repeat loop. The host refuses and logs, so the check is that
             * the setting travelled, not that a key repeated. */
            app_control_open_cat(6);            /* Keyboard */
            sh_run_frame();
            cw2 = wm_focused();
            og = wm_client_origin(cw2);
            if (app_control_row_rect(cw2, 2, &rr)) {   /* first delay row */
                feed_click(og.x + rr.x0 + 10, og.y + (rr.y0 + rr.y1) / 2);
            }
            if (app_control_row_rect(cw2, 4 + 3 + 3, &rr)) { /* last rate row */
                feed_click(og.x + rr.x0 + 10, og.y + (rr.y0 + rr.y1) / 2);
            }
            feed_key(PLAT_KEY_ENTER);
            sh_run_frame();
            SYS_LOGI("main", "MOUSEKEY-DEMO: the Keyboard panel set %d ms / "
                     "%d cps (%s)", settings_get()->key_delay_ms,
                     settings_get()->key_cps,
                     (settings_get()->key_delay_ms == 1000 &&
                      settings_get()->key_cps == 30) ? "OK" : "MISMATCH");

            /*
             * ...and that the number left the settings struct.
             *
             * Nothing above the platform layer is in the key-repeat loop, so
             * on a host there is no behaviour to observe -- the call is a
             * refusal. What IS observable is that the refusal happened with
             * these values, which is the whole path: panel -> settings ->
             * apply -> plat. Without this, deleting the plat_set_key_repeat
             * call entirely passes every check above.
             */
            {
                int kd = -1, kc = -1;
                plat_host_last_key_repeat(&kd, &kc);
                SYS_LOGI("main", "MOUSEKEY-DEMO: and the platform was actually "
                         "asked for it (%d ms / %d cps) (%s)", kd, kc,
                         (kd == 1000 && kc == 30) ? "OK" : "MISMATCH");
            }
        }
        if (opt->datetime_demo) {
            /*
             * The Control Center's Date and Time panel.
             *
             * The machine this targets comes out of a cupboard with a flat
             * coin cell, reporting 1980 and stamping every file it saves
             * wrong, so setting the clock is the first thing anybody does --
             * and it is the last Control Center category the roadmap was
             * still missing.
             *
             * Driven through the REAL buttons: the scene asks the panel where
             * they are and clicks those pixels, so the hit test is on trial
             * alongside the arithmetic. Calling the step function directly
             * would skip exactly the part that goes dead unnoticed.
             */
            WmWindow *dw;
            CRect rc;
            CPoint org;
            int y0, m0, d0, k;

            app_control_open_cat(4);            /* Date & Time */
            sh_run_frame();
            dw = wm_focused();
            org = wm_client_origin(dw);
            y0 = app_control_dt_field(dw, APP_CC_DT_YEAR);
            m0 = app_control_dt_field(dw, APP_CC_DT_MONTH);
            d0 = app_control_dt_field(dw, APP_CC_DT_DAY);
            SYS_LOGI("main", "DATETIME-DEMO: opened on %04d-%02d-%02d",
                     y0, m0, d0);
            SYS_LOGI("main", "DATETIME-DEMO: the panel starts from the "
                     "machine's own clock (%s)",
                     (y0 >= 1980 && y0 <= 2099 && m0 >= 1 && m0 <= 12 &&
                      d0 >= 1 && d0 <= 31) ? "OK" : "MISMATCH");

            /* A click on the real plus button moves the real field. */
            if (app_control_dt_rect(dw, APP_CC_DT_HOUR, 1, &rc)) {
                int h0 = app_control_dt_field(dw, APP_CC_DT_HOUR);
                feed_click(org.x + (rc.x0 + rc.x1) / 2,
                           org.y + (rc.y0 + rc.y1) / 2);
                SYS_LOGI("main", "DATETIME-DEMO: clicking + on Hour steps it "
                         "(%d -> %d) (%s)", h0,
                         app_control_dt_field(dw, APP_CC_DT_HOUR),
                         (app_control_dt_field(dw, APP_CC_DT_HOUR)
                          == (h0 + 1) % 24) ? "OK" : "MISMATCH");
            }

            /*
             * A button has to BE where the hit test says it is.
             *
             * Asking the panel for the rectangle and clicking it proves the
             * arithmetic and nothing about the drawing: painter and hit test
             * share cc_dt_minus(), so moving that moves both and the click
             * still lands. The failure that survives is a painter that draws
             * somewhere else -- a control that looks right and is dead, or
             * worse, live where nothing is drawn. Only the pixels can tell,
             * so the rectangle is checked to contain something.
             */
            if (app_control_dt_rect(dw, APP_CC_DT_MIN, 0, &rc)) {
                GfxSurface *cs2 = plat_backbuffer();
                CColor mid = gfx_get_pixel(cs2, org.x + (rc.x0 + rc.x1) / 2,
                                           org.y + (rc.y0 + rc.y1) / 2)
                             & 0xFFFFFFUL;
                CColor edge = gfx_get_pixel(cs2, org.x + rc.x0,
                                            org.y + rc.y0) & 0xFFFFFFUL;
                /* A bevelled button is not one flat colour; empty panel is. */
                SYS_LOGI("main", "DATETIME-DEMO: there is a button drawn where "
                         "the minus button is clicked (%s)",
                         (mid != edge) ? "OK" : "MISMATCH");
            }

            /* And the minus buttons themselves, which a scene that only ever
             * clicks + cannot speak for. */
            if (app_control_dt_rect(dw, APP_CC_DT_HOUR, 0, &rc)) {
                int h0 = app_control_dt_field(dw, APP_CC_DT_HOUR);
                feed_click(org.x + (rc.x0 + rc.x1) / 2,
                           org.y + (rc.y0 + rc.y1) / 2);
                SYS_LOGI("main", "DATETIME-DEMO: clicking - on Hour steps it "
                         "back (%d -> %d) (%s)", h0,
                         app_control_dt_field(dw, APP_CC_DT_HOUR),
                         (app_control_dt_field(dw, APP_CC_DT_HOUR)
                          == (h0 + 23) % 24) ? "OK" : "MISMATCH");
            }

            /*
             * The case the shared calendar exists for: stand on a 31st, then
             * move to a month that has no 31st. The day must come back to a
             * real one rather than staying at 31 or emptying.
             */
            {
                int guard;
                /* Walk the day up to 31 through the real button. */
                app_control_dt_rect(dw, APP_CC_DT_DAY, 1, &rc);
                for (guard = 0; guard < 40 &&
                     app_control_dt_field(dw, APP_CC_DT_DAY) != 31; guard++) {
                    feed_click(org.x + (rc.x0 + rc.x1) / 2,
                               org.y + (rc.y0 + rc.y1) / 2);
                }
                /* January, so 31 is reachable. */
                app_control_dt_rect(dw, APP_CC_DT_MONTH, 1, &rc);
                for (guard = 0; guard < 20 &&
                     app_control_dt_field(dw, APP_CC_DT_MONTH) != 1; guard++) {
                    feed_click(org.x + (rc.x0 + rc.x1) / 2,
                               org.y + (rc.y0 + rc.y1) / 2);
                }
                app_control_dt_rect(dw, APP_CC_DT_DAY, 1, &rc);
                for (guard = 0; guard < 40 &&
                     app_control_dt_field(dw, APP_CC_DT_DAY) != 31; guard++) {
                    feed_click(org.x + (rc.x0 + rc.x1) / 2,
                               org.y + (rc.y0 + rc.y1) / 2);
                }
                SYS_LOGI("main", "DATETIME-DEMO: 31 January is reachable (%s)",
                         (app_control_dt_field(dw, APP_CC_DT_MONTH) == 1 &&
                          app_control_dt_field(dw, APP_CC_DT_DAY) == 31)
                             ? "OK" : "MISMATCH");
                /* Now February. */
                app_control_dt_rect(dw, APP_CC_DT_MONTH, 1, &rc);
                feed_click(org.x + (rc.x0 + rc.x1) / 2,
                           org.y + (rc.y0 + rc.y1) / 2);
                k = app_control_dt_field(dw, APP_CC_DT_DAY);
                SYS_LOGI("main", "DATETIME-DEMO: moving to February pulls the "
                         "day back to %d (%s)", k,
                         (app_control_dt_field(dw, APP_CC_DT_MONTH) == 2 &&
                          (k == 28 || k == 29)) ? "OK" : "MISMATCH");
            }

            /*
             * And the honesty check. This host will not let a program set its
             * clock, so the panel must SAY that -- a no-op reporting "Clock
             * set." would look identical here and be a lie on the machine
             * that matters.
             */
            if (app_control_dt_rect(dw, 0, 2, &rc)) {
                const char *st;
                feed_click(org.x + (rc.x0 + rc.x1) / 2,
                           org.y + (rc.y0 + rc.y1) / 2);
                st = app_control_dt_status(dw);
                SYS_LOGI("main", "DATETIME-DEMO: Set says '%s'",
                         st ? st : "(none)");
                SYS_LOGI("main", "DATETIME-DEMO: it does not claim to have set "
                         "a clock it cannot set (%s)",
                         (st != NULL && strstr(st, "will not let") != NULL)
                             ? "OK" : "MISMATCH");
            }
            feed_key(PLAT_KEY_CLOSE);
        }
        if (opt->hex_demo) {
#define HX_DEMO_SIZE 8192
            /*
             * The Hex Viewer, and the claim that makes it worth having: it
             * reads the rows on the screen and nothing else.
             *
             * That is not something "a window opened" can show. So the test
             * file is built so that the byte at any offset is a function of
             * that offset -- scroll anywhere and the bytes on screen say
             * where they came from. A viewer that loaded the first screenful
             * and then only moved its address column would pass every check
             * about offsets and fail every one of these.
             */
            const char *hhome = sys_home();
            char hpath[CASTALIA_MAX_PATH];
            unsigned char *blob;
            WmWindow *hw;
            long off0, off1, off2, off3;
            int have0, k, bytes_ok = 1;

            sys_snprintf(hpath, sizeof(hpath), "%s/HEXTEST.BIN", hhome);
            blob = (unsigned char *)sys_alloc((cu32)HX_DEMO_SIZE);
            if (blob != NULL) {
                int j;
                /* byte[i] = i * 7 + 3, mod 256: every offset has its own
                 * value, and the pattern does not repeat inside a row. */
                for (j = 0; j < HX_DEMO_SIZE; j++) {
                    blob[j] = (unsigned char)((j * 7 + 3) & 0xFF);
                }
                cz_spit_public(hpath, blob, (cu32)HX_DEMO_SIZE);
                sys_free(blob, (cu32)HX_DEMO_SIZE);
            }

            app_hex_open(hpath);
            sh_run_frame();
            hw = wm_focused();
            off0 = app_hex_offset(hw);
            have0 = app_hex_have(hw);
            SYS_LOGI("main", "HEX-DEMO: opened '%s', offset %ld, holding %d "
                     "byte(s)",
                     (hw != NULL && wm_title(hw) != NULL) ? wm_title(hw) : "?",
                     off0, have0);
            SYS_LOGI("main", "HEX-DEMO: it is a Hex Viewer, at the start (%s)",
                     (hw != NULL && wm_title(hw) != NULL &&
                      strcmp(wm_title(hw), "Hex Viewer") == 0 &&
                      off0 == 0L && have0 > 0) ? "OK" : "MISMATCH");

            /* An 8 KB file, and the window is holding a screenful. This is
             * the memory claim: what it holds is a function of the window,
             * not of the file. */
            /* Exactly one screenful, not merely less than the file: a
             * viewer that read twice what it draws would satisfy "less than
             * the file" for anything big and still grow with the window. */
            SYS_LOGI("main", "HEX-DEMO: an %d-byte file costs exactly the %d "
                     "rows on screen (%d bytes held) (%s)", HX_DEMO_SIZE,
                     app_hex_visible(hw), have0,
                     (app_hex_visible(hw) > 0 &&
                      have0 == app_hex_visible(hw) * 16) ? "OK" : "MISMATCH");

            for (k = 0; k < have0; k++) {
                if (app_hex_byte(hw, k) != (int)((k * 7 + 3) & 0xFF)) {
                    bytes_ok = 0;
                }
            }
            SYS_LOGI("main", "HEX-DEMO: the bytes on screen are the file's "
                     "first %d (%s)", have0, bytes_ok ? "OK" : "MISMATCH");

            feed_key(PLAT_KEY_PGDN);
            off1 = app_hex_offset(hw);
            bytes_ok = 1;
            for (k = 0; k < app_hex_have(hw); k++) {
                long fo = off1 + k;
                if (app_hex_byte(hw, k) != (int)((fo * 7 + 3) & 0xFF)) {
                    bytes_ok = 0;
                }
            }
            SYS_LOGI("main", "HEX-DEMO: a page down seeks -- offset %ld, and "
                     "the bytes there are the file's bytes at %ld (%s)",
                     off1, off1,
                     (off1 > 0L && bytes_ok) ? "OK" : "MISMATCH");

            feed_key(PLAT_KEY_END);
            off2 = app_hex_offset(hw);
            bytes_ok = (app_hex_have(hw) > 0);
            for (k = 0; k < app_hex_have(hw); k++) {
                long fo = off2 + k;
                if (app_hex_byte(hw, k) != (int)((fo * 7 + 3) & 0xFF)) {
                    bytes_ok = 0;
                }
            }
            /* End lands the file's last row on the bottom of the window, so
             * the view still ends exactly at the end of the file and there is
             * no blank page below it. */
            SYS_LOGI("main", "HEX-DEMO: End shows the end of the file without "
                     "running past it (offset %ld + %d = %d) (%s)",
                     off2, app_hex_have(hw),
                     (int)(off2 + app_hex_have(hw)),
                     (bytes_ok && off2 + app_hex_have(hw) == HX_DEMO_SIZE)
                         ? "OK" : "MISMATCH");
            SYS_LOGI("main", "HEX-DEMO: and it cannot be pushed past the end "
                     "(%s)",
                     (feed_key(PLAT_KEY_PGDN), app_hex_offset(hw) == off2)
                         ? "OK" : "MISMATCH");

            feed_key(PLAT_KEY_HOME);
            off3 = app_hex_offset(hw);
            SYS_LOGI("main", "HEX-DEMO: Home comes back to the start (%s)",
                     (off3 == 0L) ? "OK" : "MISMATCH");
            feed_key(PLAT_KEY_CLOSE);

            /*
             * The same thing again, the way a person without a mouse has to
             * do it: File Manager, arrow to the file, F3 for its menu, arrows
             * to View Bytes, Enter.
             *
             * Until this scene was written that walk was impossible -- the
             * context menu had no keyboard opener and swallowed every key but
             * Esc once it was up, so View Bytes, Properties, Compress and
             * Back Up were mouse-only on a system whose own hardware checks
             * run with no mouse driver loaded.
             */
            {
                char hdir[CASTALIA_MAX_PATH];
                WmWindow *fw;
                int step;

                sys_snprintf(hdir, sizeof(hdir), "%s/HEXDIR", hhome);
                plat_mkdir(hdir);
                sys_snprintf(hpath, sizeof(hpath), "%s/ONE.BIN", hdir);
                cz_spit_public(hpath, "MZ\220\000abcdefgh", 12u);

                app_fileman_open_path(hdir);
                sh_run_frame();
                /* The status bar's two cells must not say the same thing. The
                 * left one counts the entries; this one used to say "%d
                 * items", which was the identical fact in different words and
                 * wasted the only line the File Manager has for telling
                 * somebody something. It now says how much is here -- the
                 * number that answers "will this fit on a floppy". */
                {
                    const char *st = app_fileman_status(wm_focused());
                    SYS_LOGI("main", "HEX-DEMO: the status bar says '%s' "
                             "rather than repeating the count (%s)",
                             st ? st : "(none)",
                             (st != NULL && strstr(st, "byte") != NULL &&
                              strstr(st, "item") == NULL) ? "OK" : "MISMATCH");
                }
                feed_key(PLAT_KEY_END);          /* the only file in there */
                feed_key(PLAT_KEY_F10);          /* its menu               */
                fw = wm_focused();
                SYS_LOGI("main", "HEX-DEMO: F3 opens the item's menu with a "
                         "landed highlight (%s)",
                         (app_fileman_ctx_item(fw) != NULL) ? "OK"
                                                            : "MISMATCH");
                /* Walk down to View Bytes by name rather than by counting, so
                 * this does not break every time the menu gains an entry --
                 * and stop, so a menu that never reaches it fails here. */
                for (step = 0; step < 12; step++) {
                    const char *lab = app_fileman_ctx_item(fw);
                    if (lab != NULL && strcmp(lab, "View Bytes") == 0) { break; }
                    feed_key(PLAT_KEY_DOWN);
                }
                SYS_LOGI("main", "HEX-DEMO: the arrows reach View Bytes (%s)",
                         (app_fileman_ctx_item(fw) != NULL &&
                          strcmp(app_fileman_ctx_item(fw), "View Bytes") == 0)
                             ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ENTER);
                hw = wm_focused();
                SYS_LOGI("main", "HEX-DEMO: Enter opens the Hex Viewer on it, "
                         "with no mouse anywhere in this walk (%s)",
                         (hw != NULL && wm_title(hw) != NULL &&
                          strcmp(wm_title(hw), "Hex Viewer") == 0 &&
                          app_hex_have(hw) == 12) ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_CLOSE);
            }
        }
        if (opt->cursor_demo) {
            /*
             * The pointer, which nothing had ever checked.
             *
             * Making sh_cursor_draw() a no-op broke no unit check, no demo
             * scene and no abuse world -- it was the last blind spot on the
             * mutation map, recorded rather than covered on the grounds that
             * a missing cursor is visible within a second. That is true of a
             * MISSING cursor. It is not true of the failure this code can
             * actually have: the save-under is what keeps the arrow from
             * smearing a trail across the desktop, and a trail looks like a
             * compositor bug, a wallpaper bug, or a dirty-rect bug long
             * before anyone suspects the pointer.
             *
             * So the checks are about what the arrow leaves behind as much as
             * what it puts down. Read straight off the back buffer after the
             * frame that drew it, since that is the surface the compositor
             * just presented.
             *
             * What this CANNOT check, said plainly: whether the cursor's new
             * rectangle reaches the physical framebuffer. That is
             * plat_present(), and on a headless host the back buffer IS the
             * framebuffer, so dropping newc from the present list changes
             * nothing here and everything on real hardware. Verified: that
             * mutation passes every check below. Nothing on a host can
             * catch it.
             */
            GfxSurface *cs = plat_backbuffer();
            PlatVideoInfo cvi;
            /* Offsets into the 12x17 arrow: two cells it must ink, and two it
             * must not. The transparent pair is the check that matters -- a
             * cursor that painted a solid block would satisfy "the pixel
             * changed" perfectly, and would not be an arrow.
             *
             * The last two are the drop shadow, which is most of what the
             * cursor costs and has a precomputed table behind it. Row 19 has
             * no arrow in it at all, so both are shadow and nothing else:
             * (9,19) takes the full step from cell (6,16) plus a soft one
             * from (7,16), and (8,19) takes a single soft step. One must come
             * out darker than the other, which is what makes it a falloff and
             * not a rectangle. */
            const int ox[6] = { 0, 2, 5, 11, 9, 8 };
            const int oy[6] = { 0, 4, 0,  0, 19, 19 };
            CColor bg[6], on[6], back[6];
            int ax, ay, bx, by, k;
            int inked = 0, clear = 0, restored = 0, shaded = 0, falloff = 0;

            plat_video_info(&cvi);
            ax = cvi.width - 60;  ay = cvi.height - 120;   /* parked, far off */
            bx = cvi.width / 2;   by = cvi.height / 3;     /* clear desktop   */

            feed_move(ax, ay);
            for (k = 0; k < 6; k++) {
                bg[k] = gfx_get_pixel(cs, bx + ox[k], by + oy[k]) & 0xFFFFFFUL;
            }
            feed_move(bx, by);
            for (k = 0; k < 6; k++) {
                on[k] = gfx_get_pixel(cs, bx + ox[k], by + oy[k]) & 0xFFFFFFUL;
            }
            /* Frames with no input at all. The shell stops repainting the
             * cursor's rectangle once the pointer holds still -- that is what
             * takes an idle frame from 37,380 instructions to 3,408 -- so the
             * arrow now has to SURVIVE those frames rather than be redrawn by
             * them. A cursor that vanished after a second of not moving would
             * be the obvious way to get this wrong. */
            {
                int f;
                CColor still[6];
                int held = 1;
                for (f = 0; f < 8; f++) { sh_run_frame(); }
                for (k = 0; k < 6; k++) {
                    still[k] = gfx_get_pixel(cs, bx + ox[k],
                                             by + oy[k]) & 0xFFFFFFUL;
                    if (still[k] != on[k]) { held = 0; }
                }
                SYS_LOGI("main", "CURSOR-DEMO: it is still there after eight "
                         "frames of nobody touching the mouse (%s)",
                         held ? "OK" : "MISMATCH");
            }
            feed_move(ax, ay);
            for (k = 0; k < 6; k++) {
                back[k] = gfx_get_pixel(cs, bx + ox[k], by + oy[k]) & 0xFFFFFFUL;
            }

            /* The two inked cells: the arrow's outline and its light fill,
             * which must differ from each other as well as from the desktop --
             * an arrow drawn in one flat colour has no edge on a pale
             * wallpaper. */
            inked = (on[0] != bg[0] && on[1] != bg[1] &&
                     colour_gap(on[0], on[1]) > 200);
            SYS_LOGI("main", "CURSOR-DEMO: the arrow inks its outline and its "
                     "fill, and they are different colours (gap %d) (%s)",
                     colour_gap(on[0], on[1]), inked ? "OK" : "MISMATCH");

            clear = (on[2] == bg[2] && on[3] == bg[3]);
            SYS_LOGI("main", "CURSOR-DEMO: the cells outside the arrow are "
                     "left alone (%s)", clear ? "OK" : "MISMATCH");

            /* The shadow DARKENS what is under it: every channel must come
             * down and none may go up. A table that painted a flat colour
             * would still "differ from the background" and would look like a
             * grey smudge nailed to the pointer. */
            shaded = (on[4] != bg[4] &&
                      GFX_R(on[4]) <= GFX_R(bg[4]) &&
                      GFX_G(on[4]) <= GFX_G(bg[4]) &&
                      GFX_B(on[4]) <= GFX_B(bg[4]));
            SYS_LOGI("main", "CURSOR-DEMO: the drop shadow darkens the desktop "
                     "rather than painting over it (%s)",
                     shaded ? "OK" : "MISMATCH");

            /* Both shadow pixels sit on the same desktop pixel value here, so
             * comparing them compares the two darkening strengths and nothing
             * else. A table that gave every shadow pixel the same slot would
             * pass every other check on this scene. */
            falloff = (GFX_R(on[4]) < GFX_R(on[5]) &&
                       GFX_G(on[4]) < GFX_G(on[5]) &&
                       GFX_B(on[4]) < GFX_B(on[5]) &&
                       on[5] != bg[5]);
            SYS_LOGI("main", "CURSOR-DEMO: the shadow falls off -- its core is "
                     "darker than its edge, and the edge is still shadow (%s)",
                     falloff ? "OK" : "MISMATCH");

            for (k = 0, restored = 1; k < 6; k++) {
                if (back[k] != bg[k]) { restored = 0; }
            }
            SYS_LOGI("main", "CURSOR-DEMO: moving away restores every pixel it "
                     "covered, exactly (%s)", restored ? "OK" : "MISMATCH");

            /* And the desktop it was standing on is not blank -- if it were,
             * "restored" would be true of a cursor that never drew and a
             * compositor that never painted. */
            SYS_LOGI("main", "CURSOR-DEMO: it was standing on a painted "
                     "desktop (%s)",
                     (bg[0] != 0UL || bg[1] != 0UL || bg[2] != 0UL)
                         ? "OK" : "MISMATCH");

            /*
             * Something repaints UNDER a pointer that is holding still.
             *
             * This is the case the skip has to get right and the only one it
             * can get wrong: the shell stops repainting the cursor's own
             * rectangle when the mouse is idle, so anything else that
             * composites over that area would paint the arrow out and leave
             * it out. Every check above passes with the redraw removed
             * entirely, because nothing in them ever repaints under a still
             * cursor. This drives it directly -- park the pointer on a
             * window, invalidate the window, take one frame with no input.
             */
            {
                WmWindow *cw;
                CRect cr;
                int wx, wy;
                CColor over = 0UL, after = 0UL;

                app_about_open();
                sh_run_frame();
                cw = wm_focused();
                cr = wm_client_rect(cw);
                wx = cr.x0 + 30;
                wy = cr.y0 + 30;
                feed_move(wx, wy);
                over = gfx_get_pixel(cs, wx, wy) & 0xFFFFFFUL;
                wm_invalidate(cw, NULL);
                sh_run_frame();               /* no mouse input at all */
                after = gfx_get_pixel(cs, wx, wy) & 0xFFFFFFUL;
                SYS_LOGI("main", "CURSOR-DEMO: a window repainting under a "
                         "still pointer does not paint it out (%s)",
                         (cw != NULL && over == after &&
                          over == (CColor)GFX_RGB(20, 22, 28))
                             ? "OK" : "MISMATCH");

                /*
                 * ...and the other half of that skip, which is the half that
                 * was wrong: something repainting somewhere ELSE must not
                 * touch the pointer at all.
                 *
                 * The shadow DARKENS what it lands on rather than covering
                 * it, so drawing the cursor twice over the same pixels is
                 * not the same as drawing it once. The old rule redrew it
                 * whenever anything anywhere had been repainted, and since
                 * nothing erases a still cursor, its shadow was darkened
                 * again on every frame that did any work at all -- a black
                 * halo growing around the arrow of anybody who typed with
                 * their hand off the mouse. Fifteen repaints of a strip
                 * nowhere near it: the shadow pixel must read exactly what
                 * it read before.
                 */
                feed_key(PLAT_KEY_CLOSE);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
                {
                    /*
                     * NOTEPAD, not the About box, and that matters: the
                     * About banner animates its sheen every frame, so the
                     * cursor's own rectangle is being repainted underneath
                     * it anyway and the shadow lands on fresh pixels each
                     * time. The bug only shows over a window that is
                     * holding still. A first version of this check ran on
                     * the About box and could not fail.
                     *
                     * The shadow CORE, at the offset the falloff check above
                     * uses. A second version sampled (+2,+14), inside the
                     * ARROW -- opaque, painted rather than blended, and so
                     * idempotent however many times it is drawn.
                     */
                    WmWindow *sw;
                    CRect scr, strip;
                    CColor sh0, sh1;
                    int px2, py2, sx, sy, r2;

                    app_notepad_open();
                    for (r2 = 0; r2 < 20; r2++) { sh_run_frame(); }
                    sw = wm_focused();
                    scr = wm_client_rect(sw);
                    px2 = (scr.x0 + scr.x1) / 2;
                    py2 = (scr.y0 + scr.y1) / 2;
                    feed_move(px2, py2);
                    sx = px2 + 9; sy = py2 + 19;
                    sh0 = gfx_get_pixel(cs, sx, sy) & 0xFFFFFFUL;
                    /* A strip in the far corner of the same window: work for
                     * the compositor to do, nowhere near the pointer. */
                    strip = crect_make(scr.x0 + 4, scr.y0 + 4, 40, 14);
                    for (r2 = 0; r2 < 15; r2++) {
                        wm_invalidate(sw, &strip);
                        sh_run_frame();          /* no mouse input at all */
                    }
                    sh1 = gfx_get_pixel(cs, sx, sy) & 0xFFFFFFUL;
                    SYS_LOGI("main", "CURSOR-DEMO: fifteen repaints elsewhere "
                             "leave the shadow exactly as dark as it was "
                             "(%06lX -> %06lX) (%s)", (unsigned long)sh0,
                             (unsigned long)sh1,
                             (sh0 == sh1) ? "OK" : "MISMATCH");
                    feed_key(PLAT_KEY_CLOSE);
                }
            }
        }
        if (opt->sysinfo_demo) {
            /*
             * The machine report, and getting it off the machine.
             *
             * The report exists for a boot that went wrong on hardware nobody
             * here has seen, and on that machine the video mode is small --
             * the DOS backend falls back to 640x480 or worse precisely when
             * something is not working. So the window shows what fits and F2
             * writes ALL of it, and the check that matters is that those two
             * numbers DIFFER and the file has the larger one. A save that
             * wrote only the visible lines would satisfy "a file appeared".
             *
             * Run this scene with a short --height to force the gap; at the
             * default size everything fits and there is nothing to prove.
             */
            const char *shome = sys_home();
            char spath[CASTALIA_MAX_PATH];
            WmWindow *sw;
            int held = 0, shown = 0;

            sys_snprintf(spath, sizeof(spath), "%s/CASTINFO.TXT", shome);
            plat_file_remove(spath);
            /* The file must not exist yet, or its presence afterwards proves
             * nothing about F2. */
            SYS_LOGI("main", "SYSINFO-DEMO: no report on disk to start (%s)",
                     (!plat_file_exists(spath)) ? "OK" : "MISMATCH");

            app_sysinfo_open();
            sh_run_frame();
            sw = wm_focused();
            held  = app_info_lines_held(sw);
            shown = app_info_lines_shown(sw);
            SYS_LOGI("main", "SYSINFO-DEMO: opened '%s', holding %d line(s), "
                     "drawing %d",
                     (sw != NULL && wm_title(sw) != NULL) ? wm_title(sw) : "?",
                     held, shown);
            SYS_LOGI("main", "SYSINFO-DEMO: it is a System Information window "
                     "(%s)",
                     (sw != NULL && wm_title(sw) != NULL &&
                      strcmp(wm_title(sw), "System Information") == 0 &&
                      held > 0) ? "OK" : "MISMATCH");
            /* The whole point: this screen cannot show the whole report. */
            SYS_LOGI("main", "SYSINFO-DEMO: the screen is too short for all of "
                     "it (%d shown of %d) (%s)", shown, held,
                     (held > shown && shown > 0) ? "OK" : "MISMATCH");

            feed_key(PLAT_KEY_F2);
            SYS_LOGI("main", "SYSINFO-DEMO: F2 wrote the report (%s)",
                     (plat_file_exists(spath) && plat_file_size(spath) > 0L)
                         ? "OK" : "MISMATCH");
            {
                /* Count the lines in the file and require the number the
                 * window HELD, not the number it drew. */
                unsigned char *blob = NULL;
                cu32 nbytes = 0, k;
                int flines = 0;
                int saw_last = 0, saw_env = 0;
                /* Byte offsets, not flags: the video-mode list is supposed to
                 * be sorted, and only the ORDER two rows appear in can show
                 * that. Negative means not found at all. */
                long at_small = -1, at_big = -1, at_head = -1;
                if (cz_slurp_public(spath, &blob, &nbytes) == CE_OK &&
                    blob != NULL) {
                    for (k = 0; k < nbytes; k++) {
                        if (blob[k] == '\n') { flines++; }
                    }
                    for (k = 0; k + 13u <= nbytes; k++) {
                        /* A line the window holds and never drew, so finding
                         * it is direct evidence that the save did not stop
                         * where the screen did. */
                        if (memcmp(blob + k, "Castalia heap", 13) == 0) {
                            saw_last = 1;
                        }
                        /* The variable snd_blaster.c parses to find the sound
                         * card. It appears nowhere else in the report, so
                         * finding it means the environment section is real. */
                        if (memcmp(blob + k, "BLASTER", 7) == 0) {
                            saw_env = 1;
                        }
                    }
                    /* The video-mode list: it is there, it reaches above the
                     * mode in use, and it is in ladder order. */
                    for (k = 0; k + 13u <= nbytes; k++) {
                        if (at_head < 0 && memcmp(blob + k, "Video modes (", 13) == 0) {
                            at_head = (long)k;
                        }
                        if (at_small < 0 && memcmp(blob + k, "640x480x8", 9) == 0) {
                            at_small = (long)k;
                        }
                        if (at_big < 0 && memcmp(blob + k, "1024x768x16", 11) == 0) {
                            at_big = (long)k;
                        }
                    }
                    sys_free(blob, nbytes);
                }
                SYS_LOGI("main", "SYSINFO-DEMO: the file has %d line(s) for "
                         "%d held (%s)", flines, held,
                         (flines == held) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "SYSINFO-DEMO: it contains a line the window "
                         "never drew (%s)", saw_last ? "OK" : "MISMATCH");
                SYS_LOGI("main", "SYSINFO-DEMO: it lists the environment this "
                         "system depends on (%s)",
                         saw_env ? "OK" : "MISMATCH");
                /* The video-mode list is the part of the report that explains
                 * the rest of it: "640x480, 8 bpp" means something different
                 * on a card that offers 1024x768 than on one that does not,
                 * and until this section existed the report could not tell
                 * those two machines apart. */
                SYS_LOGI("main", "SYSINFO-DEMO: it names who is claiming the "
                         "mode list (%s)",
                         (at_head >= 0) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "SYSINFO-DEMO: the list reaches above the "
                         "mode in use (%s)",
                         (at_big >= 0) ? "OK" : "MISMATCH");
                /* Sorted, not just present. The raw list arrives in whatever
                 * order the BIOS stored it, so this is the only check that
                 * the tidying ran at all. */
                SYS_LOGI("main", "SYSINFO-DEMO: the modes climb (640x480x8 at "
                         "%ld, 1024x768x16 at %ld) (%s)", at_small, at_big,
                         (at_small >= 0 && at_big > at_small &&
                          at_small > at_head) ? "OK" : "MISMATCH");
            }
            /* The dialog that confirms it, so a save that quietly failed
             * cannot look like a save that worked. */
            SYS_LOGI("main", "SYSINFO-DEMO: it said so on screen (%s)",
                     wm_has_modal() ? "OK" : "MISMATCH");
        }
        if (opt->archive_demo) {
#define AR_DEMO_FILES 40
            /*
             * Looking inside an archive without opening it.
             *
             * The property that makes the window worth having is that it reads
             * the DIRECTORY and nothing else -- so the checks are about what
             * it manages to say having read only the front of the file: the
             * right number of members, their real sizes, and how many of them
             * already exist where they would land. That last number is the
             * one the window exists for, because extracting replaces those
             * files without asking.
             *
             * Driven through the real File Manager open path, not by calling
             * the window directly: "double-clicking a .CAR no longer unpacks
             * it on the spot" is the behaviour that changed, and it lives in
             * app_fileman.c, not here.
             */
            const char *ahome = sys_home();
            char adir[CASTALIA_MAX_PATH], aout[CASTALIA_MAX_PATH];
            char apath[CASTALIA_MAX_PATH];
            char f1[CASTALIA_MAX_PATH];
            int packed = 0;
            long saved = 0, expect_original = 0;
            WmWindow *aw;

            sys_snprintf(adir, sizeof(adir), "%s/ARCSRC", ahome);
            sys_snprintf(aout, sizeof(aout), "%s/ARCOUT", ahome);
            plat_mkdir(adir);
            plat_mkdir(aout);
            /*
             * Forty members, not two: enough that the list scrolls, so the
             * scrollbar path is drawn at least once somewhere that would
             * notice it crashing. Each file is a different, known size and
             * deliberately compressible -- a stored size equal to the
             * original would not prove the window read the archive rather
             * than the files beside it.
             */
            for (packed = 0; packed < AR_DEMO_FILES; packed++) {
                char blob[512];
                int want = 100 + packed * 10, k;
                for (k = 0; k < want && k < (int)sizeof(blob); k++) {
                    blob[k] = (char)('A' + (k % 4));
                }
                sys_snprintf(f1, sizeof(f1), "%s/F%02d.TXT", adir, packed);
                cz_spit_public(f1, blob, (cu32)want);
                expect_original += (long)want;
            }
            packed = 0;
            sys_snprintf(apath, sizeof(apath), "%s/ARCTEST.CAR", aout);
            if (car_pack_dir(adir, apath, &packed, &saved) != CE_OK) {
                SYS_LOGI("main", "ARCHIVE-DEMO: could not build the test "
                         "archive (MISMATCH)");
            } else {
                SYS_LOGI("main", "ARCHIVE-DEMO: packed %d file(s), saved %ld "
                         "bytes", packed, saved);
            }

            /* Open it the way a person would: through the File Manager, with
             * the keyboard. The archive is alone in its folder, so End lands
             * on it whatever else the sort does with ".." above it. */
            app_fileman_open_path(aout);
            sh_run_frame();
            feed_key(PLAT_KEY_END);
            feed_key(PLAT_KEY_ENTER);
            aw = wm_focused();
            SYS_LOGI("main", "ARCHIVE-DEMO: opening it gave a '%s' window",
                     (aw != NULL && wm_title(aw) != NULL) ? wm_title(aw) : "?");
            SYS_LOGI("main", "ARCHIVE-DEMO: a .CAR opens the Archive window "
                     "rather than unpacking itself (%s)",
                     (aw != NULL && wm_title(aw) != NULL &&
                      strcmp(wm_title(aw), "Archive") == 0) ? "OK" : "MISMATCH");
            /* ...and it did NOT extract: the members must not be sitting in
             * the home folder now. */
            {
                /* A member name, not a name from an earlier version of this
                 * scene: probing for a file the archive does not contain is a
                 * check that cannot fail, and this one could not until the
                 * negative control said so. */
                char stray[CASTALIA_MAX_PATH];
                sys_snprintf(stray, sizeof(stray), "%s/F00.TXT", aout);
                SYS_LOGI("main", "ARCHIVE-DEMO: opening it wrote nothing (%s)",
                         (plat_file_size(stray) < 0) ? "OK" : "MISMATCH");
            }
            SYS_LOGI("main", "ARCHIVE-DEMO: it lists %d member(s) (%s)",
                     app_archive_count(aw),
                     (app_archive_count(aw) == packed &&
                      packed == AR_DEMO_FILES) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "ARCHIVE-DEMO: none of them exist beside the "
                     "archive yet (%s)",
                     (app_archive_clashes(aw) == 0) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "ARCHIVE-DEMO: it read the real sizes without "
                     "unpacking (%ld -> %ld, expected %ld original) (%s)",
                     app_archive_original(aw), app_archive_stored(aw),
                     expect_original,
                     (app_archive_original(aw) == expect_original &&
                      app_archive_stored(aw) > 0L &&
                      app_archive_stored(aw) < app_archive_original(aw))
                         ? "OK" : "MISMATCH");

            /*
             * Extracting, and the question that comes with it.
             *
             * Members are written with "wb", so a second extract replaces what
             * the first one wrote. The rule is that the question is asked when
             * there is something to lose and not otherwise, so this drives it
             * in both states: an empty folder must extract straight away, and
             * a full one must stop and ask.
             */
            {
                char member[CASTALIA_MAX_PATH];
                sys_snprintf(member, sizeof(member), "%s/F00.TXT", aout);
                feed_key(PLAT_KEY_ENTER);          /* Extract All */
                SYS_LOGI("main", "ARCHIVE-DEMO: with nothing in the way it "
                         "extracted without asking (%s)",
                         (!wm_has_modal() && plat_file_exists(member))
                             ? "OK" : "MISMATCH");
                SYS_LOGI("main", "ARCHIVE-DEMO: ...and now says all %d would "
                         "be replaced (%s)", app_archive_clashes(aw),
                         (app_archive_clashes(aw) == AR_DEMO_FILES)
                             ? "OK" : "MISMATCH");

                /*
                 * Now put something of our own where a member would land, so
                 * that answering the question has a visible consequence.
                 * "The file still exists" cannot tell a cancel from a
                 * re-extract -- both leave a file there -- and the first
                 * version of this check could not fail for exactly that
                 * reason. The sentinel is a different LENGTH from the member
                 * it stands in for, so which of the two is on disk is a fact.
                 */
                cz_spit_public(member, "CANCELLED\n", 10u);

                feed_key(PLAT_KEY_ENTER);
                SYS_LOGI("main", "ARCHIVE-DEMO: with files in the way it asks "
                         "first (%s)", wm_has_modal() ? "OK" : "MISMATCH");
                feed_key(PLAT_KEY_ESC);            /* Esc is No */
                SYS_LOGI("main", "ARCHIVE-DEMO: cancelling left our file "
                         "alone (%ld bytes) (%s)",
                         plat_file_size(member),
                         (!wm_has_modal() && plat_file_size(member) == 10L)
                             ? "OK" : "MISMATCH");

                /* ...and saying yes really does replace it. */
                feed_key(PLAT_KEY_ENTER);
                feed_key(PLAT_KEY_ENTER);          /* Enter is Yes */
                SYS_LOGI("main", "ARCHIVE-DEMO: confirming replaced it "
                         "(%ld bytes) (%s)", plat_file_size(member),
                         (!wm_has_modal() && plat_file_size(member) == 100L)
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->deskcache_demo) {
            /*
             * The desktop background is baked into an offscreen surface once
             * and blitted from there. Baking it costs about 24 million
             * instructions -- most of the shell's startup -- and it was being
             * paid again on every theme apply, including the ones that cannot
             * change it: the factory landscape is drawn from hardcoded colours
             * and depends on the screen size and nothing else, so switching
             * from one theme to another produces a pixel-identical desktop
             * behind repainted window frames.
             *
             * Two things have to be true and they pull in opposite directions,
             * so both are checked here: it must SKIP when the picture would be
             * the same, and it must NOT skip when the picture would differ.
             * A cache that never rebuilds is fast and wrong.
             */
            GfxSurface *bb = plat_backbuffer();
            PlatVideoInfo vi3;
            CRect full3;
            long b0, b1, b2, b3;
            CColor *snap = NULL;
            int px, npx, diff = 0;

            plat_video_info(&vi3);
            full3 = crect_make(0, 0, vi3.width, vi3.height);
            npx = vi3.width * vi3.height;

            b0 = sh_desktop_cache_builds();
            sh_desktop_build_cache();            /* same inputs as at startup */
            b1 = sh_desktop_cache_builds();
            SYS_LOGI("main", "DESKCACHE-DEMO: builds %ld -> %ld on an "
                     "unchanged rebuild", b0, b1);
            SYS_LOGI("main", "DESKCACHE-DEMO: an unchanged rebuild does no "
                     "work (%s)", (b1 == b0) ? "OK" : "MISMATCH");

            /* Keep what the desktop looks like now, so the skip can be shown
             * to have kept the picture rather than merely kept the clock. */
            snap = (CColor *)malloc((size_t)npx * sizeof(CColor));
            sh_desktop_paint(&full3);
            if (snap != NULL) {
                for (px = 0; px < npx; px++) {
                    snap[px] = gfx_get_pixel(bb, px % vi3.width, px / vi3.width);
                }
            }

            /* A different theme. Window frames and controls change; the
             * landscape behind them cannot. */
            {
                ShTheme t2;
                sh_theme_preset(&t2, SETTINGS_THEME_FOREST);
                sh_theme_apply_live(&t2);
            }
            b2 = sh_desktop_cache_builds();
            SYS_LOGI("main", "DESKCACHE-DEMO: builds %ld -> %ld across a theme "
                     "change", b1, b2);
            SYS_LOGI("main", "DESKCACHE-DEMO: a theme that cannot change the "
                     "landscape does not rebake it (%s)",
                     (b2 == b1) ? "OK" : "MISMATCH");

            /*
             * ...and the landscape still draws the same pixels it did before.
             *
             * Only the landscape. The desktop ICONS are drawn live on top of
             * the cache, out of the theme's own control palette, and they are
             * supposed to change -- the first version of this check compared
             * the whole screen and reported 722 changed pixels, every one of
             * them a correctly recoloured icon bevel.
             *
             * So the screen is read in two zones, and they assert opposite
             * things: right of the icon column nothing may move, and inside
             * the icon column something MUST. Without that second half the
             * first would pass just as well if the theme had never been
             * applied at all.
             */
            sh_desktop_paint(&full3);
            if (snap != NULL) {
                int icon_diff = 0;
                int bottom = g_sh.taskbar_rect.y0;
                int xx, yy;
                for (yy = 0; yy < bottom; yy++) {
                    for (xx = 0; xx < vi3.width; xx++) {
                        if (gfx_get_pixel(bb, xx, yy) != snap[yy * vi3.width + xx]) {
                            if (xx >= 150) { diff++; } else { icon_diff++; }
                        }
                    }
                }
                SYS_LOGI("main", "DESKCACHE-DEMO: %d landscape pixels changed, "
                         "%d icon-column pixels changed", diff, icon_diff);
                SYS_LOGI("main", "DESKCACHE-DEMO: skipping kept the landscape "
                         "(%s)", (diff == 0) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "DESKCACHE-DEMO: ...and the theme really did "
                         "apply (%s)", (icon_diff > 50) ? "OK" : "MISMATCH");
                free(snap);
            }

            /* Now something that genuinely changes it: asking for the
             * 16-colour target takes the shell off the glossy path, and the
             * flat branch draws a different background entirely. If the guard
             * were simply "never rebuild", this is the check that catches it. */
            {
                ShTheme t3;
                sh_theme_preset(&t3, SETTINGS_THEME_FOREST);
                t3.colors = 16;
                sh_theme_apply_live(&t3);
            }
            b3 = sh_desktop_cache_builds();
            SYS_LOGI("main", "DESKCACHE-DEMO: builds %ld -> %ld when the "
                     "background really changes", b2, b3);
            SYS_LOGI("main", "DESKCACHE-DEMO: a real change still rebakes it "
                     "(%s)", (b3 > b2) ? "OK" : "MISMATCH");
        }
        if (opt->menulook_demo) {
            /*
             * The glossy menu treatment, checked by looking at the pixels it
             * produces -- because "it looks better" is not a thing a test can
             * hold, but "the gutter is a different colour from the body" and
             * "the selected row is a gradient, not a slab" are.
             *
             * The second half is the part that matters most: the SAME menu is
             * redrawn with the treatment switched off, and every property the
             * first half asserted must invert. That is the 16-colour and safe
             * mode contract -- those pipelines get the flat menu -- and it is
             * the half that would otherwise rot silently, since nobody looks
             * at safe mode until they need it.
             */
            GfxSurface *bb;
            WmWindow *w;
            CPoint o;
            int mw, mh, band0 = -1, band1 = -1, yy;
            int gut_d = 0, grad_d = 0, flat_gut = -1, flat_grad = -1;

            app_notepad_open();
            for (f = 0; f < 16; f++) { sh_run_frame(); }
            w = wm_focused();
            o = wm_client_origin(w);
            feed_key(PLAT_KEY_SYSMENU);
            feed_key(PLAT_KEY_DOWN);      /* select the first live item */
            bb = plat_backbuffer();
            ui_menu_measure(&g_sh.ctx_menu, GFX_FONT_SYSTEM, &mw, &mh);

            /* Find the selected band: rows whose middle pixel is much bluer
             * than it is red. Scanning for it rather than computing it keeps
             * the check free of the menu's private row metrics. */
            for (yy = o.y; yy < o.y + mh; yy++) {
                CColor c = gfx_get_pixel(bb, o.x + mw / 2, yy);
                int r = (int)((c >> 16) & 0xFF), b = (int)(c & 0xFF);
                if (b - r > 60) {
                    if (band0 < 0) { band0 = yy; }
                    band1 = yy;
                }
            }
            SYS_LOGI("main", "MENULOOK-DEMO: selected band rows %d..%d",
                     band0, band1);
            SYS_LOGI("main", "MENULOOK-DEMO: the selection is one row tall "
                     "(%s)",
                     (band0 > 0 && band1 - band0 >= 10 && band1 - band0 <= 20)
                         ? "OK" : "MISMATCH");

            /* Gutter vs body, on a row clear of the band. */
            yy = (band1 > 0 && band1 + 6 < o.y + mh) ? band1 + 6 : o.y + 6;
            {
                CColor g = gfx_get_pixel(bb, o.x + 8, yy);
                CColor b2 = gfx_get_pixel(bb, o.x + mw - 8, yy);
                gut_d = colour_gap(g, b2);
                SYS_LOGI("main", "MENULOOK-DEMO: gutter %06lX vs body %06lX, "
                         "distance %d", (unsigned long)g, (unsigned long)b2,
                         gut_d);
                SYS_LOGI("main", "MENULOOK-DEMO: the gutter is a different "
                         "colour from the body (%s)",
                         (gut_d >= 20) ? "OK" : "MISMATCH");
            }
            /* Top vs bottom of the band: a gradient, not a slab. */
            if (band0 > 0 && band1 > band0) {
                CColor t = gfx_get_pixel(bb, o.x + mw / 2, band0 + 1);
                CColor b3 = gfx_get_pixel(bb, o.x + mw / 2, band1 - 1);
                grad_d = colour_gap(t, b3);
                SYS_LOGI("main", "MENULOOK-DEMO: band top %06lX bottom %06lX, "
                         "distance %d", (unsigned long)t, (unsigned long)b3,
                         grad_d);
                SYS_LOGI("main", "MENULOOK-DEMO: the selection is a gradient, "
                         "not a slab (%s)",
                         (grad_d >= 20) ? "OK" : "MISMATCH");
            }

            /* ---- and now the same menu with the treatment switched off --- */
            ui_set_glossy(CFALSE);
            sh_invalidate_all();
            sh_run_frame();
            for (yy = o.y; yy < o.y + mh; yy++) {
                CColor c = gfx_get_pixel(bb, o.x + mw / 2, yy);
                int r = (int)((c >> 16) & 0xFF), b = (int)(c & 0xFF);
                if (b - r > 60) { if (flat_gut < 0) { flat_gut = yy; } flat_grad = yy; }
            }
            if (flat_gut > 0 && flat_grad > flat_gut) {
                CColor t = gfx_get_pixel(bb, o.x + mw / 2, flat_gut + 1);
                CColor b4 = gfx_get_pixel(bb, o.x + mw / 2, flat_grad - 1);
                CColor g = gfx_get_pixel(bb, o.x + 8, o.y + 6);
                CColor b5 = gfx_get_pixel(bb, o.x + mw - 8, o.y + 6);
                SYS_LOGI("main", "MENULOOK-DEMO: flat band %06lX/%06lX, flat "
                         "gutter %06lX vs body %06lX", (unsigned long)t,
                         (unsigned long)b4, (unsigned long)g,
                         (unsigned long)b5);
                SYS_LOGI("main", "MENULOOK-DEMO: with gloss off the selection "
                         "is one flat colour (%s)",
                         (colour_gap(t, b4) == 0) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "MENULOOK-DEMO: with gloss off the gutter is "
                         "the body (%s)",
                         (colour_gap(g, b5) == 0) ? "OK" : "MISMATCH");
            } else {
                SYS_LOGI("main", "MENULOOK-DEMO: the flat menu lost its "
                         "selection entirely (MISMATCH)");
            }
            ui_set_glossy(CTRUE);
        }
        if (opt->sysmenu_demo) {
            /*
             * The window menu, driven the way the person it exists for would
             * drive it: with no mouse at all. Before it, Alt+F4 was the only
             * thing the keyboard could tell a window to do -- it could not
             * minimize, maximize or restore one, and nothing anywhere could
             * move a window between the virtual desktops the system has had
             * since early on.
             *
             * The number of Down presses in each pick is itself an assertion.
             * The menu disables what does not apply rather than hiding it, so
             * "four Downs reaches Maximize" is only true while Restore is
             * disabled for a window that is already restored; if it were
             * wrongly enabled, four Downs would stop on Minimize and the check
             * below would fail on a maximized-that-is-not state. Each walk
             * says which items it expects to be dead.
             */
            WmWindow *w;
            CRect before, maxed, back, moved, sized;
            int wid, cx, cy, k;

            app_notepad_open();
            sh_run_frame();
            w   = wm_focused();
            wid = wm_window_id(w);
            before = wm_frame_rect(w);

            feed_key(PLAT_KEY_SYSMENU);
            SYS_LOGI("main", "SYSMENU-DEMO: Alt+Space opens the window menu "
                     "(%s)", sh_context_is_open() ? "OK" : "MISMATCH");

            /* ---- Move: one Down reaches it (Restore is dead) ------------- */
            feed_menu_pick(1);
            SYS_LOGI("main", "SYSMENU-DEMO: Move hands the arrows to the "
                     "window (%s)", sh_kmove_active() ? "OK" : "MISMATCH");
            for (k = 0; k < 3; k++) { feed_key(PLAT_KEY_RIGHT); }
            for (k = 0; k < 2; k++) { feed_key(PLAT_KEY_DOWN); }
            moved = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: moved to (%d,%d), was (%d,%d)",
                     moved.x0, moved.y0, before.x0, before.y0);
            SYS_LOGI("main", "SYSMENU-DEMO: each arrow moves one step, and "
                     "only moves (%s)",
                     (moved.x0 == before.x0 + 3 * SH_K_STEP &&
                      moved.y0 == before.y0 + 2 * SH_K_STEP &&
                      crect_w(&moved) == crect_w(&before) &&
                      crect_h(&moved) == crect_h(&before)) ? "OK" : "MISMATCH");
            /* Esc is the half of a modal gesture people actually rely on. */
            feed_key(PLAT_KEY_ESC);
            back = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: Esc puts it back where it started "
                     "(%s)",
                     (!sh_kmove_active() &&
                      back.x0 == before.x0 && back.y0 == before.y0 &&
                      back.x1 == before.x1 && back.y1 == before.y1)
                         ? "OK" : "MISMATCH");

            /* ...and Enter keeps the result rather than undoing it. */
            feed_key(PLAT_KEY_SYSMENU);
            feed_menu_pick(1);
            feed_key(PLAT_KEY_RIGHT);
            feed_key(PLAT_KEY_ENTER);
            moved = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: Enter keeps the move (%s)",
                     (!sh_kmove_active() &&
                      moved.x0 == before.x0 + SH_K_STEP) ? "OK" : "MISMATCH");

            /* ---- Size: two Downs, and the top-left corner must not move --- */
            feed_key(PLAT_KEY_SYSMENU);
            feed_menu_pick(2);
            SYS_LOGI("main", "SYSMENU-DEMO: Size hands the arrows to the "
                     "window (%s)", sh_kmove_active() ? "OK" : "MISMATCH");
            for (k = 0; k < 2; k++) { feed_key(PLAT_KEY_RIGHT); }
            feed_key(PLAT_KEY_DOWN);
            sized = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: sized to %dx%d at (%d,%d), was "
                     "%dx%d at (%d,%d)", crect_w(&sized), crect_h(&sized),
                     sized.x0, sized.y0, crect_w(&moved), crect_h(&moved),
                     moved.x0, moved.y0);
            SYS_LOGI("main", "SYSMENU-DEMO: sizing grows the far edges and "
                     "leaves the corner alone (%s)",
                     (crect_w(&sized) == crect_w(&moved) + 2 * SH_K_STEP &&
                      crect_h(&sized) == crect_h(&moved) + SH_K_STEP &&
                      sized.x0 == moved.x0 && sized.y0 == moved.y0)
                         ? "OK" : "MISMATCH");
            /* Hold Left long enough and it must stop at the minimum rather
             * than walking down to nothing. Sixty presses is far more than the
             * forty-three it takes to get there from here. */
            for (k = 0; k < 60; k++) { feed_key(PLAT_KEY_LEFT); }
            sized = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: shrinking stops at the minimum "
                     "(%d wide) (%s)", crect_w(&sized),
                     (crect_w(&sized) == WM_MIN_W) ? "OK" : "MISMATCH");
            feed_key(PLAT_KEY_ESC);

            /* Everything below starts from wherever Esc left it. */
            before = wm_frame_rect(w);

            /* ---- Maximize: four Downs (Restore is dead, Move and Size are
             * live, then Minimize, then Maximize). ------------------------- */
            feed_key(PLAT_KEY_SYSMENU);
            feed_menu_pick(4);
            maxed = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: maximized to %dx%d (was %dx%d)",
                     crect_w(&maxed), crect_h(&maxed),
                     crect_w(&before), crect_h(&before));
            SYS_LOGI("main", "SYSMENU-DEMO: Maximize fills the work area (%s)",
                     (wm_state(w) == WM_STATE_MAXIMIZED &&
                      crect_w(&maxed) > crect_w(&before) &&
                      crect_h(&maxed) > crect_h(&before)) ? "OK" : "MISMATCH");
            /* Checked here, at the first pick, rather than at the end: Alt+Space
             * toggles, so a later press would close a menu that wrongly stayed
             * up and the check would pass on the way past. */
            SYS_LOGI("main", "SYSMENU-DEMO: picking an item closes the menu "
                     "(%s)", !sh_context_is_open() ? "OK" : "MISMATCH");

            /* Now Restore is the first live item, and it has to put the window
             * back EXACTLY where it was. An off-by-a-border here looks right
             * on screen and is still wrong, so this compares corners, not
             * sizes. */
            feed_key(PLAT_KEY_SYSMENU);
            feed_menu_pick(1);
            back = wm_frame_rect(w);
            SYS_LOGI("main", "SYSMENU-DEMO: restored to (%d,%d)-(%d,%d), was "
                     "(%d,%d)-(%d,%d)", back.x0, back.y0, back.x1, back.y1,
                     before.x0, before.y0, before.x1, before.y1);
            SYS_LOGI("main", "SYSMENU-DEMO: Restore puts it back exactly where "
                     "it was (%s)",
                     (wm_state(w) == WM_STATE_NORMAL &&
                      back.x0 == before.x0 && back.y0 == before.y0 &&
                      back.x1 == before.x1 && back.y1 == before.y1)
                         ? "OK" : "MISMATCH");

            /* Five Downs: Move, Size, Minimize, Maximize, then the first
             * desktop that is not the one the window is on -- "Send to
             * Desktop 1" is listed and dead, so this lands on 2. */
            cx = (back.x0 + back.x1) / 2;
            cy = back.y0 + 4;
            feed_key(PLAT_KEY_SYSMENU);
            feed_menu_pick(5);
            SYS_LOGI("main", "SYSMENU-DEMO: sent to desktop %d, and %s still "
                     "on screen", wm_window_desktop(w) + 1,
                     (wm_hit_test(cx, cy) == w) ? "is" : "is not");
            SYS_LOGI("main", "SYSMENU-DEMO: Send to Desktop moves it off this "
                     "one (%s)",
                     (wm_window_desktop(w) == 1 && wm_hit_test(cx, cy) != w)
                         ? "OK" : "MISMATCH");
            sh_switch_desktop(1);
            sh_run_frame();
            SYS_LOGI("main", "SYSMENU-DEMO: ...and it is there when you follow "
                     "it (%s)", (wm_hit_test(cx, cy) == w) ? "OK" : "MISMATCH");

            /* The mouse route to the same menu: right-click the title bar,
             * clear of the caption buttons. Three Downs reach Minimize, past
             * Move and Size (Restore is dead again). */
            feed_rclick(back.x0 + 30, back.y0 + 4);
            SYS_LOGI("main", "SYSMENU-DEMO: a right-click on the title bar "
                     "opens it too (%s)",
                     sh_context_is_open() ? "OK" : "MISMATCH");
            feed_menu_pick(3);
            SYS_LOGI("main", "SYSMENU-DEMO: Minimize takes it off the screen "
                     "(%s)",
                     (!wm_is_visible(w) && wm_state(w) == WM_STATE_MINIMIZED)
                         ? "OK" : "MISMATCH");

            /* A minimized window has no title bar to right-click and does not
             * hold the focus, so its taskbar button is the only way back to
             * its menu -- which is the whole reason that route exists. */
            {
                CRect btn;
                if (sh_taskbar_button_rect(w, &btn)) {
                    feed_rclick((btn.x0 + btn.x1) / 2, (btn.y0 + btn.y1) / 2);
                    SYS_LOGI("main", "SYSMENU-DEMO: its taskbar button opens "
                             "the menu while it is down (%s)",
                             sh_context_is_open() ? "OK" : "MISMATCH");
                    /* Restore, then Maximize -- Move, Size and Minimize are
                     * all dead on a window that is already down. Picking
                     * Maximize rather than Restore is what
                     * makes this check stand on its own: a plain click on a
                     * task button also brings a window back, but it brings it
                     * back to its normal size, so only the menu can produce a
                     * window that is both visible and maximized. */
                    feed_menu_pick(2);
                    SYS_LOGI("main", "SYSMENU-DEMO: a minimized window comes "
                             "back straight to full screen (%s)",
                             (wm_is_visible(w) &&
                              wm_state(w) == WM_STATE_MAXIMIZED)
                                 ? "OK" : "MISMATCH");
                } else {
                    SYS_LOGI("main", "SYSMENU-DEMO: the window has no taskbar "
                             "button to right-click (MISMATCH)");
                }
            }

            /* Up from nothing highlighted wraps to the LAST live item, which
             * is Close wherever the desktop entries have got to. */
            feed_key(PLAT_KEY_SYSMENU);
            feed_key(PLAT_KEY_UP);
            feed_key(PLAT_KEY_ENTER);
            SYS_LOGI("main", "SYSMENU-DEMO: Close closes it (%s)",
                     (wm_window_by_id(wid) == NULL) ? "OK" : "MISMATCH");

            /* The hazard the whole design has to answer: a window can close
             * while its menu is up -- an app can destroy its own window on a
             * timer -- and a window id is a slot number, so a menu left
             * pointing at a closed window is a menu that will eventually be
             * pointing at whoever takes the slot. Here the window is closed
             * out from under an open menu on purpose. */
            sh_switch_desktop(0);
            sh_run_frame();
            {
                WmWindow *v = NULL;
                app_calc_open();
                sh_run_frame();
                v = wm_focused();
                feed_key(PLAT_KEY_SYSMENU);
                SYS_LOGI("main", "SYSMENU-DEMO: menu up over the calculator "
                         "(%s)", sh_context_is_open() ? "OK" : "MISMATCH");
                wm_destroy(v);
                sh_run_frame();
                SYS_LOGI("main", "SYSMENU-DEMO: closing the window takes its "
                         "menu with it (%s)",
                         !sh_context_is_open() ? "OK" : "MISMATCH");
            }
        }
        if (opt->mem_check) {
            /* The memory budget is the constraint the whole design answers to,
             * and it is the one number that can grow without anyone noticing:
             * every allocation is individually reasonable. The documented
             * figure had already drifted by half -- it said 1.9 MB, which was
             * one full-screen back buffer, and stopped being true when the
             * desktop cache added a second one.
             *
             * So the idle desktop is measured here, with a ceiling. Two
             * 800x600x32 surfaces are 3750 KB of it; the ceiling leaves room
             * for the shell's own allocations and nothing like room for a
             * third full-screen surface, which is exactly the mistake worth
             * catching.
             */
            cu32 live_kb, peak_kb;
            sh_run_frame();
            sh_run_frame();
            live_kb = (cu32)(sys_mem_live_bytes() / 1024u);
            peak_kb = (cu32)(sys_mem_peak_bytes() / 1024u);
            SYS_LOGI("main", "MEM-CHECK: idle desktop %lu KB live, %lu KB peak",
                     (unsigned long)live_kb, (unsigned long)peak_kb);
            SYS_LOGI("main", "MEM-CHECK: inside the %d KB idle ceiling (%s)",
                     CASTALIA_IDLE_KB_MAX,
                     (live_kb <= (cu32)CASTALIA_IDLE_KB_MAX) ? "OK"
                                                             : "MISMATCH");
            SYS_LOGI("main", "MEM-CHECK: inside the %d KB hard budget (%s)",
                     CASTALIA_BUDGET_KB,
                     (peak_kb <= (cu32)CASTALIA_BUDGET_KB) ? "OK" : "MISMATCH");

            /*
             * ...and that opening a window and closing it again gives the
             * memory back.
             *
             * The ceiling above is measured on an IDLE desktop, which says
             * nothing about what a session costs. On a budget this small a
             * window that keeps a few kilobytes per open is fatal after
             * enough use, and it is invisible in every other check here: the
             * app works, the scene passes, the idle figure is measured before
             * anything was opened.
             *
             * The invariant is deliberately NOT "back to the original
             * baseline". Several things are cached on first use and kept on
             * purpose -- the icon pack, the desktop bake, a thumbnail cache --
             * so the first cycle legitimately ends higher than it started.
             * What must hold is that the SECOND and THIRD cycles end at the
             * same figure: once the one-time costs are paid, an open and a
             * close must be free. A per-open leak breaks that and nothing
             * else does.
             */
            {
                cu32 after[3], peak[3];
                int opened[3];
                int cyc, k;
                for (cyc = 0; cyc < 3; cyc++) {
                    /* Every application with a no-argument opener, not a
                     * sample of them. A leak lives in exactly one app, and
                     * checking five of twenty-four is a four-in-five chance
                     * of looking in the wrong place. */
                    app_about_open();    app_bench_open();
                    app_calc_open();     app_charmap_open();
                    app_clock_open();    app_console_open();
                    app_control_open();  app_diskuse_open();
                    app_fileman_open();  app_help_open();
                    app_logview_open();  app_media_open();
                    app_mines_open();    app_net_open();
                    app_notepad_open();  app_paint_open();
                    app_sheet_open();    app_solitaire_open();
                    app_sysinfo_open();  app_taskman_open();
                    app_theme_open();    app_view_open();
                    app_welcome_open();  app_write_open();
                    for (k = 0; k < 3; k++) { sh_run_frame(); }
                    opened[cyc] = wm_window_count();
                    peak[cyc] = (cu32)(sys_mem_live_bytes() / 1024u);
                    while (wm_window_count() > 0) {
                        WmWindow *w = wm_window_at(0);
                        if (w == NULL) { break; }
                        wm_destroy(w);
                    }
                    for (k = 0; k < 3; k++) { sh_run_frame(); }
                    after[cyc] = (cu32)(sys_mem_live_bytes() / 1024u);
                }
                SYS_LOGI("main", "MEM-CHECK: every app opened and closed three "
                         "times -- %lu, %lu, %lu KB live",
                         (unsigned long)after[0], (unsigned long)after[1],
                         (unsigned long)after[2]);
                /* Cycles two and three must agree exactly. Anything that
                 * grows per open shows up here as a rising number. */
                /*
                 * Three equal numbers would look exactly the same if nothing
                 * had opened at all, so the windows are counted and the cost
                 * while they were up is reported. A cycle that opens nothing
                 * leaks nothing, and would pass the check below for the worst
                 * possible reason.
                 */
                SYS_LOGI("main", "MEM-CHECK: %d windows were actually up, at "
                         "%lu KB (%s)", opened[1], (unsigned long)peak[1],
                         (opened[1] >= 20 && peak[1] > after[1])
                             ? "OK" : "MISMATCH");
                SYS_LOGI("main", "MEM-CHECK: an open and a close cost nothing "
                         "after the first (%s)",
                         (after[2] == after[1]) ? "OK" : "MISMATCH");
                /* And the first cycle must not have grown without bound
                 * either -- caches are allowed, a doubling is not. */
                SYS_LOGI("main", "MEM-CHECK: still inside the idle ceiling "
                         "afterwards (%lu KB) (%s)",
                         (unsigned long)after[2],
                         (after[2] <= (cu32)CASTALIA_IDLE_KB_MAX)
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->car_demo) {
            /* Pack a folder, DELETE everything in it, unpack, and compare the
             * bytes of every file. And then the part that matters more: hand
             * the extractor an archive whose member is called "..\ESCAPE.TXT"
             * and require it to refuse rather than write outside the folder it
             * was pointed at.
             */
            char home[CASTALIA_MAX_PATH], dir[CASTALIA_MAX_PATH];
            char arc[CASTALIA_MAX_PATH], p1[CASTALIA_MAX_PATH];
            char p2[CASTALIA_MAX_PATH], p3[CASTALIA_MAX_PATH];
            const char *hp = sys_home();
            static char body[6000];
            static char back[6000];
            PlatFile *f;
            int q, files = 0;
            long saved = 0;
            CResult rc;

            sys_strlcpy(home, hp, sizeof(home));
            sys_snprintf(dir, sizeof(dir), "%s/CARTEST", home);
            sys_snprintf(arc, sizeof(arc), "%s/BACKUP.CAR", home);
            plat_mkdir(dir);
            for (q = 0; q < (int)sizeof(body); q++) {
                static const char *l = "the vale of Castalia, green hills and "
                                       "a small castle above them. ";
                body[q] = l[q % 64];
            }
            sys_snprintf(p1, sizeof(p1), "%s/ONE.TXT", dir);
            sys_snprintf(p2, sizeof(p2), "%s/TWO.TXT", dir);
            sys_snprintf(p3, sizeof(p3), "%s/THREE.DAT", dir);
            f = plat_fopen(p1, "wb");
            if (f != NULL) { plat_fwrite(f, body, 6000u); plat_fclose(f); }
            f = plat_fopen(p2, "wb");
            if (f != NULL) { plat_fwrite(f, body, 1500u); plat_fclose(f); }
            f = plat_fopen(p3, "wb");
            if (f != NULL) { plat_fwrite(f, body, 40u); plat_fclose(f); }

            rc = car_pack_dir(dir, arc, &files, &saved);
            SYS_LOGI("main", "CAR-DEMO: packed %d files, %ld bytes saved, "
                     "archive %ld bytes (%s)", files, saved,
                     plat_file_size(arc),
                     (rc == CE_OK && files == 3 && plat_file_size(arc) > 0 &&
                      plat_file_size(arc) < 7540) ? "OK" : "MISMATCH");

            plat_file_remove(p1);
            plat_file_remove(p2);
            plat_file_remove(p3);
            SYS_LOGI("main", "CAR-DEMO: the folder is empty before restoring "
                     "(%s)", (plat_file_size(p1) < 0 && plat_file_size(p2) < 0)
                             ? "OK" : "MISMATCH");

            files = 0;
            rc = car_unpack(arc, dir, &files);
            SYS_LOGI("main", "CAR-DEMO: unpacked %d files (%s)", files,
                     (rc == CE_OK && files == 3) ? "OK" : "MISMATCH");
            {
                cu32 n = 0;
                int same = 1;
                f = plat_fopen(p1, "rb");
                if (f != NULL) { n = plat_fread(f, back, 6000u); plat_fclose(f); }
                if (n != 6000u) { same = 0; }
                else { for (q = 0; q < 6000; q++) {
                           if (back[q] != body[q]) { same = 0; break; } } }
                SYS_LOGI("main", "CAR-DEMO: the biggest member came back byte "
                         "for byte (%s)", same ? "OK" : "MISMATCH");
                SYS_LOGI("main", "CAR-DEMO: the small members came back too "
                         "(%s)",
                         (plat_file_size(p2) == 1500 &&
                          plat_file_size(p3) == 40) ? "OK" : "MISMATCH");
            }

            /* An archive that tries to write outside the folder. Built by
             * hand, since car_write_entry will not produce one. */
            {
                char bad[CASTALIA_MAX_PATH], escaped[CASTALIA_MAX_PATH];
                static unsigned char raw[CAR_HEADER + CAR_ENTRY + 8];
                CarEntry e;
                int got = 0;
                memset(raw, 0, sizeof(raw));
                car_write_header(1, raw, sizeof(raw));
                memset(&e, 0, sizeof(e));
                sys_strlcpy(e.name, "ESCAPE.TXT", sizeof(e.name));
                e.original = 8u; e.stored = 8u;
                e.offset = CAR_HEADER + CAR_ENTRY; e.method = CZ_STORED;
                car_write_entry(&e, 0, raw, sizeof(raw));
                /* Overwrite the name the writer sanitised.
                 *
                 * A FORWARD slash on purpose. The DOS separator is a
                 * backslash, but this scene runs on the host, where a
                 * backslash is an ordinary filename character -- so a
                 * backslash member cannot escape here and the "nothing was
                 * written outside" check below would pass whatever the code
                 * did. car_name_ok rejects both; this picks the one that can
                 * actually get out on the machine running the test. */
                memcpy(raw + CAR_HEADER, "../ESCAPE.TXT", 14);
                memcpy(raw + CAR_HEADER + CAR_ENTRY, "PWNED!!!", 8);
                sys_snprintf(bad, sizeof(bad), "%s/EVIL.CAR", dir);
                sys_snprintf(escaped, sizeof(escaped), "%s/ESCAPE.TXT", home);
                f = plat_fopen(bad, "wb");
                if (f != NULL) { plat_fwrite(f, raw, (cu32)sizeof(raw));
                                 plat_fclose(f); }
                rc = car_unpack(bad, dir, &got);
                SYS_LOGI("main", "CAR-DEMO: a member named '../ESCAPE.TXT' is "
                         "refused (%s)", (rc != CE_OK && got == 0) ? "OK"
                                                                  : "MISMATCH");
                SYS_LOGI("main", "CAR-DEMO: and nothing was written outside "
                         "the folder (%s)",
                         (plat_file_size(escaped) < 0) ? "OK" : "MISMATCH");
                plat_file_remove(bad);
                plat_file_remove(escaped);
            }

            /* And through the File Manager's own Restore, since a .CAR and a
             * .CZ both map to ASSOC_ARCHIVE and only the open path tells them
             * apart -- handing a .CAR to the .CZ restore is the bug that
             * shipped the first time an archive type was added. */
            {
                char here[CASTALIA_MAX_PATH];
                int files2 = 0;
                plat_file_remove(p1);
                plat_file_remove(p2);
                plat_file_remove(p3);
                sys_snprintf(here, sizeof(here), "%s/BACKUP.CAR", home);
                rc = car_unpack(here, dir, &files2);
                SYS_LOGI("main", "CAR-DEMO: a .CAR restores as an archive, not "
                         "as a .CZ (%s)",
                         (rc == CE_OK && files2 == 3 &&
                          plat_file_size(p1) == 6000) ? "OK" : "MISMATCH");
            }

            plat_file_remove(p1); plat_file_remove(p2); plat_file_remove(p3);
            plat_file_remove(arc);
            plat_dir_remove(dir);
        }
        if (opt->crash_demo) {
            /* The crash screen cannot be checked by causing a crash --
             * sys_fatal exits the process. Painting it is a separate function
             * for exactly that reason, so this draws it and reads it back.
             *
             * What is checked is that it is FULL of advice rather than a
             * sentence and a colour: the crash ground covers the screen, and
             * there are at least eight distinct rows carrying text. Deleting
             * lines from it drops the row count, which is the failure worth
             * catching -- a crash screen quietly losing its instructions is
             * not something anyone would notice in normal use.
             */
            GfxSurface *bb;
            PlatVideoInfo vi;
            int x, y, rows = 0, ground = 0;
            char bak[CASTALIA_MAX_PATH];
            const char *hp = sys_home();
            PlatFile *f;

            sys_snprintf(bak, sizeof(bak), "%s/SYS", hp);
            plat_mkdir(bak);
            sys_snprintf(bak, sizeof(bak), "%s/SYS/CASTALIA.BAK", hp);
            plat_file_remove(bak);
            plat_video_info(&vi);

            /* First with NO known-good copy, since that branch says something
             * different and must still be a screenful of advice rather than a
             * shorter apology. */
            sh_crash_screen(plat_backbuffer(), "wm", CE_INVALID,
                            "src/wm/wm_window.c", 240, "window pool exhausted");
            {
                int yy, xx, r2 = 0;
                GfxSurface *b2 = plat_backbuffer();
                for (yy = 0; yy < vi.height; yy++) {
                    int ink = 0;
                    for (xx = 0; xx < vi.width; xx++) {
                        if (gfx_get_pixel(b2, xx, yy) !=
                            GFX_RGB(0x00, 0x00, 0x60)) { ink++; }
                    }
                    if (ink > 20) { r2++; }
                }
                SYS_LOGI("main", "CRASH-DEMO: with no known-good copy it still "
                         "advises (%d rows) (%s)", r2,
                         (r2 >= 45) ? "OK" : "MISMATCH");
            }

            /* Then with one, which is the more useful message. */
            f = plat_fopen(bak, "wb");
            if (f != NULL) { plat_fwrite(f, "[Shell]\n", 8u); plat_fclose(f); }
            sh_crash_screen(plat_backbuffer(), "gfx", CE_NOMEM,
                            "src/gfx/gfx_surface.c", 118,
                            "could not allocate the back buffer");
            bb = plat_backbuffer();
            for (y = 0; y < vi.height; y++) {
                int ink = 0;
                for (x = 0; x < vi.width; x++) {
                    CColor c = gfx_get_pixel(bb, x, y);
                    if (c == GFX_RGB(0x00, 0x00, 0x60)) { ground++; }
                    else { ink++; }
                }
                if (ink > 20) { rows++; }
            }
            SYS_LOGI("main", "CRASH-DEMO: %d ground px, %d rows carrying text",
                     ground, rows);
            SYS_LOGI("main", "CRASH-DEMO: the crash ground covers the screen "
                     "(%s)", (ground > (vi.width * vi.height) / 2) ? "OK"
                                                                   : "MISMATCH");
            /* Ten-ish lines of 8px text land around 56 rows. The threshold
             * is set close under that rather than at "some text exists": a
             * single surviving line would clear a loose bar, and a crash
             * screen quietly losing its instructions is exactly what nobody
             * would notice in normal use. */
            SYS_LOGI("main", "CRASH-DEMO: it carries several lines of advice "
                     "(%s)", (rows >= 45) ? "OK" : "MISMATCH");
            /* Leave the crash screen on the buffer for the screenshot: the
             * frame loop below would paint the desktop back over it. */
            plat_present(NULL, 0);
            plat_file_remove(bak);
        }
        if (opt->open_console) {
            /* Open the Console and run a couple of real commands for the shot. */
            static const char *seq[] = { "ver\n", "dir\n", "mem\n" };
            int q, k;
            app_console_open();
            sh_run_frame();
            for (q = 0; q < 3; q++) {
                for (k = 0; seq[q][k] != '\0'; k++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                    if (seq[q][k] == '\n') { ke.key = PLAT_KEY_ENTER; ke.ch = 0; }
                    else { ke.key = (unsigned char)seq[q][k]; ke.ch = (unsigned char)seq[q][k]; }
                    plat_host_push_event(&ke);
                }
                sh_run_frame();
            }
        }
        if (opt->open_bench) {
            /* Open the Benchmark Suite and run it (Enter triggers Run All). */
            PlatEvent be;
            app_bench_open();
            sh_run_frame();
            memset(&be, 0, sizeof(be));
            be.type = PLAT_EV_KEY_DOWN; be.key = PLAT_KEY_ENTER;
            plat_host_push_event(&be);
            sh_run_frame();
        }
        if (opt->open_media) {
            /* Open the Media Player and start playback of the first track so
             * the visualizer + timers are exercised for the screenshot. */
            PlatEvent me;
            app_media_open();
            sh_run_frame();
            memset(&me, 0, sizeof(me));
            me.type = PLAT_EV_KEY_DOWN; me.key = PLAT_KEY_ENTER; /* play sel */
            plat_host_push_event(&me);
            sh_run_frame();
        }
        if (opt->open_dialog) {
            ui_prompt("Rename", "New name:", "README.md", NULL, NULL);
        }
        if (opt->open_apps) {
            /* Drive a real Notepad Find: type text, click the Find button, and
             * enter a query -- interleaving frames so each step is processed. */
            const char *msg = "Hello, CastaliaOS!\nFind me: CastaliaOS twice.";
            const char *query = "CastaliaOS";
            PlatEvent ev2;
            int k;
            app_notepad_open();
            for (k = 0; msg[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                if (msg[k] == '\n') { ke.key = PLAT_KEY_ENTER; ke.ch = 0; }
                else { ke.key = (unsigned char)msg[k]; ke.ch = (unsigned char)msg[k]; }
                plat_host_push_event(&ke);
            }
            sh_run_frame(); /* apply typing */
            memset(&ev2, 0, sizeof(ev2));
            ev2.type = PLAT_EV_MOUSE_DOWN; ev2.mouse_x = 358; ev2.mouse_y = 155;
            ev2.buttons = PLAT_MB_LEFT; plat_host_push_event(&ev2);
            ev2.type = PLAT_EV_MOUSE_UP; ev2.buttons = 0; plat_host_push_event(&ev2);
            sh_run_frame(); /* open the Find dialog */
            for (k = 0; query[k] != '\0'; k++) {
                PlatEvent qe; memset(&qe, 0, sizeof(qe)); qe.type = PLAT_EV_KEY_DOWN;
                qe.key = (unsigned char)query[k]; qe.ch = (unsigned char)query[k];
                plat_host_push_event(&qe);
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = 0; plat_host_push_event(&en); }
            sh_run_frame(); /* run the search, close the dialog */
        }
        if (opt->open_calc) {
            const char *keys = "12+34=";
            int k;
            app_calc_open();
            for (k = 0; keys[k] != '\0'; k++) {
                PlatEvent ke;
                memset(&ke, 0, sizeof(ke));
                ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)keys[k];
                ke.ch = (unsigned char)keys[k];
                plat_host_push_event(&ke);
            }
            sh_run_frame();
            {
                /* The arithmetic is unit-tested to death in test_calc.c. What
                 * that cannot show is whether a keystroke reaches the machine
                 * at all: 12 + 34 typed at the WINDOW must read 46. */
                WmWindow *cw2 = wm_focused();
                const char *d = app_calc_display(cw2);
                SYS_LOGI("main", "CALC-DEMO: typed 12+34= and the display "
                         "reads '%s' (%s)", (d != NULL) ? d : "(none)",
                         (d != NULL && strcmp(d, "46") == 0)
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->nav_downs > 0) {
            /* Keyboard-only launcher path, exactly as on real DOS: Esc opens
             * the menu, Down walks to the target row, Enter launches it. */
            int n;
            PlatEvent ke;
            memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
            ke.key = PLAT_KEY_ESC; ke.ch = 0; plat_host_push_event(&ke);
            sh_run_frame();
            for (n = 0; n < opt->nav_downs; n++) {
                PlatEvent de; memset(&de, 0, sizeof(de));
                de.type = PLAT_EV_KEY_DOWN; de.key = PLAT_KEY_DOWN; de.ch = 0;
                plat_host_push_event(&de);
                sh_run_frame();
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = 0; plat_host_push_event(&en); }
            sh_run_frame();
            if (opt->nav_close) {
                /* Alt+F4 (PLAT_KEY_CLOSE) closes the focused window -- the
                 * keyboard-only close path. Verifies the shot ends empty. */
                PlatEvent ce; memset(&ce, 0, sizeof(ce)); ce.type = PLAT_EV_KEY_DOWN;
                ce.key = PLAT_KEY_CLOSE; ce.ch = 0; plat_host_push_event(&ce);
                sh_run_frame();
                SYS_LOGI("main", "nav-close: %d window(s) remain",
                         wm_window_count());
            }
        }
        if (opt->icon_sel) {
            /* Single-click the "Documents" desktop icon (row 2) -> select it. */
            PlatEvent ie; memset(&ie, 0, sizeof(ie));
            ie.type = PLAT_EV_MOUSE_DOWN; ie.mouse_x = 60; ie.mouse_y = 120;
            ie.buttons = PLAT_MB_LEFT; plat_host_push_event(&ie);
            ie.type = PLAT_EV_MOUSE_UP; ie.buttons = 0; plat_host_push_event(&ie);
            sh_run_frame();
        }
        if (opt->tb_max) {
            /* Double-click the title bar -> maximize (reserving the taskbar). */
            CRect fr;
            PlatEvent te;
            int tx, ty, c;
            app_fileman_open();
            sh_run_frame();
            fr = wm_frame_rect(wm_focused());
            tx = (fr.x0 + fr.x1) / 2; ty = fr.y0 + 10;
            for (c = 0; c < 2; c++) {
                memset(&te, 0, sizeof(te));
                te.type = PLAT_EV_MOUSE_DOWN; te.mouse_x = tx; te.mouse_y = ty;
                te.buttons = PLAT_MB_LEFT; plat_host_push_event(&te);
                te.type = PLAT_EV_MOUSE_UP; te.buttons = 0; plat_host_push_event(&te);
            }
            sh_run_frame();
        }
        if (opt->np_wrap) {
            /* Type one long line, then click the Wrap button; the text should
             * reflow across several visual rows within the window width. */
            const char *para =
                "The quick brown fox jumps over the lazy dog while the "
                "CastaliaOS Notepad wraps this long single line of text "
                "neatly to the width of the window.";
            CRect cr;
            PlatEvent we;
            int k, wx, wy;
            app_notepad_open();
            for (k = 0; para[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)para[k]; ke.ch = (unsigned char)para[k];
                plat_host_push_event(&ke);
            }
            sh_run_frame();
            cr = wm_client_rect(wm_focused());
            wx = cr.x0 + 202 + 24; /* Wrap button center (mirrors np_layout) */
            wy = cr.y0 + 3 + 10;
            memset(&we, 0, sizeof(we));
            we.type = PLAT_EV_MOUSE_DOWN; we.mouse_x = wx; we.mouse_y = wy;
            we.buttons = PLAT_MB_LEFT; plat_host_push_event(&we);
            we.type = PLAT_EV_MOUSE_UP; we.buttons = 0; plat_host_push_event(&we);
            sh_run_frame();
        }
        if (opt->clip_demo) {
            /* Type text, select all (Ctrl+A), copy (Ctrl+C), go to end, newline,
             * paste (Ctrl+V) -> two identical lines. Verifies the whole
             * selection + system-clipboard path through the real event pipeline.
             * The typed text is asserted against clip_get_text() below. */
            const char *msg = "CastaliaClip";
            int k;
            app_notepad_open();
            for (k = 0; msg[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)msg[k]; ke.ch = (unsigned char)msg[k];
                plat_host_push_event(&ke);
            }
            sh_run_frame();
            {
                /* Ctrl+A, Ctrl+C (control codes, as the DOS INT16h path delivers,
                 * plus PLAT_MOD_CTRL so either detection route works). */
                PlatEvent e; memset(&e, 0, sizeof(e)); e.type = PLAT_EV_KEY_DOWN;
                e.mods = PLAT_MOD_CTRL;
                e.key = 1;  e.ch = 0; plat_host_push_event(&e);   /* Ctrl+A */
                e.key = 3;  e.ch = 0; plat_host_push_event(&e);   /* Ctrl+C */
                sh_run_frame();
                SYS_LOGI("main", "CLIP-DEMO copy: clipboard=\"%s\" (%s)",
                         clip_get_text(),
                         (strcmp(clip_get_text(), "CastaliaClip") == 0)
                             ? "OK" : "MISMATCH");
                /* End, Enter, Ctrl+V -> paste a second identical line. */
                memset(&e, 0, sizeof(e)); e.type = PLAT_EV_KEY_DOWN;
                e.key = PLAT_KEY_END;   e.ch = 0; plat_host_push_event(&e);
                e.key = PLAT_KEY_ENTER; e.ch = '\n'; plat_host_push_event(&e);
                e.mods = PLAT_MOD_CTRL; e.key = 22; e.ch = 0; plat_host_push_event(&e);
                sh_run_frame();
                /* Re-select-all + copy: if paste grew the buffer to two lines,
                 * the clipboard now holds both, proving paste (and copy across a
                 * newline) worked. */
                memset(&e, 0, sizeof(e)); e.type = PLAT_EV_KEY_DOWN;
                e.mods = PLAT_MOD_CTRL;
                e.key = 1; e.ch = 0; plat_host_push_event(&e);   /* Ctrl+A */
                e.key = 3; e.ch = 0; plat_host_push_event(&e);   /* Ctrl+C */
                sh_run_frame();
                SYS_LOGI("main", "CLIP-DEMO paste: clipboard=\"%s\" (%s)",
                         clip_get_text(),
                         (strcmp(clip_get_text(),
                                 "CastaliaClip\nCastaliaClip") == 0)
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->snap_demo) {
            /*
             * Drag a window's title bar to the left screen edge; on release it
             * should snap to the left half of the WORK AREA -- the screen
             * minus the taskbar.
             *
             * This scene used to LOG the resulting frame and assert nothing at
             * all: it reported "x0=0 y0=0 w=400 h=581" whatever happened, so a
             * snap that stopped working, or that filled the taskbar, would
             * have read exactly the same. The expected rectangle comes from
             * wm_snap_rect, the same function the manager snaps with, because
             * the claim is that the two agree -- a rectangle typed out here
             * would be a second copy of the arithmetic and would drift.
             *
             * Twice: once carrying the window's contents and once by outline
             * ([Shell] DragOutline). The second is the one nothing covered --
             * an outline drag does not move the window until the button comes
             * up, and the snap has to happen to the window rather than to the
             * band.
             */
            int pass;
            for (pass = 0; pass < 2; pass++) {
                WmWindow *w;
                CRect f, want, got, area;
                PlatVideoInfo vi2;
                int tx, ty, k;
                settings_get()->drag_outline = (pass == 1) ? CTRUE : CFALSE;
                app_fileman_open();
                for (k = 0; k < 20; k++) { sh_run_frame(); }
                w = wm_focused();
                f = wm_frame_rect(w);
                plat_video_info(&vi2);
                tx = f.x0 + 40; ty = f.y0 + 6;      /* on the title bar */
                feed_press(tx, ty);
                feed_drag(200, vi2.height / 2);
                feed_drag(2, vi2.height / 2);       /* into the left zone */
                feed_release(2, vi2.height / 2);
                for (k = 0; k < 4; k++) { sh_run_frame(); }
                area = wm_work_area();
                want = wm_snap_rect(&area, WM_SNAP_LEFT);
                got = wm_frame_rect(w);
                SYS_LOGI("main", "SNAP-DEMO: dragging %s to the left edge "
                         "snaps to the left half (%d,%d %dx%d, wanted "
                         "%d,%d %dx%d) (%s)",
                         (pass == 1) ? "by outline" : "with its contents",
                         got.x0, got.y0, crect_w(&got), crect_h(&got),
                         want.x0, want.y0, crect_w(&want), crect_h(&want),
                         (got.x0 == want.x0 && got.y0 == want.y0 &&
                          crect_w(&got) == crect_w(&want) &&
                          crect_h(&got) == crect_h(&want)) ? "OK" : "MISMATCH");
                /* ...and the taskbar is still there under it. */
                SYS_LOGI("main", "SNAP-DEMO: ...and stops at the taskbar "
                         "(%d, work area ends at %d) (%s)", got.y1, area.y1,
                         (got.y1 <= area.y1) ? "OK" : "MISMATCH");
                wm_destroy(w);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
            }
            settings_get()->drag_outline = CFALSE;

            /*
             * ---- what counts as a double-click on a title bar -------------
             *
             * Two presses maximize a window. Which two was decided by the
             * window pointer and the clock alone, and both of those lie.
             *
             * The pool hands out slots again, so a window closed and another
             * opened inside the double-click time compare EQUAL -- close a
             * window, click the next one's title bar, and it maximizes
             * itself. That is a real gesture, not a contrived one, and it is
             * how the scene above found this: two File Managers in a row.
             *
             * And two presses at opposite ends of a title bar are not a
             * double-click in any system.
             */
            {
                WmWindow *a1, *b1;
                CRect fr1, fr2;
                int k;

                app_fileman_open();
                for (k = 0; k < 12; k++) { sh_run_frame(); }
                a1 = wm_focused();
                fr1 = wm_frame_rect(a1);
                feed_click(fr1.x0 + 40, fr1.y0 + 6);      /* one title press */
                wm_destroy(a1);
                for (k = 0; k < 2; k++) { sh_run_frame(); }

                app_fileman_open();
                for (k = 0; k < 12; k++) { sh_run_frame(); }
                b1 = wm_focused();
                fr2 = wm_frame_rect(b1);
                feed_click(fr2.x0 + 40, fr2.y0 + 6);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
                {
                    CRect now = wm_frame_rect(b1);
                    SYS_LOGI("main", "SNAP-DEMO: a title press on a window "
                             "opened where a closed one stood is not a double "
                             "click (%dx%d) (%s)", crect_w(&now),
                             crect_h(&now),
                             (crect_w(&now) == crect_w(&fr2)) ? "OK"
                                                              : "MISMATCH");
                }

                /* Two presses far apart along the same bar: also not a pair. */
                feed_click(fr2.x0 + 30, fr2.y0 + 6);
                feed_click(fr2.x0 + 200, fr2.y0 + 6);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
                {
                    CRect now = wm_frame_rect(b1);
                    SYS_LOGI("main", "SNAP-DEMO: ...nor are two presses at "
                             "opposite ends of the bar (%dx%d) (%s)",
                             crect_w(&now), crect_h(&now),
                             (crect_w(&now) == crect_w(&fr2)) ? "OK"
                                                              : "MISMATCH");
                }

                /* ...and two in the SAME place still are, or none of the
                 * above proves anything except that maximize is broken. */
                feed_click(fr2.x0 + 60, fr2.y0 + 6);
                feed_click(fr2.x0 + 60, fr2.y0 + 6);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
                {
                    CRect now = wm_frame_rect(b1);
                    SYS_LOGI("main", "SNAP-DEMO: ...but two in the same place "
                             "still maximize (%dx%d) (%s)", crect_w(&now),
                             crect_h(&now),
                             (crect_w(&now) > crect_w(&fr2)) ? "OK"
                                                             : "MISMATCH");
                }
                wm_destroy(b1);
                for (k = 0; k < 2; k++) { sh_run_frame(); }
            }

            /*
             * ---- a window closed while it is being DRAGGED ----------------
             *
             * Alt+F4 while the title bar is held needs nothing but two hands,
             * and the manager kept a pointer to the dragged window across the
             * destroy. The pool hands slots out again, so the next motion
             * event set the frame of a slot that was not in use, or of
             * whatever window had been given it since -- a write into a
             * window that is not there any more, which is not something a
             * screenshot can be asked about.
             *
             * Driven twice, carrying the contents and by outline, because the
             * outline path holds a band as well as a pointer and a band left
             * with nothing tracking it never goes away.
             *
             * What is checked is what CAN be: the surviving window keeps the
             * frame it had, and the count is right. The corruption itself is
             * caught by make memcheck and the sanitizers, which run every
             * scene -- this is the scene that reaches it.
             */
            {
                int pass2;
                for (pass2 = 0; pass2 < 2; pass2++) {
                    WmWindow *victim, *bystander;
                    CRect vf, bf2, after;
                    int k, n0;
                    settings_get()->drag_outline = (pass2 == 1) ? CTRUE : CFALSE;

                    app_calc_open();
                    for (k = 0; k < 16; k++) { sh_run_frame(); }
                    bystander = wm_focused();
                    bf2 = crect_make(430, 260, 260, 200);
                    wm_set_frame(bystander, &bf2);

                    app_fileman_open();
                    for (k = 0; k < 16; k++) { sh_run_frame(); }
                    victim = wm_focused();
                    vf = crect_make(60, 60, 300, 200);
                    wm_set_frame(victim, &vf);
                    for (k = 0; k < 6; k++) { sh_run_frame(); }

                    n0 = wm_window_count();
                    feed_press(vf.x0 + 40, vf.y0 + 6);   /* hold the title */
                    feed_drag(vf.x0 + 70, vf.y0 + 40);
                    wm_destroy(victim);                  /* ...and it closes */
                    for (k = 0; k < 3; k++) { sh_run_frame(); }

                    /* Mouse motion with the drag's window gone. */
                    feed_drag(300, 300);
                    feed_drag(340, 340);
                    feed_release(340, 340);
                    for (k = 0; k < 4; k++) { sh_run_frame(); }

                    after = wm_frame_rect(bystander);
                    SYS_LOGI("main", "SNAP-DEMO: closing a window %s mid-drag "
                             "leaves the others alone (%d,%d %dx%d, count "
                             "%d -> %d) (%s)",
                             (pass2 == 1) ? "dragged by outline"
                                          : "dragged by its contents",
                             after.x0, after.y0, crect_w(&after),
                             crect_h(&after), n0, wm_window_count(),
                             (after.x0 == bf2.x0 && after.y0 == bf2.y0 &&
                              crect_w(&after) == crect_w(&bf2) &&
                              wm_window_count() == n0 - 1) ? "OK"
                                                           : "MISMATCH");
                    /* Long enough for the CLOSE animation to finish: it
                     * draws a shrinking outline over the scene, and a probe
                     * taken through one compares two frames of an animation
                     * rather than two repaints. */
                    for (k = 0; k < 24; k++) { sh_run_frame(); }
                    /* ...and nothing of the band is left on the desktop. */
                    {
                        CRect watch2 = crect_make(20, 20, 500, 400);
                        probe_repaint_rect("SNAP-DEMO",
                                           "the desktop after a drag that "
                                           "lost its window", &watch2);
                    }

                    /*
                     * The same thing with a RESIZE, which is where it bites
                     * hardest: do_resize does not merely write the frame, it
                     * sends WM_MSG_SIZE -- so a resize that outlived its
                     * window called that window's proc with the payload its
                     * DESTROY handler had already freed. A use-after-free on
                     * every motion event, reached by holding a corner and
                     * pressing Alt+F4.
                     *
                     * Driven under make memcheck and make sanitize, which
                     * run every scene -- though it should be said that
                     * neither reported anything with the fix removed: the
                     * File Manager does not handle WM_MSG_SIZE, so the send
                     * returns without touching the payload it was passed.
                     * The write into a pool slot that is not in use is the
                     * part that stays, and it is defended against rather
                     * than demonstrated.
                     */
                    {
                        WmWindow *rv;
                        CRect rf;
                        app_fileman_open();
                        for (k = 0; k < 16; k++) { sh_run_frame(); }
                        rv = wm_focused();
                        rf = crect_make(70, 70, 280, 190);
                        wm_set_frame(rv, &rf);
                        for (k = 0; k < 6; k++) { sh_run_frame(); }
                        feed_press(rf.x1 - 1, rf.y1 - 1);   /* corner grip */
                        feed_drag(rf.x1 + 10, rf.y1 + 10);
                        wm_destroy(rv);
                        for (k = 0; k < 3; k++) { sh_run_frame(); }
                        feed_drag(rf.x1 + 60, rf.y1 + 60);
                        feed_drag(rf.x1 + 90, rf.y1 + 90);
                        feed_release(rf.x1 + 90, rf.y1 + 90);
                        for (k = 0; k < 24; k++) { sh_run_frame(); }
                        SYS_LOGI("main", "SNAP-DEMO: ...and a RESIZE that "
                                 "outlives its window sends it nothing (%d "
                                 "window(s) still open) (%s)",
                                 wm_window_count(),
                                 (wm_window_count() == n0 - 1) ? "OK"
                                                               : "MISMATCH");
                    }
                    wm_destroy(bystander);
                    for (k = 0; k < 3; k++) { sh_run_frame(); }
                }
                settings_get()->drag_outline = CFALSE;
            }
        }
        if (opt->taskman_demo) {
            /* Open two apps + the Task Manager, then End Task the first listed
             * window (Delete). The live window count should drop by one. */
            int before, after;
            PlatEvent e;
            app_calc_open();
            app_notepad_open();
            app_taskman_open();
            sh_run_frame();
            before = wm_window_count();
            memset(&e, 0, sizeof(e)); e.type = PLAT_EV_KEY_DOWN;
            e.key = PLAT_KEY_HOME;   e.ch = 0; plat_host_push_event(&e); /* sel 0 */
            e.key = PLAT_KEY_DELETE; e.ch = 0; plat_host_push_event(&e); /* end   */
            sh_run_frame();
            after = wm_window_count();
            SYS_LOGI("main", "TASKMAN-DEMO: windows %d -> %d (%s)", before, after,
                     (after == before - 1) ? "OK" : "MISMATCH");
            /* Let real time pass so the Performance graphs have real samples
             * to draw: headless frames are otherwise sub-millisecond and the
             * history would be honestly, but uselessly, flat. */
            {
                int f;
                for (f = 0; f < 40; f++) { plat_sleep_ms(25); sh_run_frame(); }
            }

            /*
             * The memory gauge: what is PAINTED against what ui_meter_fill
             * says should be. Four windows used to compute this themselves,
             * and this one overflowed -- (live * width) / budget passes 2^31
             * at 5.31 MB of an 8 MB budget on a 32-bit long, wraps negative,
             * and clamps to zero, so the bar read EMPTY exactly when memory
             * was nearly gone. The arithmetic is unit-tested; this checks the
             * painter is still calling it rather than doing its own again.
             */
            {
                WmWindow *tw = NULL;
                CRect bar;
                long live = 0, budget = 0;
                int i;
                for (i = 0; i < wm_window_count(); i++) {
                    WmWindow *w = wm_window_at(i);
                    if (w != NULL && wm_title(w) != NULL &&
                        strcmp(wm_title(w), "Task Manager") == 0) { tw = w; }
                }
                if (tw != NULL && app_taskman_mem_bar(tw, &bar, &live, &budget)) {
                    GfxSurface *bb = plat_backbuffer();
                    int inner = crect_w(&bar) - 2;
                    int want = ui_meter_fill((cs32)live, (cs32)budget, inner);
                    int midy = (bar.y0 + bar.y1) / 2;
                    int painted = 0, x;
                    CColor accent = ui_palette()->accent;
                    for (x = bar.x0 + 1; x < bar.x1 - 1; x++) {
                        if (gfx_get_pixel(bb, x, midy) == accent) { painted++; }
                    }
                    SYS_LOGI("main", "TASKMAN-DEMO: memory %ld of %ld -- "
                             "painted %d px, arithmetic says %d (%s)",
                             live, budget, painted, want,
                             (painted >= want - 1 && painted <= want + 1)
                                 ? "OK" : "MISMATCH");
                    /* A gauge pinned at either end would satisfy the above
                     * while telling the user nothing, so it must also be
                     * genuinely partial at this point in the run. */
                    SYS_LOGI("main", "TASKMAN-DEMO: and it is neither empty "
                             "nor full (%d of %d) (%s)", painted, inner,
                             (painted > 0 && painted < inner)
                                 ? "OK" : "MISMATCH");
                    /*
                     * The GRAPHS, which are a different thing from the gauge
                     * above: the gauge reads sys_mem_live_bytes directly and
                     * would look right even with an empty sample history, so
                     * every check here passed while the two Performance boxes
                     * drew nothing at all. That is exactly the failure an
                     * uninitialised ring produces.
                     */
                    SYS_LOGI("main", "TASKMAN-DEMO: %d samples in the "
                             "performance history (%s)",
                             app_taskman_samples(tw),
                             (app_taskman_samples(tw) > 0)
                                 ? "OK" : "MISMATCH");
                } else {
                    SYS_LOGW("main", "TASKMAN-DEMO: no memory gauge to read "
                             "(MISMATCH)");
                }
            }
        }
        if (opt->vdesk_demo) {
            /* Open Calculator on desktop 1, switch to desktop 2, open Notepad
             * there, and confirm each window stays on its own desktop and focus
             * follows the switch. Leaves the shell on desktop 2 for the shot. */
            WmWindow *calc, *note;
            app_calc_open();  sh_run_frame();
            calc = wm_focused();
            sh_switch_desktop(1); sh_run_frame();
            app_notepad_open(); sh_run_frame();
            note = wm_focused();
            SYS_LOGI("main", "VDESK-DEMO: desktop=%d calc@%d note@%d focus=%s (%s)",
                     wm_current_desktop() + 1, wm_window_desktop(calc) + 1,
                     wm_window_desktop(note) + 1, wm_title(wm_focused()),
                     (wm_current_desktop() == 1 && wm_window_desktop(calc) == 0 &&
                      wm_window_desktop(note) == 1 &&
                      wm_focused() == note) ? "OK" : "MISMATCH");
            sh_switch_desktop(0); sh_run_frame();
            SYS_LOGI("main", "VDESK-DEMO: back to desktop=%d focus=%s (%s)",
                     wm_current_desktop() + 1,
                     wm_focused() ? wm_title(wm_focused()) : "(none)",
                     (wm_current_desktop() == 0 && wm_focused() == calc)
                         ? "OK" : "MISMATCH");
            sh_switch_desktop(1); sh_run_frame(); /* back to d2 for the screenshot */
        }
        if (opt->paint_demo) {
            /* Open Paint, pick a red swatch, choose the Ellipse tool and its
             * filled style, drag a shape out, save it with Ctrl+S through the
             * prompt, then load the file back and confirm the drawing survived
             * -- Paint + paint_core + gfx_bmp_save + gfx_bmp_load end to end.
             * Finally open the Viewer on it for the screenshot. */
            WmWindow *pw;
            CPoint o;
            PlatEvent e;
            int k;
            GfxSurface *loaded;
            /* The file dialog resolves a bare name against CASTALIA_HOME, so
             * the demo reloads from exactly the same place. */
            const char *fname = "PAINTOUT.BMP";
            const char *phome = sys_home();
            char fn[CASTALIA_MAX_PATH];
            /* The layout mirrors app_paint.c: a 16 px menu bar, a two-column
             * tool box 24 px per button, the options box under it, the image
             * inset 5 px into the workspace, and the palette above the status
             * bar with 13 px swatches starting 40 px in. */
            const int TOOLX = 2 + 24 / 2, TOOLY = 16 + 2;
            /* The options box sits under the whole tool box: eleven tools in
             * two columns is six rows of 22 px, plus the box's own padding. */
            const int OPTY = 16 + (22 * 6 + 2 * 2) + 2 + 4;
            const int OPTH = (70 - 8) / 4;
            const int CANX = 52 + 5, CANY = 16 + 5;
            sys_snprintf(fn, sizeof(fn), "%s/%s", phome, fname);
            app_paint_open(); sh_run_frame();
            pw = wm_focused(); o = wm_client_origin(pw);
            memset(&e, 0, sizeof(e));

            /* palette: row 1, column 3 is red (0xFF0000) */
            {
                CRect cr = wm_client_rect(pw);
                int paly = crect_h(&cr) - 18 - 36;
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + 40 + 2 * 13 + 6;
                e.mouse_y = o.y + paly + (36 - 26) / 2 + 13 + 6;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
            }
            /* and the background color, set with a right click: yellow */
            {
                CRect cr = wm_client_rect(pw);
                int paly = crect_h(&cr) - 18 - 36;
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + 40 + 3 * 13 + 6;
                e.mouse_y = o.y + paly + (36 - 26) / 2 + 13 + 6;
                e.buttons = PLAT_MB_RIGHT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
            }
            /* tool box: Ellipse is index 8 -> column 0, row 4 */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + TOOLX; e.mouse_y = o.y + TOOLY + 4 * 22 + 11;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            /* options box: the second slot is "filled" */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + 26;
            e.mouse_y = o.y + OPTY + OPTH + OPTH / 2;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* drag the shape out on the image */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + CANX + 40; e.mouse_y = o.y + CANY + 30;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_MOVE;
            e.mouse_x = o.x + CANX + 300; e.mouse_y = o.y + CANY + 200;
            plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP;
            e.mouse_x = o.x + CANX + 300; e.mouse_y = o.y + CANY + 200;
            e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* Ctrl+S -> the Save As dialog, whose field is pre-filled */
            { PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
              ke.key = 19; ke.ch = 's'; ke.mods = PLAT_MOD_CTRL;
              plat_host_push_event(&ke); }
            sh_run_frame();
            for (k = 0; k < 24; k++) {
                PlatEvent be; memset(&be, 0, sizeof(be));
                be.type = PLAT_EV_KEY_DOWN; be.key = PLAT_KEY_BACKSP;
                plat_host_push_event(&be);
            }
            for (k = 0; fname[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)fname[k]; ke.ch = (unsigned char)fname[k];
                plat_host_push_event(&ke);
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = '\n'; plat_host_push_event(&en); }
            sh_run_frame();

            loaded = gfx_bmp_load(fn);
            if (loaded == NULL) {
                SYS_LOGW("main", "PAINT-DEMO: could not load %s (MISMATCH)", fn);
            } else {
                int x, y, red = 0, yellow = 0;
                for (y = 0; y < loaded->h; y++) {
                    for (x = 0; x < loaded->w; x++) {
                        CColor c = loaded->pixels[y * loaded->pitch + x] & 0xFFFFFFUL;
                        if (c == 0xFF0000UL) { red++; }      /* the outline  */
                        if (c == 0xFFFF00UL) { yellow++; }   /* the fill     */
                    }
                }
                SYS_LOGI("main", "PAINT-DEMO: loaded %dx%d outline=%d fill=%d (%s)",
                         loaded->w, loaded->h, red, yellow,
                         (loaded->w == 420 && loaded->h == 260 &&
                          red > 400 && yellow > 5000) ? "OK" : "MISMATCH");
                gfx_surface_free(loaded);
            }
            if (!opt->paint_demo_keep) {
                /* Cut a block out of the shape and paste it somewhere else,
                 * then save and look for it there: the clipboard round trip
                 * has to move real pixels, not just change a status line. */
                int k2;
                /* white as the background, so a cut leaves a visible hole */
                {
                    CRect cr2 = wm_client_rect(pw);
                    int paly2 = crect_h(&cr2) - 18 - 36;
                    e.type = PLAT_EV_MOUSE_DOWN;
                    e.mouse_x = o.x + 40 + 6;
                    e.mouse_y = o.y + paly2 + (36 - 26) / 2 + 13 + 6;
                    e.buttons = PLAT_MB_RIGHT; plat_host_push_event(&e);
                    e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                    plat_host_push_event(&e);
                    sh_run_frame();
                }
                /* the Select tool is the eleventh: column 0, row 5 */
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + TOOLX; e.mouse_y = o.y + TOOLY + 5 * 22 + 11;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
                /* drag a marquee inside the shape, then Ctrl+X */
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + CANX + 120; e.mouse_y = o.y + CANY + 80;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_MOVE;
                e.mouse_x = o.x + CANX + 200; e.mouse_y = o.y + CANY + 140;
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP;
                e.mouse_x = o.x + CANX + 200; e.mouse_y = o.y + CANY + 140;
                e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
                { PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                  ke.key = 24; ke.ch = 'x'; ke.mods = PLAT_MOD_CTRL;
                  plat_host_push_event(&ke); }
                sh_run_frame();
                /* mark where it should land, then Ctrl+V */
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + CANX + 20; e.mouse_y = o.y + CANY + 205;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_MOVE;
                e.mouse_x = o.x + CANX + 34; e.mouse_y = o.y + CANY + 219;
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP;
                e.mouse_x = o.x + CANX + 34; e.mouse_y = o.y + CANY + 219;
                e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
                { PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                  ke.key = 22; ke.ch = 'v'; ke.mods = PLAT_MOD_CTRL;
                  plat_host_push_event(&ke); }
                sh_run_frame();
                { PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                  ke.key = 19; ke.ch = 's'; ke.mods = PLAT_MOD_CTRL;
                  plat_host_push_event(&ke); }
                sh_run_frame();
                loaded = gfx_bmp_load(fn);
                if (loaded != NULL) {
                    int x, y, hole = 0, moved = 0;
                    for (y = 90; y < 130; y++) {          /* where it was    */
                        for (x = 130; x < 190; x++) {
                            if ((loaded->pixels[y * loaded->pitch + x] &
                                 0xFFFFFFUL) == 0xFFFFFFUL) { hole++; }
                        }
                    }
                    for (y = 210; y < 250; y++) {         /* where it went   */
                        for (x = 25; x < 95; x++) {
                            if ((loaded->pixels[y * loaded->pitch + x] &
                                 0xFFFFFFUL) == 0xFFFF00UL) { moved++; }
                        }
                    }
                    SYS_LOGI("main", "PAINT-DEMO: cut left %d white, paste put "
                                     "%d fill pixels elsewhere (%s)",
                             hole, moved,
                             (hole > 1000 && moved > 1000) ? "OK" : "MISMATCH");
                    gfx_surface_free(loaded);
                }
                CASTALIA_UNUSED(k2);
                /* Now undo through the Edit menu and save again: the paste
                 * must come back OUT, which proves the menu path and the undo
                 * ring are wired to the same canvas. */
                {
                static const char *const PMENU[] = { "File", "Edit",
                                                    "Image", "Help" };
                CRect bar = wm_client_rect(pw);
                CRect word;
                bar = crect_make(0, 0, crect_w(&bar), OF_MENUBAR_H);
                word = office_menu_word(&bar, PMENU, 1);      /* the Edit word */
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + word.x0 + 4; e.mouse_y = o.y + 8;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
                e.type = PLAT_EV_MOUSE_DOWN;                 /* Undo, item 0  */
                e.mouse_x = o.x + word.x0 + 20;
                e.mouse_y = o.y + OF_MENUBAR_H + 8;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
                sh_run_frame();
                { PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                  ke.key = 19; ke.ch = 's'; ke.mods = PLAT_MOD_CTRL;
                  plat_host_push_event(&ke); }
                sh_run_frame();
                loaded = gfx_bmp_load(fn);
                if (loaded != NULL) {
                    int x, y, left = 0, elsewhere = 0;
                    for (y = 210; y < 250; y++) {      /* where it was pasted */
                        for (x = 25; x < 95; x++) {
                            if ((loaded->pixels[y * loaded->pitch + x] &
                                 0xFFFFFFUL) == 0xFFFF00UL) { left++; }
                        }
                    }
                    for (y = 40; y < 60; y++) {        /* the shape itself    */
                        for (x = 150; x < 250; x++) {
                            if ((loaded->pixels[y * loaded->pitch + x] &
                                 0xFFFFFFUL) == 0xFFFF00UL) { elsewhere++; }
                        }
                    }
                    SYS_LOGI("main", "PAINT-DEMO: after Edit>Undo the paste is "
                                     "%s and the shape is %s (%s)",
                             (left == 0) ? "gone" : "still there",
                             (elsewhere > 500) ? "intact" : "damaged",
                             (left == 0 && elsewhere > 500) ? "OK" : "MISMATCH");
                    gfx_surface_free(loaded);
                }
                app_view_open_file(fn); sh_run_frame();
                }
            }
            if (opt->paint_demo_keep) {
                /* A few more tools on top, for a screenshot that shows what
                 * the editor can actually do: a solid box, an airbrush haze
                 * and a line of text. */
                int t;
                CRect cr = wm_client_rect(pw);
                int paly = crect_h(&cr) - 18 - 36;
                struct { int sw; int tool; int opt; } step[3];
                step[0].sw = 6;  step[0].tool = 7; step[0].opt = -1;  /* rect  */
                step[1].sw = 18; step[1].tool = 2; step[1].opt = 3;   /* air   */
                step[2].sw = 20; step[2].tool = 9; step[2].opt = -1;  /* text  */
                for (t = 0; t < 3; t++) {
                    /* foreground color */
                    e.type = PLAT_EV_MOUSE_DOWN;
                    e.mouse_x = o.x + 40 + (step[t].sw % 14) * 13 + 6;
                    e.mouse_y = o.y + paly + (36 - 26) / 2
                              + (step[t].sw / 14) * 13 + 6;
                    e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                    e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                    plat_host_push_event(&e);
                    /* tool */
                    e.type = PLAT_EV_MOUSE_DOWN;
                    e.mouse_x = o.x + TOOLX + (step[t].tool % 2) * 24;
                    e.mouse_y = o.y + TOOLY + (step[t].tool / 2) * 22 + 11;
                    e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                    e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                    plat_host_push_event(&e);
                    sh_run_frame();
                    if (step[t].opt >= 0) {
                        e.type = PLAT_EV_MOUSE_DOWN;
                        e.mouse_x = o.x + 26;
                        e.mouse_y = o.y + OPTY + step[t].opt * OPTH + OPTH / 2;
                        e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                        e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                        plat_host_push_event(&e);
                        sh_run_frame();
                    }
                    if (step[t].tool == 7) {          /* drag the box        */
                        e.type = PLAT_EV_MOUSE_DOWN;
                        e.mouse_x = o.x + CANX + 250; e.mouse_y = o.y + CANY + 150;
                        e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                        e.type = PLAT_EV_MOUSE_MOVE;
                        e.mouse_x = o.x + CANX + 390; e.mouse_y = o.y + CANY + 235;
                        plat_host_push_event(&e);
                        e.type = PLAT_EV_MOUSE_UP;
                        e.mouse_x = o.x + CANX + 390; e.mouse_y = o.y + CANY + 235;
                        e.buttons = 0; plat_host_push_event(&e);
                    } else if (step[t].tool == 2) {   /* sweep the airbrush  */
                        int k;
                        e.type = PLAT_EV_MOUSE_DOWN;
                        e.mouse_x = o.x + CANX + 30; e.mouse_y = o.y + CANY + 225;
                        e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                        for (k = 0; k < 12; k++) {
                            e.type = PLAT_EV_MOUSE_MOVE;
                            e.mouse_x = o.x + CANX + 30 + k * 14;
                            e.mouse_y = o.y + CANY + 225 - (k % 3) * 4;
                            plat_host_push_event(&e);
                        }
                        e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                        plat_host_push_event(&e);
                    } else {                          /* place the text      */
                        const char *msg = "CastaliaPaint";
                        int k;
                        e.type = PLAT_EV_MOUSE_DOWN;
                        e.mouse_x = o.x + CANX + 24; e.mouse_y = o.y + CANY + 16;
                        e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                        e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                        plat_host_push_event(&e);
                        sh_run_frame();
                        for (k = 0; msg[k] != '\0'; k++) {
                            PlatEvent ke; memset(&ke, 0, sizeof(ke));
                            ke.type = PLAT_EV_KEY_DOWN;
                            ke.key = (unsigned char)msg[k];
                            ke.ch = (unsigned char)msg[k];
                            plat_host_push_event(&ke);
                        }
                        { PlatEvent en; memset(&en, 0, sizeof(en));
                          en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                          en.ch = '\n'; plat_host_push_event(&en); }
                    }
                    sh_run_frame();
                }
            }
        }
        if (opt->net_demo) {
            /* Drive the real protocol code (net_stack.c) across the host's
             * SIMULATED wire (net_host.c): resolve an address with ARP, ping
             * the station that exists and one that does not, then bounce a
             * datagram off the UDP echo port. Nothing here fakes a reply --
             * every frame is built, sent, received and parsed for real. */
            NetStack ns;
            NetDeviceInfo di;
            NsRx rx;
            static cu8 frame[NET_MTU];
            cu8 mac[NET_MAC_LEN];
            char abuf[24];
            cu32 n;
            cu16 seq = 0;
            int got, replies = 0, i;
            const cu32 MY_IP = 0x0A00020FUL;     /* 10.0.2.15               */
            const cu32 GW_IP = 0x0A000202UL;     /* 10.0.2.2                */
            const cu32 ECHO_IP = 0x0A000203UL;   /* 10.0.2.3                */
            const cu32 DEAD_IP = 0x0A000263UL;   /* 10.0.2.99: nobody home  */

            net_device_info(&di);
            net_mac_format(di.mac, abuf, sizeof abuf);
            SYS_LOGI("main", "NET-DEMO: device '%s' mac %s available=%s",
                     di.name, abuf, net_available() ? "yes" : "no");
            ns_init(&ns, di.mac, MY_IP, 0xFFFFFF00UL, GW_IP);

            /* 1. Who has 10.0.2.2? */
            n = ns_build_arp_request(&ns, ns_next_hop(&ns, GW_IP),
                                     frame, sizeof frame);
            net_send_frame(frame, n);
            while ((got = net_poll_frame(frame, sizeof frame)) > 0) {
                ns_receive(&ns, frame, (cu32)got, &rx);
            }
            if (ns_arp_get(&ns, GW_IP, mac)) {
                net_mac_format(mac, abuf, sizeof abuf);
                SYS_LOGI("main", "NET-DEMO: ARP 10.0.2.2 is at %s (OK)", abuf);
            } else {
                SYS_LOGW("main", "NET-DEMO: ARP got no answer (MISMATCH)");
            }

            /* 2. Four echo requests, each matched to its own reply. */
            for (i = 0; i < 4; i++) {
                cu16 sent = 0;
                if (!ns_arp_get(&ns, GW_IP, mac)) { break; }
                n = ns_build_ping(&ns, GW_IP, mac, 32, &sent,
                                  frame, sizeof frame);
                net_send_frame(frame, n);
                while ((got = net_poll_frame(frame, sizeof frame)) > 0) {
                    if (ns_receive(&ns, frame, (cu32)got, &rx) ==
                            NS_RX_PING_REPLY && rx.seq == sent) {
                        replies++;
                    }
                }
                seq = sent;
            }
            SYS_LOGI("main", "NET-DEMO: ping 10.0.2.2 -- 4 sent, %d received, "
                             "last seq %d (%s)", replies, (int)seq,
                     (replies == 4) ? "OK" : "MISMATCH");

            /* 3. An address nobody answers for must simply stay silent. */
            replies = 0;
            n = ns_build_arp_request(&ns, DEAD_IP, frame, sizeof frame);
            net_send_frame(frame, n);
            while ((got = net_poll_frame(frame, sizeof frame)) > 0) {
                if (ns_receive(&ns, frame, (cu32)got, &rx) == NS_RX_ARP_REPLY) {
                    replies++;
                }
            }
            SYS_LOGI("main", "NET-DEMO: ARP 10.0.2.99 -- %d answers (%s)",
                     replies, (replies == 0) ? "OK" : "MISMATCH");

            /* 4. A datagram to the echo port comes back byte for byte. */
            {
                static const char msg[] = "castalia";
                int echoed = 0;
                n = ns_build_arp_request(&ns, ECHO_IP, frame, sizeof frame);
                net_send_frame(frame, n);
                while ((got = net_poll_frame(frame, sizeof frame)) > 0) {
                    ns_receive(&ns, frame, (cu32)got, &rx);
                }
                if (ns_arp_get(&ns, ECHO_IP, mac)) {
                    n = ns_build_udp(&ns, ECHO_IP, mac, 4096, 7, msg,
                                     (cu32)sizeof msg - 1, frame, sizeof frame);
                    net_send_frame(frame, n);
                    while ((got = net_poll_frame(frame, sizeof frame)) > 0) {
                        if (ns_receive(&ns, frame, (cu32)got, &rx) == NS_RX_UDP &&
                            rx.payload_len == (cu32)sizeof msg - 1 &&
                            rx.payload[0] == 'c' && rx.dport == 4096) {
                            echoed = 1;
                        }
                    }
                }
                SYS_LOGI("main", "NET-DEMO: UDP echo 10.0.2.3:7 -- %s (%s)",
                         echoed ? "returned" : "silent",
                         echoed ? "OK" : "MISMATCH");
            }
            SYS_LOGI("main", "NET-DEMO: %ld frames out, %ld in, %d ARP entries",
                     plat_host_net_tx(), plat_host_net_rx(),
                     ns_arp_count(&ns));
        }
        if (opt->net_ping_demo) {
            /* Open the Network window on the simulated wire and run a ping
             * from the UI: click Ping, type the address, then let the frames
             * flow so the replies land in the log and the ARP table fills. */
            WmWindow *nw;
            CPoint o;
            CRect pb;
            PlatEvent e;
            int k;
            const char *addr = "10.0.2.2";
            app_net_open(); sh_run_frame();
            nw = wm_focused();
            o = wm_client_origin(nw);
            memset(&e, 0, sizeof(e));
            /*
             * Ask the window where its Ping button is, rather than recomputing
             * it here. This used to read
             *   e.mouse_y = o.y + 8 + 58 + 8 + 58 + 8 + 27;
             * with a comment naming app_net.c's "two 58 px group boxes" -- so
             * when those group boxes had to grow (they were four pixels too
             * short for the fields inside them), the click landed on empty
             * background and the scene reported 0 frames out. It failed
             * loudly, which is the harness working; but a scene that copies a
             * layout constant will keep doing this.
             */
            if (!app_net_ping_button(nw, &pb)) {
                SYS_LOGI("main", "NET-PING-DEMO: no ping button (MISMATCH)");
            }
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + (pb.x0 + pb.x1) / 2;
            e.mouse_y = o.y + (pb.y0 + pb.y1) / 2;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            for (k = 0; k < 24; k++) {
                PlatEvent be; memset(&be, 0, sizeof(be));
                be.type = PLAT_EV_KEY_DOWN; be.key = PLAT_KEY_BACKSP;
                plat_host_push_event(&be);
            }
            for (k = 0; addr[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)addr[k]; ke.ch = (unsigned char)addr[k];
                plat_host_push_event(&ke);
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = '\n'; plat_host_push_event(&en); }
            for (k = 0; k < 40; k++) { sh_run_frame(); }
            SYS_LOGI("main", "NET-PING-DEMO: %ld frames out, %ld in (%s)",
                     plat_host_net_tx(), plat_host_net_rx(),
                     (plat_host_net_tx() > 0 && plat_host_net_rx() > 0)
                         ? "OK" : "MISMATCH");
        }
        if (opt->theme_demo) {
            /* Drive the Theme Editor: take a preset, repaint the accent from
             * the quick palette, apply it to the live desktop, save it, and
             * read the file back to prove what was saved is what was edited. */
            WmWindow *tw;
            CPoint o;
            CRect cr;
            PlatEvent e;
            ShTheme back;
            char path[CASTALIA_MAX_PATH];
            const char *home = sys_home();
            const char *fname = "DEMO.INI";
            int k, cwid, chgt, bw, qx, qw;

            app_theme_open(); sh_run_frame();
            tw = wm_focused();
            o = wm_client_origin(tw);
            cr = wm_client_rect(tw);
            cwid = crect_w(&cr);
            chgt = crect_h(&cr);
            bw = (cwid - 2 * 8 - 3 * 6) / 4;
            qx = 8 + 150 + 8;                 /* the right column starts here */
            qw = (cwid - qx - 8) / 14;        /* one quick-palette swatch     */
            memset(&e, 0, sizeof(e));

            /* Next Preset -> Storm. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + 8 + bw / 2; e.mouse_y = o.y + chgt - 18;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* The Accent slot is row 13 of the list. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + 60; e.mouse_y = o.y + 8 + 2 + 13 * 15 + 7;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* Row 1, column 5 of the quick palette is the strong red. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + qx + 5 * qw + qw / 2;
            e.mouse_y = o.y + 216 + 14 + 14 + 6;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* Apply, so the whole desktop repaints in the edited theme. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + 8 + (bw + 6) + bw / 2;
            e.mouse_y = o.y + chgt - 18;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            SYS_LOGI("main", "THEME-DEMO: applied '%s' accent #%06lX",
                     sh_theme_active()->name,
                     (unsigned long)(sh_theme_active()->ui.accent & 0xFFFFFFUL));

            /* Save As... -> the prompt. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + 8 + 2 * (bw + 6) + bw / 2;
            e.mouse_y = o.y + chgt - 18;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            for (k = 0; k < 24; k++) {
                PlatEvent be; memset(&be, 0, sizeof(be));
                be.type = PLAT_EV_KEY_DOWN; be.key = PLAT_KEY_BACKSP;
                plat_host_push_event(&be);
            }
            for (k = 0; fname[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)fname[k]; ke.ch = (unsigned char)fname[k];
                plat_host_push_event(&ke);
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = '\n'; plat_host_push_event(&en); }
            sh_run_frame();

            sys_snprintf(path, sizeof(path), "%s/THEMES/%s", home, fname);
            if (sh_theme_load(&back, path) == CE_OK) {
                SYS_LOGI("main", "THEME-DEMO: %s holds '%s' accent #%06lX (%s)",
                         path, back.name,
                         (unsigned long)(back.ui.accent & 0xFFFFFFUL),
                         (back.ui.accent == GFX_RGB(0xC0, 0x30, 0x30))
                             ? "OK" : "MISMATCH");
            } else {
                SYS_LOGW("main", "THEME-DEMO: could not read %s (MISMATCH)",
                         path);
            }
        }
        if (opt->filedlg_demo) {
            /* Raise the shared file dialog, walk the listing with the keyboard
             * and take a file: the callback is what proves the whole path --
             * the dialog resolves the folder, joins the name, and hands back
             * something that opens. The Viewer then reports what it got. */
            app_view_open(); sh_run_frame();
            {
                WmWindow *vw = wm_focused();
                CPoint o = wm_client_origin(vw);
                PlatEvent e;
                int k;
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + 30; e.mouse_y = o.y + 14;   /* Open button */
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
                for (k = 0; k < 7; k++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN; ke.key = PLAT_KEY_DOWN;
                    plat_host_push_event(&ke);
                    sh_run_frame();
                }
                if (!opt->filedlg_keep) {
                    PlatEvent en; memset(&en, 0, sizeof(en));
                    en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                    en.ch = '\n'; plat_host_push_event(&en);
                    sh_run_frame();
                    /* The dialog is gone and the Viewer is back on top with a
                     * file in it -- which is the whole point of the control. */
                    SYS_LOGI("main", "FILEDLG-DEMO: focus is '%s' after the "
                                     "pick (%s)",
                             (wm_focused() != NULL) ? wm_title(wm_focused())
                                                    : "(none)",
                             (wm_focused() == vw) ? "OK" : "MISMATCH");
                }
            }
        }
        if (opt->thumbs_demo) {
            /* Open the File Manager and cycle it into the Icons view (the
             * "View" word in its menu bar), then run frames: the thumbnail
             * cache decodes a few images per frame and asks for a repaint
             * until the visible ones are all in. */
            WmWindow *fw;
            CPoint o;
            PlatEvent e;
            int k, mag = 0;
            CRect body;
            /*
             * A bitmap the scene AUTHORS, in a colour nothing else on the
             * desktop uses.
             *
             * The claim is "a picture file is shown as a picture of itself",
             * and the only way to check that without knowing what the
             * shipped test image looks like is to put a known one in the
             * folder and look for its colour. This scene used to cycle the
             * view, run twenty frames and assert nothing -- so a thumbnail
             * cache that had stopped decoding, and fell back to the generic
             * file icon, reported exactly what a working one reported.
             */
            {
                GfxSurface *tp = gfx_surface_new(48, 36);
                if (tp != NULL) {
                    gfx_clear(tp, GFX_RGB(0xE0, 0x18, 0xC8));   /* magenta */
                    gfx_bmp_save(tp, "THUMB.BMP");
                    gfx_surface_free(tp);
                }
            }
            app_fileman_open();
            for (k = 0; k < 16; k++) { sh_run_frame(); }
            fw = wm_focused(); o = wm_client_origin(fw);
            memset(&e, 0, sizeof(e));
            /* "View" is the third word: File and Edit are 4 chars plus the
             * 14 px gap the menu bar puts between words. */
            for (k = 0; k < 2; k++) {          /* Details -> Tiles -> Icons */
                e.type = PLAT_EV_MOUSE_DOWN;
                e.mouse_x = o.x + 4 + 2 * (4 * 6 + 14) + 12;
                e.mouse_y = o.y + 8;
                e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
            }
            for (k = 0; k < 30; k++) { sh_run_frame(); }
            {
                CRect c = wm_client_rect(fw);
                GfxSurface *bb = plat_backbuffer();
                int x, y;
                /* The file area: below the toolbar and address strip, above
                 * the status line. */
                body = crect_make(c.x0 + 4, c.y0 + 76, crect_w(&c) - 8,
                                  crect_h(&c) - 100);
                for (y = body.y0; y < body.y1; y++) {
                    for (x = body.x0; x < body.x1; x++) {
                        CColor col = gfx_get_pixel(bb, x, y);
                        if (GFX_R(col) > 160 && GFX_B(col) > 140 &&
                            GFX_G(col) < 90) { mag++; }
                    }
                }
            }
            SYS_LOGI("main", "THUMBS-DEMO: the Icons view shows a bitmap as a "
                     "picture of itself (%d px of its own colour) (%s)", mag,
                     (mag > 200) ? "OK" : "MISMATCH");
        }
        if (opt->menuaccel_demo) {
            /*
             * A menu whose items carry keyboard shortcuts.
             *
             * The label format is "Undo\tCtrl+Z", and until now nothing in
             * the menu knew what the tab meant. It went straight to
             * gfx_draw_text, where 0x09 is below the font's first code point
             * and comes out as the MISSING-GLYPH BOX -- so Paint's Edit menu
             * has been reading "Undo[box]Ctrl+Z" for as long as it has had
             * shortcuts. Nothing failed. Nothing could fail: no check in this
             * tree looks at a menu's pixels, and a drawing bug announces
             * itself only by being looked at.
             *
             * Two things are checked, because the fix has two halves that can
             * each be wrong on their own:
             *
             *   - the menu is WIDER than the labels alone would need, which
             *     is the only way an accelerator column can exist at all;
             *   - the shortcut ends INSIDE the menu. Right-aligning text
             *     against a box that was measured without it is how the
             *     accelerator would have run out over the desktop.
             */
            WmWindow *pw;
            CRect word;
            CPoint o;
            PlatEvent e;
            int k;

            app_paint_open();
            for (k = 0; k < 20; k++) { sh_run_frame(); }
            pw = wm_focused();
            o = wm_client_origin(pw);
            /* Click "Edit" -- the rect comes from the app itself, not from
             * menu-bar arithmetic repeated out here where it could drift. */
            word = app_paint_menu_word(pw, 1);
            memset(&e, 0, sizeof(e));
            e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
            e.mouse_x = o.x + (word.x0 + word.x1) / 2;
            e.mouse_y = o.y + (word.y0 + word.y1) / 2;
            plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
            plat_host_push_event(&e);
            for (k = 0; k < 4; k++) { sh_run_frame(); }

            /*
             * The width logic is pure and lives in the unit tests. What no
             * unit test can see is whether the accelerator was actually
             * PAINTED: a fix that measured the column and then forgot to
             * draw into it would leave every check green and every menu
             * blank on the right. So the pixels get looked at.
             *
             * The strip scanned is the accelerator column itself -- the
             * right-hand end of the drop-down, inside the padding. Any ink
             * there is a shortcut, because nothing else in a menu row reaches
             * that far.
             */
            {
                GfxSurface *bb = plat_backbuffer();
                int mw = app_paint_menu_width(pw);
                int mx = o.x + word.x0, my = o.y + OF_MENUBAR_H;
                int ink = 0, xx, yy;
                CColor body = 0;
                /*
                 * ONE item's row band -- the first, "Undo\tCtrl+Z" -- and
                 * not the whole menu.
                 *
                 * The first version scanned a hundred rows and counted 108
                 * "ink" pixels with the accelerators switched off entirely,
                 * so it passed against the exact bug it was written for. The
                 * ink was the SEPARATOR lines, which run the full width of
                 * the menu and straight through the shortcut column. Staying
                 * inside a single text row leaves nothing in the band that
                 * is not a shortcut.
                 */
                if (mw > 0 && bb != NULL) {
                    body = gfx_get_pixel(bb, mx + mw - 10, my + 4);
                    for (yy = my + 4; yy < my + 18; yy++) {
                        for (xx = mx + mw - 56; xx < mx + mw - 8; xx++) {
                            if (gfx_get_pixel(bb, xx, yy) != body) { ink++; }
                        }
                    }
                }
                SYS_LOGI("main", "MENUACCEL-DEMO: %dpx menu, %d ink pixels in "
                         "the shortcut column (%s)", mw, ink,
                         (mw > 0 && ink > 20) ? "OK" : "MISMATCH");
            }
            while (wm_window_count() > 0) {
                WmWindow *w2 = wm_window_at(0);
                if (w2 == NULL) { break; }
                wm_destroy(w2);
            }
            for (k = 0; k < 3; k++) { sh_run_frame(); }

            /*
             * ...and the menu as a way IN, not only as a label.
             *
             * Notepad had no menu bar at all until now -- five toolbar
             * buttons and nothing else -- so its ten keyboard shortcuts were
             * undiscoverable: nothing in the program said Ctrl+Z would undo.
             * The check drives the new menu the way somebody who does not
             * know the shortcuts has to: click "Edit", click "Select All",
             * and see the whole document selected.
             */
            {
                WmWindow *nw;
                CRect w2;
                PlatEvent me;
                CPoint no;
                int mx, my, before, after;
                app_notepad_open();
                for (k = 0; k < 20; k++) { sh_run_frame(); }
                nw = wm_focused();
                no = wm_client_origin(nw);
                {
                    const char *t = "hello";
                    int j;
                    for (j = 0; t[j] != '\0'; j++) {
                        PlatEvent ke; memset(&ke, 0, sizeof(ke));
                        ke.type = PLAT_EV_KEY_DOWN;
                        ke.key = (unsigned char)t[j];
                        ke.ch  = (unsigned char)t[j];
                        plat_host_push_event(&ke);
                    }
                    sh_run_frame();
                }
                before = (int)strlen(app_notepad_text(nw));
                w2 = app_notepad_menu_word(nw, 1);      /* Edit */
                memset(&me, 0, sizeof(me));
                me.type = PLAT_EV_MOUSE_DOWN; me.buttons = PLAT_MB_LEFT;
                me.mouse_x = no.x + (w2.x0 + w2.x1) / 2;
                me.mouse_y = no.y + (w2.y0 + w2.y1) / 2;
                plat_host_push_event(&me);
                me.type = PLAT_EV_MOUSE_UP; me.buttons = 0;
                plat_host_push_event(&me);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
                /*
                 * Hover down the Edit menu until the row under the pointer is
                 * "Select All", then click it.
                 *
                 * This used to compute a pixel from a row count, and its own
                 * comment called Select All "the last row of the Edit menu" --
                 * which it is not, Time/Date sits below it. Counting also has
                 * to guess each separator's height. That is the arithmetic
                 * that made cz-demo click Decompress when a menu gained one
                 * entry, so it is gone from here too.
                 */
                mx = no.x + w2.x0 + 30;
                my = no.y + w2.y1 + 3 + 8;
                {
                    int row, found = -1;
                    for (row = 0; row < 16 && found < 0; row++) {
                        const char *lab;
                        int ry = my + row * 16;
                        memset(&me, 0, sizeof(me));
                        me.type = PLAT_EV_MOUSE_MOVE;
                        me.mouse_x = mx; me.mouse_y = ry;
                        plat_host_set_mouse(mx, ry, 0);
                        plat_host_push_event(&me);
                        sh_run_frame();
                        lab = app_notepad_menu_item(nw);
                        if (lab != NULL && demo_contains(lab, "Select All")) {
                            found = ry;
                        }
                    }
                    SYS_LOGI("main", "MENUACCEL-DEMO: the pointer found Select "
                             "All on the Edit menu (%s)",
                             (found >= 0) ? "OK" : "MISMATCH");
                    my = (found >= 0) ? found : my;
                }
                memset(&me, 0, sizeof(me));
                me.type = PLAT_EV_MOUSE_DOWN; me.buttons = PLAT_MB_LEFT;
                me.mouse_x = mx; me.mouse_y = my;
                plat_host_push_event(&me);
                me.type = PLAT_EV_MOUSE_UP; me.buttons = 0;
                plat_host_push_event(&me);
                for (k = 0; k < 3; k++) { sh_run_frame(); }
                after = app_notepad_sel_len(nw);
                /*
                 * ...and sliding along the BAR with a menu open switches to
                 * the one you slide onto. Without it you have to close the
                 * menu and click again, which no menu of this era did.
                 * CastaliaWrite and CastaliaSheet had it; Notepad and Paint
                 * did not, and nothing said so because a menu that stays put
                 * is not an error.
                 */
                {
                    CRect w0 = app_notepad_menu_word(nw, 0);   /* File   */
                    CRect w2b = app_notepad_menu_word(nw, 2);  /* Search */
                    const char *lab;
                    int slid = 0;
                    memset(&me, 0, sizeof(me));
                    me.type = PLAT_EV_MOUSE_DOWN; me.buttons = PLAT_MB_LEFT;
                    me.mouse_x = no.x + (w0.x0 + w0.x1) / 2;
                    me.mouse_y = no.y + (w0.y0 + w0.y1) / 2;
                    plat_host_push_event(&me);
                    me.type = PLAT_EV_MOUSE_UP; me.buttons = 0;
                    plat_host_push_event(&me);
                    for (k = 0; k < 3; k++) { sh_run_frame(); }
                    /* Slide right onto Search WITHOUT clicking. */
                    memset(&me, 0, sizeof(me));
                    me.type = PLAT_EV_MOUSE_MOVE;
                    me.mouse_x = no.x + (w2b.x0 + w2b.x1) / 2;
                    me.mouse_y = no.y + (w2b.y0 + w2b.y1) / 2;
                    plat_host_set_mouse(me.mouse_x, me.mouse_y, 0);
                    plat_host_push_event(&me);
                    sh_run_frame();
                    /* Now hover the first row: it must be Search's, not
                     * File's -- checking by NAME, since both menus are open
                     * menus and only their contents tell them apart. */
                    memset(&me, 0, sizeof(me));
                    me.type = PLAT_EV_MOUSE_MOVE;
                    me.mouse_x = no.x + w2b.x0 + 30;
                    me.mouse_y = no.y + w2b.y1 + 3 + 8;
                    plat_host_set_mouse(me.mouse_x, me.mouse_y, 0);
                    plat_host_push_event(&me);
                    sh_run_frame();
                    lab = app_notepad_menu_item(nw);
                    if (lab != NULL && demo_contains(lab, "Find")) { slid = 1; }
                    SYS_LOGI("main", "MENUACCEL-DEMO: sliding from File onto "
                             "Search opens Search (first row \"%s\") (%s)",
                             lab ? lab : "(none)", slid ? "OK" : "MISMATCH");
                    feed_key(PLAT_KEY_ESC);
                }
                SYS_LOGI("main", "MENUACCEL-DEMO: Edit > Select All selected "
                         "%d of %d chars (%s)", after, before,
                         (before > 0 && after == before) ? "OK" : "MISMATCH");
                while (wm_window_count() > 0) {
                    WmWindow *w3 = wm_window_at(0);
                    if (w3 == NULL) { break; }
                    wm_destroy(w3);
                }
                for (k = 0; k < 3; k++) { sh_run_frame(); }
            }

            /*
             * ...and the same menu with NO MOUSE AT ALL.
             *
             * This is the case that matters on the target: the DOS mouse is a
             * loadable driver, and a machine booted without one could not
             * reach Paint's Image menu, Write's alignment or Notepad's Word
             * Wrap by any means -- those commands have no shortcut, and the
             * menus opened on click alone. The launcher and the File Manager
             * had been keyboard-complete for months; the apps had not.
             *
             * F10, then Right to Edit, then Down to the first live row, then
             * Enter. Every key, no pointer.
             */
            {
                WmWindow *nw;
                int j, sel;
                app_notepad_open();
                for (k = 0; k < 20; k++) { sh_run_frame(); }
                nw = wm_focused();
                { const char *t = "abcd";
                  for (j = 0; t[j] != '\0'; j++) {
                      PlatEvent ke; memset(&ke, 0, sizeof(ke));
                      ke.type = PLAT_EV_KEY_DOWN;
                      ke.key = (unsigned char)t[j];
                      ke.ch  = (unsigned char)t[j];
                      plat_host_push_event(&ke);
                  }
                  sh_run_frame(); }
                feed_key(PLAT_KEY_F10);     /* the bar opens on File     */
                sh_run_frame();
                feed_key(PLAT_KEY_RIGHT);   /* ...along to Edit          */
                sh_run_frame();
                /*
                 * End, then Up: the last live row of the Edit menu is
                 * Time/Date and "Select All" sits just above it.
                 *
                 * The first version of this counted Downs from the top and
                 * assumed Undo would be greyed -- but the scene types before
                 * it navigates, so Undo is live and one Down landed on it.
                 * The check then undid a character and reported no selection,
                 * which is a MISMATCH for a reason that has nothing to do
                 * with keyboard menus. Anchoring at the END makes the route
                 * independent of what the document happens to allow.
                 */
                feed_key(PLAT_KEY_END);
                sh_run_frame();
                feed_key(PLAT_KEY_UP);
                sh_run_frame();
                feed_key(PLAT_KEY_ENTER);
                sh_run_frame();
                sel = app_notepad_sel_len(nw);
                SYS_LOGI("main", "MENUACCEL-DEMO: F10 menu, no mouse, selected"
                         " %d of 4 chars (%s)", sel,
                         (sel == 4) ? "OK" : "MISMATCH");
                while (wm_window_count() > 0) {
                    WmWindow *w4 = wm_window_at(0);
                    if (w4 == NULL) { break; }
                    wm_destroy(w4);
                }
                for (k = 0; k < 3; k++) { sh_run_frame(); }
            }
        }
        if (opt->undo_demo) {
            /*
             * Ctrl+Z in Notepad, driven through the real key path.
             *
             * Every check here is against the exact text, never against "the
             * length changed". An undo stack that restores the right NUMBER
             * of characters and the wrong characters leaves a document that
             * still opens, still saves and still looks like prose -- the
             * author finds out much later, if at all.
             */
            WmWindow *nw;
            const char *msg = "hello world";
            int k;
            app_notepad_open();
            sh_run_frame();
            nw = wm_focused();
            for (k = 0; msg[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke));
                ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)msg[k];
                ke.ch  = (unsigned char)msg[k];
                plat_host_push_event(&ke);
            }
            sh_run_frame();
            SYS_LOGI("main", "UNDO-DEMO: typed '%s' (%s)",
                     app_notepad_text(nw),
                     (strcmp(app_notepad_text(nw), "hello world") == 0)
                         ? "OK" : "MISMATCH");
            /* Five undos take back exactly five characters -- "world" -> "" */
            for (k = 0; k < 5; k++) {
                PlatEvent ue; memset(&ue, 0, sizeof(ue));
                ue.type = PLAT_EV_KEY_DOWN; ue.key = 26; ue.ch = 26;
                plat_host_push_event(&ue);
                sh_run_frame();
            }
            SYS_LOGI("main", "UNDO-DEMO: five undos leave '%s' (%s)",
                     app_notepad_text(nw),
                     (strcmp(app_notepad_text(nw), "hello ") == 0)
                         ? "OK" : "MISMATCH");
            /* ...and five redos put back exactly what was taken. */
            for (k = 0; k < 5; k++) {
                PlatEvent re; memset(&re, 0, sizeof(re));
                re.type = PLAT_EV_KEY_DOWN; re.key = 25; re.ch = 25;
                plat_host_push_event(&re);
                sh_run_frame();
            }
            SYS_LOGI("main", "UNDO-DEMO: five redos restore '%s' (%s)",
                     app_notepad_text(nw),
                     (strcmp(app_notepad_text(nw), "hello world") == 0)
                         ? "OK" : "MISMATCH");
            while (wm_window_count() > 0) {
                WmWindow *w = wm_window_at(0);
                if (w == NULL) { break; }
                wm_destroy(w);
            }
            for (k = 0; k < 3; k++) { sh_run_frame(); }

            /*
             * ...and the spreadsheet, whose edits are cells rather than
             * characters. A sheet is the worst place for a silent undo bug:
             * every formula recalculates, so a wrong restore spreads through
             * the book and the result is a page of plausible numbers.
             */
            {
                WmWindow *sw;
                const char *typed = "42";
                char was[64];
                int j;
                app_sheet_open();
                sh_run_frame();
                sw = wm_focused();
                /*
                 * Whatever the cursor happens to be on, remembered BEFORE the
                 * edit. The first version of this assumed A1 and the sample
                 * book's "Item" -- the cursor actually opens on D10, and
                 * Enter then moves it down, so the check was reading a cell
                 * the edit never touched. Capturing the real value first
                 * makes the check say what it means: undo puts back exactly
                 * what was there.
                 */
                sys_strlcpy(was, app_sheet_cur_raw(sw), sizeof was);
                for (j = 0; typed[j] != '\0'; j++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN;
                    ke.key = (unsigned char)typed[j];
                    ke.ch  = (unsigned char)typed[j];
                    plat_host_push_event(&ke);
                }
                { PlatEvent en; memset(&en, 0, sizeof(en));
                  en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                  en.ch = '\n'; plat_host_push_event(&en); }
                sh_run_frame();
                { PlatEvent ue; memset(&ue, 0, sizeof(ue));
                  ue.type = PLAT_EV_KEY_DOWN; ue.key = 26; ue.ch = 26;
                  plat_host_push_event(&ue); }
                sh_run_frame();
                /* Exactly what the cell held, not merely something. A
                 * location-only record would leave it empty here and the book
                 * would still recalculate and still print. */
                SYS_LOGI("main", "UNDO-DEMO: sheet undo put '%s' back where "
                         "'%s' had been (%s)", app_sheet_cur_raw(sw), was,
                         (strcmp(app_sheet_cur_raw(sw), was) == 0)
                             ? "OK" : "MISMATCH");
                /*
                 * ...and again with Ctrl+Y delivered the way a HOST backend
                 * encodes it: the letter plus a CTRL modifier, rather than
                 * the ASCII control code DOS sends.
                 *
                 * This seam is why the check exists. Every Ctrl handler in
                 * this tree has to answer both encodings, and one that tests
                 * only the control code answers half the contract: it works
                 * on the target and ignores host-encoded input entirely.
                 * Two of the four editors had exactly that defect.
                 */
                { PlatEvent ye; memset(&ye, 0, sizeof(ye));
                  ye.type = PLAT_EV_KEY_DOWN; ye.key = 'y'; ye.ch = 'y';
                  ye.mods = PLAT_MOD_CTRL; plat_host_push_event(&ye); }
                sh_run_frame();
                SYS_LOGI("main", "UNDO-DEMO: host-encoded Ctrl+Y redid to "
                         "'%s', want '%s' (%s)", app_sheet_cur_raw(sw), typed,
                         (strcmp(app_sheet_cur_raw(sw), typed) == 0)
                             ? "OK" : "MISMATCH");
                while (wm_window_count() > 0) {
                    WmWindow *w = wm_window_at(0);
                    if (w == NULL) { break; }
                    wm_destroy(w);
                }
                for (j = 0; j < 3; j++) { sh_run_frame(); }
            }

            /*
             * ...and CastaliaWrite, where a character carries emphasis. This
             * is the case the flat stack cannot serve: restoring the WORDS
             * without their bold is a partial undo that reads as a complete
             * one, so the check looks at the attribute byte and not only the
             * text.
             */
            {
                WmWindow *ww;
                int j, before_attr;
                app_write_open();
                sh_run_frame();
                ww = wm_focused();
                /*
                 * Ctrl+B FIRST, so the character carries an attribute worth
                 * losing. The earlier version of this scene typed a plain
                 * character, and "the attribute came back" was 0 == 0 -- a
                 * check that would have passed just as cheerfully if undo
                 * dropped formatting entirely, which is the one bug this
                 * whole stack exists to prevent.
                 */
                { PlatEvent be; memset(&be, 0, sizeof(be));
                  be.type = PLAT_EV_KEY_DOWN; be.key = 2; be.ch = 2;
                  plat_host_push_event(&be); }
                sh_run_frame();
                { PlatEvent ke; memset(&ke, 0, sizeof(ke));
                  ke.type = PLAT_EV_KEY_DOWN; ke.key = 'Q'; ke.ch = 'Q';
                  plat_host_push_event(&ke); }
                sh_run_frame();
                before_attr = app_write_attr_at(ww, 0);
                /* Non-zero, and specifically bold: "some attribute" would let
                 * a stack that stored a constant through. */
                SYS_LOGI("main", "UNDO-DEMO: write typed bold, first char attr "
                         "%d (%s)", before_attr,
                         (before_attr == 1) ? "OK" : "MISMATCH");
                { PlatEvent ue; memset(&ue, 0, sizeof(ue));
                  ue.type = PLAT_EV_KEY_DOWN; ue.key = 26; ue.ch = 26;
                  plat_host_push_event(&ue); }
                sh_run_frame();
                { PlatEvent re; memset(&re, 0, sizeof(re));
                  re.type = PLAT_EV_KEY_DOWN; re.key = 25; re.ch = 25;
                  plat_host_push_event(&re); }
                sh_run_frame();
                /* Redone: the character AND its bold must be back. The text
                 * is checked too -- an undo that restores the emphasis onto
                 * the wrong letter is no better than losing it. */
                SYS_LOGI("main", "UNDO-DEMO: write redo restored '%c' attr %d,"
                         " was 'Q' attr %d (%s)", app_write_text(ww)[0],
                         app_write_attr_at(ww, 0), before_attr,
                         (app_write_attr_at(ww, 0) == before_attr &&
                          app_write_text(ww)[0] == 'Q')
                             ? "OK" : "MISMATCH");
                /*
                 * Help has always listed these keystrokes; until now not one
                 * of them was wired to anything, and a reader who tried them
                 * would have concluded the keyboard was broken rather than
                 * the manual. So the manual's claims get checked.
                 *
                 * Ctrl+U on top of the bold that is still armed: 0x01|0x02.
                 * Testing it alone would pass against a handler that assigned
                 * the attribute instead of toggling one bit into it.
                 */
                { PlatEvent xe; memset(&xe, 0, sizeof(xe));
                  xe.type = PLAT_EV_KEY_DOWN; xe.key = 21; xe.ch = 21;
                  plat_host_push_event(&xe); }
                sh_run_frame();
                { PlatEvent ke; memset(&ke, 0, sizeof(ke));
                  ke.type = PLAT_EV_KEY_DOWN; ke.key = 'R'; ke.ch = 'R';
                  plat_host_push_event(&ke); }
                sh_run_frame();
                SYS_LOGI("main", "UNDO-DEMO: Ctrl+U over Ctrl+B gives attr %d,"
                         " want 3 (%s)", app_write_attr_at(ww, 1),
                         (app_write_attr_at(ww, 1) == 3) ? "OK" : "MISMATCH");
                /* ...and Ctrl+O reaches the same command the menu does. A
                 * dialog on screen is the only externally visible proof that
                 * the keystroke was routed rather than swallowed. */
                {
                    int nw_before = wm_window_count();
                    PlatEvent oe; memset(&oe, 0, sizeof(oe));
                    oe.type = PLAT_EV_KEY_DOWN; oe.key = 15; oe.ch = 15;
                    plat_host_push_event(&oe);
                    sh_run_frame();
                    SYS_LOGI("main", "UNDO-DEMO: Ctrl+O opened %d window(s) "
                             "(%s)", wm_window_count() - nw_before,
                             (wm_window_count() > nw_before)
                                 ? "OK" : "MISMATCH");
                }
                while (wm_window_count() > 0) {
                    WmWindow *w = wm_window_at(0);
                    if (w == NULL) { break; }
                    wm_destroy(w);
                }
                for (j = 0; j < 3; j++) { sh_run_frame(); }
            }
        }
        if (opt->winicon_demo) {
            /*
             * Every app window must carry an icon in its title bar and on its
             * taskbar button.
             *
             * The mapping is a table of title fragments, and a table like that
             * rots silently: an entry whose window was renamed stops matching
             * and the icon simply is not there, which nobody reports. Two were
             * already dead -- "Paint" against a window called "CastaliaPaint",
             * and the office apps, whose titles put the document first
             * ("Book1 - CastaliaSheet") where no prefix could ever reach. A
             * dozen more apps had no entry at all, Reversi among them, added
             * in this same session.
             *
             * So the check opens one of each and asks the WM what it got.
             */
            static const struct { const char *name; void (*open)(void); } APPS[] = {
                { "File Manager",   app_fileman_open   },
                { "Notepad",        app_notepad_open   },
                { "CastaliaPaint",  app_paint_open     },
                { "CastaliaSheet",  app_sheet_open     },
                { "CastaliaWrite",  app_write_open     },
                { "Calculator",     app_calc_open      },
                { "Media Player",   app_media_open     },
                { "Character Map",  app_charmap_open   },
                { "Theme Editor",   app_theme_open     },
                { "Network",        app_net_open       },
                { "Console",        app_console_open   },
                { "Clock",          app_clock_open     },
                { "Disk Usage",     app_diskuse_open   },
                { "Task Manager",   app_taskman_open   },
                { "Mines",          app_mines_open     },
                { "Solitaire",      app_solitaire_open },
                { "FreeCell",       app_freecell_open  },
                { "Reversi",        app_reversi_open   },
                { "Help",           app_help_open      },
                { "Viewer",         app_view_open      }
            };
            int n = (int)(sizeof(APPS) / sizeof(APPS[0]));
            int i, missing = 0;
            char first[64];
            first[0] = '\0';
            for (i = 0; i < n; i++) {
                WmWindow *w;
                int f;
                APPS[i].open();
                for (f = 0; f < 6; f++) { sh_run_frame(); }
                w = wm_focused();
                if (w == NULL || wm_icon(w) == NULL) {
                    missing++;
                    if (first[0] == '\0') {
                        sys_strlcpy(first, APPS[i].name, sizeof first);
                    }
                }
                while (wm_window_count() > 0) {
                    WmWindow *d = wm_window_at(0);
                    if (d == NULL) { break; }
                    wm_destroy(d);
                }
                for (f = 0; f < 3; f++) { sh_run_frame(); }
            }
            SYS_LOGI("main", "WINICON-DEMO: %d of %d app windows have no icon"
                     "%s%s (%s)", missing, n,
                     (missing > 0) ? ", first: " : "", first,
                     (missing == 0) ? "OK" : "MISMATCH");
        }
        if (opt->reversi_demo) {
            /*
             * Reversi, played through the window.
             *
             * Every check here is about something a screenshot cannot see. A
             * board of black and white discs looks exactly the same whether
             * the rules are right or wrong: a lost disc, an accepted illegal
             * move, a flip that ran the wrong way -- all of them draw as an
             * ordinary position, and the player concludes they misunderstood
             * the game.
             */
            WmWindow *rw;
            CRect cell;
            CPoint ro;
            PlatEvent e;
            int k, before, after, turn0;

            app_reversi_open();
            for (k = 0; k < 20; k++) { sh_run_frame(); }
            rw = wm_focused();
            ro = wm_client_origin(rw);
            before = app_reversi_discs(rw, 0);
            turn0 = app_reversi_turn(rw);
            SYS_LOGI("main", "REVERSI-DEMO: opened with %d discs, %s to move "
                     "(%s)", before,
                     (turn0 == 1) ? "black" : "white",
                     (before == 4 && turn0 == 1) ? "OK" : "MISMATCH");

            /*
             * An ILLEGAL move first, and the board must not move at all.
             * (2,2) is diagonally out from the opening square and turns
             * nothing over. A program that accepts it is playing a different
             * game, and nothing on screen would say so.
             */
            if (app_reversi_cell_rect(rw, 2, 2, &cell)) {
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
                e.mouse_x = ro.x + (cell.x0 + cell.x1) / 2;
                e.mouse_y = ro.y + (cell.y0 + cell.y1) / 2;
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
            }
            SYS_LOGI("main", "REVERSI-DEMO: illegal move left %d discs and "
                     "%s still to move (%s)", app_reversi_discs(rw, 0),
                     (app_reversi_turn(rw) == 1) ? "black" : "white",
                     (app_reversi_discs(rw, 0) == 4 &&
                      app_reversi_turn(rw) == 1) ? "OK" : "MISMATCH");

            /*
             * ...then a legal one. (2,3) turns exactly one white disc, so
             * black gains two: the disc played and the disc turned. Checking
             * the TOTAL as well catches the other direction -- a flip that
             * removes a disc instead of turning it leaves a board that still
             * looks like a game of Reversi.
             */
            if (app_reversi_cell_rect(rw, 2, 3, &cell)) {
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
                e.mouse_x = ro.x + (cell.x0 + cell.x1) / 2;
                e.mouse_y = ro.y + (cell.y0 + cell.y1) / 2;
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
            }
            after = app_reversi_discs(rw, 0);
            SYS_LOGI("main", "REVERSI-DEMO: black played, %d discs of which "
                     "%d black (%s)", after, app_reversi_discs(rw, 1),
                     (after == 5 && app_reversi_discs(rw, 1) == 4)
                         ? "OK" : "MISMATCH");

            /* White answers on the timer tick, and the count grows by one
             * again -- one disc placed, the rest turned over, never lost. */
            for (k = 0; k < 40; k++) { sh_run_frame(); }
            SYS_LOGI("main", "REVERSI-DEMO: white answered, %d discs, black "
                     "to move again (%s)", app_reversi_discs(rw, 0),
                     (app_reversi_discs(rw, 0) == 6 &&
                      app_reversi_turn(rw) == 1) ? "OK" : "MISMATCH");

            /*
             * The HINT KEY, driven as a reader of Help will drive it.
             *
             * Help says "H offers the move it would make in your place", and
             * a documented key that does nothing is the exact defect this
             * tree spent two commits removing from CastaliaWrite. Asking
             * app_reversi_hint whether it has an answer is a different
             * question from whether pressing H does anything, and the scene
             * had only been asking the first one -- about a key added in the
             * same hour.
             *
             * Checked by its observable effects: the cursor lands on a LEGAL
             * move, and the status line says so.
             */
            {
                int hr = -1, hc = -1, cr = -1, ccx = -1;
                PlatEvent he;
                app_reversi_hint(rw, &hr, &hc);
                /* Park the cursor somewhere it certainly is not. */
                for (k = 0; k < 7; k++) { feed_key(PLAT_KEY_UP); }
                for (k = 0; k < 7; k++) { feed_key(PLAT_KEY_LEFT); }
                memset(&he, 0, sizeof(he));
                he.type = PLAT_EV_KEY_DOWN; he.key = 'h'; he.ch = 'h';
                plat_host_push_event(&he);
                sh_run_frame();
                app_reversi_cursor(rw, &cr, &ccx);
                SYS_LOGI("main", "REVERSI-DEMO: H moved the cursor to %d,%d "
                         "(hint %d,%d) and said '%s' (%s)", cr, ccx, hr, hc,
                         app_reversi_status(rw),
                         (cr == hr && ccx == hc && hr >= 0 &&
                          app_reversi_status(rw)[0] == 'H')
                             ? "OK" : "MISMATCH");
            }

            /*
             * ...and now the whole game, played out through the window with
             * the Hint key's own judgement on both sides.
             *
             * This is the check that can catch a HANG, and a hang is the
             * classic Reversi bug: a position where neither side can move is
             * over WITH EMPTY SQUARES LEFT, and a program that waits for
             * sixty-four stones sits on a finished game forever. A scene
             * that plays two moves cannot see that; only playing to the end
             * can. The loop is bounded, so a game that will not finish
             * reports it rather than hanging the harness too.
             *
             * Conservation is checked on every single move as well: the disc
             * count must rise by exactly one each time and never fall. A flip
             * that loses a disc draws as a perfectly ordinary board.
             */
            {
                int moves = 0, prev = app_reversi_discs(rw, 0);
                int bad_step = 0, hr = 0, hc = 0;
                while (!app_reversi_over(rw) && moves < 80) {
                    if (app_reversi_turn(rw) == 1) {      /* black: us */
                        int now;
                        if (!app_reversi_hint(rw, &hr, &hc)) { break; }
                        if (app_reversi_cell_rect(rw, hr, hc, &cell)) {
                            memset(&e, 0, sizeof(e));
                            e.type = PLAT_EV_MOUSE_DOWN;
                            e.buttons = PLAT_MB_LEFT;
                            e.mouse_x = ro.x + (cell.x0 + cell.x1) / 2;
                            e.mouse_y = ro.y + (cell.y0 + cell.y1) / 2;
                            plat_host_push_event(&e);
                            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                            plat_host_push_event(&e);
                            sh_run_frame();
                        }
                        now = app_reversi_discs(rw, 0);
                        if (now != prev + 1) { bad_step++; }
                        prev = now;
                        moves++;
                    } else {
                        /* White thinks for a few frames; let it. */
                        int spin = 0, now;
                        while (app_reversi_turn(rw) != 1 &&
                               !app_reversi_over(rw) && spin < 12) {
                            sh_run_frame();
                            spin++;
                        }
                        now = app_reversi_discs(rw, 0);
                        if (now < prev) { bad_step++; }
                        prev = now;
                        moves++;
                    }
                }
                /*
                 * "It ended" is not enough, and the first version of this
                 * check asked only that. Deleting the turn hand-off makes
                 * the pass counter reach two immediately, so the game was
                 * declared over after ONE turn with six discs -- and the
                 * check said OK. A game that ends instantly is exactly the
                 * failure this was written to catch.
                 *
                 * So it must also have been PLAYED. Both AIs are
                 * deterministic, so a real game here is 58 turns and a full
                 * board every time; the floors are set far below that and
                 * far above a broken one.
                 */
                SYS_LOGI("main", "REVERSI-DEMO: full game ended after %d "
                         "turns with %d discs, over=%d, %d bad step(s) (%s)",
                         moves, app_reversi_discs(rw, 0),
                         app_reversi_over(rw) ? 1 : 0, bad_step,
                         (app_reversi_over(rw) && moves < 80 && moves >= 20 &&
                          bad_step == 0 &&
                          app_reversi_discs(rw, 0) >= 30 &&
                          app_reversi_discs(rw, 0) <= 64) ? "OK" : "MISMATCH");
            }
            for (k = 0; k < 3; k++) { sh_run_frame(); }
        }
        if (opt->freecell_demo) {
            /*
             * FreeCell, played through the window rather than the rules.
             *
             * The check that matters for any card game is CONSERVATION: after
             * a move there must still be exactly fifty-two cards on the
             * table. A move that loses one, or leaves a copy behind, draws a
             * board that looks completely normal -- the player only finds out
             * when the game turns out to be unwinnable, which they will
             * reasonably blame on themselves.
             */
            WmWindow *fw;
            CRect from, to;
            PlatEvent e;
            int before, after, k;
            /*
             * A FIXED deal. The "Send Home puts an ace up" check below is
             * deal-dependent, and with the clock-seeded shuffle it failed two
             * runs in six purely because that board exposed no ace -- which
             * looks exactly like a broken Send Home. A check that fails at
             * random teaches the reader to ignore a red result, which is the
             * one habit this harness cannot afford.
             */
            app_freecell_set_seed(23u);
            app_freecell_open();
            app_freecell_set_seed(0u);      /* players keep their shuffle */
            sh_run_frame();
            fw = wm_focused();
            before = app_freecell_cards_on_table(fw);
            SYS_LOGI("main", "FREECELL-DEMO: dealt %d cards, %d moves (%s)",
                     before, app_freecell_moves(fw),
                     (before == 52 && app_freecell_moves(fw) == 0)
                         ? "OK" : "MISMATCH");
            /* Click the top card of the first cascade, then the first free
             * cell. Both rectangles come from the window, not from arithmetic
             * repeated here. */
            if (app_freecell_rect(fw, 2, 0, &from) &&
                app_freecell_rect(fw, 0, 0, &to)) {
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
                e.mouse_x = (from.x0 + from.x1) / 2;
                e.mouse_y = (from.y0 + from.y1) / 2;
                { CPoint o = wm_client_origin(fw);
                  e.mouse_x += o.x; e.mouse_y += o.y; }
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
                e.mouse_x = (to.x0 + to.x1) / 2;
                e.mouse_y = (to.y0 + to.y1) / 2;
                { CPoint o = wm_client_origin(fw);
                  e.mouse_x += o.x; e.mouse_y += o.y; }
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
            } else {
                SYS_LOGI("main", "FREECELL-DEMO: no rects to click (MISMATCH)");
            }
            after = app_freecell_cards_on_table(fw);
            SYS_LOGI("main", "FREECELL-DEMO: a card moved to a free cell "
                     "(%d moves) (%s)", app_freecell_moves(fw),
                     (app_freecell_moves(fw) == 1) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "FREECELL-DEMO: still %d cards on the table (%s)",
                     after, (after == 52) ? "OK" : "MISMATCH");
            /*
             * Send Home, which walks the board sending every card that can go
             * to a foundation. A deal always has at least one ace reachable
             * from a cascade top or a cell, so this must move something -- and
             * the deck must still be whole afterwards.
             */
            if (app_freecell_rect(fw, 4, 0, &to)) {
                CPoint o2 = wm_client_origin(fw);
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
                e.mouse_x = (to.x0 + to.x1) / 2 + o2.x;
                e.mouse_y = (to.y0 + to.y1) / 2 + o2.y;
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
            }
            SYS_LOGI("main", "FREECELL-DEMO: Send Home put %d on the "
                     "foundations, %d cards still in play (%s)",
                     app_freecell_found_count(fw),
                     app_freecell_cards_on_table(fw),
                     /* Exactly two, not "at least one": the deal is fixed
                      * now, so the loose bound bought nothing and would have
                      * hidden a Send Home that found one ace of the two. */
                     (app_freecell_found_count(fw) == 2 &&
                      app_freecell_cards_on_table(fw) == 52)
                         ? "OK" : "MISMATCH");
            /* New Game deals a fresh board: no moves, nothing home, 52 out. */
            if (app_freecell_rect(fw, 3, 0, &to)) {
                CPoint o3 = wm_client_origin(fw);
                memset(&e, 0, sizeof(e));
                e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
                e.mouse_x = (to.x0 + to.x1) / 2 + o3.x;
                e.mouse_y = (to.y0 + to.y1) / 2 + o3.y;
                plat_host_push_event(&e);
                e.type = PLAT_EV_MOUSE_UP; e.buttons = 0;
                plat_host_push_event(&e);
                sh_run_frame();
            }
            SYS_LOGI("main", "FREECELL-DEMO: New Game deals %d cards, %d "
                     "moves, %d home (%s)",
                     app_freecell_cards_on_table(fw),
                     app_freecell_moves(fw), app_freecell_found_count(fw),
                     (app_freecell_cards_on_table(fw) == 52 &&
                      app_freecell_moves(fw) == 0 &&
                      app_freecell_found_count(fw) == 0)
                         ? "OK" : "MISMATCH");
            while (wm_window_count() > 0) {
                WmWindow *w = wm_window_at(0);
                if (w == NULL) { break; }
                wm_destroy(w);
            }
            for (k = 0; k < 3; k++) { sh_run_frame(); }
        }
        if (opt->console_demo) {
            /*
             * The console keeps a scrollback and a command history, and both
             * are rings.
             *
             * This scene exists because of a regression nothing else could
             * see. When the four hand-rolled rings in this tree were replaced
             * by one shared core, the console's ring was left uninitialised;
             * a zeroed Ring has capacity zero, and the push rounded that up
             * to one. The result was a ONE-LINE console -- the banner and the
             * directory listing scrolled off the moment they were printed --
             * and every suite stayed green. It was caught by opening the
             * window and looking at it, which is not a thing that happens on
             * every commit.
             */
            WmWindow *cw;
            int lines, hist, k;
            const char *cmds[3];
            cmds[0] = "ver"; cmds[1] = "mem"; cmds[2] = "help";
            app_console_open();
            sh_run_frame();
            cw = wm_focused();
            lines = app_console_line_count(cw);
            /* The banner alone is three lines. One would mean the ring is
             * holding a single slot, which is the bug above. */
            SYS_LOGI("main", "CONSOLE-DEMO: banner is %d lines (%s)", lines,
                     (lines >= 3) ? "OK" : "MISMATCH");
            /* Type three commands; each must be echoed AND remembered. */
            for (k = 0; k < 3; k++) {
                const char *cmd = cmds[k];
                int j;
                for (j = 0; cmd[j] != '\0'; j++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN;
                    ke.key = (unsigned char)cmd[j];
                    ke.ch  = (unsigned char)cmd[j];
                    plat_host_push_event(&ke);
                }
                { PlatEvent en; memset(&en, 0, sizeof(en));
                  en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                  en.ch = '\n'; plat_host_push_event(&en); }
                sh_run_frame();
            }
            hist = app_console_hist_count(cw);
            SYS_LOGI("main", "CONSOLE-DEMO: %d commands remembered (%s)", hist,
                     (hist == 3) ? "OK" : "MISMATCH");
            /* The scrollback grew by more than the three echoed lines,
             * because each command printed something. */
            SYS_LOGI("main", "CONSOLE-DEMO: scrollback %d -> %d lines (%s)",
                     lines, app_console_line_count(cw),
                     (app_console_line_count(cw) > lines + 3)
                         ? "OK" : "MISMATCH");

            /*
             * ...and `tree`, which has to draw a TREE.
             *
             * Counting lines cannot tell a folder tree from an error message
             * -- both are output. So the scrollback is read back and required
             * to contain an elbow and a folder that is really there. A tree
             * that silently printed nothing, or printed only the header, is
             * the failure worth catching: the command would look implemented.
             */
            {
                const char *t = "tree";
                int j, elbows = 0, named = 0, n, li;
                for (j = 0; t[j] != '\0'; j++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN;
                    ke.key = (unsigned char)t[j];
                    ke.ch  = (unsigned char)t[j];
                    plat_host_push_event(&ke);
                }
                { PlatEvent en; memset(&en, 0, sizeof(en));
                  en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                  en.ch = '\n'; plat_host_push_event(&en); }
                sh_run_frame();
                n = app_console_line_count(cw);
                for (li = 0; li < n; li++) {
                    const char *ln = app_console_line(cw, li);
                    if (ln == NULL) { continue; }
                    if (strstr(ln, "+---") != NULL ||
                        strstr(ln, "\\---") != NULL) { elbows++; }
                    if (strstr(ln, "SYS") != NULL &&
                        strstr(ln, "---") != NULL) { named++; }
                }
                SYS_LOGI("main", "CONSOLE-DEMO: tree drew %d elbow(s), SYS "
                         "named %d time(s) (%s)", elbows, named,
                         (elbows >= 2 && named >= 1) ? "OK" : "MISMATCH");
            }

            while (wm_window_count() > 0) {
                WmWindow *w = wm_window_at(0);
                if (w == NULL) { break; }
                wm_destroy(w);
            }
            for (k = 0; k < 3; k++) { sh_run_frame(); }
        }
        if (opt->dlg_exhaust) {
            /*
             * Every dialog refused, because the window pool is full.
             *
             * ui_msgbox allocates its Dlg first and hands ownership to the
             * window, which frees it on WM_MSG_DESTROY. If wm_create fails
             * there is no window, so that message never arrives and the Dlg
             * is never freed. The failure is worst exactly where it is most
             * likely: what makes wm_create fail is running out of window
             * slots or memory, which is also the moment something wants to
             * put a message box on screen to say so.
             *
             * The pool is filled here deliberately and then a hundred more
             * dialogs are asked for. Every one must be refused, and the
             * accounted allocator must not grow by a byte across them.
             */
            cu32 before, after;
            int k, made = 0, refused_count;
            for (k = 0; k < CASTALIA_MAX_WINDOWS + 4; k++) {
                CRect fr = crect_make(0, 0, 40, 30);
                if (wm_create("filler", &fr, WM_STYLE_BORDER, NULL, NULL)
                    != NULL) { made++; }
            }
            refused_count = wm_window_count();
            before = (cu32)sys_mem_live_bytes();
            for (k = 0; k < 100; k++) {
                ui_msgbox("Full", "no room for this", UI_MB_OK, NULL, NULL);
                ui_prompt("Full", "nor this", "x", NULL, NULL);
            }
            after = (cu32)sys_mem_live_bytes();
            /* The pool really is full -- otherwise the dialogs would have
             * been created, nothing would have been refused, and zero growth
             * would prove nothing at all. */
            SYS_LOGI("main", "DLG-EXHAUST: pool holds %d, %d filled, %d up "
                     "(%s)", CASTALIA_MAX_WINDOWS, made, refused_count,
                     (refused_count >= CASTALIA_MAX_WINDOWS) ? "OK"
                                                            : "MISMATCH");
            SYS_LOGI("main", "DLG-EXHAUST: 200 refused dialogs, live %lu -> "
                     "%lu bytes (%s)", (unsigned long)before,
                     (unsigned long)after,
                     /* The harness greps for these two words exactly, so the
                      * failing one has to BE "MISMATCH" -- a scene that
                      * reports a leak in its own vocabulary reports it to
                      * nobody. */
                     (after == before) ? "OK" : "MISMATCH -- dialogs leaked");
            while (wm_window_count() > 0) {
                WmWindow *w = wm_window_at(0);
                if (w == NULL) { break; }
                wm_destroy(w);
            }
            for (k = 0; k < 3; k++) { sh_run_frame(); }
        }
        if (opt->nav_demo) {
            /* Walking into a folder and back out again, in both browsers.
             *
             * A screenshot cannot check this: two different folders holding
             * the same names draw identically, so a navigation that quietly
             * went nowhere -- or somewhere else -- looks exactly like one
             * that worked. What is checked instead is the path each app
             * ends up on, and that the two agree.
             *
             * They agree because they now share ui_path_join and
             * ui_path_parent_rel. They used to carry a copy each, character
             * for character identical, which is the arrangement where one
             * gets fixed and the other does not. */
            WmWindow *fw, *cw;
            PlatEvent ke;
            char start[CASTALIA_MAX_PATH];
            char down[CASTALIA_MAX_PATH];
            char up[CASTALIA_MAX_PATH];
            const char *p;
            int k;

            app_fileman_open(); sh_run_frame();
            fw = wm_focused();
            p = app_fileman_path(fw);
            sys_strlcpy(start, (p != NULL) ? p : "", sizeof(start));

            /* Home selects "..", which is pinned to the top of every
             * listing; one Down lands on the first real folder, because
             * folders sort ahead of files. DOCS is that folder. */
            memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
            ke.key = PLAT_KEY_HOME; plat_host_push_event(&ke); sh_run_frame();
            ke.key = PLAT_KEY_DOWN; plat_host_push_event(&ke); sh_run_frame();
            ke.key = PLAT_KEY_ENTER; plat_host_push_event(&ke); sh_run_frame();
            p = app_fileman_path(fw);
            sys_strlcpy(down, (p != NULL) ? p : "", sizeof(down));
            SYS_LOGI("main", "NAV-DEMO: File Manager went down to '%s' (%s)",
                     down,
                     (sys_stricmp(ui_path_base(down), "DOCS") == 0)
                         ? "OK" : "MISMATCH");

            /* Back up the way a person does it: select ".." and press Enter.
             * The round trip has to land on the very same string -- an extra
             * separator or a lost drive letter is a different path to every
             * file call that follows, even when it prints the same. */
            ke.key = PLAT_KEY_HOME; plat_host_push_event(&ke); sh_run_frame();
            ke.key = PLAT_KEY_ENTER; plat_host_push_event(&ke); sh_run_frame();
            p = app_fileman_path(fw);
            SYS_LOGI("main", "NAV-DEMO: and back up to '%s' (%s)",
                     (p != NULL) ? p : "(none)",
                     (p != NULL && strcmp(p, start) == 0) ? "OK" : "MISMATCH");

            /* Backspace is the other way up, and a separate call site. */
            ke.key = PLAT_KEY_BACKSP; plat_host_push_event(&ke); sh_run_frame();
            p = app_fileman_path(fw);
            sys_strlcpy(up, (p != NULL) ? p : "", sizeof(up));
            SYS_LOGI("main", "NAV-DEMO: Backspace left '%s' for '%s' (%s)",
                     start, up,
                     (up[0] != '\0' && strcmp(up, start) != 0 &&
                      strncmp(start, up, sys_strnlen(up, sizeof(up))) == 0)
                         ? "OK" : "MISMATCH");

            /* Now the same walk in the Console, which reaches the same two
             * helpers through CD instead of through a list row. */
            app_console_open(); sh_run_frame();
            cw = wm_focused();
            p = app_console_cwd(cw);
            SYS_LOGI("main", "NAV-DEMO: Console starts at '%s' (%s)",
                     (p != NULL) ? p : "(none)",
                     (p != NULL && strcmp(p, start) == 0) ? "OK" : "MISMATCH");
            {
                static const char *cmds[] = { "cd DOCS\n", "cd ..\n" };
                int q;
                for (q = 0; q < 2; q++) {
                    for (k = 0; cmds[q][k] != '\0'; k++) {
                        PlatEvent ce; memset(&ce, 0, sizeof(ce));
                        ce.type = PLAT_EV_KEY_DOWN;
                        if (cmds[q][k] == '\n') {
                            ce.key = PLAT_KEY_ENTER; ce.ch = 0;
                        } else {
                            ce.key = (unsigned char)cmds[q][k];
                            ce.ch = (unsigned char)cmds[q][k];
                        }
                        plat_host_push_event(&ce);
                    }
                    sh_run_frame();
                    p = app_console_cwd(cw);
                    /* "cd DOCS" must land where the File Manager landed, and
                     * "cd .." must come back to where both started. */
                    SYS_LOGI("main", "NAV-DEMO: '%s' -> '%s' (%s)",
                             (q == 0) ? "cd DOCS" : "cd ..",
                             (p != NULL) ? p : "(none)",
                             (p != NULL &&
                              strcmp(p, (q == 0) ? down : start) == 0)
                                 ? "OK" : "MISMATCH");
                }
            }

            /* The relative case, which is what both browsers get when
             * CASTALIA_HOME is unset: they fall back to ".". Every path they
             * then build would carry a leading "./" that means nothing, and
             * that climbing up would have to unpick again -- so the join
             * collapses "." instead, and the climb out of "." goes to ".."
             * rather than refusing. (The file dialog wants the opposite and
             * uses ui_path_up, which stops at "." on purpose.)
             *
             * The scene runs from the demo home, so "." is that folder and
             * DOCS is really there. */
            app_fileman_open_path("."); sh_run_frame();
            fw = wm_focused();
            ke.key = PLAT_KEY_HOME; plat_host_push_event(&ke); sh_run_frame();
            ke.key = PLAT_KEY_DOWN; plat_host_push_event(&ke); sh_run_frame();
            ke.key = PLAT_KEY_ENTER; plat_host_push_event(&ke); sh_run_frame();
            p = app_fileman_path(fw);
            SYS_LOGI("main", "NAV-DEMO: from '.' down to '%s' (%s)",
                     (p != NULL) ? p : "(none)",
                     (p != NULL && strcmp(p, "DOCS") == 0) ? "OK" : "MISMATCH");
            ke.key = PLAT_KEY_BACKSP; plat_host_push_event(&ke); sh_run_frame();
            p = app_fileman_path(fw);
            SYS_LOGI("main", "NAV-DEMO: and back to '%s' (%s)",
                     (p != NULL) ? p : "(none)",
                     (p != NULL && strcmp(p, ".") == 0) ? "OK" : "MISMATCH");
            /* One more step up leaves the current directory entirely, which
             * a shell is allowed to do. */
            ke.key = PLAT_KEY_BACKSP; plat_host_push_event(&ke); sh_run_frame();
            p = app_fileman_path(fw);
            SYS_LOGI("main", "NAV-DEMO: '.' climbs out to '%s' (%s)",
                     (p != NULL) ? p : "(none)",
                     (p != NULL && strcmp(p, "..") == 0) ? "OK" : "MISMATCH");
        }
        if (opt->assoc_demo) {
            /* Open one file of each kind and let the log say which app
             * answered -- the association table is only worth having if the
             * same gesture really lands somewhere different each time. A
             * fresh File Manager per file keeps the focus unambiguous. */
            int i, k, hits = 0;
            for (i = 0; i < 5; i++) {
                app_fileman_open(); sh_run_frame();
                /* End, then Up: counting DOWN from the top depends on how
                 * many folders happen to sort first, and that is not the
                 * association table's business. The five files are the tail
                 * of the listing whatever else is in the folder. */
                {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN; ke.key = PLAT_KEY_END;
                    plat_host_push_event(&ke);
                    sh_run_frame();
                }
                for (k = 0; k < i; k++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN; ke.key = PLAT_KEY_UP;
                    plat_host_push_event(&ke);
                    sh_run_frame();
                }
                {
                    PlatEvent en; memset(&en, 0, sizeof(en));
                    en.type = PLAT_EV_KEY_DOWN; en.key = PLAT_KEY_ENTER;
                    en.ch = '\n';
                    plat_host_push_event(&en);
                }
                sh_run_frame();
                /* The window that answered is the evidence: its title has to
                 * name the app the table promised. */
                {
                    /* Walking up from the end: SONG, PHOTO, NOTES, LETTER,
                     * BUDGET. */
                    static const char *WANT[5] = { "Media", "Paint", "Notepad",
                                                   "Write", "Sheet" };
                    const char *title = (wm_focused() != NULL)
                                      ? wm_title(wm_focused()) : "(none)";
                    if (strstr(title, WANT[i]) != NULL) { hits++; }
                    SYS_LOGI("main", "ASSOC-DEMO: %d from the end opened '%s',"
                                     " wanted %s", i, title, WANT[i]);
                }
                /* Close what opened and the File Manager behind it, so the
                 * next round starts with one window and no ambiguity about
                 * where a synthetic key lands. */
                for (k = 0; k < 2; k++) {
                    PlatEvent ke; memset(&ke, 0, sizeof(ke));
                    ke.type = PLAT_EV_KEY_DOWN; ke.key = PLAT_KEY_CLOSE;
                    plat_host_push_event(&ke);
                    sh_run_frame();
                }
            }
            SYS_LOGI("main", "ASSOC-DEMO: %d of 5 file types opened the right "
                             "app (%s)", hits, (hits == 5) ? "OK" : "MISMATCH");
        }
        if (opt->capture_demo) {
            /* Capture the screen through the shell command the menus use, then
             * load the file back: a capture that writes nothing, or writes
             * something that is not the screen, fails here. */
            char dir[CASTALIA_MAX_PATH], path[CASTALIA_MAX_PATH];
            const char *chome2 = sys_home();
            GfxSurface *shot;
            PlatVideoInfo vi2;
            cu32 mem_before, mem_after, grew;
            app_calc_open(); sh_run_frame();      /* something to see       */
            /*
             * What it costs to save, not just whether it saves.
             *
             * gfx_bmp_save() used to build the entire file in memory first --
             * 1.4 MB for an 800x600 screen, asked for at the moment somebody
             * presses Capture Screen, on a machine with four megabytes in
             * total and 3.7 of them already spoken for. It writes a row at a
             * time now, so the peak barely moves. The ceiling below is
             * generous (64 KB against a 2.4 KB row) because the point is to
             * catch a return to whole-image buffering, not to pin an exact
             * figure that would need editing every time something else
             * allocates.
             */
            mem_before = sys_mem_peak_bytes();
            sh_dispatch_command(SH_CMD_CAPTURE);
            sh_run_frame();
            mem_after = sys_mem_peak_bytes();
            grew = (mem_after > mem_before) ? (mem_after - mem_before) : 0u;
            SYS_LOGI("main", "CAPTURE-DEMO: saving grew the peak by %lu bytes "
                     "(%lu -> %lu)", (unsigned long)grew,
                     (unsigned long)mem_before, (unsigned long)mem_after);
            SYS_LOGI("main", "CAPTURE-DEMO: saving a screen does not cost a "
                     "screen's worth of memory (%s)",
                     (grew < 65536u) ? "OK" : "MISMATCH");
            sys_snprintf(dir, sizeof(dir), "%s/PHOTOS", chome2);
            sys_snprintf(path, sizeof(path), "%s/SHOT0001.BMP", dir);
            plat_video_info(&vi2);
            shot = gfx_bmp_load(path);
            if (shot == NULL) {
                SYS_LOGW("main", "CAPTURE-DEMO: %s was not written (MISMATCH)",
                         path);
            } else {
                SYS_LOGI("main", "CAPTURE-DEMO: %s is %dx%d, screen is %dx%d (%s)",
                         path, shot->w, shot->h, vi2.width, vi2.height,
                         (shot->w == vi2.width && shot->h == vi2.height)
                             ? "OK" : "MISMATCH");
                gfx_surface_free(shot);
            }
            /* A second capture must not overwrite the first. */
            sh_dispatch_command(SH_CMD_CAPTURE);
            sh_run_frame();
            sys_snprintf(path, sizeof(path), "%s/SHOT0002.BMP", dir);
            SYS_LOGI("main", "CAPTURE-DEMO: the second went to SHOT0002.BMP (%s)",
                     (plat_file_size(path) > 0) ? "OK" : "MISMATCH");
        }
        if (opt->clock_demo) {
            /* Open the Clock, pick a day in the grid, click New..., type an
             * appointment and confirm it -- then read CASTALIA_HOME\SYS\
             * AGENDA.TXT back to prove the whole path (grid hit -> prompt ->
             * agenda_core -> disk) works, not just the drawing. */
            WmWindow *cwnd;
            CRect cr;
            CPoint o;
            PlatEvent e;
            int k, colw, rowh, panel_x;
            const char *entry = "14:00 Team review";
            char path[CASTALIA_MAX_PATH];
            const char *home = sys_home();

            app_clock_open(); sh_run_frame();
            cwnd = wm_focused();
            cr = wm_client_rect(cwnd);
            o = wm_client_origin(cwnd);
            /* Mirror app_clock.c's layout: 6 px margin, a 172 px agenda column
             * on the right, and the 7x6 day grid starting 233 px down. */
            panel_x = crect_w(&cr) - 172;
            colw = (panel_x - 12) / 7;
            rowh = (crect_h(&cr) - 233 - 6) / 6;
            if (colw < 1) { colw = 1; }
            if (rowh < 1) { rowh = 1; }

            memset(&e, 0, sizeof(e));
            /* Row 2, column 3 is a real day in every month, whatever weekday
             * the 1st falls on, so the demo does not depend on today's date. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + 6 + 3 * colw + colw / 2;
            e.mouse_y = o.y + 233 + 2 * rowh + rowh / 2;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* New... (left half of the button row at the foot of the panel) */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = o.x + panel_x + 40;
            e.mouse_y = o.y + crect_h(&cr) - 16;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();

            /* The prompt opens pre-filled with "09:00 "; clear it, then type. */
            for (k = 0; k < 8; k++) {
                PlatEvent be; memset(&be, 0, sizeof(be)); be.type = PLAT_EV_KEY_DOWN;
                be.key = PLAT_KEY_BACKSP; be.ch = 0; plat_host_push_event(&be);
            }
            for (k = 0; entry[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)entry[k]; ke.ch = (unsigned char)entry[k];
                plat_host_push_event(&ke);
            }
            sh_run_frame();
            if (!opt->clock_demo_keep) {
                PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
                en.key = PLAT_KEY_ENTER; en.ch = '\n'; plat_host_push_event(&en);
                sh_run_frame();
            }

            sys_snprintf(path, sizeof(path), "%s/SYS/AGENDA.TXT", home);
            if (opt->clock_demo_keep) {
                SYS_LOGI("main", "CLOCK-DEMO: prompt left open for the shot");
            } else {
                PlatFile *af = plat_fopen(path, "rb");
                char ab[1024];
                cu32 got = 0;
                if (af != NULL) {
                    got = plat_fread(af, ab, (cu32)sizeof(ab) - 1);
                    plat_fclose(af);
                }
                ab[got] = '\0';
                SYS_LOGI("main", "CLOCK-DEMO: %s holds %lu bytes, entry %s (%s)",
                         path, (unsigned long)got,
                         (strstr(ab, "Team review") != NULL) ? "found" : "missing",
                         (got > 0 && strstr(ab, "14:00 Team review") != NULL)
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->search_demo) {
            /*
             * Find looks at NAMES and at what is INSIDE files, and this checks
             * both in one search.
             *
             * It used to assert nothing at all -- it typed a query and let the
             * count go to the log. A count says a search happened; only the
             * rows say what it found, which is the difference between "Find
             * works" and "Find still runs".
             *
             * The two files below are the two ways a search can succeed: one
             * whose NAME carries the word and one that only mentions it on its
             * second line. A name-only Find finds the first and misses the
             * second, which is exactly what this used to be.
             */
            WmWindow *fw;
            CPoint o;
            PlatEvent e;
            int k;
            const char *q = "FINDME";
            {
                char home[CASTALIA_MAX_PATH], p1[CASTALIA_MAX_PATH];
                char p2[CASTALIA_MAX_PATH];
                sys_strlcpy(home, sys_home(), sizeof(home));
                sys_snprintf(p1, sizeof(p1), "%s/FINDME.TXT", home);
                sys_snprintf(p2, sizeof(p2), "%s/INSIDE.TXT", home);
                feed_write_text(p1, "this one matches by its name\n");
                feed_write_text(p2, "first line\nhere is FINDME on line two\n");
            }
            app_fileman_open(); sh_run_frame();
            fw = wm_focused(); o = wm_client_origin(fw);
            memset(&e, 0, sizeof(e));
            e.type = PLAT_EV_MOUSE_DOWN; e.mouse_x = o.x + 418; e.mouse_y = o.y + 30;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);   /* Find button */
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            for (k = 0; q[k] != '\0'; k++) {
                PlatEvent ke; memset(&ke, 0, sizeof(ke)); ke.type = PLAT_EV_KEY_DOWN;
                ke.key = (unsigned char)q[k]; ke.ch = (unsigned char)q[k];
                plat_host_push_event(&ke);
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; en.ch = '\n'; plat_host_push_event(&en); }
            sh_run_frame();
            {
                char row[CASTALIA_MAX_NAME];
                int i, by_name = 0, by_text = 0;
                for (i = 0; i < 64; i++) {
                    if (!app_fileman_result(fw, i, row, sizeof(row))) { break; }
                    if (demo_contains(row, "FINDME.TXT") &&
                        !demo_contains(row, "line")) { by_name++; }
                    /* The content hit names the line it was found on -- a
                     * result that says only "INSIDE.TXT" would not tell you
                     * where to look in it. */
                    if (demo_contains(row, "INSIDE.TXT") &&
                        demo_contains(row, "(line 2)")) { by_text++; }
                }
                SYS_LOGI("main", "SEARCH-DEMO: found the file whose NAME "
                         "matches (%d) (%s)", by_name,
                         (by_name == 1) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "SEARCH-DEMO: found the file that only "
                         "MENTIONS it, at the right line (%d) (%s)", by_text,
                         (by_text == 1) ? "OK" : "MISMATCH");
                SYS_LOGI("main", "SEARCH-DEMO: %s (%s)",
                         app_fileman_status(fw),
                         demo_contains(app_fileman_status(fw), "read")
                             ? "OK" : "MISMATCH");
            }
        }
        if (opt->dual_demo) {
            /*
             * The commander view: two panes that are two independent places.
             *
             * That independence is the whole feature and the one thing a
             * screenshot cannot check -- two panes showing the same folder and
             * two panes that are the same pane drawn twice are the same
             * picture. So this reads each pane's path and requires them to
             * come apart and stay apart.
             *
             * This scene existed before and asserted nothing at all, and it
             * was never listed in tools/run_demos.sh either, so it had never
             * run. Breaking two-pane mode outright failed no check anywhere.
             */
            WmWindow *fw;
            PlatEvent e;
            CRect btn;
            char left0[CASTALIA_MAX_PATH];
            const char *lp, *rp;

            app_fileman_open(); sh_run_frame();
            fw = wm_focused();
            lp = app_fileman_pane_path(fw, 0);
            sys_strlcpy(left0, (lp != NULL) ? lp : "", sizeof(left0));
            SYS_LOGI("main", "DUAL-DEMO: one pane to start (%s)",
                     (!app_fileman_is_dual(fw)) ? "OK" : "MISMATCH");
            SYS_LOGI("main", "DUAL-DEMO: and no mate to read (%s)",
                     (app_fileman_pane_path(fw, 1) == NULL)
                         ? "OK" : "MISMATCH");

            /* The button is asked for by name, not by a pixel offset: the
             * offset this scene used to carry would have started pressing a
             * different command the moment the toolbar gained an entry. */
            btn = app_fileman_btn_rect(fw, app_fileman_btn_dual());
            memset(&e, 0, sizeof(e));
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = (btn.x0 + btn.x1) / 2;
            e.mouse_y = (btn.y0 + btn.y1) / 2;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            SYS_LOGI("main", "DUAL-DEMO: 2-Pane opened the second pane (%s)",
                     app_fileman_is_dual(fw) ? "OK" : "MISMATCH");
            rp = app_fileman_pane_path(fw, 1);
            /* It opens where the first one is -- a commander that dropped you
             * somewhere unrelated would be worse than useless. */
            SYS_LOGI("main", "DUAL-DEMO: the new pane opened at '%s' (%s)",
                     (rp != NULL) ? rp : "(none)",
                     (rp != NULL && strcmp(rp, left0) == 0)
                         ? "OK" : "MISMATCH");
            SYS_LOGI("main", "DUAL-DEMO: input still goes to the left pane "
                     "(%s)",
                     (app_fileman_active_pane(fw) == 0) ? "OK" : "MISMATCH");

            /* Tab hands the keyboard to the other pane. */
            feed_key(PLAT_KEY_TAB);
            SYS_LOGI("main", "DUAL-DEMO: Tab moved to the right pane (%s)",
                     (app_fileman_active_pane(fw) == 1) ? "OK" : "MISMATCH");

            /* ...and now the essential property: navigating the right pane
             * moves the right pane and leaves the left one where it was. */
            feed_key(PLAT_KEY_BACKSP);
            lp = app_fileman_pane_path(fw, 0);
            rp = app_fileman_pane_path(fw, 1);
            SYS_LOGI("main", "DUAL-DEMO: right pane climbed to '%s' (%s)",
                     (rp != NULL) ? rp : "(none)",
                     (rp != NULL && strcmp(rp, left0) != 0)
                         ? "OK" : "MISMATCH");
            SYS_LOGI("main", "DUAL-DEMO: left pane stayed at '%s' (%s)",
                     (lp != NULL) ? lp : "(none)",
                     (lp != NULL && strcmp(lp, left0) == 0)
                         ? "OK" : "MISMATCH");

            /* Tab back, and the left pane is the one that moves. */
            feed_key(PLAT_KEY_TAB);
            SYS_LOGI("main", "DUAL-DEMO: Tab came back to the left pane (%s)",
                     (app_fileman_active_pane(fw) == 0) ? "OK" : "MISMATCH");

            /* Closing the second pane leaves one pane and no mate to read. */
            e.type = PLAT_EV_MOUSE_DOWN;
            e.mouse_x = (btn.x0 + btn.x1) / 2;
            e.mouse_y = (btn.y0 + btn.y1) / 2;
            e.buttons = PLAT_MB_LEFT; plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            sh_run_frame();
            SYS_LOGI("main", "DUAL-DEMO: 2-Pane closed it again (%s)",
                     (!app_fileman_is_dual(fw) &&
                      app_fileman_pane_path(fw, 1) == NULL)
                         ? "OK" : "MISMATCH");
        }
        if (opt->capp_demo) {
            /* Author a .CAPP in memory whose code section is a builtin reference
             * to the bundled 'hello' add-on, then run it through the loader. The
             * plugin's OWN code opens the window and draws it -- entirely via the
             * CappHostApi -- proving package code executes behind the ABI. */
            static const unsigned char code[] =
                { 'C','B','L','T','h','e','l','l','o','\0' };
            unsigned char pkg[512];
            CappInfo meta;
            cu16 stypes[1], sflags[1];
            const void *sdata[1];
            cu32 ssizes[1];
            int plen, before = wm_window_count();

            memset(&meta, 0, sizeof(meta));
            meta.abi_version = CAPP_ABI_VERSION;
            sys_strlcpy(meta.name, "HelloAddon", sizeof(meta.name));
            sys_strlcpy(meta.version, "1.0.0", sizeof(meta.version));
            sys_strlcpy(meta.author, "Castalia", sizeof(meta.author));
            sys_strlcpy(meta.description, "Sample plugin", sizeof(meta.description));
            stypes[0] = CAPP_SEC_CODE; sflags[0] = 0;
            sdata[0] = code; ssizes[0] = (cu32)sizeof(code);
            plen = capp_build(pkg, sizeof(pkg), &meta, stypes, sflags,
                              sdata, ssizes, 1);
            if (plen <= 0) {
                SYS_LOGE("capp", "capp-demo: build failed (%d)", plen);
            } else {
                CResult rc = capp_launch_image(pkg, (cu32)plen);
                int okwin = (wm_window_count() == before + 1);
                SYS_LOGI("capp",
                         "capp-demo: launch rc=%d running=%d windows %d->%d (%s)",
                         (int)rc, capp_running_count(), before, wm_window_count(),
                         (rc == CE_OK && capp_running_count() == 1 && okwin)
                             ? "OK" : "MISMATCH");
            }
            sh_run_frame();
        }
        if (opt->anim_demo) {
            /* Open a window (starts the open zoom), run frames so the outline
             * grows to the frame, then close it (starts the shrink zoom) and run
             * more frames. sh_anim_set_debug logs each outline so we can confirm
             * the geometry grows then shrinks. */
            /*
             * The open zoom GROWS and the close zoom SHRINKS.
             *
             * This scene ran both and asserted nothing -- it turned on the
             * outline debug log and left a human to read it, so an animation
             * that had stopped animating, or one that ran backwards, printed
             * a different set of numbers and still reported success.
             *
             * Measured as the outline's AREA frame by frame: it has to end
             * up bigger than it started on the way in and smaller on the way
             * out, and it has to move at least twice, because a single jump
             * from nothing to the whole frame is not an animation.
             */
            int f, steps_up = 0, steps_down = 0;
            long a0 = 0, a1 = 0, prev = 0;
            CRect ob2;
            sh_anim_set_debug(CTRUE);
            app_sysinfo_open();
            for (f = 0; f < 12; f++) {
                sh_run_frame();
                if (!sh_anim_bounds(&ob2)) { continue; }
                {
                    long ar = (long)crect_w(&ob2) * (long)crect_h(&ob2);
                    if (a0 == 0) { a0 = ar; }
                    if (prev != 0 && ar > prev) { steps_up++; }
                    a1 = ar;
                    prev = ar;
                }
            }
            SYS_LOGI("main", "ANIM-DEMO: the open outline grows (%ld -> %ld "
                     "px in %d steps) (%s)", a0, a1, steps_up,
                     (a0 > 0 && a1 > a0 * 2 && steps_up >= 2) ? "OK"
                                                             : "MISMATCH");

            prev = 0; a0 = 0; a1 = 0;
            {
                PlatEvent ce;
                memset(&ce, 0, sizeof(ce));
                ce.type = PLAT_EV_KEY_DOWN; ce.key = PLAT_KEY_CLOSE;
                plat_host_push_event(&ce);
            }
            for (f = 0; f < 12; f++) {
                sh_run_frame();
                if (!sh_anim_bounds(&ob2)) { continue; }
                {
                    long ar = (long)crect_w(&ob2) * (long)crect_h(&ob2);
                    if (a0 == 0) { a0 = ar; }
                    if (prev != 0 && ar < prev) { steps_down++; }
                    a1 = ar;
                    prev = ar;
                }
            }
            SYS_LOGI("main", "ANIM-DEMO: ...and the close outline shrinks "
                     "(%ld -> %ld px in %d steps) (%s)", a0, a1, steps_down,
                     (a0 > 0 && a1 * 2 < a0 && steps_down >= 2) ? "OK"
                                                               : "MISMATCH");

            /*
             * ---- and MINIMIZE, which is a different animation ------------
             *
             * Minimizing flies the outline down to the window's own taskbar
             * button; restoring flies it back up. Neither was checked, and
             * area alone would not have checked them if it had been: a
             * minimize that shrank into the window's own middle -- which is
             * what the close zoom does -- passes every area test there is.
             * What makes it a minimize is WHERE IT GOES.
             *
             * It is also the animation most able to vanish silently. Both
             * are started only `if (sh_taskbar_button_rect(win, &btn))`, so
             * a button that cannot be found means no animation at all, and
             * nothing anywhere would have said so.
             */
            {
                WmWindow *mw;
                CRect mfr, mbtn, ob3;
                int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0, moves = 0;
                cbool got_btn, first = CTRUE;
                app_sysinfo_open();
                for (f = 0; f < 16; f++) { sh_run_frame(); }
                mw = wm_focused();
                mfr = wm_frame_rect(mw);
                got_btn = sh_taskbar_button_rect(mw, &mbtn);
                wm_minimize(mw);
                for (f = 0; f < 12; f++) {
                    sh_run_frame();
                    if (!sh_anim_bounds(&ob3)) { continue; }
                    if (first) {
                        cx0 = (ob3.x0 + ob3.x1) / 2;
                        cy0 = (ob3.y0 + ob3.y1) / 2;
                        first = CFALSE;
                    } else if ((ob3.x0 + ob3.x1) / 2 != cx1 ||
                               (ob3.y0 + ob3.y1) / 2 != cy1) {
                        moves++;
                    }
                    cx1 = (ob3.x0 + ob3.x1) / 2;
                    cy1 = (ob3.y0 + ob3.y1) / 2;
                }
                SYS_LOGI("main", "ANIM-DEMO: minimizing flies the outline "
                         "from the window's middle (%d,%d) into its taskbar "
                         "button (%d,%d in %d,%d..%d,%d) in %d steps (%s)",
                         cx0, cy0, cx1, cy1, mbtn.x0, mbtn.y0, mbtn.x1,
                         mbtn.y1, moves,
                         (got_btn && !first && moves >= 2 &&
                          cy0 < mbtn.y0 && crect_contains(&mbtn, cx1, cy1))
                             ? "OK" : "MISMATCH");

                /* ...and back up again. */
                first = CTRUE; moves = 0;
                cx0 = cy0 = cx1 = cy1 = 0;
                wm_show(mw, CTRUE);
                for (f = 0; f < 12; f++) {
                    sh_run_frame();
                    if (!sh_anim_bounds(&ob3)) { continue; }
                    if (first) {
                        cx0 = (ob3.x0 + ob3.x1) / 2;
                        cy0 = (ob3.y0 + ob3.y1) / 2;
                        first = CFALSE;
                    } else if ((ob3.x0 + ob3.x1) / 2 != cx1 ||
                               (ob3.y0 + ob3.y1) / 2 != cy1) {
                        moves++;
                    }
                    cx1 = (ob3.x0 + ob3.x1) / 2;
                    cy1 = (ob3.y0 + ob3.y1) / 2;
                }
                {
                    /*
                     * Which END each sample is nearer, rather than "inside
                     * the button": the first outline a scene can see has
                     * already advanced one step of six, so it has left the
                     * button by the time anything looks. Asking for
                     * containment there is asking for something that is
                     * never true, which is how the first spelling of this
                     * reported a working animation as broken.
                     */
                    int win_cy = (mfr.y0 + mfr.y1) / 2;
                    int btn_cy = (mbtn.y0 + mbtn.y1) / 2;
                    int d0b = (cy0 > btn_cy) ? cy0 - btn_cy : btn_cy - cy0;
                    int d0w = (cy0 > win_cy) ? cy0 - win_cy : win_cy - cy0;
                    int d1b = (cy1 > btn_cy) ? cy1 - btn_cy : btn_cy - cy1;
                    int d1w = (cy1 > win_cy) ? cy1 - win_cy : win_cy - cy1;
                    SYS_LOGI("main", "ANIM-DEMO: ...and restoring flies it "
                             "back up, starting nearer the button at y=%d "
                             "(%d vs %d) and ending nearer the window at "
                             "y=%d (%d vs %d), in %d steps (%s)",
                             cy0, d0b, d0w, cy1, d1w, d1b, moves,
                             (!first && moves >= 2 && d0b < d0w && d1w < d1b)
                                 ? "OK" : "MISMATCH");
                }
            }
        }
        if (opt->splash_demo) {
            /* Render the boot splash at ~65% progress and screenshot it. */
            CRect full = crect_make(0, 0, info.width, info.height);
            sh_splash_draw(plat_backbuffer(), 168);
            plat_present(&full, 1);
            /*
             * The result is CHECKED. This used to be discarded and followed
             * by an unconditional "wrote ..." -- which was demonstrably a lie
             * whenever the scene ran in a world with no build/ directory:
             * the file never appeared and the log said it had. A scene that
             * reports success it did not verify is worse than one that
             * reports nothing.
             */
            {
                char shot[CASTALIA_MAX_PATH];
                CResult rc;
                sys_snprintf(shot, sizeof shot, "%s/SPLASH.BMP", sys_home());
                rc = plat_screenshot(shot);
                SYS_LOGI("main", "SPLASH-DEMO: wrote %s (%s)", shot,
                         (rc == CE_OK) ? "OK" : "MISMATCH");
            }
            sh_invalidate_all();
        }
        if (opt->saver_demo) {
            CRect full = crect_make(0, 0, info.width, info.height);
            /*
             * Every mode, not just the configured one.
             *
             * sh_saver_draw_mode has four branches and this scene only ever
             * ran the one in settings -- so three of the four were drawn by
             * nothing in the gate, and a mode that had stopped painting
             * altogether would have been found by a user turning it on.
             *
             * "Painted something" is the check: count the pixels that are not
             * the background. A saver that draws nothing still presents a
             * frame and still writes a file, so the file existing proves
             * nothing at all.
             */
            {
                int m;
                for (m = 0; m < SETTINGS_SAVER_COUNT; m++) {
                    GfxSurface *bb = plat_backbuffer();
                    int x, y, lit = 0;
                    CColor bg;
                    CRect clr = crect_make(0, 0, info.width, info.height);
                    gfx_fill_rect(bb, &clr, GFX_RGB(0, 0, 0));
                    sh_saver_draw_mode(bb, 140, m);
                    /*
                     * Against the mode's OWN background, taken from a corner,
                     * not against pure black. The first version of this asked
                     * "is the pixel non-black" and every mode answered
                     * 30000 of 30000 -- because these savers lay down a very
                     * dark ground rather than leaving the buffer at zero, so
                     * the question was satisfied by the fill alone and a mode
                     * that painted nothing on top would have passed.
                     */
                    bg = gfx_get_pixel(bb, 0, 0);
                    for (y = 0; y < info.height; y += 4) {
                        for (x = 0; x < info.width; x += 4) {
                            if (gfx_get_pixel(bb, x, y) != bg) { lit++; }
                        }
                    }
                    SYS_LOGI("main", "SAVER-DEMO: mode %d painted %d of %d "
                             "sampled pixels above its ground (%s)", m, lit,
                             (info.width / 4) * (info.height / 4),
                             (lit > 0) ? "OK" : "MISMATCH");
                }
            }
            sh_saver_draw(plat_backbuffer(), 140);
            plat_present(&full, 1);
            {
                /* Checked, for the reason above -- and written where the
                 * scene's own world actually is, rather than into a build/
                 * directory that only exists when it is run from the source
                 * tree by hand. */
                char shot[CASTALIA_MAX_PATH];
                CResult rc;
                sys_snprintf(shot, sizeof shot, "%s/SAVER.BMP", sys_home());
                rc = plat_screenshot(shot);
                SYS_LOGI("main", "SAVER-DEMO: mode %d written to %s (%s)",
                         settings_get()->screensaver, shot,
                         (rc == CE_OK) ? "OK" : "MISMATCH");
            }
            sh_invalidate_all();
        }
        if (opt->shutdn_demo) {
            /*
             * The last thing this system ever draws has to SAY something.
             *
             * A screen that is blank, or one whose text is the same colour as
             * its background, leaves somebody staring at a monitor with no
             * idea whether the machine is safe to switch off -- and this
             * scene took a screenshot of it and reported success either way.
             * Checked the way --crash-demo checks its screen: ink, in the
             * middle, in a colour that is not the background's.
             */
            CRect full = crect_make(0, 0, info.width, info.height);
            GfxSurface *bb;
            CColor bg;
            int x, y, ink = 0;
            /*
             * The TEXT rows only. A band that also caught the logo above them
             * reported 691 sharp edges with the words drawn in the background
             * colour -- the logo's own edges, passing a check about words.
             * sh_shutdown_screen puts the two lines at 52% and 52% + 16.
             */
            CRect mid = crect_make(info.width / 6, info.height * 50 / 100,
                                   info.width * 2 / 3, info.height * 8 / 100);
            sh_shutdown_screen(plat_backbuffer());
            plat_present(&full, 1);
            bb = plat_backbuffer();
            /*
             * Counting pixels that merely DIFFER from the corner would pass
             * on a plain gradient -- the first version of this did, reporting
             * 106,600 of a 106,600-pixel band. What says "there are words
             * here" is sharp horizontal CONTRAST: a glyph edge jumps by a
             * hundred levels in one pixel, and a gradient never does.
             */
            (void)bg;
            for (y = mid.y0; y < mid.y1; y++) {
                for (x = mid.x0 + 1; x < mid.x1; x++) {
                    CColor c = gfx_get_pixel(bb, x, y);
                    CColor l = gfx_get_pixel(bb, x - 1, y);
                    int d = (GFX_R(c) - GFX_R(l)) + (GFX_G(c) - GFX_G(l)) +
                            (GFX_B(c) - GFX_B(l));
                    if (d < 0) { d = -d; }
                    if (d > 100) { ink++; }
                }
            }
            SYS_LOGI("main", "SHUTDOWN-DEMO: the shutdown screen has words on "
                     "it (%d sharp edges in the middle band) (%s)", ink,
                     (ink > 200) ? "OK" : "MISMATCH");
            plat_screenshot("shutdown.bmp");
            SYS_LOGI("main", "shutdown-demo: wrote shutdown.bmp");
            sh_invalidate_all();
        }
        if (opt->wall_demo) {
            /*
             * The three wallpaper modes have to be three DIFFERENT things.
             *
             * This scene authored an orange bitmap, stretched it and saved a
             * screenshot -- and asserted nothing, so a mode selector that had
             * stopped selecting, or a stretch that quietly centred, looked
             * exactly like a working one. What is checked now is what a
             * person would look at: how much of the desktop the picture
             * covers. Centre puts one 80x60 block in the middle, tile repeats
             * it across the whole area, and stretch fills it with one copy --
             * so tile and stretch both cover far more than centre, and centre
             * covers about a bitmap.
             *
             * The count is of the DESKTOP behind the windows, taken above the
             * taskbar and away from the icons down the left, and the exact
             * pixel value is not compared: a vignette darkens the edges, so
             * "orange-ish" is the honest test -- red high, green middling,
             * blue low.
             */
            static const struct { int mode; const char *name; } WMODE[3] = {
                { WALLPAPER_CENTER,  "centre"  },
                { WALLPAPER_TILE,    "tile"    },
                { WALLPAPER_STRETCH, "stretch" }
            };
            GfxSurface *wp = gfx_surface_new(80, 60);
            int covered[3], stripes[3], mi;
            CRect band = crect_make(200, 40, info.width - 240,
                                    info.height - 120);
            if (wp != NULL) {
                CRect bar4;
                gfx_clear(wp, GFX_RGB(0xE0, 0x80, 0x20));   /* orange */
                /*
                 * ...with a blue STRIPE down its left edge, which is what
                 * tells tile from stretch. A flat picture cannot: both fill
                 * the desktop with the same colour and a count of coloured
                 * pixels comes out identical, so the check that compared them
                 * was passing on an escape clause. A stripe repeats ten times
                 * across the screen when tiled and once when stretched, and
                 * counting the runs along one row says which happened.
                 */
                bar4 = crect_make(0, 0, 4, 60);
                gfx_fill_rect(wp, &bar4, GFX_RGB(0x20, 0x40, 0xC0));
                /* Beside the scene's working directory, not in build/.
                 * The demo harness runs every scene from a throwaway home, so
                 * "build/testwall.bmp" landed in a folder that does not exist
                 * there -- the save failed, the wallpaper never loaded, and
                 * the scene reported success because it asserted nothing. */
                gfx_bmp_save(wp, "testwall.bmp");
                gfx_surface_free(wp);
            }
            sys_strlcpy(settings_get()->wallpaper, "testwall.bmp",
                        sizeof(settings_get()->wallpaper));
            for (mi = 0; mi < 3; mi++) {
                GfxSurface *bb;
                int x, y, n = 0;
                settings_get()->wallpaper_mode = WMODE[mi].mode;
                sh_apply_settings();
                sh_invalidate_all();
                sh_run_frame();
                bb = plat_backbuffer();
                for (y = band.y0; y < band.y1; y++) {
                    for (x = band.x0; x < band.x1; x++) {
                        CColor c = gfx_get_pixel(bb, x, y);
                        if (GFX_R(c) > 120 && GFX_G(c) > 40 &&
                            GFX_G(c) < GFX_R(c) - 40 && GFX_B(c) < GFX_G(c)) {
                            n++;
                        }
                    }
                }
                covered[mi] = n;
                /* Runs of blue along one row: the stripe, counted. */
                {
                    int runs = 0, in = 0;
                    y = info.height / 2;
                    for (x = 0; x < info.width; x++) {
                        CColor c = gfx_get_pixel(bb, x, y);
                        int isblue = (GFX_B(c) > GFX_R(c) + 20 &&
                                      GFX_B(c) > GFX_G(c) + 20) ? 1 : 0;
                        if (isblue && !in) { runs++; }
                        in = isblue;
                    }
                    stripes[mi] = runs;
                }
                SYS_LOGI("wall", "WALL-DEMO: %s covers %d px of the strip "
                         "watched, and puts %d stripe(s) across the middle",
                         WMODE[mi].name, n, stripes[mi]);
            }
            SYS_LOGI("wall", "WALL-DEMO: centre puts one bitmap down, tile and "
                     "stretch cover the desktop (%d / %d / %d) (%s)",
                     covered[0], covered[1], covered[2],
                     (covered[0] > 0 &&
                      covered[1] > covered[0] * 3 &&
                      covered[2] > covered[0] * 3) ? "OK" : "MISMATCH");
            /* ...and tile is not simply stretch under another name: one
             * repeats the picture across the screen and the other scales a
             * single copy, so the stripe appears many times or once. */
            SYS_LOGI("wall", "WALL-DEMO: ...and tile repeats the picture where "
                     "stretch scales one copy (%d stripes against %d) (%s)",
                     stripes[1], stripes[2],
                     (stripes[2] == 1 && stripes[1] > 3) ? "OK" : "MISMATCH");
            settings_get()->wallpaper_mode = WALLPAPER_STRETCH;
            sh_apply_settings();
            sh_invalidate_all();
            sh_run_frame();
            plat_screenshot("wall.bmp");
            SYS_LOGI("wall", "wall-demo: wrote wall.bmp");
        }
        if (opt->min_demo) {
            /*
             * Down to the taskbar and back, through the two buttons that do
             * it: the caption box and the taskbar button.
             *
             * This scene used to drive both gestures and assert NOTHING -- it
             * turned on the animation debug log and left it at that, so a
             * minimize that stopped hiding the window, or a taskbar button
             * that stopped restoring it, read exactly like a working one.
             * The animation outline it logs is checked by --anim-demo; what
             * belongs here is whether the window went away and came back.
             */
            WmWindow *w;
            CRect fr;
            int k, mbx, mby, tbx, tby;
            sh_anim_set_debug(CTRUE);
            app_fileman_open();   /* WM_STYLE_APP -> has a minimize box */
            for (k = 0; k < 20; k++) { sh_run_frame(); }
            w = wm_focused();
            fr = wm_frame_rect(w);
            mbx = fr.x1 - 48; mby = fr.y0 + 12;  /* minimize caption button */
            tbx = 150; tby = info.height - 15;   /* its taskbar button       */

            feed_click(mbx, mby);
            for (k = 0; k < 10; k++) { sh_run_frame(); }   /* minimize zoom */
            SYS_LOGI("main", "MIN-DEMO: the caption box puts the window down "
                     "(visible %d, still open %d) (%s)",
                     wm_is_visible(w) ? 1 : 0, wm_window_count(),
                     (!wm_is_visible(w) && wm_window_count() == 1) ? "OK"
                                                                  : "MISMATCH");

            feed_click(tbx, tby);
            for (k = 0; k < 10; k++) { sh_run_frame(); }   /* restore zoom  */
            SYS_LOGI("main", "MIN-DEMO: ...its taskbar button brings it back, "
                     "with the keyboard (visible %d) (%s)",
                     wm_is_visible(w) ? 1 : 0,
                     (wm_is_visible(w) && wm_focused() == w) ? "OK"
                                                            : "MISMATCH");

            /*
             * ...and pressing that button AGAIN puts it away, which is what
             * a taskbar button has done since the taskbar was invented.
             * Without it the only way down is the caption box, so a window
             * reached from the taskbar had to be dismissed somewhere else.
             */
            feed_click(tbx, tby);
            for (k = 0; k < 10; k++) { sh_run_frame(); }
            SYS_LOGI("main", "MIN-DEMO: ...and pressing it again puts it down "
                     "(visible %d) (%s)", wm_is_visible(w) ? 1 : 0,
                     (!wm_is_visible(w)) ? "OK" : "MISMATCH");

            /* Back up, and nothing left on the desktop where it was. */
            feed_click(tbx, tby);
            for (k = 0; k < 24; k++) { sh_run_frame(); }
            probe_repaint_rect("MIN-DEMO", "the desktop after down and up",
                               &fr);

            /*
             * ---- and the caption boxes PACK, on a window that has two ----
             *
             * The slots used to be fixed -- close 0, maximize 1, minimize 2 --
             * so a window with no maximize box left slot 1 empty and put its
             * minimize box a whole button-width from the close box, with a
             * hole between them. The hole sits exactly where a lifetime of
             * muscle memory says maximize is, and clicking it drags the
             * window instead of doing anything.
             *
             * The File Manager above cannot show this: it has all three, so
             * nothing is missing and nothing packs. The Calculator has close
             * and minimize and no maximize, which is the case that needed a
             * second window to exist at all -- a gap needs two buttons to be
             * between.
             *
             * Checked as a CLICK as well as a measurement: adjacency of two
             * rectangles proves the arithmetic, and the arithmetic is what
             * was wrong, but only pressing the thing proves the drawing and
             * the hit test moved together.
             */
            {
                WmWindow *cw;
                CRect cb, mb;
                int gap;
                app_calc_open();
                for (k = 0; k < 24; k++) { sh_run_frame(); }
                cw = wm_focused();
                cb = wm_frame_close_btn(cw);
                mb = wm_frame_min_btn(cw);
                gap = cb.x0 - mb.x1;
                SYS_LOGI("main", "MIN-DEMO: the Calculator has no maximize "
                         "box, so its minimize box sits against the close box "
                         "-- %d px between them, boxes %d wide (%s)",
                         gap, crect_w(&mb),
                         (crect_w(&mb) > 0 && gap >= 0 &&
                          gap < crect_w(&mb)) ? "OK" : "MISMATCH");
                feed_click((mb.x0 + mb.x1) / 2, (mb.y0 + mb.y1) / 2);
                for (k = 0; k < 12; k++) { sh_run_frame(); }
                SYS_LOGI("main", "MIN-DEMO: ...and pressing it there really "
                         "puts it down (visible %d) (%s)",
                         wm_is_visible(cw) ? 1 : 0,
                         (!wm_is_visible(cw)) ? "OK" : "MISMATCH");
            }
        }
        if (opt->hover_demo) {
            /* Two task buttons; hover the first (non-focused) one, shot; then
             * move the pointer to the desktop, shot. The hovered button should
             * render brighter than its baseline. */
            /*
             * The TASKBAR's hover, which is the one --hotbtn-demo does not
             * cover: that scene drives push buttons inside windows, and a
             * task button is drawn by the shell with its own treatment.
             *
             * This scene took two screenshots and asserted nothing, so a
             * task button that had stopped lighting up looked exactly like
             * one that lit. What is checked is the BAND across the button,
             * away from its icon and its text: hovered it must be lighter
             * than at rest, and putting the pointer back on the desktop must
             * take it back to exactly what it was.
             */
            CRect band;
            long lit = 0, rest = 0, back = 0;
            int bx = 150, by = info.height - 15;
            int hx, hy, n5;
            app_fileman_open();
            app_calc_open();
            for (n5 = 0; n5 < 16; n5++) { sh_run_frame(); }
            band = crect_make(bx - 30, by - 6, 60, 4);

            feed_move(bx, 100);          /* pointer well clear of the bar */
            for (n5 = 0; n5 < 3; n5++) { sh_run_frame(); }
            {
                GfxSurface *bb = plat_backbuffer();
                for (hy = band.y0; hy < band.y1; hy++) {
                    for (hx = band.x0; hx < band.x1; hx++) {
                        CColor c = gfx_get_pixel(bb, hx, hy);
                        rest += GFX_R(c) + GFX_G(c) + GFX_B(c);
                    }
                }
            }
            feed_move(bx, by);           /* on the first task button */
            for (n5 = 0; n5 < 3; n5++) { sh_run_frame(); }
            {
                GfxSurface *bb = plat_backbuffer();
                for (hy = band.y0; hy < band.y1; hy++) {
                    for (hx = band.x0; hx < band.x1; hx++) {
                        CColor c = gfx_get_pixel(bb, hx, hy);
                        lit += GFX_R(c) + GFX_G(c) + GFX_B(c);
                    }
                }
            }
            plat_screenshot("hover_on.bmp");
            SYS_LOGI("main", "HOVER-DEMO: a task button lightens under the "
                     "pointer (%ld against %ld at rest) (%s)", lit, rest,
                     (lit > rest + crect_w(&band) * crect_h(&band) * 3)
                         ? "OK" : "MISMATCH");

            feed_move(bx, 100);
            for (n5 = 0; n5 < 3; n5++) { sh_run_frame(); }
            {
                GfxSurface *bb = plat_backbuffer();
                for (hy = band.y0; hy < band.y1; hy++) {
                    for (hx = band.x0; hx < band.x1; hx++) {
                        CColor c = gfx_get_pixel(bb, hx, hy);
                        back += GFX_R(c) + GFX_G(c) + GFX_B(c);
                    }
                }
            }
            plat_screenshot("hover_off.bmp");
            SYS_LOGI("main", "HOVER-DEMO: ...and goes back exactly when the "
                     "pointer leaves the bar (%ld against %ld) (%s)", back,
                     rest, (back == rest) ? "OK" : "MISMATCH");
            SYS_LOGI("hover", "hover-demo: wrote hover_on.bmp / hover_off.bmp");
        }
        if (opt->tip_demo) {
            /* Rest the pointer on the taskbar clock past the hover delay: the
             * info-yellow tooltip with the full weekday date pops above the
             * tray (real wall-clock wait; the delay is time-based). */
            PlatEvent mv;
            int cx = info.width - 40, cy = info.height - 15;
            cu32 t0;
            plat_host_set_mouse(cx, cy, 0);
            memset(&mv, 0, sizeof(mv));
            mv.type = PLAT_EV_MOUSE_MOVE; mv.mouse_x = cx; mv.mouse_y = cy;
            plat_host_push_event(&mv);
            sh_run_frame();
            t0 = plat_ticks_ms();
            /* Keep feeding same-spot moves: they hold the screensaver's idle
             * counter at zero without retargeting the tooltip clock (the
             * delay only restarts when the hovered ELEMENT changes). */
            /*
             * What the strip above the tray looked like BEFORE the tooltip is
             * due, so "a tooltip appeared" is a comparison rather than a
             * screenshot somebody has to look at. This scene used to end at
             * plat_screenshot and assert nothing, so a tooltip that stopped
             * appearing -- or one that appeared and never went away -- read
             * exactly like a working one.
             */
            {
                CRect strip = crect_make(info.width - 260, info.height - 70,
                                         250, 46);
                int shown, gone;
                probe_snap(&strip);
                while (plat_ticks_ms() - t0 < 800u) {
                    plat_host_push_event(&mv);
                    sh_run_frame();
                }
                shown = probe_diff();
                SYS_LOGI("tip", "TOOLTIP-DEMO: resting on the clock puts a "
                         "tooltip over the tray (%d px) (%s)", shown,
                         (shown > 200) ? "OK" : "MISMATCH");

                /*
                 * ...and moving away takes it back down, leaving the strip
                 * EXACTLY as it was. A tooltip is drawn over whatever is
                 * underneath it and nothing else erases it, so this is the
                 * check that it does not smear across the taskbar.
                 */
                probe_snap(&strip);
                feed_move(info.width / 2, info.height / 3);
                for (t0 = 0; t0 < 8u; t0++) { sh_run_frame(); }
                gone = probe_diff();
                SYS_LOGI("tip", "TOOLTIP-DEMO: ...and moving away takes it "
                         "down again (%d px changed back) (%s)", gone,
                         (gone >= shown - 64) ? "OK" : "MISMATCH");
                probe_repaint_rect("TOOLTIP-DEMO",
                                   "the tray after a tooltip came and went",
                                   &strip);
            }
            plat_screenshot("tooltip.bmp");
            SYS_LOGI("tip", "tooltip-demo: wrote tooltip.bmp");
        }
        if (opt->mines_demo) {
            /* Open Mines and reveal the center cell from the keyboard (Enter
             * fires the cell cursor); the first reveal seeds the board and
             * usually floods a region open. */
            /*
             * The first reveal opens a REGION, and revealing the same cell
             * again opens nothing.
             *
             * This scene pressed Enter, took a screenshot and asserted
             * nothing -- so a board that had stopped revealing, or one that
             * revealed the whole grid at once, reported what a working one
             * reported. mines_core is unit-tested without a window; what
             * belongs here is that the key reaches it and the result is
             * drawn.
             */
            WmWindow *mw;
            CRect board;
            int k, opened, again;
            app_mines_open();
            for (k = 0; k < 16; k++) { sh_run_frame(); }
            mw = wm_focused();
            {
                CRect c = wm_client_rect(mw);
                /* The grid, inside the status strip at the top. */
                board = crect_make(c.x0 + 6, c.y0 + 40,
                                   crect_w(&c) - 12, crect_h(&c) - 48);
            }
            probe_snap(&board);
            feed_key(PLAT_KEY_ENTER);
            for (k = 0; k < 4; k++) { sh_run_frame(); }
            opened = probe_diff();
            /*
             * At least a cell. NOT "a flooded region": the first reveal
             * floods only when the cell it lands on has no mine beside it,
             * and the board is seeded from the clock -- so a check that
             * demanded a flood would pass most runs and fail some, which is
             * worse than not checking at all. One cell is about 270 pixels
             * here; a caret is twenty.
             */
            SYS_LOGI("mines", "MINES-DEMO: the first reveal opens at least a "
                     "cell (%d px of the grid changed) (%s)", opened,
                     (opened > 100) ? "OK" : "MISMATCH");

            /* ...and it does not open the WHOLE grid: a flood that swallowed
             * the board would be a game over on the first key. */
            SYS_LOGI("mines", "MINES-DEMO: ...and not all of it (%d of %d) "
                     "(%s)", opened, crect_w(&board) * crect_h(&board),
                     (opened < crect_w(&board) * crect_h(&board) * 9 / 10)
                         ? "OK" : "MISMATCH");

            probe_snap(&board);
            feed_key(PLAT_KEY_ENTER);
            for (k = 0; k < 4; k++) { sh_run_frame(); }
            again = probe_diff();
            SYS_LOGI("mines", "MINES-DEMO: ...and revealing it again changes "
                     "nothing (%d px) (%s)", again,
                     (again == 0) ? "OK" : "MISMATCH");
            plat_screenshot("mines.bmp");
            SYS_LOGI("mines", "mines-demo: wrote mines.bmp");
        }
        if (opt->ql_demo) {
            /* Quick Launch, end to end: with two windows open, click cell 0
             * (Show Desktop) -- both windows minimize to their taskbar
             * buttons; then click cell 3 (Mines) -- the game opens. The cell
             * centers are computed from the taskbar layout knowns: the strip
             * starts 6px right of the Start orb, cells are 20px wide. */
            PlatEvent e;
            int orb_d = ((info.height >= 600) ? 30 : 26) + 9 - 1;
            int qx0 = 4 + orb_d + 6 + 2;
            int qy = info.height - 15;
            int k;
            WmWindow *w1, *w2;
            int before, up;
            app_fileman_open();
            for (k = 0; k < 12; k++) { sh_run_frame(); }
            w1 = wm_focused();
            app_calc_open();
            for (k = 0; k < 12; k++) { sh_run_frame(); }
            w2 = wm_focused();
            before = wm_window_count();

            /*
             * Cell 0, Show Desktop. This scene used to click it, screenshot,
             * click Mines, screenshot, and assert nothing -- so a Quick
             * Launch strip whose cells had shifted by one, or one that had
             * stopped answering clicks entirely, reported exactly what a
             * working one reported.
             *
             * "Minimized" is checked as VISIBLE rather than as a window
             * count: Show Desktop puts windows down, it does not close them,
             * and a version that closed them would pass a count check that
             * only looked for "fewer windows on screen".
             */
            memset(&e, 0, sizeof(e));
            e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
            e.mouse_x = qx0 + 9; e.mouse_y = qy;          /* cell 0 */
            plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            for (k = 0; k < 6; k++) { sh_run_frame(); }
            SYS_LOGI("ql", "QL-DEMO: Show Desktop puts both windows down and "
                     "keeps them open (%d -> %d open, %d visible) (%s)",
                     before, wm_window_count(),
                     (wm_is_visible(w1) ? 1 : 0) + (wm_is_visible(w2) ? 1 : 0),
                     (wm_window_count() == before && !wm_is_visible(w1) &&
                      !wm_is_visible(w2)) ? "OK" : "MISMATCH");
            plat_screenshot("ql_showdesktop.bmp");

            /* Cell 3, Mines. Checked by the window's TITLE: "a window
             * appeared" would pass whichever cell had been hit. */
            e.type = PLAT_EV_MOUSE_DOWN; e.buttons = PLAT_MB_LEFT;
            e.mouse_x = qx0 + 3 * 20 + 9; e.mouse_y = qy; /* cell 3: Mines */
            plat_host_push_event(&e);
            e.type = PLAT_EV_MOUSE_UP; e.buttons = 0; plat_host_push_event(&e);
            for (k = 0; k < 12; k++) { sh_run_frame(); }
            up = wm_window_count();
            SYS_LOGI("ql", "QL-DEMO: ...and cell 3 opens Mines (\"%s\", %d "
                     "window(s)) (%s)", wm_title(wm_focused()), up,
                     (up == before + 1 &&
                      strncmp(wm_title(wm_focused()), "Mines", 5) == 0)
                         ? "OK" : "MISMATCH");
            plat_screenshot("ql_mines.bmp");
            SYS_LOGI("ql", "ql-demo: wrote ql_showdesktop.bmp / ql_mines.bmp");
        }
        if (opt->desk_bench) {
            /* Show the cached background blit beats recomputing the per-pixel
             * gradient on every desktop repaint. */
            unsigned long tcached = 0, tlive = 0;
            sh_desktop_bench(500, &tcached, &tlive);
            SYS_LOGI("bench", "desktop x500 @%dx%d: cached=%lu ms, live-gradient=%lu ms, speedup=%lu%%",
                     info.width, info.height, tcached, tlive,
                     (tcached > 0 ? tlive * 100UL / tcached : 0));
        }
        if (opt->alttab) {
            /* Open two windows (Calculator ends up focused/on top), then
             * Alt+Tab brings the File Manager to the front. */
            PlatEvent at;
            app_fileman_open();
            app_calc_open();
            sh_run_frame();
            memset(&at, 0, sizeof(at));
            at.type = PLAT_EV_KEY_DOWN; at.key = PLAT_KEY_NEXTWIN;
            plat_host_push_event(&at);
            sh_run_frame();
        }
        if (opt->fm_open_row > 0) {
            /* Open the File Manager, walk the selection down to a file, and
             * activate it with Enter -- it should open in Notepad. */
            int k;
            app_fileman_open();
            sh_run_frame();
            for (k = 0; k < opt->fm_open_row; k++) {
                PlatEvent de; memset(&de, 0, sizeof(de));
                de.type = PLAT_EV_KEY_DOWN; de.key = PLAT_KEY_DOWN;
                plat_host_push_event(&de);
                sh_run_frame();
            }
            { PlatEvent en; memset(&en, 0, sizeof(en)); en.type = PLAT_EV_KEY_DOWN;
              en.key = PLAT_KEY_ENTER; plat_host_push_event(&en); }
            sh_run_frame();
        }
        if (opt->cc_demo) {
            /* Open the Control Center and drive the click path end-to-end:
             * pick the Forest Green theme radio, then Apply -- the desktop and
             * title bars should turn green live, proving the settings chain.
             * The offsets mirror app_control.c's layout. */
            CRect cr, patch;
            PlatEvent ev2;
            int px0, py0, ry, ax, ay, k9, greens0 = 0, greens1 = 0;
            int was_preset = settings_get()->theme_preset;
            app_control_open();
            for (k9 = 0; k9 < 12; k9++) { sh_run_frame(); }
            cr = wm_client_rect(wm_focused());
            /*
             * A patch of DESKTOP, well clear of the window and the icons, so
             * "the theme changed" is measured where a person would see it.
             * This scene drove the whole click path and asserted nothing --
             * its own comment said the desktop "should turn green live,
             * proving the settings chain", and nothing proved it. The
             * offsets below mirror app_control.c's layout by hand, which is
             * exactly the kind of copy that goes stale silently.
             */
            /*
             * The focused window's TITLE BAR, which is what a theme change
             * moves first and most visibly.
             *
             * Not the desktop: the default background is a painted scene --
             * sky at the top, land at the bottom -- so it already holds green
             * whatever the theme says, and a check that counted green pixels
             * out there would have been measuring the grass. Forest's title
             * is 1E5A38 to 3C8458; Aurora's is blue.
             */
            {
                CRect wf = wm_frame_rect(wm_focused());
                patch = crect_make(wf.x0 + 6, wf.y0 + 4,
                                   crect_w(&wf) / 3, 12);
            }
            {
                GfxSurface *bb = plat_backbuffer();
                int x, y;
                for (y = patch.y0; y < patch.y1; y++) {
                    for (x = patch.x0; x < patch.x1; x++) {
                        CColor c = gfx_get_pixel(bb, x, y);
                        if (GFX_G(c) > GFX_R(c) + 12 &&
                            GFX_G(c) > GFX_B(c) + 12) { greens0++; }
                    }
                }
            }
            px0 = cr.x0 + 120 + 14;   /* panel.x0 = client + sidebar + gap */
            py0 = cr.y0 + 6;          /* panel.y0                          */
            ry  = py0 + 12 + 3 * 18 + 8; /* Forest row = radio index 2 -> row 3 */
            memset(&ev2, 0, sizeof(ev2));
            ev2.type = PLAT_EV_MOUSE_DOWN; ev2.mouse_x = px0 + 30; ev2.mouse_y = ry;
            ev2.buttons = PLAT_MB_LEFT; plat_host_push_event(&ev2);
            ev2.type = PLAT_EV_MOUSE_UP; ev2.buttons = 0; plat_host_push_event(&ev2);
            sh_run_frame();
            /* Apply button: mirrors cc_layout (client-relative). */
            ax = cr.x0 + crect_w(&cr) - (64 + 6) * 3 - 4 + 1 * (64 + 6) + 32;
            ay = cr.y0 + crect_h(&cr) - 22 - 6 + 11;
            memset(&ev2, 0, sizeof(ev2));
            ev2.type = PLAT_EV_MOUSE_DOWN; ev2.mouse_x = ax; ev2.mouse_y = ay;
            ev2.buttons = PLAT_MB_LEFT; plat_host_push_event(&ev2);
            ev2.type = PLAT_EV_MOUSE_UP; ev2.buttons = 0; plat_host_push_event(&ev2);
            for (k9 = 0; k9 < 8; k9++) { sh_run_frame(); }

            SYS_LOGI("main", "CC-DEMO: picking Forest Green and pressing Apply "
                     "changes the setting (%d -> %d) (%s)", was_preset,
                     settings_get()->theme_preset,
                     (settings_get()->theme_preset == SETTINGS_THEME_FOREST)
                         ? "OK" : "MISMATCH");
            {
                GfxSurface *bb = plat_backbuffer();
                int x, y;
                for (y = patch.y0; y < patch.y1; y++) {
                    for (x = patch.x0; x < patch.x1; x++) {
                        CColor c = gfx_get_pixel(bb, x, y);
                        if (GFX_G(c) > GFX_R(c) + 12 &&
                            GFX_G(c) > GFX_B(c) + 12) { greens1++; }
                    }
                }
            }
            /*
             * ...and the window's title bar turned green with it, live,
             * without a restart. The setting alone would be a preference
             * nobody applied.
             */
            SYS_LOGI("main", "CC-DEMO: ...and the title bar goes green with it "
                     "(%d green px of %d, was %d) (%s)", greens1,
                     crect_w(&patch) * crect_h(&patch), greens0,
                     (greens1 > greens0 + crect_w(&patch) * crect_h(&patch) / 3)
                         ? "OK" : "MISMATCH");
        }
        if (opt->maximize) {
            app_fileman_open();
            sh_run_frame();
            wm_set_state(wm_focused(), WM_STATE_MAXIMIZED);
            sh_run_frame();
        }
        if (opt->resize) {
            CRect fr;
            PlatEvent me;
            app_fileman_open();
            sh_run_frame();
            fr = wm_frame_rect(wm_focused());
            /* Grab the bottom-right corner and drag it out by (90,70). */
            memset(&me, 0, sizeof(me));
            me.type = PLAT_EV_MOUSE_DOWN; me.mouse_x = fr.x1 - 2; me.mouse_y = fr.y1 - 2;
            me.buttons = PLAT_MB_LEFT; plat_host_push_event(&me);
            sh_run_frame();
            me.type = PLAT_EV_MOUSE_MOVE; me.mouse_x = fr.x1 + 88; me.mouse_y = fr.y1 + 68;
            plat_host_push_event(&me);
            sh_run_frame();
            me.type = PLAT_EV_MOUSE_UP; me.buttons = 0; plat_host_push_event(&me);
            sh_run_frame();
        }
        if (opt->open_ctx) {
            /* Right-click the empty desktop to raise the context menu. */
            PlatEvent rc; memset(&rc, 0, sizeof(rc));
            rc.type = PLAT_EV_MOUSE_DOWN; rc.mouse_x = 300; rc.mouse_y = 220;
            rc.buttons = PLAT_MB_RIGHT; plat_host_push_event(&rc);
            { PlatEvent ru; memset(&ru, 0, sizeof(ru)); ru.type = PLAT_EV_MOUSE_UP;
              ru.mouse_x = 300; ru.mouse_y = 220; ru.buttons = 0;
              plat_host_push_event(&ru); }
            sh_run_frame();
        }
        if (opt->open_launcher) {
            sh_open_launcher(CTRUE);
            /* Position the pointer over the open launcher so the shot shows a
             * live highlight, and feed a move event so the highlight logic
             * runs.
             *
             * This used to run for EVERY scene, unconditionally, which is a
             * screenshot detail with a side effect: the move lands in whatever
             * popup happens to be open, misses it, and clears its highlight.
             * Any other scene that had raised a menu was quietly de-selected a
             * moment before its picture was taken -- including the window menu
             * and the desktop context menu, whose highlights simply never
             * appeared in a screenshot. */
            plat_host_set_mouse(menu_x, menu_y, 0);
            {
                PlatEvent mv;
                memset(&mv, 0, sizeof(mv));
                mv.type = PLAT_EV_MOUSE_MOVE;
                mv.mouse_x = menu_x; mv.mouse_y = menu_y; mv.buttons = 0;
                plat_host_push_event(&mv);
            }
        }
        for (f = 0; f < opt->frames; f++) { sh_run_frame(); }
        if (opt->shot_path != NULL) {
            if (plat_screenshot(opt->shot_path) == CE_OK) {
                fprintf(stderr, "CastaliaOS: wrote screenshot %s\n", opt->shot_path);
            }
        }
        reason = SH_EXIT_TO_DOS;
        snd_shutdown();
        capp_shutdown_all();
        capp_host_wm_shutdown();
        sh_shutdown();
        wm_shutdown();
        plat_shutdown();
        return reason;
    }
#endif

    /* Boot splash: a short full-screen progress moment before the desktop
     * (skipped in safe mode -- the recovery profile boots plain and fast). */
    if (!opt->safe) {
        CRect full = crect_make(0, 0, info.width, info.height);
        int p;
        for (p = 0; p <= 256; p += 24) {
            sh_splash_draw(plat_backbuffer(), p);
            plat_present(&full, 1);
            /* The pacing exists so a person can see the bar fill. With no
             * display attached it is a third of a second of nothing, paid by
             * every scripted scene, so headless draws the frames and skips
             * the wait. */
            if (!opt->headless) { plat_sleep_ms(28); }
        }
        sh_invalidate_all();   /* the first frame repaints the desktop over it */
    }

    /* First-run Welcome tour (interactive sessions only -- the headless/CI
     * path above returns before reaching here). Its "show at startup"
     * checkbox persists the [Shell] Welcome flag, so it greets until
     * dismissed for good -- and stays reopenable from the launcher. */
    if (!opt->safe && settings_get()->welcome_startup) {
        sh_dispatch_command(SH_CMD_WELCOME);
    }

    /* Interactive session loop (real hardware / emulator). */
    {
        cu32 next = plat_ticks_ms();
        while (sh_run_frame()) {
            /* ~60 Hz cap; the loop is cooperative and mostly idle. */
            cu32 now = plat_ticks_ms();
            if (now < next + 16) { plat_sleep_ms(4); }
            next = now;
        }
    }
    reason = sh_exit_reason();
    /* The "safe to turn off" screen, while video is still up. */
    if (reason == SH_EXIT_SHUTDOWN) {
        CRect full = crect_make(0, 0, info.width, info.height);
        snd_shutdown();
        sh_shutdown_screen(plat_backbuffer());
        plat_present(&full, 1);
        plat_sleep_ms(1600);
    } else {
        snd_shutdown();
    }
    capp_shutdown_all();
    capp_host_wm_shutdown();
    sh_shutdown();
    wm_shutdown();
    plat_shutdown();
    return reason;
}

int main(int argc, char **argv)
{
    char logpath[CASTALIA_MAX_PATH];
    char crashpath[CASTALIA_MAX_PATH];
    char dirtypath[CASTALIA_MAX_PATH];
    char inipath[CASTALIA_MAX_PATH];
    char bakpath[CASTALIA_MAX_PATH];
    RunOptions opt;
    int i;
    ShExitReason reason;

    /* Zero every option first so newly added demo flags default to off even if
     * not listed below (avoids stack-garbage triggering spurious demos). */
    memset(&opt, 0, sizeof(opt));
    /* Defaults: 800x600x16 preferred (Bible), headless off. */
    opt.width = 800; opt.height = 600; opt.bpp = 16;
    opt.safe = CFALSE; opt.headless = CFALSE; opt.frames = 3;
    opt.open_launcher = CFALSE; opt.open_sysinfo = CFALSE;
    opt.open_fileman = CFALSE; opt.open_dialog = CFALSE;
    opt.open_apps = CFALSE; opt.open_calc = CFALSE; opt.nav_downs = 0;
    opt.open_sheet = CFALSE; opt.open_write = CFALSE;
    opt.open_logview = CFALSE; opt.open_notepad = CFALSE;
    opt.nav_close = CFALSE; opt.open_ctx = CFALSE;
    opt.maximize = CFALSE; opt.resize = CFALSE; opt.cc_demo = CFALSE;
    opt.fm_open_row = 0; opt.alttab = CFALSE; opt.np_wrap = CFALSE;
    opt.tb_max = CFALSE; opt.icon_sel = CFALSE; opt.icons_dir = NULL;
    opt.font_face = -1;
    opt.shot_path = NULL;
    opt.record_dir = NULL;
    opt.scene = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--safe") == 0) { opt.safe = CTRUE; }
        else if (strcmp(argv[i], "--headless") == 0) { opt.headless = CTRUE; }
        else if (strcmp(argv[i], "--open-launcher") == 0) { opt.open_launcher = CTRUE; }
        else if (strcmp(argv[i], "--open-sysinfo") == 0) { opt.open_sysinfo = CTRUE; }
        else if (strcmp(argv[i], "--open-fileman") == 0) { opt.open_fileman = CTRUE; }
        else if (strcmp(argv[i], "--open-dialog") == 0) { opt.open_dialog = CTRUE; }
        else if (strcmp(argv[i], "--open-apps") == 0) { opt.open_apps = CTRUE; }
        else if (strcmp(argv[i], "--open-calc") == 0) { opt.open_calc = CTRUE; }
        else if (strcmp(argv[i], "--open-about") == 0) { opt.open_about = CTRUE; }
        else if (strcmp(argv[i], "--open-bench") == 0) { opt.open_bench = CTRUE; }
        else if (strcmp(argv[i], "--open-media") == 0) { opt.open_media = CTRUE; }
        else if (strcmp(argv[i], "--open-clock") == 0) { opt.open_clock = CTRUE; }
        else if (strcmp(argv[i], "--open-charmap") == 0) { opt.open_charmap = CTRUE; }
        else if (strcmp(argv[i], "--open-freecell") == 0) { opt.open_freecell = CTRUE; }
        else if (strcmp(argv[i], "--open-solitaire") == 0) { opt.open_solitaire = CTRUE; }
        else if (strcmp(argv[i], "--open-sheet") == 0) { opt.open_sheet = CTRUE; }
        else if (strcmp(argv[i], "--open-write") == 0) { opt.open_write = CTRUE; }
        else if (strcmp(argv[i], "--open-logview") == 0) { opt.open_logview = CTRUE; }
        else if (strcmp(argv[i], "--open-notepad") == 0) { opt.open_notepad = CTRUE; }
        else if (strcmp(argv[i], "--open-console") == 0) { opt.open_console = CTRUE; }
        else if (strcmp(argv[i], "--open-welcome") == 0) { opt.open_welcome = CTRUE; }
        else if (strcmp(argv[i], "--open-net") == 0) { opt.open_net = CTRUE; }
        else if (strcmp(argv[i], "--open-theme") == 0) { opt.open_theme = CTRUE; }
        else if (strcmp(argv[i], "--open-help") == 0) { opt.open_help = CTRUE; }
        else if (strcmp(argv[i], "--help-demo") == 0) { opt.open_help = CTRUE; opt.help_walk = CTRUE; }
        else if (strcmp(argv[i], "--diskuse-demo") == 0) { opt.diskuse_demo = CTRUE; }
        else if (strcmp(argv[i], "--open-diskuse") == 0) { opt.open_diskuse = CTRUE; }
        else if (strcmp(argv[i], "--clock-tick-demo") == 0) { opt.clock_tick_demo = CTRUE; }
        else if (strcmp(argv[i], "--repaint-demo") == 0) { opt.repaint_demo = CTRUE; }
        else if (strcmp(argv[i], "--cz-demo") == 0) { opt.cz_demo = CTRUE; }
        else if (strcmp(argv[i], "--trash-demo") == 0) { opt.trash_demo = CTRUE; }
        else if (strcmp(argv[i], "--orb-demo") == 0) { opt.orb_demo = CTRUE; }
        else if (strcmp(argv[i], "--vale-demo") == 0) { opt.vale_demo = CTRUE; }
        else if (strcmp(argv[i], "--launch-demo") == 0) { opt.launch_demo = CTRUE; }
        else if (strcmp(argv[i], "--lscroll-demo") == 0) { opt.lscroll_demo = CTRUE; }
        else if (strcmp(argv[i], "--compare-demo") == 0) { opt.compare_demo = CTRUE; }
        else if (strcmp(argv[i], "--bigfile-demo") == 0) { opt.bigfile_demo = CTRUE; }
        else if (strcmp(argv[i], "--renmany-demo") == 0) { opt.renmany_demo = CTRUE; }
        else if (strcmp(argv[i], "--hotbtn-demo") == 0) { opt.hotbtn_demo = CTRUE; }
        else if (strcmp(argv[i], "--focus-demo") == 0) { opt.focus_demo = CTRUE; }
        else if (strcmp(argv[i], "--shrink-demo") == 0) { opt.shrink_demo = CTRUE; }
        else if (strcmp(argv[i], "--cost-demo") == 0) { opt.cost_demo = CTRUE; }
        else if (strcmp(argv[i], "--wheel-demo") == 0) { opt.wheel_demo = CTRUE; }
        else if (strcmp(argv[i], "--stale-demo") == 0) { opt.stale_demo = CTRUE; }
        else if (strcmp(argv[i], "--unsaved-demo") == 0) { opt.unsaved_demo = CTRUE; }
        else if (strcmp(argv[i], "--screen-check") == 0) { opt.screen_check = CTRUE; }
        else if (strcmp(argv[i], "--cpu-demo") == 0) { opt.cpu_demo = CTRUE; }
        else if (strcmp(argv[i], "--switch-demo") == 0) { opt.switch_demo = CTRUE; }
        else if (strcmp(argv[i], "--sysmenu-demo") == 0) { opt.sysmenu_demo = CTRUE; }
        else if (strcmp(argv[i], "--sysmenu-keep") == 0) { opt.sysmenu_keep = CTRUE; }
        else if (strcmp(argv[i], "--menulook-demo") == 0) { opt.menulook_demo = CTRUE; }
        else if (strcmp(argv[i], "--deskcache-demo") == 0) { opt.deskcache_demo = CTRUE; }
        else if (strcmp(argv[i], "--archive-demo") == 0) { opt.archive_demo = CTRUE; }
        else if (strcmp(argv[i], "--deskkeys-demo") == 0) { opt.deskkeys_demo = CTRUE; }
        else if (strcmp(argv[i], "--sysinfo-demo") == 0) { opt.sysinfo_demo = CTRUE; }
        else if (strcmp(argv[i], "--cursor-demo") == 0) { opt.cursor_demo = CTRUE; }
        else if (strcmp(argv[i], "--hex-demo") == 0) { opt.hex_demo = CTRUE; }
        else if (strcmp(argv[i], "--datetime-demo") == 0) { opt.datetime_demo = CTRUE; }
        else if (strcmp(argv[i], "--mousekey-demo") == 0) { opt.mousekey_demo = CTRUE; }
        else if (strcmp(argv[i], "--mem-check") == 0) { opt.mem_check = CTRUE; }
        else if (strcmp(argv[i], "--car-demo") == 0) { opt.car_demo = CTRUE; }
        else if (strcmp(argv[i], "--crash-demo") == 0) { opt.crash_demo = CTRUE; }
        else if (strcmp(argv[i], "--capture-demo") == 0) { opt.capture_demo = CTRUE; }
        else if (strcmp(argv[i], "--theme-demo") == 0) { opt.theme_demo = CTRUE; }
        else if (strcmp(argv[i], "--thumbs-demo") == 0) { opt.thumbs_demo = CTRUE; }
        else if (strcmp(argv[i], "--nav-demo") == 0) { opt.nav_demo = CTRUE; }
        else if (strcmp(argv[i], "--dlg-exhaust") == 0) { opt.dlg_exhaust = CTRUE; }
        else if (strcmp(argv[i], "--console-demo") == 0) { opt.console_demo = CTRUE; }
        else if (strcmp(argv[i], "--freecell-demo") == 0) { opt.freecell_demo = CTRUE; }
        else if (strcmp(argv[i], "--reversi-demo") == 0) { opt.reversi_demo = CTRUE; }
        else if (strcmp(argv[i], "--winicon-demo") == 0) { opt.winicon_demo = CTRUE; }
        else if (strcmp(argv[i], "--undo-demo") == 0) { opt.undo_demo = CTRUE; }
        else if (strcmp(argv[i], "--menuaccel-demo") == 0) { opt.menuaccel_demo = CTRUE; }
        else if (strcmp(argv[i], "--assoc-demo") == 0) { opt.assoc_demo = CTRUE; }
        else if (strcmp(argv[i], "--filedlg-demo") == 0) { opt.filedlg_demo = CTRUE; }
        else if (strcmp(argv[i], "--filedlg-demo-keep") == 0) { opt.filedlg_demo = CTRUE; opt.filedlg_keep = CTRUE; }
        else if (strcmp(argv[i], "--nav-downs") == 0 && i + 1 < argc) { opt.nav_downs = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--nav-close") == 0) { opt.nav_close = CTRUE; }
        else if (strcmp(argv[i], "--ctx") == 0) { opt.open_ctx = CTRUE; }
        else if (strcmp(argv[i], "--maximize") == 0) { opt.maximize = CTRUE; }
        else if (strcmp(argv[i], "--resize") == 0) { opt.resize = CTRUE; }
        else if (strcmp(argv[i], "--cc-demo") == 0) { opt.cc_demo = CTRUE; }
        else if (strcmp(argv[i], "--fm-open-row") == 0 && i + 1 < argc) { opt.fm_open_row = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--alttab") == 0) { opt.alttab = CTRUE; }
        else if (strcmp(argv[i], "--np-wrap") == 0) { opt.np_wrap = CTRUE; }
        else if (strcmp(argv[i], "--clip-demo") == 0) { opt.clip_demo = CTRUE; }
        else if (strcmp(argv[i], "--snap-demo") == 0) { opt.snap_demo = CTRUE; }
        else if (strcmp(argv[i], "--taskman-demo") == 0) { opt.taskman_demo = CTRUE; }
        else if (strcmp(argv[i], "--vdesk-demo") == 0) { opt.vdesk_demo = CTRUE; }
        else if (strcmp(argv[i], "--paint-demo") == 0) { opt.paint_demo = CTRUE; }
        else if (strcmp(argv[i], "--paint-demo-keep") == 0) { opt.paint_demo = CTRUE; opt.paint_demo_keep = CTRUE; }
        else if (strcmp(argv[i], "--net-loopback") == 0) { opt.net_loopback = CTRUE; }
        else if (strcmp(argv[i], "--net-demo") == 0) { opt.net_demo = CTRUE; opt.net_loopback = CTRUE; }
        else if (strcmp(argv[i], "--net-ping-demo") == 0) { opt.net_ping_demo = CTRUE; opt.net_loopback = CTRUE; }
        else if (strcmp(argv[i], "--clock-demo") == 0) { opt.clock_demo = CTRUE; }
        else if (strcmp(argv[i], "--clock-demo-keep") == 0) { opt.clock_demo = CTRUE; opt.clock_demo_keep = CTRUE; }
        else if (strcmp(argv[i], "--search-demo") == 0) { opt.search_demo = CTRUE; }
        else if (strcmp(argv[i], "--dual-demo") == 0) { opt.dual_demo = CTRUE; }
        else if (strcmp(argv[i], "--capp-demo") == 0) { opt.capp_demo = CTRUE; }
        else if (strcmp(argv[i], "--anim-demo") == 0) { opt.anim_demo = CTRUE; }
        else if (strcmp(argv[i], "--desk-bench") == 0) { opt.desk_bench = CTRUE; }
        else if (strcmp(argv[i], "--hover-demo") == 0) { opt.hover_demo = CTRUE; }
        else if (strcmp(argv[i], "--tooltip-demo") == 0) { opt.tip_demo = CTRUE; }
        else if (strcmp(argv[i], "--mines-demo") == 0) { opt.mines_demo = CTRUE; }
        else if (strcmp(argv[i], "--ql-demo") == 0) { opt.ql_demo = CTRUE; }
        else if (strcmp(argv[i], "--min-demo") == 0) { opt.min_demo = CTRUE; }
        else if (strcmp(argv[i], "--wall-demo") == 0) { opt.wall_demo = CTRUE; }
        else if (strcmp(argv[i], "--splash-demo") == 0) { opt.splash_demo = CTRUE; }
        else if (strcmp(argv[i], "--saver-demo") == 0) { opt.saver_demo = CTRUE; }
        else if (strcmp(argv[i], "--shutdown-demo") == 0) { opt.shutdn_demo = CTRUE; }
        else if (strcmp(argv[i], "--tb-max") == 0) { opt.tb_max = CTRUE; }
        else if (strcmp(argv[i], "--icon-sel") == 0) { opt.icon_sel = CTRUE; }
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) { opt.width = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) { opt.height = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--bpp") == 0 && i + 1 < argc) { opt.bpp = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { opt.frames = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) { opt.shot_path = argv[++i]; }
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) { opt.record_dir = argv[++i]; }
        else if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc) { opt.scene = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--icons") == 0 && i + 1 < argc) { opt.icons_dir = argv[++i]; }
        else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            i++;
            opt.font_face = (strcmp(argv[i], "spleen") == 0) ? GFX_FACE_SPLEEN
                          : (strcmp(argv[i], "system") == 0) ? GFX_FACE_SYSTEM : -1;
        }
        else {
            /*
             * An argument nobody recognises used to be dropped in silence.
             * This driver is how every scene in the gate is run, so a
             * mistyped flag meant the scene simply did not happen -- and a
             * scene that does not happen reports no mismatch, which reads
             * exactly like a scene that passed. Three bogus "--open-" flags
             * were handed to it during a screenshot pass and it ran three
             * plain desktops without a word about any of them.
             *
             * Refusing is the whole point: a harness that quietly does
             * nothing is worse than one that stops.
             */
            fprintf(stderr, "CastaliaOS: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "  (see docs/BUILDING.md for the driver flags)\n");
            return 2;
        }
    }

    resolve_paths(logpath, sizeof(logpath), crashpath, sizeof(crashpath),
                  dirtypath, sizeof(dirtypath), inipath, sizeof(inipath));
    /*
     * Make the two folders the state lives in before anything tries to write
     * into them. sys_log_init only fopen()s its path, so on a first boot or a
     * half-finished install -- no LOGS folder yet -- the session log was
     * silently never created. Silently is the problem: the crash screen tells
     * the reader to go and look at that log, and the Log Viewer opens on it.
     *
     * Both calls are plain mkdir underneath and need nothing initialised, so
     * they can run before plat_init. CE_BUSY means it was already there.
     * Creating on demand is what the rest of this system already does for
     * DOCS, THEMES, TRASH and PHOTOS.
     */
    {
        char dir[CASTALIA_MAX_PATH];
        sys_home_path(dir, (cu32)sizeof dir, "LOGS");
        plat_mkdir(dir);
        sys_home_path(dir, (cu32)sizeof dir, "SYS");
        plat_mkdir(dir);
    }
    sys_log_init(logpath, SYS_LOG_INFO);
    sys_crash_init(crashpath, dirtypath);
    /* Before anything reads it: if the live config is unusable, put the last
     * one that booted cleanly back in its place. */
    resolve_bak(inipath, bakpath, sizeof(bakpath));
    cfg_apply_lastgood(inipath, bakpath);
    /* Load user settings (theme, clock, boot flags) from CASTALIA.INI; the
     * Control Center saves back to the same file. */
    settings_load(inipath);
    /* A --icons DIR flag overrides the saved [Assets] Icons= path (used by the
     * `make icons-demo` before/after render). */
    if (opt.icons_dir != NULL) {
        sys_strlcpy(settings_get()->icons_dir, opt.icons_dir,
                    sizeof(settings_get()->icons_dir));
    }
    if (opt.font_face >= 0) { settings_get()->font_face = opt.font_face; }
    /* Select the system text face before anything draws (splash included). */
    gfx_font_select(settings_get()->font_face);

    /* Plugin loader: expose the shared INI to add-ons' config_get and register
     * the bundled sample plugins so packages that reference them can run. */
    capp_loader_set_config(inipath);
    capp_register_builtins();

    /* Discover and validate add-on packages in APPS\ (logs each valid .CAPP's
     * manifest and rejects corrupt ones), then index the runnable ones so the
     * launcher can offer them. Launching still executes code only on demand. */
    {
        char appspath[CASTALIA_MAX_PATH];
        const char *chome = sys_home();
        sys_snprintf(appspath, sizeof(appspath), "%s/APPS", chome);
        capp_scan_dir(appspath);
        capp_index_dir(appspath);
    }

    /* Optional networking: null on host, packet-driver client on DOS. Absent
     * hardware is not an error; System Info reports the device either way.
     * --net-loopback swaps the host's null backend for the simulated wire. */
#ifdef CASTALIA_HOST
    if (opt.net_loopback) { plat_host_set_loopback(CTRUE); }
#endif
    net_init();

    SYS_LOGI("main", "%s %s (%s) starting", CASTALIA_NAME, CASTALIA_VER_STRING,
             CASTALIA_VER_STAGE);
    if (sys_crash_previous_was_dirty()) {
        SYS_LOGW("main", "previous session ended uncleanly; Safe Mode recommended");
        /* On real hardware CBOOT offers Safe Mode; honor an explicit flag here. */
    }

    /* Session loop with restart support. */
    for (;;) {
        reason = run_session(&opt);
        if (reason == SH_EXIT_RESTART_SHELL) {
            SYS_LOGI("main", "restarting shell");
            continue;
        }
        break;
    }

    /* Clean shutdown paths. */
    if (reason == SH_EXIT_SHUTDOWN) {
        SYS_LOGI("main", "shutdown requested");
        cfg_promote_lastgood(inipath, bakpath);
        sys_crash_mark_clean();
        plat_poweroff(); /* no-op on host */
    } else if (reason == SH_EXIT_REBOOT) {
        cfg_promote_lastgood(inipath, bakpath);
        sys_crash_mark_clean();
        plat_reboot();
    } else {
        SYS_LOGI("main", "exited to DOS");
        cfg_promote_lastgood(inipath, bakpath);
        sys_crash_mark_clean();
    }

    net_shutdown();
    capp_loader_shutdown();
    sys_crash_shutdown();
    sys_log_shutdown();
    return 0;
}
