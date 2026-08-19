/*
 * plat_dos.c - DOS/VESA implementation of the platform contract (plat.h).
 *
 * Ties together vesa.c (video), mouse.c (INT 33h), keyboard.c (INT 16h), and
 * dos_proc.c (child launch / power) into the single Castalia-owned platform
 * interface. The upper stack is identical to the host build; only this file
 * and its siblings change.
 *
 * Event synthesis: plat_poll_event() turns the polled DOS input state
 * (mouse position/buttons, keyboard buffer) into the same event stream the
 * host backend produces, so wm/shell code is platform-agnostic.
 *
 * Files use the C runtime (Open Watcom stdio maps onto INT 21h). Directory
 * enumeration uses _dos_findfirst/_dos_findnext.
 *
 * COMPILED ONLY under CASTALIA_DOS. Verified on QEMU/Bochs/hardware per
 * docs/TESTING.md.
 */
#ifdef CASTALIA_DOS

#include "castalia/plat.h"
#include "castalia/sys.h"

#include "vesa.h"
#include "mouse.h"
#include "keyboard.h"
#include "dos_proc.h"

#include <i86.h>
#include <dos.h>
#include <direct.h>   /* mkdir (Open Watcom) */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GfxSurface   g_back;
static CColor      *g_pixels = NULL;
static PlatVideoInfo g_info;
static VesaMode     g_vmode;
static cbool        g_inited = CFALSE;
static cu32         g_start_tick = 0;

/* previous input state for event synthesis */
static int g_last_x = 0, g_last_y = 0, g_last_b = 0;

/* ---- BIOS timer tick (0040:006C, ~18.2 Hz) --------------------------- */
static cu32 read_bios_ticks(void)
{
    return *(volatile cu32 *)0x0000046CUL; /* low 1MB identity-mapped */
}
static cu32 ticks_to_ms(cu32 t) { return (cu32)((t * 55UL)); }

/* ---- lifecycle ------------------------------------------------------- */
CResult plat_init(const PlatVideoRequest *req, PlatVideoInfo *out_info)
{
    int want_w, want_h, want_bpp;
    cbool safe;
    CResult vr;

    if (g_inited) { return CE_BUSY; }
    safe = (req && req->prefer_safe) ? CTRUE : CFALSE;
    want_w   = (req && req->width  > 0) ? req->width  : 800;
    want_h   = (req && req->height > 0) ? req->height : 600;
    want_bpp = (req && req->bpp    > 0) ? req->bpp    : 16;

    if (vesa_init() != CE_OK) {
        SYS_LOGE("plat", "no VESA BIOS; cannot start graphical session");
        return CE_UNSUPPORTED;
    }

    /* Fallback ladder (Bible perf budget). Safe mode forces 640x480x8. */
    vr = CE_FAIL;
    if (!safe) {
        vr = vesa_set_mode(want_w, want_h, want_bpp, &g_vmode);
        if (vr != CE_OK) { vr = vesa_set_mode(640, 480, 16, &g_vmode); }
    }
    if (vr != CE_OK) { vr = vesa_set_mode(640, 480, 8, &g_vmode); }
    if (vr != CE_OK) {
        SYS_LOGE("plat", "no usable VESA mode; returning to DOS");
        vesa_shutdown();
        return CE_UNSUPPORTED;
    }

    /* 32-bit XRGB back buffer in system RAM (not VRAM). */
    g_pixels = (CColor *)sys_alloc((cu32)g_vmode.width * (cu32)g_vmode.height *
                                   (cu32)sizeof(CColor));
    if (g_pixels == NULL) {
        vesa_shutdown();
        return CE_NOMEM;
    }
    gfx_surface_wrap(&g_back, g_pixels, g_vmode.width, g_vmode.height,
                     g_vmode.width);

    g_info.width = g_vmode.width;
    g_info.height = g_vmode.height;
    g_info.bpp = g_vmode.bpp;
    g_info.driver_name = g_vmode.linear ? "vesa-linear" : "vesa-banked";

    mouse_init(g_vmode.width, g_vmode.height);
    keyboard_init();
    g_last_x = g_vmode.width / 2;
    g_last_y = g_vmode.height / 2;
    g_last_b = 0;
    g_start_tick = read_bios_ticks();
    g_inited = CTRUE;

    if (out_info) { *out_info = g_info; }
    SYS_LOGI("plat", "DOS backend up: %dx%dx%d", g_info.width, g_info.height,
             g_info.bpp);
    return CE_OK;
}

