/*
 * mach_core.c - Naming DOS versions and PCI devices (see mach_core.h).
 *
 * Tables and string building. No interrupts, no allocation, no I/O ports.
 */
#include "mach_core.h"
#include "castalia/sys.h"

/* ---- DOS ---------------------------------------------------------------- */
static const char *oem_name(int oem)
{
    switch (oem) {
    case MACH_OEM_IBM:       return "PC DOS";
    case MACH_OEM_COMPAQ:    return "Compaq DOS";
    case MACH_OEM_DIGITAL:   return "DR-DOS";
    case MACH_OEM_FREEDOS:   return "FreeDOS";
    case MACH_OEM_MICROSOFT: return "MS-DOS";
    default:                 return NULL;
    }
}

void mach_dos_label(int major, int minor, int oem, char *dst, cu32 dstsz)
{
    const char *name;
    if (dst == NULL || dstsz == 0u) { return; }

    /* INT 21h AH=30h cannot report a major below 2 on anything this system
     * will ever run on, so a 0 or a 1 means the call did not happen. Saying
     * "DOS 0.00" would present a failed read as a measurement. */
    if (major < 2) {
        sys_strlcpy(dst, "not DOS (version not reported)", dstsz);
        return;
    }

    name = oem_name(oem);
    if (oem == MACH_OEM_FREEDOS) {
        /* FreeDOS reports whichever version it is set to impersonate, so the
         * number is about compatibility and not about which FreeDOS this is.
         * Presenting it as "FreeDOS 7.10" would name a release that has never
         * existed. */
        sys_snprintf(dst, dstsz, "FreeDOS (reports DOS %d.%02d)", major, minor);
    } else if (name != NULL) {
        sys_snprintf(dst, dstsz, "%s %d.%02d", name, major, minor);
    } else {
        sys_snprintf(dst, dstsz, "DOS %d.%02d (OEM 0x%02X)", major, minor,
                     (unsigned)(oem & 0xFF));
    }
}

cbool mach_windows_label(int major, int minor, char *dst, cu32 dstsz)
{
    if (dst == NULL || dstsz == 0u) { return CFALSE; }
    dst[0] = '\0';
    /* AL=0 or 80h means no Windows; AL=1 and AL=0FFh mean Windows/386 2.x,
     * which cannot run anything this system produces. */
    if (major == 0 || major == 1 || major == 0x80 || major == 0xFF) {
        return CFALSE;
    }
    if (major == 3) {
        sys_snprintf(dst, dstsz, "Windows 3.%d DOS box", minor);
    } else if (major == 4) {
        /* One major for the whole 9x line; the minor separates them. */
        if (minor >= 90)      { sys_strlcpy(dst, "Windows Me DOS box", dstsz); }
        else if (minor >= 10) { sys_strlcpy(dst, "Windows 98 DOS box", dstsz); }
        else                  { sys_strlcpy(dst, "Windows 95 DOS box", dstsz); }
    } else {
        sys_snprintf(dst, dstsz, "Windows %d.%d DOS box", major, minor);
    }
    return CTRUE;
}

/* ---- PCI ---------------------------------------------------------------- */
typedef struct { cu32 id; const char *name; } MachId;

/* Makers whose parts turn up in a machine of this era, plus the emulators
 * this system is developed on -- somebody running under QEMU should see the
 * adapter named rather than a bare hex pair. */
