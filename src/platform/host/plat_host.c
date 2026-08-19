/*
 * plat_host.c - Headless host implementation of the platform contract.
 *
 * This backend renders the entire environment into an in-memory 32-bit
 * XRGB back buffer and never touches a display. It exists so the whole
 * upper stack (gfx, wm, ui, shell, apps) can be built and RUN on a normal
 * developer machine or CI: plat_present() is a no-op flush, plat_screenshot()
 * writes a 24-bit BMP of the current frame, and the input queue can be fed
 * by the host front-end (main.c's headless driver).
 *
 * File and time services map onto the C runtime. Process launch is a logged
 * no-op. Nothing here is DOS-specific; the DOS/VESA backend lives in
 * src/platform/dos and is compiled only by the DOS makefile.
 *
 * Built only when CASTALIA_HOST is defined.
 */
#ifdef CASTALIA_HOST

#include "castalia/plat.h"
#include "castalia/sys.h"
#include "plat_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>   /* rmdir */
#include <sys/stat.h>

/* ---- state ----------------------------------------------------------- */
static GfxSurface    g_back;             /* wraps g_pixels                */
static CColor       *g_pixels = NULL;
static PlatVideoInfo g_info;
static cbool         g_inited = CFALSE;
static cu32          g_start_ticks = 0;

/* Simple input event ring, fed by the host front-end. */
#define HOST_EVQ 256
static PlatEvent g_evq[HOST_EVQ];
static int       g_ev_head = 0, g_ev_tail = 0;
static int       g_mouse_x = 0, g_mouse_y = 0, g_mouse_b = 0;

/* The hooks this file exposes to the host front-end (main.c's headless driver
 * and the tests) live in plat_host.h, which is now INCLUDED rather than having
 * two of its three prototypes copied here by hand. Copying them meant the
 * definitions below were never checked against the declarations main.c calls
 * through: a signature could have diverged silently, and the third hook --
 * plat_host_last_key_repeat -- had no local copy at all. */

/* ---- monotonic ticks ------------------------------------------------- */
static cu32 host_now(void)
{
    /*
     * Wall time, not CPU time. This used clock(), which counts processor
     * time: a frame loop that sleeps between frames burns almost no CPU, so
     * the whole system's clock crawled -- uptime ran slow, the double-click
     * window measured the wrong thing, and anything sampling elapsed time
     * saw zero. The DOS backend returns the BIOS tick, which is wall time,
     * so this is also what makes the two backends agree.
     *
     * This file is the host backend and is deliberately outside the C89 /
     * Watcom lint set (see tools/c89_lint.sh), so POSIX is available here.
     */
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (cu32)((cu32)ts.tv_sec * 1000u + (cu32)(ts.tv_nsec / 1000000L));
    }
#endif
    return (cu32)((cu32)time(NULL) * 1000u);
}

/* ---- lifecycle ------------------------------------------------------- */
CResult plat_init(const PlatVideoRequest *req, PlatVideoInfo *out_info)
{
    int w, h;
    if (g_inited) { return CE_BUSY; }

    w = (req && req->width  > 0) ? req->width  : 640;
    h = (req && req->height > 0) ? req->height : 480;
    if (req && req->prefer_safe) { w = 640; h = 480; }

    g_pixels = (CColor *)sys_alloc((cu32)w * (cu32)h * (cu32)sizeof(CColor));
    if (g_pixels == NULL) {
        SYS_LOGE("plat", "host: failed to allocate %dx%d back buffer", w, h);
        return CE_NOMEM;
    }
    gfx_surface_wrap(&g_back, g_pixels, w, h, w);

    g_info.width = w;
    g_info.height = h;
    g_info.bpp = 32; /* host renders/presents in 32bpp XRGB */
    g_info.driver_name = "host-headless";

    g_ev_head = g_ev_tail = 0;
    g_mouse_x = w / 2;
    g_mouse_y = h / 2;
    g_mouse_b = 0;
    g_start_ticks = host_now();
    g_inited = CTRUE;

    if (out_info) { *out_info = g_info; }
    SYS_LOGI("plat", "host backend up: %dx%dx%d (%s)", w, h, g_info.bpp,
             g_info.driver_name);
    return CE_OK;
}