void plat_shutdown(void)
{
    if (!g_inited) { return; }
    mouse_shutdown();
    keyboard_shutdown();
    vesa_shutdown();
    if (g_pixels != NULL) {
        sys_free(g_pixels, (cu32)g_info.width * (cu32)g_info.height *
                           (cu32)sizeof(CColor));
        g_pixels = NULL;
    }
    g_inited = CFALSE;
}

GfxSurface *plat_backbuffer(void) { return g_inited ? &g_back : NULL; }

void plat_present(const CRect *rects, int count)
{
    int i;
    if (!g_inited) { return; }
    if (rects == NULL || count <= 0) {
        CRect full = crect_make(0, 0, g_info.width, g_info.height);
        vesa_present_rect(&g_back, &full);
        return;
    }
    for (i = 0; i < count; i++) {
        vesa_present_rect(&g_back, &rects[i]);
    }
}

void plat_video_info(PlatVideoInfo *out_info)
{
    if (out_info) { *out_info = g_info; }
}

/*
 * The card's own list, as the BIOS reports it. Text modes are dropped here
 * because nothing downstream has any use for them; everything else is passed
 * through untouched, including depths this system cannot drive, so that the
 * report can say what the card offers rather than only what worked.
 */
int plat_video_modes(PlatVideoMode *out, int max)
{
    /* static, not automatic: 256 entries is several kilobytes and the DOS
     * stack is not the place for it. */
    static VbeMode raw[VESA_MAX_MODES];
    int i, n, k = 0;
    if (out == NULL || max <= 0) { return 0; }
    n = vesa_enum_modes(raw, VESA_MAX_MODES);
    for (i = 0; i < n && k < max; i++) {
        if (!raw[i].supported || !raw[i].graphics) { continue; }
        out[k].w   = raw[i].w;
        out[k].h   = raw[i].h;
        out[k].bpp = raw[i].bpp;
        out[k].lfb = raw[i].lfb;
        k++;
    }
    return k;
}

const char *plat_video_modes_source(void)
{
    return "VESA BIOS";
}

/* ---- input (state polling -> event stream) --------------------------- */
cbool plat_poll_event(PlatEvent *ev)
{
    int x, y, b;
    int key = 0, ch = 0;

    if (ev == NULL || !g_inited) { return CFALSE; }

    /* Keyboard first so typing stays responsive. */
    if (keyboard_poll(&key, &ch)) {
        ev->type = PLAT_EV_KEY_DOWN;
        ev->key = key;
        ev->ch = ch;
        ev->mods = keyboard_mods();
        ev->wheel = 0;
        mouse_read(&ev->mouse_x, &ev->mouse_y, &ev->buttons);
        return CTRUE;
    }

    /*
     * The wheel BEFORE position and buttons, because reading it consumes the
     * count: a notch turned in the same poll as a move would otherwise be
     * dropped on the floor while the move was reported, and scrolling would
     * miss detents exactly when the hand was also moving the mouse.
     */
    {
        int notches = mouse_read_wheel();
        if (notches != 0) {
            mouse_read(&x, &y, &b);
            ev->type = PLAT_EV_MOUSE_WHEEL;
            ev->mouse_x = x; ev->mouse_y = y; ev->buttons = b;
            ev->key = 0; ev->ch = 0; ev->mods = keyboard_mods();
            ev->wheel = notches;
            g_last_x = x; g_last_y = y;
            return CTRUE;
        }
    }

    mouse_read(&x, &y, &b);
    if (b != g_last_b) {
        int changed = b ^ g_last_b;
        ev->type = (b & changed) ? PLAT_EV_MOUSE_DOWN : PLAT_EV_MOUSE_UP;
        ev->mouse_x = x; ev->mouse_y = y; ev->buttons = b;
        ev->key = 0; ev->ch = 0; ev->mods = keyboard_mods();
        ev->wheel = 0;
        g_last_b = b; g_last_x = x; g_last_y = y;
        return CTRUE;
    }
    if (x != g_last_x || y != g_last_y) {
        ev->type = PLAT_EV_MOUSE_MOVE;
        ev->mouse_x = x; ev->mouse_y = y; ev->buttons = b;
        ev->key = 0; ev->ch = 0; ev->mods = keyboard_mods();
        ev->wheel = 0;
        g_last_x = x; g_last_y = y;
        return CTRUE;
    }
    return CFALSE;
}

