/*
 * capp_sample.c - Built-in sample .CAPP add-ons, reached only through the ABI.
 *
 * These are ordinary CappPlugin implementations: they never touch the shell's
 * internals, only the CappHostApi they are handed at init. They are compiled
 * into the image and registered as builtins (capp_register_builtin), so a
 * package whose code section is a builtin reference ("<tag>hello") runs this
 * exact code through the loader -- proving the plugin pipeline end to end on a
 * machine with no dynamic code loader. A DOS-loaded native plugin would export
 * the same CappPlugin under CAPP_ENTRY_SYMBOL and behave identically.
 *
 * 'hello' is a static one-shot; 'clock' uses the cooperative frame() tick and
 * config_get, exercising the full host api surface.
 */
#include "castalia/capp_loader.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

#include <string.h>

/* ---- 'hello': draws a card once on open ------------------------------ */
static const CappHostApi *g_hello_host;
static void *g_hello_win;

static int hello_init(const CappHostApi *host)
{
    g_hello_host = host;
    host->log(CAPP_LOG_INFO, "hello", "hello add-on initialized");
    return 0;
}

static void hello_open(void)
{
    GfxSurface *s;
    CRect r;
    if (g_hello_host == NULL) { return; }
    g_hello_win = g_hello_host->open_window("Add-on: Hello", 240, 96);
    if (g_hello_win == NULL) { return; }
    s = g_hello_host->window_surface(g_hello_win);
    if (s == NULL) { return; }
    gfx_clear(s, GFX_RGB(0x10, 0x50, 0x60));
    r = crect_make(0, 0, s->w, s->h);
    gfx_frame_rect(s, &r, GFX_RGB(0x80, 0xD0, 0xE0));
    gfx_draw_text(s, GFX_FONT_BOLD, 12, 16, "Hello from a .CAPP add-on!",
                  GFX_RGB(0xF0, 0xF8, 0xFF));
    gfx_draw_text(s, GFX_FONT_SYSTEM, 12, 40,
                  "This window was opened by plugin", GFX_RGB(0xC8, 0xE0, 0xE8));
    gfx_draw_text(s, GFX_FONT_SYSTEM, 12, 52,
                  "code, entirely through the ABI.", GFX_RGB(0xC8, 0xE0, 0xE8));
    g_hello_host->invalidate(g_hello_win);
}

static void hello_shutdown(void)
{
    if (g_hello_host != NULL && g_hello_win != NULL) {
        g_hello_host->close_window(g_hello_win);
    }
    g_hello_win = NULL;
}

static const CappPlugin HELLO_PLUGIN = {
    CAPP_ABI_VERSION, 0, "Hello", "1.0.0",
    hello_init, hello_open, NULL, hello_shutdown
};
static const CappPlugin *hello_entry(void) { return &HELLO_PLUGIN; }

/* ---- 'clock': live digital clock via the frame() tick ---------------- */
static const CappHostApi *g_clock_host;
static void *g_clock_win;
static cbool g_clock_seconds;
static char  g_clock_last[16];

static int clock_init(const CappHostApi *host)
{
    const char *sec;
    g_clock_host = host;
    /* Honor the shared taskbar setting through the config service. */
    sec = host->config_get("Taskbar", "ClockSeconds", "1");
    g_clock_seconds = (sec != NULL && (sec[0] == '1' || sec[0] == 't' ||
                                       sec[0] == 'T' || sec[0] == 'y' ||
                                       sec[0] == 'Y')) ? CTRUE : CFALSE;
    g_clock_last[0] = '\0';
    host->log(CAPP_LOG_INFO, "clock", "clock add-on initialized");
    return 0;
}

static void clock_render(const char *now)
{
    GfxSurface *s;
    CRect r;
    if (g_clock_host == NULL || g_clock_win == NULL) { return; }
    s = g_clock_host->window_surface(g_clock_win);
    if (s == NULL) { return; }
    gfx_clear(s, GFX_RGB(0x08, 0x10, 0x18));
    r = crect_make(0, 0, s->w, s->h);
    gfx_frame_rect(s, &r, GFX_RGB(0x30, 0x50, 0x60));
    gfx_draw_text(s, GFX_FONT_BOLD, 24, 24, now, GFX_RGB(0x40, 0xFF, 0x80));
    g_clock_host->invalidate(g_clock_win);
}

static void clock_open(void)
{
    char now[16];
    if (g_clock_host == NULL) { return; }
    g_clock_win = g_clock_host->open_window("Add-on: Clock", 140, 60);
    if (g_clock_win == NULL) { return; }
    sys_format_clock_ex(now, sizeof(now), g_clock_seconds);
    sys_strlcpy(g_clock_last, now, sizeof(g_clock_last));
    clock_render(now);
}

static void clock_frame(void)
{
    char now[16];
    if (g_clock_win == NULL) { return; }
    sys_format_clock_ex(now, sizeof(now), g_clock_seconds);
    if (sys_stricmp(now, g_clock_last) != 0) {   /* redraw only on change */
        sys_strlcpy(g_clock_last, now, sizeof(g_clock_last));
        clock_render(now);
    }
}

static void clock_shutdown(void)
{
    if (g_clock_host != NULL && g_clock_win != NULL) {
        g_clock_host->close_window(g_clock_win);
    }
    g_clock_win = NULL;
}

static const CappPlugin CLOCK_PLUGIN = {
    CAPP_ABI_VERSION, 0, "Clock", "1.0.0",
    clock_init, clock_open, clock_frame, clock_shutdown
};
static const CappPlugin *clock_entry(void) { return &CLOCK_PLUGIN; }

/* ---- registration ---------------------------------------------------- */
void capp_register_builtins(void)
{
    capp_register_builtin("hello", hello_entry);
    capp_register_builtin("clock", clock_entry);
}
