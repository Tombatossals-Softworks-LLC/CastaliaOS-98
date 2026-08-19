/*
 * test_cpu.c - CPUID decoding (cpu_core.c).
 *
 * A lookup table with edge cases, which is exactly the kind of code that looks
 * finished and is not. The checks below pin the three things it can get wrong:
 * the same family/model meaning different processors for different vendors,
 * the answer for a CPU the table has never heard of, and a vendor field that
 * is not text at all -- which means the read failed, and must not come back
 * looking like a new manufacturer.
 */
#include "ctest.h"
#include "../src/sys/cpu_core.h"

#include <string.h>

static void mk(CpuInfo *ci, const char *vendor, int fam, int mod, cu32 feat)
{
    memset(ci, 0, sizeof(*ci));
    ci->has_cpuid = CTRUE;
    strcpy(ci->vendor, vendor);
    ci->family = fam;
    ci->model = mod;
    ci->features = feat;
}

void test_cpu(void)
{
    CpuInfo ci;
    char buf[96];

    printf("- cpu identification\n");

    /* ---- vendors ------------------------------------------------------ */
    CHECK_STR(cpu_vendor_label("GenuineIntel"), "Intel");
    CHECK_STR(cpu_vendor_label("AuthenticAMD"), "AMD");
    CHECK_STR(cpu_vendor_label("CyrixInstead"), "Cyrix");
    CHECK_STR(cpu_vendor_label("UMC UMC UMC "), "UMC");
    CHECK_STR(cpu_vendor_label("GenuineTMx86"), "Transmeta");
    /* Unknown but printable: shown verbatim rather than guessed at. */
    CHECK_STR(cpu_vendor_label("MyOwnCPU123"), "MyOwnCPU123");
    /* Not text: the read went wrong, and saying so beats inventing a name. */
    CHECK_STR(cpu_vendor_label("\001\002\003\004\005\006\007\010\011\012\013\014"),
              "Unknown");
    CHECK_STR(cpu_vendor_label(""), "Unknown");
    CHECK_STR(cpu_vendor_label(NULL), "Unknown");

    /* ---- the same numbers mean different chips per vendor -------------- */
    mk(&ci, "GenuineIntel", 5, 1, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel Pentium");
    mk(&ci, "AuthenticAMD", 5, 1, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "AMD K5");
    mk(&ci, "CyrixInstead", 5, 1, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Cyrix 6x86");

    mk(&ci, "GenuineIntel", 6, 1, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel Pentium Pro");
    mk(&ci, "AuthenticAMD", 6, 1, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "AMD Athlon");

    /* ---- the era this actually targets --------------------------------- */
    mk(&ci, "GenuineIntel", 3, 0, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel 386");
    mk(&ci, "GenuineIntel", 4, 2, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel 486SX");
    mk(&ci, "GenuineIntel", 4, 8, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel 486DX4");
    mk(&ci, "GenuineIntel", 5, 4, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel Pentium MMX");
    mk(&ci, "AuthenticAMD", 5, 8, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "AMD K6-2");
    mk(&ci, "AuthenticAMD", 4, 14, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "AMD Am5x86");

    /* Family 6 outlived this era by twenty years; a model the table does not
     * know must not be announced as a Pentium Pro. */
    mk(&ci, "GenuineIntel", 6, 207, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel family 6 model 207");

    /* ---- unknown to the table: the numbers, not a guess ---------------- */
    mk(&ci, "GenuineIntel", 21, 3, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "Intel family 21 model 3");
    mk(&ci, "MyOwnCPU123", 7, 2, 0);
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "MyOwnCPU123 family 7 model 2");

    /* ---- before CPUID existed ------------------------------------------ */
    memset(&ci, 0, sizeof(ci));
    ci.has_cpuid = CFALSE;
    cpu_describe(&ci, buf, sizeof(buf));
    CHECK_STR(buf, "80386 or early 80486 (no CPUID)");

    /* ---- refusals ------------------------------------------------------ */
    cpu_describe(NULL, buf, sizeof(buf));
    CHECK_STR(buf, "Unknown");
    buf[0] = 'Z'; buf[1] = '\0';
    cpu_describe(&ci, buf, 0u);       /* must not write through a zero cap */
    CHECK_STR(buf, "Z");

    /* A name longer than the buffer is truncated, never overrun. */
    {
        char small[8];
        mk(&ci, "GenuineIntel", 6, 7, 0);
        cpu_describe(&ci, small, sizeof(small));
        CHECK(strlen(small) < sizeof(small));
    }

    /* ---- feature bits -------------------------------------------------- */
    CHECK_EQI(cpu_feature_list(CPU_FEAT_FPU, buf, sizeof(buf)), 1);
    CHECK_STR(buf, "FPU");
    CHECK_EQI(cpu_feature_list(CPU_FEAT_FPU | CPU_FEAT_MMX, buf, sizeof(buf)), 2);
    CHECK_STR(buf, "FPU MMX");
    /* Fixed order regardless of which bits are set, so two machines can be
     * compared by eye. */
    CHECK_EQI(cpu_feature_list(CPU_FEAT_SSE2 | CPU_FEAT_FPU | CPU_FEAT_TSC,
                               buf, sizeof(buf)), 3);
    CHECK_STR(buf, "FPU TSC SSE2");
    CHECK_EQI(cpu_feature_list(0u, buf, sizeof(buf)), 0);
    CHECK_STR(buf, "none");
    /* A 486SX has no FPU, and the list has to be able to say so. */
    CHECK_EQI(cpu_feature_list(CPU_FEAT_TSC, buf, sizeof(buf)), 1);
    CHECK_STR(buf, "TSC");
    CHECK_EQI(cpu_feature_list(0xFFFFFFFFUL, buf, sizeof(buf)), 7);
    CHECK_STR(buf, "FPU TSC CX8 CMOV MMX SSE SSE2");
    CHECK_EQI(cpu_feature_list(CPU_FEAT_FPU, NULL, 10u), 0);
    {
        char tiny[6];
        cpu_feature_list(0xFFFFFFFFUL, tiny, sizeof(tiny));
        CHECK(strlen(tiny) < sizeof(tiny));
    }
}