void plat_mouse_state(int *x, int *y, int *buttons)
{
    mouse_read(x, y, buttons);
}

/* ---- timing ---------------------------------------------------------- */
cu32 plat_ticks_ms(void)
{
    if (!g_inited) { return ticks_to_ms(read_bios_ticks()); }
    return ticks_to_ms(read_bios_ticks() - g_start_tick);
}

void plat_sleep_ms(cu32 ms)
{
    cu32 target = plat_ticks_ms() + ms;
    /* Yield to DOS/TSRs while we wait (INT 28h idle). */
    while (plat_ticks_ms() < target) {
        union REGS r;
        int386(0x28, &r, &r);
    }
}

void plat_wall_clock(int *hour, int *minute, int *second)
{
    union REGS r;
    r.h.ah = 0x2C;              /* DOS get time */
    int386(0x21, &r, &r);
    if (hour)   { *hour   = r.h.ch; }
    if (minute) { *minute = r.h.cl; }
    if (second) { *second = r.h.dh; }
}

CResult plat_set_wall_clock(int hour, int minute, int second)
{
    union REGS r;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) { return CE_INVALID; }
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x2D;                    /* DOS set time */
    r.h.ch = (unsigned char)hour;
    r.h.cl = (unsigned char)minute;
    r.h.dh = (unsigned char)second;
    r.h.dl = 0;                       /* hundredths */
    int386(0x21, &r, &r);
    /* AL is 0FFh when DOS rejected the time and 0 when it took it. The values
     * were range-checked above, so a refusal here means the clock hardware
     * would not take it -- which is a real answer on a machine with a dead
     * battery and must not be reported as success. */
    return (r.h.al == 0) ? CE_OK : CE_FAIL;
}

CResult plat_set_key_repeat(int delay_ms, int cps)
{
    /* The BIOS steps. Rate index r gives roughly 30/(8+(r&7)) >> (r>>3)
     * characters per second; this table is that curve sampled at the values
     * worth offering, so the panel picks a real step instead of computing an
     * index nobody can check. */
    static const int CPS[8]   = { 2, 3, 5, 8, 10, 15, 20, 30 };
    static const int RATE[8]  = { 0x1F, 0x1A, 0x14, 0x0D, 0x0A, 0x05, 0x02, 0x00 };
    union REGS r;
    int i, best = 0, bestd = 0x7FFF, d2;

    if (delay_ms <= 375)      { d2 = 0; }
    else if (delay_ms <= 625) { d2 = 1; }
    else if (delay_ms <= 875) { d2 = 2; }
    else                      { d2 = 3; }

    for (i = 0; i < 8; i++) {
        int diff = (CPS[i] > cps) ? (CPS[i] - cps) : (cps - CPS[i]);
        if (diff < bestd) { bestd = diff; best = i; }
    }

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0305;                       /* set typematic rate and delay */
    r.h.bh = (unsigned char)d2;
    r.h.bl = (unsigned char)RATE[best];
    int386(0x16, &r, &r);
    /* INT 16h AX=0305h reports nothing, so this cannot say more than that it
     * was asked. A BIOS that ignores it leaves the old rate, which the user
     * will see immediately -- better than inventing a confirmation. */
    return CE_OK;
}

CResult plat_set_wall_date(int year, int month, int day)
{
    union REGS r;
    if (year < 1980 || year > 2099) { return CE_INVALID; }
    if (month < 1 || month > 12 || day < 1 || day > 31) { return CE_INVALID; }
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x2B;                    /* DOS set date */
    r.w.cx = (unsigned short)year;
    r.h.dh = (unsigned char)month;
    r.h.dl = (unsigned char)day;
    int386(0x21, &r, &r);
    return (r.h.al == 0) ? CE_OK : CE_FAIL;
}

