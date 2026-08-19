/*
 * app_sysinfo.c - System Information window with live data.
 *
 * Gathers real runtime facts (version, platform identity, DOS and memory
 * inventory, PCI bus, video mode, live memory accounting, wall clock) and
 * presents them through the generic text window. This is the flagship proof
 * that the platform -> gfx -> wm -> app pipeline works end to end.
 *
 * It is also the screen somebody reads when a boot on unfamiliar hardware
 * goes wrong, which is why F2 writes the whole thing to a file: the machine
 * that cannot run this properly is exactly the machine whose inventory needs
 * to leave on a floppy. What the window shows is bounded by what fits on a
 * 640x480 screen; what the file holds is everything.
 */
#include "apps.h"
#include "../sys/cpu_core.h"
#include "../sys/mach_core.h"
#include "../gfx/vbe_report.h"
#include "castalia/castalia.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/snd.h"
#include "castalia/net.h"

#include <stdlib.h>   /* getenv */

#define SI_LINES 60
#define SI_W 92

/* Devices listed in the window. A 440BX board with sound and network fills
 * about eight slots; anything past this is real but does not fit, and the
 * count of what was left out is printed rather than left to be inferred. */
#define SI_PCI_SHOWN 10

/* Video modes listed in the window. A real card reports far more than this
 * once text modes are dropped; the tidier keeps the top of the ladder (the
 * end that explains the resolution actually in use) and the count of what was
 * left out is printed rather than left to be inferred. */
#define SI_MODES_SHOWN 12

/*
 * The environment variables this system's behaviour actually depends on --
 * not every variable DOS is holding.
 *
 * A full listing would be thirty rows of DIRCMD and PROMPT for the sake of
 * the few that matter, and it would need a platform seam to enumerate at all
 * (there is no C89 way to walk the environment). These are the ones that
 * change what CastaliaOS does: BLASTER is what src/platform/dos/snd_blaster.c
 * parses to find the card, TEMP is where DOS programs scribble, COMSPEC is
 * what a Run command launches through, and CASTALIA_HOME is where every
 * setting this system saves ends up. PATH is here because "it will not start"
 * is usually PATH.
 */
static const char *const SI_ENV[] = {
    "CASTALIA_HOME", "BLASTER", "PATH", "TEMP", "COMSPEC"
};
#define SI_NENV ((int)(sizeof(SI_ENV) / sizeof(SI_ENV[0])))

static void si_report_path(char *dst, cu32 dstsz)
{
    sys_home_path(dst, dstsz, "CASTINFO.TXT");
}

