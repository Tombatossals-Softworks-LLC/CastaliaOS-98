/*
 * plat.h - Layer 1 platform abstraction (the Castalia-owned host contract).
 *
 * This is the ONLY seam between the portable upper stack and the machine.
 * Two backends implement it:
 *   - src/platform/host/  headless framebuffer + screenshot + scripted
 *     input. Builds with any C89 compiler; used for development, CI, and
 *     the golden screenshot. This is what runs in this repository's tests.
 *   - src/platform/dos/   VESA (int 10h/VBE), PS/2 mouse (int 33h),
 *     keyboard (int 16h/09h), timer, DOS file I/O and child-process launch
 *     via Open Watcom. Compiled only by the DOS makefile; never by the host
 *     build, so DOS-specific code can never break portability.
 *
 * Nothing above this layer calls DOS, VESA, SDL, or the OS directly.
 */
#ifndef CASTALIA_PLAT_H
#define CASTALIA_PLAT_H

#include "castalia/ctypes.h"
#include "castalia/gfx.h"

/* ---------------------------------------------------------------------- */
/* Video                                                                  */
/* ---------------------------------------------------------------------- */
typedef struct {
    int  width;
    int  height;
    int  bpp;         /* requested hardware depth: 8 or 16 (32 on host)   */
    cbool prefer_safe;/* force the 640x480x8 fallback path                */
} PlatVideoRequest;

typedef struct {
    int  width;
    int  height;
    int  bpp;         /* actual hardware depth chosen                     */
    const char *driver_name; /* "host-headless", "vesa-banked", ...       */
} PlatVideoInfo;

/*
 * One graphics mode the video hardware says it can set.
 *
 * This is what the card OFFERS, not what this system can use. Depths it
 * cannot drive are reported too, and marked -- "the card has 800x600 but only
 * at 15bpp" is the whole reason a hardware report is worth reading, and a list
 * filtered down to what already worked would never say it.
 */
typedef struct {
    int   w, h, bpp;
    cbool lfb;        /* a linear framebuffer is available for this mode  */
} PlatVideoMode;

/*
 * Fill 'out' with up to 'max' graphics modes the video hardware reports, and
 * return how many were written. Zero means this platform cannot enumerate its
 * modes, which is a different answer from "the card offers none".
 *
 * The list is raw: unsorted, possibly with duplicates, and including depths
 * the presenter cannot drive. Tidying it is vbe_report.c's job, so that the
 * policy can be tested against mode lists no machine here has.
 */
int plat_video_modes(PlatVideoMode *out, int max);

/*
 * Where that list came from, for the report to say plainly -- "VESA BIOS" on
 * DOS, "software presenter" on the host. A list of modes with no statement of
 * who is claiming them invites the reader to trust it more than they should.
 */
const char *plat_video_modes_source(void);

/*
 * Bring up the platform: allocate the back buffer, initialize video, input,
 * and the timer. On the DOS backend this sets the VESA mode with graceful
 * fallback (800x600x16 -> 640x480x16 -> 640x480x8). Returns CE_OK, or an
 * error after which the caller must drop to DOS with a message.
 */
CResult plat_init(const PlatVideoRequest *req, PlatVideoInfo *out_info);

/* Tear down input, restore text mode, free the back buffer. Symmetric. */
void    plat_shutdown(void);

/*
 * The shared back buffer that the whole UI renders into (32-bit XRGB). The
 * pointer is stable between plat_init and plat_shutdown.
 */
GfxSurface *plat_backbuffer(void);

/*
 * Push the given dirty rectangles from the back buffer to the visible
 * framebuffer, converting to the hardware depth. Passing NULL/0 presents
 * the whole surface (used on mode/theme switch only).
 */
void    plat_present(const CRect *rects, int count);

/* Current video mode info. */
void    plat_video_info(PlatVideoInfo *out_info);