void plat_wall_date(int *year, int *month, int *day, int *weekday)
{
    union REGS r;
    r.h.ah = 0x2A;              /* DOS get date: CX=year DH=month DL=day
                                 * AL=weekday (0=Sunday) */
    int386(0x21, &r, &r);
    if (year)    { *year    = (int)r.w.cx; }
    if (month)   { *month   = (int)r.h.dh; }
    if (day)     { *day     = (int)r.h.dl; }
    if (weekday) { *weekday = (int)r.h.al; }
}

/* ---- files (C runtime == INT 21h) ------------------------------------ */
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
{ return f ? (cu32)fread(buf, 1, (size_t)bytes, f->fp) : 0; }
cu32 plat_fwrite(PlatFile *f, const void *buf, cu32 bytes)
{ return f ? (cu32)fwrite(buf, 1, (size_t)bytes, f->fp) : 0; }
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
    if (fp) { fclose(fp); return CTRUE; }
    return CFALSE;
}
CResult plat_file_remove(const char *path)
{ return (remove(path) == 0) ? CE_OK : CE_IO; }
CResult plat_file_rename(const char *from, const char *to)
{ return (rename(from, to) == 0) ? CE_OK : CE_IO; }
long plat_file_size(const char *path)
{
    long sz; FILE *fp = fopen(path, "rb");
    if (!fp) { return -1; }
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fclose(fp);
    return sz;
}

CResult plat_mkdir(const char *path)
{
    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    if (mkdir(path) == 0) { return CE_OK; }   /* Open Watcom: single-arg */
    if (errno == EEXIST) { return CE_BUSY; }
    return CE_IO;
}

CResult plat_dir_remove(const char *path)
{
    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    if (rmdir(path) == 0) { return CE_OK; }   /* Open Watcom: <direct.h> */
    if (errno == EACCES || errno == ENOTEMPTY) { return CE_BUSY; }
    return CE_IO;
}

/* ---- directory enumeration (INT 21h findfirst/findnext) -------------- */
struct PlatDir {
    struct find_t f;
    cbool first;
    cbool done;
};

PlatDir *plat_opendir(const char *path)
{
    PlatDir *d;
    char pattern[CASTALIA_MAX_PATH];
    unsigned rc;
    d = (PlatDir *)sys_alloc((cu32)sizeof(PlatDir));
    if (d == NULL) { return NULL; }
    sys_strlcpy(pattern, path, sizeof(pattern));
    /* Ensure a trailing backslash then wildcard. */
    {
        cu32 n = sys_strnlen(pattern, sizeof(pattern));
        if (n > 0 && pattern[n - 1] != '\\' && pattern[n - 1] != '/') {
            sys_strlcat(pattern, "\\", sizeof(pattern));
        }
        sys_strlcat(pattern, "*.*", sizeof(pattern));
    }
    rc = _dos_findfirst(pattern, _A_NORMAL | _A_SUBDIR | _A_HIDDEN | _A_RDONLY,
                        &d->f);
    d->first = CTRUE;
    d->done  = (rc != 0) ? CTRUE : CFALSE;
    return d;
}

cbool plat_readdir(PlatDir *d, PlatDirEntry *out)
{
    if (d == NULL || out == NULL || d->done) { return CFALSE; }
    if (!d->first) {
        if (_dos_findnext(&d->f) != 0) { d->done = CTRUE; return CFALSE; }
    }
    d->first = CFALSE;
    sys_strlcpy(out->name, d->f.name, sizeof(out->name));
    out->is_dir = (d->f.attrib & _A_SUBDIR) ? CTRUE : CFALSE;
    out->size = (long)d->f.size;
    return CTRUE;
}

void plat_closedir(PlatDir *d)
{
    if (d == NULL) { return; }
    _dos_findclose(&d->f);
    sys_free(d, (cu32)sizeof(PlatDir));
}

/* ---- process / power ------------------------------------------------- */
CResult plat_run_program(const char *path, const char *args,
                         const char *workdir, int *out_exit_code)
{
    int rc;
    VesaMode saved = g_vmode;
    /* Suspend the graphical session: drop to text mode, run, then restore. */
    vesa_shutdown();
    rc = dos_exec(path, args, workdir);
    /* Re-enter the previous VESA mode and force a full repaint upstream. */
    if (vesa_init() == CE_OK) {
        vesa_set_mode(saved.width, saved.height, saved.bpp, &g_vmode);
        if (g_vmode.bpp == 8) { vesa_set_palette_332(); }
    }
    if (out_exit_code) { *out_exit_code = rc; }
    return (rc < 0) ? CE_FAIL : CE_OK;
}