void plat_shutdown(void)
{
    if (!g_inited) { return; }
    if (g_pixels != NULL) {
        sys_free(g_pixels, (cu32)g_info.width * (cu32)g_info.height *
                           (cu32)sizeof(CColor));
        g_pixels = NULL;
    }
    g_inited = CFALSE;
    SYS_LOGI("plat", "host backend down");
}

GfxSurface *plat_backbuffer(void)
{
    return g_inited ? &g_back : NULL;
}

void plat_present(const CRect *rects, int count)
{
    /* Headless: nothing to flush to a display. We keep the signature and
     * validate the rectangles so bugs in dirty tracking surface in tests. */
    int i;
    CASTALIA_UNUSED(count);
    if (rects == NULL) { return; }
    for (i = 0; i < count; i++) {
        if (crect_empty(&rects[i])) {
            SYS_LOGT("plat", "present: empty dirty rect %d", i);
        }
    }
}

void plat_video_info(PlatVideoInfo *out_info)
{
    if (out_info) { *out_info = g_info; }
}

/*
 * The host has no card to ask, so it reports what this build can actually DO
 * rather than pretending to enumerate hardware: the presenter packs 3:3:2 at
 * 8bpp and 5:6:5 at 16, at a buffer of any size. plat_video_modes_source()
 * says whose claim it is, so the report cannot be read as a hardware reading.
 *
 * The order here is deliberately NOT the ladder order, and that matters. A
 * real VBE list arrives in whatever sequence the BIOS stored it, and the
 * report's whole job is to tidy that. Emitted already-sorted, the sorting
 * could break completely and every demo would still pass -- which is exactly
 * what happened the first time this was written. It is shuffled so the sort
 * has real work to do, and the current mode is repeated so the de-duplication
 * does too.
 */
int plat_video_modes(PlatVideoMode *out, int max)
{
    static const int MODES[][3] = {
        /* w, h, bpp -- jumbled on purpose; see above. */
        { 1024, 768, 8 },
        {  640, 480, 16 },
        {  800, 600, 16 },
        { 1024, 768, 16 },
        {  640, 480, 8 },
        {  800, 600, 8 }
    };
    int i, n = 0;
    if (out == NULL || max <= 0) { return 0; }
    for (i = 0; i < (int)(sizeof(MODES) / sizeof(MODES[0])) && n < max; i++) {
        out[n].w   = MODES[i][0];
        out[n].h   = MODES[i][1];
        out[n].bpp = MODES[i][2];
        out[n].lfb = CTRUE;   /* a malloc'd buffer is as linear as it gets */
        n++;
    }
    /* The size actually on screen, which the headless driver can set to
     * anything with --height. Listed even when it duplicates one above, so
     * the report's de-duplication is exercised rather than assumed. */
    if (n < max) {
        out[n].w = g_info.width; out[n].h = g_info.height;
        out[n].bpp = 8; out[n].lfb = CTRUE; n++;
    }
    if (n < max) {
        out[n].w = g_info.width; out[n].h = g_info.height;
        out[n].bpp = 16; out[n].lfb = CTRUE; n++;
    }
    return n;
}

const char *plat_video_modes_source(void)
{
    return "software presenter";
}

/* ---- input ----------------------------------------------------------- */
/*
 * Queue one synthetic event for the scenes.
 *
 * A FULL queue is reported, not swallowed. It used to drop silently, and a
 * scene that pushed more than HOST_EVQ between frames -- which is easier than
 * it sounds: clearing a full path takes 268 backspaces -- lost the END of its
 * burst. The typed name and the Enter after it never arrived, the dialog sat
 * there having received only backspaces, and the scene reported that saving
 * had not happened. Which was true, and not for the reason it looked like.
 *
 * Once per overflow run, so a scene that floods the queue says so without
 * filling the log with one line per lost key.
 */
void plat_host_push_event(const PlatEvent *ev)
{
    static cbool warned = CFALSE;
    int next = (g_ev_head + 1) % HOST_EVQ;
    if (ev == NULL) { return; }
    if (next == g_ev_tail) {
        if (!warned) {
            warned = CTRUE;
            SYS_LOGW("plat", "host: event queue full (%d) -- events are being "
                             "DROPPED; run a frame to drain it", HOST_EVQ);
        }
        return;
    }
    warned = CFALSE;
    g_evq[g_ev_head] = *ev;
    g_ev_head = next;
    /* Track latest mouse state from the event. */
    if (ev->type == PLAT_EV_MOUSE_MOVE || ev->type == PLAT_EV_MOUSE_DOWN ||
        ev->type == PLAT_EV_MOUSE_UP) {
        g_mouse_x = ev->mouse_x;
        g_mouse_y = ev->mouse_y;
        g_mouse_b = ev->buttons;
    }
}