void app_sysinfo_open(void)
{
    static char buf[SI_LINES][SI_W];
    const char *lines[SI_LINES];
    PlatVideoInfo vi;
    PlatMachineInfo mi;
    NetDeviceInfo ni;
    PlatCpuId cid;
    CpuInfo cpu;
    char cpuname[64], cpufeat[64];
    char dos[64], win[64], conv[24], ext[24], path[CASTALIA_MAX_PATH];
    int h = 0, m = 0, sec = 0;
    int i, n = 0;

    plat_video_info(&vi);
    plat_wall_clock(&h, &m, &sec);
    plat_machine_info(&mi);
    net_device_info(&ni);

    /* The processor. Reading CPUID is the platform's job; naming what it read
     * is cpu_core.c, which is why an unfamiliar chip still gets an honest
     * line here instead of a blank one. */
    plat_cpu_id(&cid);
    cpu.has_cpuid = cid.has_cpuid;
    sys_strlcpy(cpu.vendor, cid.vendor, sizeof(cpu.vendor));
    cpu.family = cid.family;
    cpu.model = cid.model;
    cpu.stepping = cid.stepping;
    cpu.features = cid.features;
    cpu_describe(&cpu, cpuname, sizeof(cpuname));
    cpu_feature_list(cpu.features, cpufeat, sizeof(cpufeat));

    mach_dos_label(mi.dos_major, mi.dos_minor, mi.dos_oem, dos, sizeof(dos));
    mach_kb_label(mi.conv_free_kb, conv, sizeof(conv));
    mach_kb_label(mi.ext_free_kb, ext, sizeof(ext));

    sys_snprintf(buf[n++], SI_W, "%s  (%s)", CASTALIA_NAME, CASTALIA_EDITION);
    sys_snprintf(buf[n++], SI_W, "Version %s  -  %s", CASTALIA_VER_STRING,
                 CASTALIA_VER_STAGE);
    sys_snprintf(buf[n++], SI_W, "Build date: %s", __DATE__);
    sys_snprintf(buf[n++], SI_W, " ");

    sys_snprintf(buf[n++], SI_W, "Platform : %s", plat_identity());
    sys_snprintf(buf[n++], SI_W, "System   : %s", dos);
    /* Only drawn when true: a line reading "Windows: none" on a machine that
     * has never seen Windows is noise on a screen that is short of room. */
    if (mach_windows_label(mi.win_major, mi.win_minor, win, sizeof(win))) {
        sys_snprintf(buf[n++], SI_W, "           running under %s", win);
    }
    if (mi.dpmi_major != 0) {
        sys_snprintf(buf[n++], SI_W, "           DPMI host %d.%02d",
                     mi.dpmi_major, mi.dpmi_minor);
    }
    sys_snprintf(buf[n++], SI_W, "Processor: %s", cpuname);
    if (cid.has_cpuid) {
        sys_snprintf(buf[n++], SI_W, "  family %d, model %d, stepping %d",
                     cpu.family, cpu.model, cpu.stepping);
        sys_snprintf(buf[n++], SI_W, "  features: %s", cpufeat);
    } else {
        sys_snprintf(buf[n++], SI_W, "  the processor predates CPUID");
    }
    sys_snprintf(buf[n++], SI_W, "Video    : %dx%d, %d bpp (%s)", vi.width,
                 vi.height, vi.bpp, vi.driver_name);
    sys_snprintf(buf[n++], SI_W, "Sound    : %s", snd_device_name());
    sys_snprintf(buf[n++], SI_W, "Network  : %s", ni.name);
    sys_snprintf(buf[n++], SI_W, "Clock    : %02d:%02d:%02d", h, m, sec);
    sys_snprintf(buf[n++], SI_W, " ");

    sys_snprintf(buf[n++], SI_W, "Conventional free: %s", conv);
    sys_snprintf(buf[n++], SI_W, "Extended free    : %s", ext);
    sys_snprintf(buf[n++], SI_W, "Castalia heap    : %lu KB live, %lu KB peak",
                 (unsigned long)(sys_mem_live_bytes() / 1024),
                 (unsigned long)(sys_mem_peak_bytes() / 1024));
    sys_snprintf(buf[n++], SI_W, "Allocations      : %lu live blocks",
                 (unsigned long)sys_mem_alloc_count());
    sys_snprintf(buf[n++], SI_W, " ");

    sys_snprintf(buf[n++], SI_W, "Environment:");
    for (i = 0; i < SI_NENV; i++) {
        mach_env_line(SI_ENV[i], getenv(SI_ENV[i]), buf[n++], SI_W);
    }
    sys_snprintf(buf[n++], SI_W, " ");

    /* The bus. "No PCI BIOS" and "a PCI bus with nothing on it" are different
     * machines and different problems, so they get different lines. */
    if (!mi.pci_bios) {
        sys_snprintf(buf[n++], SI_W, "PCI bus  : no PCI BIOS on this machine");
    } else if (mi.pci_count == 0) {
        sys_snprintf(buf[n++], SI_W, "PCI bus  : present, no devices reported");
    } else {
        sys_snprintf(buf[n++], SI_W, "PCI bus  : %d device(s)", mi.pci_count);
        for (i = 0; i < mi.pci_count && i < SI_PCI_SHOWN &&
                    n < SI_LINES - 1; i++) {
            char name[64];
            const char *cls;
            mach_pci_describe(mi.pci[i].vendor, mi.pci[i].device,
                              name, sizeof(name));
            cls = mach_pci_class(mi.pci[i].cls, mi.pci[i].sub);
            sys_snprintf(buf[n++], SI_W, "  %02d:%02d.%d  %s%s%s%s",
                         mi.pci[i].bus, mi.pci[i].dev, mi.pci[i].fn, name,
                         (cls ? "  [" : ""), (cls ? cls : ""),
                         (cls ? "]" : ""));
        }
        if (mi.pci_count > SI_PCI_SHOWN) {
            sys_snprintf(buf[n++], SI_W, "  ... and %d more",
                         mi.pci_count - SI_PCI_SHOWN);
        }
    }

    /* The video modes the hardware says it can set.
     *
     * This is the part of the report that explains the rest of it. "Video:
     * 640x480, 8 bpp" on a machine whose card offers 1024x768 is a different
     * problem from the same line on a card that offers nothing better, and
     * until now the report could not tell those apart. Depths this system
     * cannot drive are listed too, and marked, because a card offering 800x600
     * only at 15bpp is exactly why a desktop ends up at 640x480. */
    {
        static PlatVideoMode raw[128];
        static PlatVideoMode tidy[128];
        int got = plat_video_modes(raw, (int)(sizeof raw / sizeof raw[0]));
        /* Tidied in full first, then a window onto the top of it. Counting
         * "not shown" as (raw - displayed) would report collapsed duplicates
         * as hidden modes: the host reports its current size twice over, and
         * the first draft of this said two modes were missing when none were.
         * The number has to come from the tidied total, not the raw one. */
        int total = vbe_report_modes(raw, got, tidy,
                                     (int)(sizeof tidy / sizeof tidy[0]));
        int first = (total > SI_MODES_SHOWN) ? (total - SI_MODES_SHOWN) : 0;
        sys_snprintf(buf[n++], SI_W, " ");
        if (got == 0) {
            /* Not the same as "the card offers none", and said differently. */
            sys_snprintf(buf[n++], SI_W,
                         "Video modes: this platform cannot enumerate them");
        } else {
            sys_snprintf(buf[n++], SI_W, "Video modes (%s):",
                         plat_video_modes_source());
            if (first > 0) {
                sys_snprintf(buf[n++], SI_W,
                             "  ... %d smaller mode(s) not shown", first);
            }
            for (i = first; i < total && n < SI_LINES - 1; i++) {
                /* Matched on depth as well as size. Marking every row at the
                 * current resolution would put the marker on two lines at
                 * once and say neither is the mode in use. */
                cbool cur = (tidy[i].w == vi.width && tidy[i].h == vi.height &&
                             tidy[i].bpp == vi.bpp) ? CTRUE : CFALSE;
                vbe_mode_line(&tidy[i], cur, buf[n], SI_W);
                n++;
            }
        }
    }

    si_report_path(path, sizeof(path));
    for (i = 0; i < n; i++) { lines[i] = buf[i]; }
    app_info_open_ex("System Information", lines, n, path);
}