void plat_reboot(void)   { dos_reboot(); }
void plat_poweroff(void) { dos_poweroff(); }

/* ---------------------------------------------------------------------- */
/* Machine inventory                                                      */
/* ---------------------------------------------------------------------- */
/*
 * Six questions, each one interrupt, each answer left undecoded -- naming
 * what comes back is mach_core.c's job and is tested there.
 *
 * The one judgement made on this side is which memory figure to report.
 * Asking the XMS driver how much extended memory it holds gives a number
 * that is true and useless: this program runs under a 32-bit extender, and
 * what it can actually allocate is whatever the DPMI host will part with.
 * Those differ, sometimes by a lot, and only the second one predicts whether
 * CastaliaOS will run on the machine in front of you. So this asks DPMI.
 */
static long dos_conv_free_kb(void)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;   /* 1 MB in paragraphs: below 640 KB this cannot be
                        * satisfied, and the documented failure hands back
                        * the largest block that COULD have been. Asking for
                        * something possible would mean owning it. */
    int386(0x21, &r, &r);
    if (!r.w.cflag) {
        /* Cannot happen below the 640 KB line, but if DOS ever did satisfy
         * it we would be holding a megabyte we never wanted. Give it back
         * before reporting, or the reading costs what it measures. */
        union REGS f;
        memset(&f, 0, sizeof(f));
        f.h.ah = 0x49;
        int386(0x21, &f, &f);
        return -1;
    }
    return (long)r.w.bx / 64L;   /* paragraphs -> KB */
}

static long dos_dpmi_free_kb(int *out_major, int *out_minor)
{
    union REGS  r;
    struct SREGS s;
    cu32 info[12];               /* DPMI 0.9 free-memory block, 48 bytes */

    if (out_major) { *out_major = 0; }
    if (out_minor) { *out_minor = 0; }

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0400;             /* get DPMI version */
    int386(0x31, &r, &r);
    if (r.w.cflag) { return -1; }
    if (out_major) { *out_major = (int)r.h.bh; }
    if (out_minor) { *out_minor = (int)r.h.bl; }

    memset(info, 0, sizeof(info));
    segread(&s);
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0500;             /* get free memory information */
    s.es    = FP_SEG(info);      /* same idiom as dpmi_int: ES:EDI -> buffer */
    r.x.edi = (unsigned)FP_OFF(info);
    int386x(0x31, &r, &r, &s);
    if (r.w.cflag) { return -1; }
    return (long)(info[0] / 1024UL);   /* [0] = largest free block, bytes */
}

/* PCI configuration space, dword at a time, through the BIOS rather than the
 * 0CF8h/0CFCh ports: the BIOS knows which access mechanism this chipset
 * needs, and getting that wrong on a machine with mechanism 2 means writing
 * to something that is not the configuration space. */
static cbool dos_pci_read32(int bus, int devfn, int reg, cu32 *out)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0xB10A;
    r.h.bh = (unsigned char)bus;
    r.h.bl = (unsigned char)devfn;
    r.w.di = (unsigned short)reg;
    int386(0x1A, &r, &r);
    if (r.w.cflag || r.h.ah != 0) { return CFALSE; }
    *out = r.x.ecx;
    return CTRUE;
}