/* ---------------------------------------------------------------------- */
/* Processor identification                                               */
/* ---------------------------------------------------------------------- */
/*
 * Raw CPUID results, undecoded. Reading them is four instructions and belongs
 * here; turning them into "Intel Pentium MMX" is a table with edge cases and
 * lives in src/sys/cpu_core.c, where it can be tested without a CPU.
 *
 * Returns CFALSE when the processor is old enough to predate CPUID (a 386 or
 * an early 486), which is a real answer on this hardware, not a failure --
 * out->has_cpuid is set accordingly either way.
 */
typedef struct {
    cbool has_cpuid;
    char  vendor[13];
    int   family, model, stepping;
    cu32  features;      /* leaf 1, EDX */
} PlatCpuId;
cbool   plat_cpu_id(PlatCpuId *out);

/* ---------------------------------------------------------------------- */
/* Machine inventory                                                      */
/* ---------------------------------------------------------------------- */
/*
 * What DOS says it is, what memory is left below and above the line, and what
 * is on the PCI bus. Same division as CPUID: reading these is a handful of
 * interrupts and belongs here; deciding that OEM byte 0FDh means FreeDOS and
 * that 1013h:00B8h is a Cirrus GD 5446 is a table with edge cases and lives in
 * src/sys/mach_core.c, where it is tested without a machine.
 *
 * This is the inventory somebody takes when a boot on unfamiliar hardware
 * goes wrong, so every field distinguishes "nothing there" from "did not
 * find out": the KB counts are -1 when unknown and 0 only when genuinely
 * empty, and pci_bios says whether the bus was enumerable at all, which is a
 * different fact from a pci_count of zero.
 */
#define PLAT_MAX_PCI 24

typedef struct {
    int  bus, dev, fn;
    cu32 vendor, device;   /* as read; 0FFFFh in both means an empty slot   */
    int  cls, sub;         /* base class and subclass bytes                 */
} PlatPciDevice;

typedef struct {
    int   dos_major, dos_minor, dos_oem;  /* major < 2: not asked/not DOS   */
    int   win_major, win_minor;           /* 0: not inside a Windows DOS box*/
    int   dpmi_major, dpmi_minor;         /* 0: no DPMI host answered       */
    long  conv_free_kb;                   /* largest free block below 640 KB*/
    /* The largest block the DPMI host will actually hand this program --
     * NOT what the XMS driver reports it is holding. Under a 32-bit extender
     * those are different numbers, and only the first one predicts whether
     * this system will run. -1 when no host answered. */
    long  ext_free_kb;
    cbool pci_bios;                       /* CFALSE: bus could not be walked*/
    int   pci_count;                      /* devices written to pci[]       */
    PlatPciDevice pci[PLAT_MAX_PCI];
} PlatMachineInfo;

void    plat_machine_info(PlatMachineInfo *out);

/* ---------------------------------------------------------------------- */
/* Input                                                                  */
/* ---------------------------------------------------------------------- */
typedef enum {
    PLAT_EV_NONE = 0,
    PLAT_EV_MOUSE_MOVE,
    PLAT_EV_MOUSE_DOWN,
    PLAT_EV_MOUSE_UP,
    PLAT_EV_KEY_DOWN,
    PLAT_EV_KEY_UP,
    /*
     * The wheel, in NOTCHES: positive is away from the user (scroll up),
     * negative is toward them. A notch rather than a pixel count because
     * that is what the hardware reports on this target -- the DOS wheel API
     * hands back a signed count of detents, and inventing a pixel scale here
     * would only be a number the upper layers had to undo.
     */
    PLAT_EV_MOUSE_WHEEL,
    PLAT_EV_QUIT       /* window closed / Ctrl-Alt-Del-like quit request  */
} PlatEventType;

/* Mouse button bitmask. */
#define PLAT_MB_LEFT   0x01
#define PLAT_MB_RIGHT  0x02
#define PLAT_MB_MIDDLE 0x04

/* Keyboard modifier bitmask (PlatEvent.mods), so the UI can implement
 * Shift-select, Ctrl-shortcuts, etc. Populated by both backends: the DOS
 * backend reads the BIOS shift-flags byte, the host backend copies whatever the
 * synthetic event carries. */
#define PLAT_MOD_SHIFT 0x01
#define PLAT_MOD_CTRL  0x02
#define PLAT_MOD_ALT   0x04