void plat_host_set_mouse(int x, int y, int buttons)
{
    g_mouse_x = x; g_mouse_y = y; g_mouse_b = buttons;
}

cbool plat_poll_event(PlatEvent *ev)
{
    if (ev == NULL || g_ev_tail == g_ev_head) { return CFALSE; }
    *ev = g_evq[g_ev_tail];
    g_ev_tail = (g_ev_tail + 1) % HOST_EVQ;
    return CTRUE;
}

void plat_mouse_state(int *x, int *y, int *buttons)
{
    if (x) { *x = g_mouse_x; }
    if (y) { *y = g_mouse_y; }
    if (buttons) { *buttons = g_mouse_b; }
}

/* ---- timing ---------------------------------------------------------- */
cu32 plat_ticks_ms(void)
{
    if (!g_inited) { return host_now(); }
    return host_now() - g_start_ticks;
}

void plat_sleep_ms(cu32 ms)
{
    /* This used to be a no-op "because headless runs do not need to pace",
     * which quietly made every caller wrong: a scene that slept between
     * frames to let the wall clock advance did not advance it, and the
     * measurements it then took were of zero elapsed time. A sleep that does
     * not sleep is worse than no sleep at all, because the caller believes
     * it. So it sleeps. Callers that only want pacing for a human to watch
     * should not be calling it in a headless run in the first place. */
    struct timespec ts;
    if (ms == 0u) { return; }
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        /* resume the remainder */
    }
}

void plat_wall_clock(int *hour, int *minute, int *second)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt != NULL) {
        if (hour)   { *hour   = lt->tm_hour; }
        if (minute) { *minute = lt->tm_min;  }
        if (second) { *second = lt->tm_sec;  }
    } else {
        if (hour)   { *hour = 0; }
        if (minute) { *minute = 0; }
        if (second) { *second = 0; }
    }
}

/*
 * The host will not have its clock set by a program in a window.
 *
 * It could be attempted -- settimeofday exists -- and it would fail without
 * privilege, or succeed and move a clock this program does not own. Neither
 * is what a headless test run wants, and a silent no-op returning CE_OK would
 * be worse than both: the Control Center would report success and the DOS
 * path, which is the one that matters, would never be exercised differently.
 */
CResult plat_set_wall_clock(int hour, int minute, int second)
{
    SYS_LOGI("plat", "host: refusing to set the clock to %02d:%02d:%02d",
             hour, minute, second);
    return CE_UNSUPPORTED;
}

static int g_last_kr_delay = -1, g_last_kr_cps = -1;

void plat_host_last_key_repeat(int *delay_ms, int *cps)
{
    if (delay_ms) { *delay_ms = g_last_kr_delay; }
    if (cps)      { *cps      = g_last_kr_cps; }
}

CResult plat_set_key_repeat(int delay_ms, int cps)
{
    /* Recorded before the refusal: what a scene needs to know is that the
     * request arrived, which is true whether or not it could be honoured. */
    g_last_kr_delay = delay_ms;
    g_last_kr_cps   = cps;
    SYS_LOGI("plat", "host: refusing to set typematic %d ms / %d cps",
             delay_ms, cps);
    return CE_UNSUPPORTED;
}

CResult plat_set_wall_date(int year, int month, int day)
{
    SYS_LOGI("plat", "host: refusing to set the date to %04d-%02d-%02d",
             year, month, day);
    return CE_UNSUPPORTED;
}

void plat_wall_date(int *year, int *month, int *day, int *weekday)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt != NULL) {
        if (year)    { *year    = lt->tm_year + 1900; }
        if (month)   { *month   = lt->tm_mon + 1; }
        if (day)     { *day     = lt->tm_mday; }
        if (weekday) { *weekday = lt->tm_wday; }
    } else {
        if (year)    { *year = 1999; }
        if (month)   { *month = 1; }
        if (day)     { *day = 1; }
        if (weekday) { *weekday = 5; }
    }
}

/* ---- files ----------------------------------------------------------- */
struct PlatFile { FILE *fp; };