static void dos_pci_scan(PlatMachineInfo *out)
{
    union REGS r;
    int bus, dev, fn, last_bus;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0xB101;                        /* PCI BIOS installation check */
    int386(0x1A, &r, &r);
    if (r.w.cflag || r.h.ah != 0 || r.x.edx != 0x20494350UL) {
        return;                             /* EDX != "PCI " -> no BIOS    */
    }
    out->pci_bios = CTRUE;
    last_bus = (int)r.h.cl;
    if (last_bus > 7) { last_bus = 7; }     /* a walk, not an audit        */

    for (bus = 0; bus <= last_bus; bus++) {
        for (dev = 0; dev < 32; dev++) {
            int nfn = 1;
            for (fn = 0; fn < nfn; fn++) {
                int devfn = (dev << 3) | fn;
                cu32 id = 0, cls = 0, hdr = 0;
                if (!dos_pci_read32(bus, devfn, 0x00, &id)) { continue; }
                if ((id & 0xFFFFUL) == 0xFFFFUL) { continue; }  /* empty */
                /* Only a multi-function device has functions past 0, and
                 * probing all eight on every slot walks addresses that do
                 * not exist. Bit 7 of the header type is the flag. */
                if (fn == 0 &&
                    dos_pci_read32(bus, devfn, 0x0C, &hdr) &&
                    (((hdr >> 16) & 0x80UL) != 0UL)) {
                    nfn = 8;
                }
                if (out->pci_count >= PLAT_MAX_PCI) {
                    /* Say so rather than stopping quietly: a list that ends
                     * without a word reads as the whole bus. */
                    SYS_LOGW("plat", "PCI scan stopped at %d devices; "
                             "bus %d device %d onwards not listed",
                             PLAT_MAX_PCI, bus, dev);
                    return;
                }
                dos_pci_read32(bus, devfn, 0x08, &cls);
                out->pci[out->pci_count].bus    = bus;
                out->pci[out->pci_count].dev    = dev;
                out->pci[out->pci_count].fn     = fn;
                out->pci[out->pci_count].vendor = id & 0xFFFFUL;
                out->pci[out->pci_count].device = (id >> 16) & 0xFFFFUL;
                out->pci[out->pci_count].cls    = (int)((cls >> 24) & 0xFFUL);
                out->pci[out->pci_count].sub    = (int)((cls >> 16) & 0xFFUL);
                out->pci_count++;
            }
        }
    }
}

void plat_machine_info(PlatMachineInfo *out)
{
    union REGS r;

    if (out == NULL) { return; }
    memset(out, 0, sizeof(*out));
    out->conv_free_kb = -1;
    out->ext_free_kb  = -1;

    memset(&r, 0, sizeof(r));
    r.h.ah = 0x30;                          /* DOS get version */
    int386(0x21, &r, &r);
    out->dos_major = (int)r.h.al;
    out->dos_minor = (int)r.h.ah;
    out->dos_oem   = (int)r.h.bh;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x1600;                        /* Windows enhanced-mode check */
    int386(0x2F, &r, &r);
    out->win_major = (int)r.h.al;
    out->win_minor = (int)r.h.ah;

    out->conv_free_kb = dos_conv_free_kb();
    out->ext_free_kb  = dos_dpmi_free_kb(&out->dpmi_major, &out->dpmi_minor);
    dos_pci_scan(out);

    SYS_LOGI("plat", "machine: DOS %d.%02d oem 0x%02X, conv %ld KB, ext %ld KB,"
             " %d PCI device(s)", out->dos_major, out->dos_minor,
             (unsigned)out->dos_oem, out->conv_free_kb, out->ext_free_kb,
             out->pci_count);
}

/* ---------------------------------------------------------------------- */
/* Processor identification                                               */
/* ---------------------------------------------------------------------- */
/*
 * CPUID, which is the whole reason cpu_core.c exists -- and which the DOS
 * backend did not implement, while System Information and --cpu-check both
 * call it unconditionally. The host backend defined it, the header declared
 * it, nothing on this side did, and every host check passed because the host
 * has one. That is an undefined symbol at wlink time, not a wrong reading.
 *
 * The three instructions are written as bytes rather than mnemonics. Nothing
 * on a host can compile this file -- Open Watcom is not installed on any
 * runner -- so it gets exactly one chance to be right on the target, and a
 * 'db' cannot be rejected by an assembler that has never heard of the
 * instruction it encodes:
 *   pushfd = 9Ch   popfd = 9Dh   (no 66h prefix: -mf gives a 32-bit segment)
 *   cpuid  = 0Fh 0A2h
 */
static cu32 dos_eflags(void);
#pragma aux dos_eflags =        \
    "db 09Ch"                   \
    "pop eax"                   \
    value [eax];

static void dos_set_eflags(cu32 v);
#pragma aux dos_set_eflags =    \
    "push eax"                  \
    "db 09Dh"                   \
    parm [eax];

/* Four accessors rather than one call with four out-pointers: CPUID for a
 * given leaf returns the same thing every time, so asking four times costs a
 * few hundred cycles once at startup and keeps each #pragma aux trivial
 * enough to read. */