/* Key codes: printable ASCII is itself; specials use the range below. */
#define PLAT_KEY_ESC      0x1B
#define PLAT_KEY_ENTER    0x0D
#define PLAT_KEY_BACKSP   0x08
#define PLAT_KEY_TAB      0x09
#define PLAT_KEY_SPACE    0x20
#define PLAT_KEY_EXT      0x100 /* offset for non-ASCII keys              */
#define PLAT_KEY_UP       (PLAT_KEY_EXT + 1)
#define PLAT_KEY_DOWN     (PLAT_KEY_EXT + 2)
#define PLAT_KEY_LEFT     (PLAT_KEY_EXT + 3)
#define PLAT_KEY_RIGHT    (PLAT_KEY_EXT + 4)
#define PLAT_KEY_F1       (PLAT_KEY_EXT + 5)
#define PLAT_KEY_F2       (PLAT_KEY_EXT + 6)
#define PLAT_KEY_F3       (PLAT_KEY_EXT + 11)
/* F5: re-read what is on screen. The File Manager's Refresh button had no
 * keyboard equivalent, which on a machine with no mouse driver meant no way
 * to ask a folder what it looks like now. */
#define PLAT_KEY_F5       (PLAT_KEY_EXT + 21)
/* F10: the menu for whatever is selected. Windows' convention, and it leaves
 * F3 as Find, which is what it already means in Notepad. */
#define PLAT_KEY_F10      (PLAT_KEY_EXT + 20)
#define PLAT_KEY_HOME     (PLAT_KEY_EXT + 7)
#define PLAT_KEY_END      (PLAT_KEY_EXT + 8)
#define PLAT_KEY_PGUP     (PLAT_KEY_EXT + 9)
#define PLAT_KEY_PGDN     (PLAT_KEY_EXT + 10)
#define PLAT_KEY_DELETE   (PLAT_KEY_EXT + 16)
#define PLAT_KEY_CLOSE    (PLAT_KEY_EXT + 17) /* Alt+F4: close focused window */
#define PLAT_KEY_NEXTWIN  (PLAT_KEY_EXT + 18) /* Alt+Tab: switch window       */
/* Alt+Space: the focused window's own menu (restore/minimise/maximise/close).
 * A logical key like PLAT_KEY_CLOSE rather than "space with a modifier",
 * because the DOS keyboard reports these combinations as scan codes with no
 * character, and the shell should not have to know that. */
#define PLAT_KEY_SYSMENU  (PLAT_KEY_EXT + 19)

typedef struct PlatEvent {
    PlatEventType type;
    int  mouse_x, mouse_y; /* absolute, in back-buffer coordinates        */
    int  buttons;          /* current button bitmask                      */
    int  key;              /* key code for KEY_* events                   */
    int  ch;               /* translated ASCII char for KEY_DOWN (0 if none)*/
    int  mods;             /* PLAT_MOD_* bitmask active at the event       */
    int  wheel;            /* notches for MOUSE_WHEEL (+up / -down)        */
} PlatEvent;

/*
 * Pop the next input event. Returns CTRUE if one was written to 'ev', or
 * CFALSE if the queue is empty. Non-blocking; the cooperative main loop
 * drains it each frame.
 */
cbool   plat_poll_event(PlatEvent *ev);

/* Current mouse position/buttons without consuming events. */
void    plat_mouse_state(int *x, int *y, int *buttons);

/* ---------------------------------------------------------------------- */
/* Timing                                                                 */
/* ---------------------------------------------------------------------- */
cu32    plat_ticks_ms(void);   /* monotonic milliseconds since plat_init  */
void    plat_sleep_ms(cu32 ms);/* yield/idle so we don't spin the CPU     */
/* Wall-clock components for the taskbar clock. */
void    plat_wall_clock(int *hour, int *minute, int *second);
/* Local calendar date (clock tooltip). weekday is 0=Sunday .. 6=Saturday.
 * Any out pointer may be NULL. */
void    plat_wall_date(int *year, int *month, int *day, int *weekday);