static const MachId g_vendors[] = {
    { 0x1000UL, "LSI Logic"        }, { 0x1002UL, "ATI"              },
    { 0x1004UL, "VLSI"             }, { 0x100BUL, "National Semiconductor" },
    { 0x1011UL, "DEC"              }, { 0x1013UL, "Cirrus Logic"     },
    { 0x1022UL, "AMD"              }, { 0x1039UL, "SiS"              },
    { 0x1045UL, "OPTi"             }, { 0x1078UL, "Cyrix"            },
    { 0x102BUL, "Matrox"           }, { 0x1050UL, "Winbond"          },
    { 0x10B7UL, "3Com"             }, { 0x10DEUL, "NVIDIA"           },
    { 0x10ECUL, "Realtek"          }, { 0x1102UL, "Creative Labs"    },
    { 0x1106UL, "VIA"              }, { 0x1186UL, "D-Link"           },
    { 0x121AUL, "3dfx"             }, { 0x1234UL, "QEMU"             },
    { 0x125DUL, "ESS"              }, { 0x1274UL, "Ensoniq"          },
    { 0x1317UL, "ADMtek"           }, { 0x15ADUL, "VMware"           },
    { 0x1AF4UL, "Red Hat"          }, { 0x1B36UL, "Red Hat"          },
    { 0x5333UL, "S3"               }, { 0x80EEUL, "VirtualBox"       },
    { 0x8086UL, "Intel"            }, { 0x9004UL, "Adaptec"          }
};
#define MACH_NVENDORS ((int)(sizeof(g_vendors) / sizeof(g_vendors[0])))

/* Specific parts, keyed on vendor<<16 | device. Deliberately short: it names
 * the chips that actually appear in the machines this system is built for and
 * tested on, and everything else falls back to the vendor and the hex. */
static const MachId g_devices[] = {
    { 0x101300A0UL, "GD 5430"                 },
    { 0x101300A8UL, "GD 5434"                 },
    { 0x101300B8UL, "GD 5446"                 },
    { 0x101300BCUL, "GD 5480"                 },
    { 0x10222000UL, "PCnet-PCI Am79C970"      },
    { 0x10222001UL, "PCnet-Home"              },
    { 0x102B051BUL, "Millennium II"           },
    { 0x10B79050UL, "3c905 Boomerang"         },
    { 0x10DE0020UL, "RIVA TNT"                },
    { 0x10EC8139UL, "RTL8139"                 },
    { 0x11020002UL, "SB Live! EMU10K1"        },
    { 0x11060586UL, "VT82C586 ISA bridge"     },
    { 0x121A0003UL, "Voodoo Banshee"          },
    { 0x121A0005UL, "Voodoo3"                 },
    { 0x12341111UL, "standard VGA"            },
    { 0x12741371UL, "AudioPCI ES1371"         },
    { 0x1AF41000UL, "virtio network"          },
    { 0x53338811UL, "Trio64"                  },
    { 0x5333883DUL, "ViRGE/VX"                },
    { 0x8086100EUL, "82540EM Gigabit"         },
    { 0x80861229UL, "EtherExpress Pro/100"    },
    { 0x80861237UL, "440FX host bridge"       },
    { 0x80862415UL, "82801AA AC'97 Audio"     },
    { 0x80867000UL, "PIIX3 ISA bridge"        },
    { 0x80867010UL, "PIIX3 IDE"               },
    { 0x80867020UL, "PIIX3 USB"               },
    { 0x80867113UL, "PIIX4 power management"  },
    { 0x80867190UL, "440BX host bridge"       },
    { 0x80867191UL, "440BX AGP bridge"        }
};
#define MACH_NDEVICES ((int)(sizeof(g_devices) / sizeof(g_devices[0])))

const char *mach_pci_vendor(cu32 vendor)
{
    int i;
    for (i = 0; i < MACH_NVENDORS; i++) {
        if (g_vendors[i].id == (vendor & 0xFFFFUL)) { return g_vendors[i].name; }
    }
    return NULL;
}

const char *mach_pci_class(int cls, int sub)
{
    /* Subclasses worth separating, because "storage controller" and "IDE
     * controller" are a different amount of help when a disk will not read. */
    switch ((cls << 8) | (sub & 0xFF)) {
    case 0x0100: return "SCSI controller";
    case 0x0101: return "IDE controller";
    case 0x0102: return "floppy controller";
    case 0x0106: return "SATA controller";
    case 0x0200: return "Ethernet controller";
    case 0x0280: return "network controller";
    case 0x0300: return "VGA display controller";
    case 0x0301: return "XGA display controller";
    case 0x0400: return "video device";
    case 0x0401: return "audio device";
    case 0x0600: return "host bridge";
    case 0x0601: return "ISA bridge";
    case 0x0602: return "EISA bridge";
    case 0x0604: return "PCI-to-PCI bridge";
    case 0x0607: return "CardBus bridge";
    case 0x0700: return "serial controller";
    case 0x0701: return "parallel controller";
    case 0x0C03: return "USB controller";
    default: break;
    }
    switch (cls) {
    case 0x00: return "unclassified device";
    case 0x01: return "storage controller";
    case 0x02: return "network controller";
    case 0x03: return "display controller";
    case 0x04: return "multimedia device";
    case 0x05: return "memory controller";
    case 0x06: return "bridge";
    case 0x07: return "communication controller";
    case 0x08: return "system peripheral";
    case 0x09: return "input device";
    case 0x0B: return "processor";
    case 0x0C: return "serial bus controller";
    case 0x0D: return "wireless controller";
    default:   return NULL;
    }
}