static cu32 dos_cpuid_a(cu32 leaf);
#pragma aux dos_cpuid_a =       \
    "db 0Fh, 0A2h"              \
    parm [eax] value [eax] modify [ebx ecx edx];

static cu32 dos_cpuid_b(cu32 leaf);
#pragma aux dos_cpuid_b =       \
    "db 0Fh, 0A2h"              \
    parm [eax] value [ebx] modify [eax ecx edx];

static cu32 dos_cpuid_c(cu32 leaf);
#pragma aux dos_cpuid_c =       \
    "db 0Fh, 0A2h"              \
    parm [eax] value [ecx] modify [eax ebx edx];

static cu32 dos_cpuid_d(cu32 leaf);
#pragma aux dos_cpuid_d =       \
    "db 0Fh, 0A2h"              \
    parm [eax] value [edx] modify [eax ebx ecx];

/* EFLAGS bit 21 stays put on anything older than a late 486; that it can be
 * flipped at all IS the documented test for CPUID, and it must run before the
 * first CPUID or the probe faults on the processors it exists to detect. */
#define DOS_EFLAGS_ID 0x00200000UL

static cbool dos_has_cpuid(void)
{
    cu32 before = dos_eflags();
    cu32 after;
    dos_set_eflags(before ^ DOS_EFLAGS_ID);
    after = dos_eflags();
    dos_set_eflags(before);                 /* leave EFLAGS as we found it */
    return ((before ^ after) & DOS_EFLAGS_ID) ? CTRUE : CFALSE;
}

cbool plat_cpu_id(PlatCpuId *out)
{
    cu32 b, c, d, a1;
    int i;

    if (out == NULL) { return CFALSE; }
    memset(out, 0, sizeof(*out));
    if (!dos_has_cpuid()) {
        /* A 386 or an early 486. Not a failure -- it is the answer, and
         * cpu_core.c has a branch for exactly this. */
        return CFALSE;
    }

    b = dos_cpuid_b(0UL);
    d = dos_cpuid_d(0UL);
    c = dos_cpuid_c(0UL);
    for (i = 0; i < 4; i++) { out->vendor[i]     = (char)((b >> (i * 8)) & 0xFF); }
    for (i = 0; i < 4; i++) { out->vendor[4 + i] = (char)((d >> (i * 8)) & 0xFF); }
    for (i = 0; i < 4; i++) { out->vendor[8 + i] = (char)((c >> (i * 8)) & 0xFF); }
    out->vendor[12] = '\0';

    a1 = dos_cpuid_a(1UL);
    out->family   = (int)((a1 >> 8) & 0xF);
    out->model    = (int)((a1 >> 4) & 0xF);
    out->stepping = (int)(a1 & 0xF);
    if (out->family == 0xF) { out->family += (int)((a1 >> 20) & 0xFF); }
    if (out->family == 0x6 || out->family >= 0xF) {
        out->model += (int)(((a1 >> 16) & 0xF) << 4);
    }
    out->features  = dos_cpuid_d(1UL);
    out->has_cpuid = CTRUE;
    return CTRUE;
}

const char *plat_identity(void)
{
    return "CastaliaOS DOS backend (Open Watcom / VESA / INT33 / FreeDOS)";
}

/*
 * Dump the back buffer to a 24-bit BMP.
 *
 * This returned CE_UNSUPPORTED for a long time, deferred on the grounds that
 * the BMP writer was host-only -- which it has not been for a while. gfx_bmp_save is portable, is already
 * compiled and linked into the DOS product (the Capture Screen command in the
 * shell uses it), and the back buffer here is an ordinary GfxSurface. So there
 * is nothing DOS-specific left to write.
 *
 * It matters more than a tidied loose end: automated screenshots under QEMU
 * are how
 * the DOS path gets checked without someone watching a screen, and that is
 * exactly what the parts of it changed since the last FreeDOS run need.
 */
CResult plat_screenshot(const char *path)
{
    if (path == NULL || path[0] == '\0') { return CE_INVALID; }
    if (g_pixels == NULL) { return CE_FAIL; }
    return gfx_bmp_save(&g_back, path);
}

#endif /* CASTALIA_DOS */