CResult plat_fseek(PlatFile *f, long offset)
{
    if (f == NULL || f->fp == NULL || offset < 0L) { return CE_INVALID; }
    return (fseek(f->fp, offset, SEEK_SET) == 0) ? CE_OK : CE_IO;
}

PlatFile *plat_fopen(const char *path, const char *mode)
{
    PlatFile *f;
    FILE *fp = fopen(path, mode);
    if (fp == NULL) { return NULL; }
    f = (PlatFile *)sys_alloc((cu32)sizeof(PlatFile));
    if (f == NULL) { fclose(fp); return NULL; }
    f->fp = fp;
    return f;
}

cu32 plat_fread(PlatFile *f, void *buf, cu32 bytes)
{
    if (f == NULL) { return 0; }
    return (cu32)fread(buf, 1, (size_t)bytes, f->fp);
}

cu32 plat_fwrite(PlatFile *f, const void *buf, cu32 bytes)
{
    if (f == NULL) { return 0; }
    return (cu32)fwrite(buf, 1, (size_t)bytes, f->fp);
}

CResult plat_fclose(PlatFile *f)
{
    CResult r = CE_OK;
    if (f == NULL) { return CE_INVALID; }
    if (fclose(f->fp) != 0) { r = CE_IO; }
    sys_free(f, (cu32)sizeof(PlatFile));
    return r;
}

cbool plat_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp != NULL) { fclose(fp); return CTRUE; }
    return CFALSE;
}

CResult plat_file_remove(const char *path)
{
    return (remove(path) == 0) ? CE_OK : CE_IO;
}

CResult plat_file_rename(const char *from, const char *to)
{
    return (rename(from, to) == 0) ? CE_OK : CE_IO;
}