cbool mach_pci_describe(cu32 vendor, cu32 device, char *dst, cu32 dstsz)
{
    const char *vn;
    cu32 key;
    int i;

    if (dst == NULL || dstsz == 0u) { return CFALSE; }
    vendor &= 0xFFFFUL;
    device &= 0xFFFFUL;
    vn = mach_pci_vendor(vendor);
    key = (vendor << 16) | device;

    for (i = 0; i < MACH_NDEVICES; i++) {
        if (g_devices[i].id == key) {
            /* A device is only in that table because its vendor is in the
             * other one, so vn cannot be NULL here -- but printing the part
             * number alone would be useless if it ever were. */
            sys_snprintf(dst, dstsz, "%s %s", (vn ? vn : "PCI"),
                         g_devices[i].name);
            return CTRUE;
        }
    }
    if (vn != NULL) {
        sys_snprintf(dst, dstsz, "%s device %04X", vn, (unsigned)device);
    } else {
        sys_snprintf(dst, dstsz, "PCI %04X:%04X", (unsigned)vendor,
                     (unsigned)device);
    }
    return CFALSE;
}

/* ---- environment -------------------------------------------------------- */
/* Width of the name column, so the values line up down the report. */
#define MACH_ENV_NAMEW 14

cbool mach_env_line(const char *name, const char *value, char *dst, cu32 dstsz)
{
    cu32 len;
    if (dst == NULL || dstsz == 0u) { return CFALSE; }
    if (name == NULL) { name = "?"; }
    if (value == NULL) {
        sys_snprintf(dst, dstsz, "  %-*s(not set)", MACH_ENV_NAMEW, name);
        return CTRUE;
    }
    sys_snprintf(dst, dstsz, "  %-*s%s", MACH_ENV_NAMEW, name, value);
    /* Measured rather than inferred from the row coming back full: a value
     * that happens to fill the row EXACTLY is not truncated, and marking it
     * would destroy three characters of a correct answer to say something
     * untrue about it. sys_snprintf clamps its own return, so the length has
     * to be worked out here. */
    {
        cu32 nlen = sys_strnlen(name, dstsz);
        cu32 vlen = sys_strnlen(value, 4096u);
        cu32 wide = (nlen > (cu32)MACH_ENV_NAMEW) ? nlen : (cu32)MACH_ENV_NAMEW;
        len = 2u + wide + vlen;
    }
    if (len < dstsz) { return CTRUE; }
    if (dstsz >= 4u) {
        dst[dstsz - 4u] = '.';
        dst[dstsz - 3u] = '.';
        dst[dstsz - 2u] = '.';
        dst[dstsz - 1u] = '\0';
    }
    return CFALSE;
}

/* ---- sizes -------------------------------------------------------------- */
void mach_kb_label(long kb, char *dst, cu32 dstsz)
{
    if (dst == NULL || dstsz == 0u) { return; }
    if (kb < 0) {
        sys_strlcpy(dst, "n/a", dstsz);
    } else if (kb < 1024) {
        sys_snprintf(dst, dstsz, "%ld KB", kb);
    } else {
        /* One decimal, computed in integers because there is no FPU on the
         * oldest machine this targets. */
        sys_snprintf(dst, dstsz, "%ld.%ld MB", kb / 1024,
                     ((kb % 1024) * 10) / 1024);
    }
}
