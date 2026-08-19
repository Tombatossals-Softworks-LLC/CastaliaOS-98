/*
 * test_mach.c - Machine identification (mach_core.c).
 *
 * The same shape of code as cpu_core.c and the same trap: a table that looks
 * finished. What these checks pin is not "the table has an entry for X" -- a
 * table can be extended without breaking anything -- but the three decisions
 * that would be WRONG rather than incomplete:
 *
 *   - a version number that came from nowhere must not be printed as a
 *     measurement,
 *   - a device this system has never heard of must come back as its hex ID
 *     rather than as a plausible name, because the reason to open System
 *     Information on an unfamiliar machine is to find out what is in it,
 *   - FreeDOS reports a DOS version it is impersonating, and presenting that
 *     as the product version names a release that has never existed.
 */
#include "ctest.h"
#include "../src/sys/mach_core.h"

#include <string.h>

static void mt_dos(void)
{
    char b[80];

    mach_dos_label(6, 22, MACH_OEM_MICROSOFT, b, sizeof(b));
    CHECK_STR(b, "MS-DOS 6.22");
    mach_dos_label(7, 0, MACH_OEM_IBM, b, sizeof(b));
    CHECK_STR(b, "PC DOS 7.00");
    mach_dos_label(6, 0, MACH_OEM_DIGITAL, b, sizeof(b));
    CHECK_STR(b, "DR-DOS 6.00");

    /* The version FreeDOS reports is a compatibility setting, not a release.
     * "FreeDOS 7.10" would name something that does not exist. */
    mach_dos_label(7, 10, MACH_OEM_FREEDOS, b, sizeof(b));
    CHECK_STR(b, "FreeDOS (reports DOS 7.10)");
    mach_dos_label(5, 0, MACH_OEM_FREEDOS, b, sizeof(b));
    CHECK_STR(b, "FreeDOS (reports DOS 5.00)");

    /* An OEM byte this table does not know still shows the number, so the
     * line does not read as authoritative when it is not. */
    mach_dos_label(6, 22, 0x42, b, sizeof(b));
    CHECK_STR(b, "DOS 6.22 (OEM 0x42)");

    /* A major below 2 means the read did not happen. Printing "DOS 0.00"
     * would present a failure as a fact. */
    mach_dos_label(0, 0, MACH_OEM_MICROSOFT, b, sizeof(b));
    CHECK_STR(b, "not DOS (version not reported)");
    mach_dos_label(1, 0, MACH_OEM_FREEDOS, b, sizeof(b));
    CHECK_STR(b, "not DOS (version not reported)");

    /* The minor is two digits: 6.2 and 6.20 are different DOS versions and
     * writing "6.2" for the latter is the classic way to conflate them. */
    mach_dos_label(6, 2, MACH_OEM_MICROSOFT, b, sizeof(b));
    CHECK_STR(b, "MS-DOS 6.02");

    /* No output buffer at all is a no-op, not a crash. */
    mach_dos_label(6, 22, MACH_OEM_MICROSOFT, NULL, 0);
    b[0] = 'z';
    mach_dos_label(6, 22, MACH_OEM_MICROSOFT, b, 0);
    CHECK(b[0] == 'z');
}