long plat_file_size(const char *path)
{
    long sz;
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { return -1; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    sz = ftell(fp);
    fclose(fp);
    return sz;
}

CResult plat_mkdir(const char *path)
{
    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    if (mkdir(path, 0777) == 0) { return CE_OK; }
    if (errno == EEXIST) { return CE_BUSY; }
    return CE_IO;
}

CResult plat_dir_remove(const char *path)
{
    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    if (rmdir(path) == 0) { return CE_OK; }
    if (errno == ENOTEMPTY || errno == EEXIST) { return CE_BUSY; }
    return CE_IO;
}

/* ---- directory enumeration (portable POSIX/Win via dirent) ----------- */
/* Kept behind the platform layer; the DOS backend uses INT 21h findfirst. */
#include <dirent.h>
#include <sys/stat.h>

struct PlatDir { DIR *d; char base[CASTALIA_MAX_PATH]; };

PlatDir *plat_opendir(const char *path)
{
    PlatDir *pd;
    DIR *d = opendir(path);
    if (d == NULL) { return NULL; }
    pd = (PlatDir *)sys_alloc((cu32)sizeof(PlatDir));
    if (pd == NULL) { closedir(d); return NULL; }
    pd->d = d;
    sys_strlcpy(pd->base, path, sizeof(pd->base));
    return pd;
}

cbool plat_readdir(PlatDir *d, PlatDirEntry *out)
{
    struct dirent *de;
    if (d == NULL || out == NULL) { return CFALSE; }
    de = readdir(d->d);
    if (de == NULL) { return CFALSE; }
    sys_strlcpy(out->name, de->d_name, sizeof(out->name));
    {
        char full[CASTALIA_MAX_PATH];
        struct stat st;
        sys_strlcpy(full, d->base, sizeof(full));
        sys_strlcat(full, "/", sizeof(full));
        sys_strlcat(full, de->d_name, sizeof(full));
        if (stat(full, &st) == 0) {
            out->is_dir = (st.st_mode & S_IFDIR) ? CTRUE : CFALSE;
            out->size = (long)st.st_size;
        } else {
            out->is_dir = CFALSE;
            out->size = 0;
        }
    }
    return CTRUE;
}

void plat_closedir(PlatDir *d)
{
    if (d == NULL) { return; }
    closedir(d->d);
    sys_free(d, (cu32)sizeof(PlatDir));
}

/* ---- process launch / power ------------------------------------------ */
CResult plat_run_program(const char *path, const char *args,
                         const char *workdir, int *out_exit_code)
{
    SYS_LOGI("plat", "host: run_program stub path='%s' args='%s' cwd='%s'",
             (path ? path : ""), (args ? args : ""), (workdir ? workdir : ""));
    if (out_exit_code) { *out_exit_code = 0; }
    return CE_UNSUPPORTED; /* the DOS backend runs real child programs */
}

void plat_reboot(void)   { SYS_LOGI("plat", "host: reboot requested (no-op)"); }
void plat_poweroff(void) { SYS_LOGI("plat", "host: poweroff requested (no-op)"); }

const char *plat_identity(void)
{
    return "CastaliaOS host backend (portable C / no display)";
}

/* ---- screenshot: 24-bit BMP of the current back buffer --------------- */
/*
 * Through the shared codec, exactly as the DOS backend does it.
 *
 * This used to be a second BMP writer -- its own header, its own row loop, no
 * test covering it -- sitting a few files away from the one that is tested.
 * Two writers of one format is one writer too many: they can disagree, and
 * only one of them would be noticed. It also called fwrite() once per PIXEL,
 * which is 480,000 calls for one 800x600 screenshot, and made saving a picture
 * cost more than drawing a hundred and twenty frames of the desktop.
 */
CResult plat_screenshot(const char *path)
{
    CResult rc;
    if (!g_inited || g_pixels == NULL) { return CE_FAIL; }
    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    rc = gfx_bmp_save(&g_back, path);
    if (rc == CE_OK) {
        SYS_LOGI("plat", "host: wrote screenshot %s (%dx%d)", path,
                 g_info.width, g_info.height);
    }
    return rc;
}

#endif /* CASTALIA_HOST */

/*
 * Processor identification on the host.
 *
 * Real CPUID where the compiler gives us a portable way to ask for it, so the
 * decoding in cpu_core.c is exercised against a genuine processor and not only
 * against the numbers a test made up. Anywhere else -- a non-x86 host, or a
 * compiler without <cpuid.h> -- this reports honestly that it does not know,
 * rather than fabricating a plausible-looking Pentium.
 */
#if defined(__i386__) || defined(__x86_64__)
#if defined(__GNUC__)
#define CASTALIA_HAVE_CPUID 1
#include <cpuid.h>
#endif
#endif

/*
 * Machine inventory on the host: there isn't one.
 *
 * This host is not DOS, has no conventional-memory line, and its PCI bus --
 * if the machine even has one -- says nothing about the machine CastaliaOS
 * will run on. Every field is filled with the value that means "did not find
 * out", which is what mach_core.c turns into "n/a" and "not DOS". The
 * temptation here is to report the Linux box's real memory so the window
 * looks alive in a screenshot; that would put a number on the line somebody
 * reads to decide whether a target machine has enough RAM.
 */
void plat_machine_info(PlatMachineInfo *out)
{
    if (out == NULL) { return; }
    memset(out, 0, sizeof(*out));
    out->conv_free_kb = -1;
    out->ext_free_kb  = -1;
    out->pci_bios     = CFALSE;
}

cbool plat_cpu_id(PlatCpuId *out)
{
#ifdef CASTALIA_HAVE_CPUID
    unsigned int a = 0, b = 0, c = 0, d = 0;
    int i;
    if (out == NULL) { return CFALSE; }
    memset(out, 0, sizeof(*out));
    if (!__get_cpuid(0u, &a, &b, &c, &d)) { return CFALSE; }
    for (i = 0; i < 4; i++) { out->vendor[i]     = (char)((b >> (i * 8)) & 0xFF); }
    for (i = 0; i < 4; i++) { out->vendor[4 + i] = (char)((d >> (i * 8)) & 0xFF); }
    for (i = 0; i < 4; i++) { out->vendor[8 + i] = (char)((c >> (i * 8)) & 0xFF); }
    out->vendor[12] = '\0';
    if (!__get_cpuid(1u, &a, &b, &c, &d)) { return CFALSE; }
    out->family   = (int)((a >> 8) & 0xF);
    out->model    = (int)((a >> 4) & 0xF);
    out->stepping = (int)(a & 0xF);
    /* Extended family/model, as every processor past the Pentium Pro needs. */
    if (out->family == 0xF) { out->family += (int)((a >> 20) & 0xFF); }
    if (out->family == 0x6 || out->family >= 0xF) {
        out->model += (int)(((a >> 16) & 0xF) << 4);
    }
    out->features = (cu32)d;
    out->has_cpuid = CTRUE;
    return CTRUE;
#else
    if (out == NULL) { return CFALSE; }
    memset(out, 0, sizeof(*out));
    return CFALSE;
#endif
}
