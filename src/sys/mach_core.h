/*
 * mach_core.h - Turning raw machine numbers into something a person can read.
 *
 * The same split as cpu_core.h, for the same reason. Asking DOS its version is
 * one interrupt; asking the PCI BIOS to enumerate the bus is a short loop.
 * Neither is where mistakes happen. Mistakes happen in deciding that OEM byte
 * 0xFD means FreeDOS, that class 03h subclass 00h is a VGA display controller
 * rather than "class 3", and that vendor 1234h device 1111h is the QEMU
 * adapter somebody is almost certainly looking at right now. That is a lookup
 * table with edge cases, so it lives here as pure functions over numbers and
 * is walked by tests/test_mach.c.
 *
 * The rule throughout is that this file NEVER guesses. An unknown vendor comes
 * back as its hex ID, not as a plausible-sounding name, because the reason
 * anybody opens System Information on an unfamiliar machine is to find out
 * what is actually in it. A wrong name is worse than a number.
 *
 * Nothing here executes an interrupt, allocates, or knows what a bus is. Feed
 * it numbers and it hands back strings.
 */
#ifndef CASTALIA_MACH_CORE_H
#define CASTALIA_MACH_CORE_H

#include "castalia/ctypes.h"

/* OEM bytes returned in BH by INT 21h AH=30h. Only the ones this system can
 * actually be running under are named. */
#define MACH_OEM_IBM       0x00   /* PC DOS                                  */
#define MACH_OEM_COMPAQ    0x01
#define MACH_OEM_DIGITAL   0xEE   /* DR-DOS / Novell DOS                     */
#define MACH_OEM_FREEDOS   0xFD
#define MACH_OEM_MICROSOFT 0xFF   /* MS-DOS                                  */

/*
 * "FreeDOS 1.x (reports DOS 7.10)", "MS-DOS 6.22", "PC DOS 7.00".
 *
 * FreeDOS reports whatever DOS version it is configured to impersonate, which
 * is usually 7.10 and says nothing about which FreeDOS this is -- so the OEM
 * byte names the vendor and the reported version is shown as what it is,
 * rather than being presented as the product version. An OEM byte this table
 * does not know produces "DOS 6.22 (OEM 0x42)": the number is the honest
 * answer and hiding it would leave the line looking authoritative.
 *
 * A major version below 2 means the read did not happen or is not DOS at all;
 * that is written as such rather than as "DOS 0.00".
 */
void mach_dos_label(int major, int minor, int oem, char *dst, cu32 dstsz);

/*
 * The Windows version reported by INT 2Fh AX=1600h, if any. Running inside a
 * DOS box changes what the video and timer code can expect, so it belongs on
 * the same screen as the rest. Writes "" and returns CFALSE when the machine
 * is running plain DOS, which is the normal case for this system.
 */
cbool mach_windows_label(int major, int minor, char *dst, cu32 dstsz);

/*
 * The maker behind a PCI vendor ID ("Intel", "Cirrus Logic"), or NULL when
 * this table has never heard of it -- callers print the hex in that case.
 */
const char *mach_pci_vendor(cu32 vendor);

/*
 * What a PCI class/subclass pair is FOR ("VGA display controller", "Ethernet
 * controller"). Falls back from the specific subclass to the base class, and
 * returns NULL only when neither is known.
 */
const char *mach_pci_class(int cls, int sub);

/*
 * The best available name for one device, always written:
 *   "Cirrus Logic GD 5446"          both IDs known
 *   "Intel device 1F30"             vendor known, device not
 *   "PCI 1AF4:1000"                 neither known
 * The class is NOT included -- callers that have room print it separately, so
 * that a narrow window can drop it without this function having to know how
 * wide the window is. Returns CTRUE when the device itself was named.
 */
cbool mach_pci_describe(cu32 vendor, cu32 device, char *dst, cu32 dstsz);

/*
 * "639 KB" / "15.4 MB" for a kilobyte count, so three memory figures line up
 * without each caller inventing its own rounding. A negative count means "not
 * known", which is written as "n/a" rather than as a zero somebody would read
 * as a measurement.
 */
void mach_kb_label(long kb, char *dst, cu32 dstsz);

/*
 * One "  NAME          value" row for an environment variable, for the report.
 *
 * A NULL value is "(not set)", which is a different fact from an empty one and
 * is written differently. A value too long for the row ends in "...", because
 * a DOS PATH runs to 127 characters, this row holds fewer, and a value cut off
 * without a mark reads as the whole value -- on the line somebody checks to
 * find out why a program will not start, that is the worst way to be wrong.
 * Returns CTRUE when the whole value fitted.
 */
cbool mach_env_line(const char *name, const char *value, char *dst, cu32 dstsz);

#endif /* CASTALIA_MACH_CORE_H */