static void mt_windows(void)
{
    char b[80];

    /* Plain DOS is the normal case and must report nothing, not "Windows
     * 0.0" -- this line is only drawn when the answer is CTRUE. */
    CHECK(mach_windows_label(0, 0, b, sizeof(b)) == CFALSE);
    CHECK_STR(b, "");
    CHECK(mach_windows_label(0x80, 0, b, sizeof(b)) == CFALSE);
    CHECK(mach_windows_label(0xFF, 0, b, sizeof(b)) == CFALSE);
    CHECK(mach_windows_label(1, 0, b, sizeof(b)) == CFALSE);

    /* One major version covers the whole 9x line; only the minor separates
     * them, which is the part that is easy to get wrong. */
    CHECK(mach_windows_label(4, 0, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "Windows 95 DOS box");
    CHECK(mach_windows_label(4, 10, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "Windows 98 DOS box");
    CHECK(mach_windows_label(4, 90, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "Windows Me DOS box");

    mach_windows_label(3, 1, b, sizeof(b));
    CHECK_STR(b, "Windows 3.1 DOS box");
    /* Something newer than this table knows is still reported, numerically. */
    mach_windows_label(5, 1, b, sizeof(b));
    CHECK_STR(b, "Windows 5.1 DOS box");

    CHECK(mach_windows_label(4, 0, NULL, 0) == CFALSE);
}

static void mt_pci(void)
{
    char b[80];

    CHECK_STR(mach_pci_vendor(0x8086UL), "Intel");
    CHECK_STR(mach_pci_vendor(0x5333UL), "S3");
    CHECK_STR(mach_pci_vendor(0x1013UL), "Cirrus Logic");
    /* The high half of a full 32-bit read must not reach the comparison. */
    CHECK_STR(mach_pci_vendor(0x12348086UL), "Intel");
    CHECK(mach_pci_vendor(0x0BADUL) == NULL);

    /* Both known: the part gets its name. */
    CHECK(mach_pci_describe(0x1013UL, 0x00B8UL, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "Cirrus Logic GD 5446");
    CHECK(mach_pci_describe(0x8086UL, 0x7010UL, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "Intel PIIX3 IDE");
    CHECK(mach_pci_describe(0x1234UL, 0x1111UL, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "QEMU standard VGA");

    /* Vendor known, part not: say so, and give the number. Inventing a name
     * here is the failure mode this whole module exists to avoid. */
    CHECK(mach_pci_describe(0x8086UL, 0x1F30UL, b, sizeof(b)) == CFALSE);
    CHECK_STR(b, "Intel device 1F30");
    /* Neither known: nothing but the numbers, which are still the answer. */
    CHECK(mach_pci_describe(0x0BADUL, 0xF00DUL, b, sizeof(b)) == CFALSE);
    CHECK_STR(b, "PCI 0BAD:F00D");
    /* An empty slot reads as all-ones on a real bus. It must not be dressed
     * up as a device. */
    CHECK(mach_pci_describe(0xFFFFUL, 0xFFFFUL, b, sizeof(b)) == CFALSE);
    CHECK_STR(b, "PCI FFFF:FFFF");

    /* Two different parts from the same maker must not collide -- a table
     * keyed on the vendor alone would return the same name for both. */
    mach_pci_describe(0x8086UL, 0x7000UL, b, sizeof(b));
    CHECK_STR(b, "Intel PIIX3 ISA bridge");
    mach_pci_describe(0x8086UL, 0x7020UL, b, sizeof(b));
    CHECK_STR(b, "Intel PIIX3 USB");
    /* ...and the same device number from two makers must not, either. */
    mach_pci_describe(0x1013UL, 0x0020UL, b, sizeof(b));
    CHECK_STR(b, "Cirrus Logic device 0020");
    mach_pci_describe(0x10DEUL, 0x0020UL, b, sizeof(b));
    CHECK_STR(b, "NVIDIA RIVA TNT");

    CHECK(mach_pci_describe(0x8086UL, 0x7010UL, NULL, 0) == CFALSE);

    /* Classes: the specific subclass wins, the base class is the fallback,
     * and an unknown base is NULL rather than a made-up label. */
    CHECK_STR(mach_pci_class(0x03, 0x00), "VGA display controller");
    CHECK_STR(mach_pci_class(0x03, 0x77), "display controller");
    CHECK_STR(mach_pci_class(0x01, 0x01), "IDE controller");
    CHECK_STR(mach_pci_class(0x01, 0x00), "SCSI controller");
    CHECK_STR(mach_pci_class(0x02, 0x00), "Ethernet controller");
    CHECK_STR(mach_pci_class(0x04, 0x01), "audio device");
    CHECK_STR(mach_pci_class(0x06, 0x00), "host bridge");
    CHECK_STR(mach_pci_class(0x06, 0x01), "ISA bridge");
    CHECK_STR(mach_pci_class(0x0C, 0x03), "USB controller");
    CHECK(mach_pci_class(0x7F, 0x00) == NULL);
}

static void mt_sizes(void)
{
    char b[40];

    mach_kb_label(639, b, sizeof(b));
    CHECK_STR(b, "639 KB");
    mach_kb_label(0, b, sizeof(b));
    CHECK_STR(b, "0 KB");
    mach_kb_label(1023, b, sizeof(b));
    CHECK_STR(b, "1023 KB");
    mach_kb_label(1024, b, sizeof(b));
    CHECK_STR(b, "1.0 MB");
    mach_kb_label(15 * 1024 + 512, b, sizeof(b));
    CHECK_STR(b, "15.5 MB");
    mach_kb_label(65536, b, sizeof(b));
    CHECK_STR(b, "64.0 MB");

    /* "Not known" is not zero. A machine with no XMS driver and a machine
     * with no free XMS are different problems, and a bare "0 KB" would read
     * as the second when it is the first. */
    mach_kb_label(-1, b, sizeof(b));
    CHECK_STR(b, "n/a");

    mach_kb_label(1024, NULL, 0);
    b[0] = 'z';
    mach_kb_label(1024, b, 0);
    CHECK(b[0] == 'z');
}

static void mt_env(void)
{
    char b[40];
    /* 40 - 2 - 14 = 24 characters of value fit in this row. */

    CHECK(mach_env_line("BLASTER", "A220 I5 D1 T4", b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "  BLASTER       A220 I5 D1 T4");

    /* "not set" and "set to nothing" are different facts about a machine and
     * must not print the same. */
    CHECK(mach_env_line("TEMP", NULL, b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "  TEMP          (not set)");
    CHECK(mach_env_line("TEMP", "", b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "  TEMP          ");

    /* A DOS PATH is longer than any row this report has. Cut it, and SAY it
     * was cut -- this is the line somebody reads to find out why a program
     * will not start, and a silently shortened PATH looks like a short PATH. */
    CHECK(mach_env_line("PATH", "C:\\DOS;C:\\WINDOWS;C:\\CASTALIA;C:\\UTIL",
                        b, sizeof(b)) == CFALSE);
    CHECK_STR(b, "  PATH          C:\\DOS;C:\\WINDOWS;C:...");

    /* One character too many is still too many. */
    CHECK(mach_env_line("X", "123456789012345678901234", b, sizeof(b)) == CFALSE);
    /* ...and a value that fills the row EXACTLY is not truncated, so it must
     * keep all of itself. Marking this one would destroy three characters of
     * a correct answer in order to say something untrue about it. */
    CHECK(mach_env_line("X", "12345678901234567890123", b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "  X             12345678901234567890123");

    /* A name wider than the column pushes the value along rather than being
     * cut itself, and what it pushes past the end is what gets marked. */
    CHECK(mach_env_line("A_VERY_LONG_VARIABLE_NAME", "x", b, sizeof(b)) == CTRUE);
    CHECK_STR(b, "  A_VERY_LONG_VARIABLE_NAMEx");
    CHECK(mach_env_line("A_VERY_LONG_VARIABLE_NAME",
                        "0123456789012", b, sizeof(b)) == CFALSE);
    CHECK_STR(b, "  A_VERY_LONG_VARIABLE_NAME012345678...");

    /* No buffer is a no-op, not a crash. */
    CHECK(mach_env_line("PATH", "x", NULL, 0) == CFALSE);
    b[0] = 'z';
    CHECK(mach_env_line("PATH", "x", b, 0) == CFALSE);
    CHECK(b[0] == 'z');
}

void test_mach(void)
{
    printf("- machine identification\n");
    mt_dos();
    mt_windows();
    mt_pci();
    mt_sizes();
    mt_env();
}