/*
 * Set the machine's clock and calendar.
 *
 * On DOS this is INT 21h AH=2Dh and AH=2Bh, which write the CMOS clock -- the
 * reason the Control Center has a Date and Time panel at all, since a 1999
 * machine with a flat coin cell comes up in 1980 and every file it saves is
 * stamped wrong.
 *
 * Callers must handle CE_UNSUPPORTED: a host has a system clock somebody else
 * owns, and this returns rather than pretending. CE_INVALID for a date DOS
 * would refuse (see CAL_YEAR_MIN in src/sys/cal_core.h).
 */
CResult plat_set_wall_clock(int hour, int minute, int second);
CResult plat_set_wall_date(int year, int month, int day);

/*
 * Keyboard typematic rate: how long a held key waits before repeating, and
 * how fast it repeats after that.
 *
 * 'delay_ms' is one of 250/500/750/1000 and 'cps' is characters per second
 * (2..30). Those are the BIOS's own steps, not a range this system invented
 * -- INT 16h AX=0305h takes a two-bit delay and a five-bit rate index, so
 * anything in between would be rounded to one of these anyway and offering it
 * would be a slider that lies. Nearest step wins.
 *
 * CE_UNSUPPORTED where a program does not own the keyboard.
 */
CResult plat_set_key_repeat(int delay_ms, int cps);

/* ---------------------------------------------------------------------- */
/* Files (thin wrappers; DOS backend uses INT 21h, host uses stdio)       */
/* ---------------------------------------------------------------------- */
typedef struct PlatFile PlatFile; /* opaque                              */

PlatFile *plat_fopen(const char *path, const char *mode); /* "rb","wb","ab" */
cu32      plat_fread(PlatFile *f, void *buf, cu32 bytes);
/* Move the read/write position to an absolute byte offset from the start.
 * Absolute only: "from the end" is plat_file_size() minus what you want, and
 * "from here" is a position the caller was already tracking. One form is one
 * thing to get right on two backends. CE_OK, or CE_IO if the seek failed. */
CResult   plat_fseek(PlatFile *f, long offset);
cu32      plat_fwrite(PlatFile *f, const void *buf, cu32 bytes);
CResult   plat_fclose(PlatFile *f);
cbool     plat_file_exists(const char *path);
CResult   plat_file_remove(const char *path);
CResult   plat_file_rename(const char *from, const char *to);
long      plat_file_size(const char *path); /* -1 if unknown              */
/* Create a directory. Returns CE_OK if created, CE_BUSY if it already
 * exists, or an error code. */
CResult   plat_mkdir(const char *path);
/* Remove an EMPTY directory (the caller clears it first). CE_BUSY when the
 * directory still has contents, CE_IO on any other failure. */
CResult   plat_dir_remove(const char *path);

/* Directory enumeration for the file manager. */
typedef struct {
    char  name[CASTALIA_MAX_NAME];
    cbool is_dir;
    long  size;
} PlatDirEntry;
typedef struct PlatDir PlatDir; /* opaque                                 */
PlatDir *plat_opendir(const char *path);
cbool    plat_readdir(PlatDir *d, PlatDirEntry *out);
void     plat_closedir(PlatDir *d);

/* ---------------------------------------------------------------------- */
/* Process launch (DOS child programs; host stub logs and returns)        */
/* ---------------------------------------------------------------------- */
/*
 * Suspend the graphical session, run a DOS program to completion, capture
 * its exit code, and hand control back to the caller (who restores video).
 * On the host backend this is a no-op that logs the request.
 */
CResult plat_run_program(const char *path, const char *args,
                         const char *workdir, int *out_exit_code);

/* Reboot / power-off requests for the shutdown dialog. Best-effort. */
void    plat_reboot(void);
void    plat_poweroff(void);

/* Identity string for System Info ("Open Watcom / FreeDOS", "host/gcc"). */
const char *plat_identity(void);

/*
 * Save the current back buffer to a 24-bit BMP. Implemented by the host
 * backend (used for the golden screenshot and CI); the DOS backend may
 * implement it later and returns CE_UNSUPPORTED until then.
 */
CResult plat_screenshot(const char *path);

#endif /* CASTALIA_PLAT_H */
