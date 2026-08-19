/*
 * cpu_core.c - CPUID number to readable name (see cpu_core.h).
 *
 * The tables below cover the processors this system can plausibly run on and
 * a little beyond, and stop there. Where a family is unknown the answer says
 * so with the numbers attached rather than inventing a marketing name, because
 * the person reading System Information on an unfamiliar machine is better
 * served by "family 21 model 3" than by a confident wrong answer.
 */
#include "castalia/ctypes.h"
#include "castalia/sys.h"
#include "cpu_core.h"

static cbool cpu_streq(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 12; i++) {
        if (a[i] != b[i]) { return CFALSE; }
        if (a[i] == '\0') { return CTRUE; }
    }
    return CTRUE;
}

const char *cpu_vendor_label(const char *vendor12)
{
    int i;
    if (vendor12 == NULL || vendor12[0] == '\0') { return "Unknown"; }
    if (cpu_streq(vendor12, "GenuineIntel")) { return "Intel"; }
    if (cpu_streq(vendor12, "AuthenticAMD")) { return "AMD"; }
    if (cpu_streq(vendor12, "CyrixInstead")) { return "Cyrix"; }
    if (cpu_streq(vendor12, "CentaurHauls")) { return "Centaur"; }
    if (cpu_streq(vendor12, "NexGenDriven")) { return "NexGen"; }
    if (cpu_streq(vendor12, "UMC UMC UMC ")) { return "UMC"; }
    if (cpu_streq(vendor12, "RiseRiseRise")) { return "Rise"; }
    if (cpu_streq(vendor12, "GenuineTMx86")) { return "Transmeta"; }
    if (cpu_streq(vendor12, "Geode by NSC")) { return "National"; }
    /* Unrecognised. Show it verbatim if it is text, refuse to show it at all
     * if it is not -- a vendor field of control bytes means the read failed,
     * and printing it as a name would disguise that as a new manufacturer. */
    for (i = 0; i < 12 && vendor12[i] != '\0'; i++) {
        if (vendor12[i] < 0x20 || vendor12[i] > 0x7E) { return "Unknown"; }
    }
    return vendor12;
}

/* Intel, by the families this project can meet. */
static const char *cpu_intel_name(int family, int model)
{
    switch (family) {
    case 3: return "386";
    case 4:
        switch (model) {
        case 0: case 1: return "486DX";
        case 2:         return "486SX";
        case 3:         return "486DX2";
        case 4:         return "486SL";
        case 5:         return "486SX2";
        case 7:         return "486DX2 (write-back)";
        case 8:         return "486DX4";
        default:        return "486";
        }
    case 5:
        switch (model) {
        case 1: case 2: case 3: return "Pentium";
        case 4: case 7: case 8: return "Pentium MMX";
        default:                return "Pentium";
        }
    case 6:
        switch (model) {
        case 1:                 return "Pentium Pro";
        case 3: case 5:         return "Pentium II";
        case 6:                 return "Celeron";
        case 7: case 8:
        case 10: case 11:       return "Pentium III";
        case 9: case 13:        return "Pentium M";
        /* Family 6 ran for two decades past anything this system targets.
         * Calling an unknown model "Pentium Pro family" is the confident
         * wrong answer the numeric fallback exists to avoid. */
        default:                return NULL;
        }
    case 15: return "Pentium 4";
    default: return NULL;
    }
}

static const char *cpu_amd_name(int family, int model)
{
    switch (family) {
    case 4:
        return (model == 14 || model == 15) ? "Am5x86" : "Am486";
    case 5:
        switch (model) {
        case 0: case 1: case 2: case 3: return "K5";
        case 6: case 7:                 return "K6";
        case 8:                         return "K6-2";
        case 9: case 13:                return "K6-III";
        default:                        return "K5/K6";
        }
    case 6: return "Athlon";
    case 15: return "Athlon 64";
    default: return NULL;
    }
}

static const char *cpu_cyrix_name(int family, int model)
{
    CASTALIA_UNUSED(model);
    switch (family) {
    case 4: return "Cx486";
    case 5: return "6x86";
    case 6: return "6x86MX";
    default: return NULL;
    }
}

void cpu_describe(const CpuInfo *ci, char *dst, cu32 dstsz)
{
    const char *vendor, *name = NULL;
    if (dst == NULL || dstsz == 0u) { return; }
    if (ci == NULL) { sys_strlcpy(dst, "Unknown", dstsz); return; }

    if (!ci->has_cpuid) {
        /* Before CPUID there is nothing to decode. Say that plainly instead
         * of pretending to a model number we cannot know. */
        sys_strlcpy(dst, "80386 or early 80486 (no CPUID)", dstsz);
        return;
    }

    vendor = cpu_vendor_label(ci->vendor);
    if (cpu_streq(ci->vendor, "GenuineIntel")) {
        name = cpu_intel_name(ci->family, ci->model);
    } else if (cpu_streq(ci->vendor, "AuthenticAMD")) {
        name = cpu_amd_name(ci->family, ci->model);
    } else if (cpu_streq(ci->vendor, "CyrixInstead")) {
        name = cpu_cyrix_name(ci->family, ci->model);
    }

    if (name != NULL) {
        sys_snprintf(dst, dstsz, "%s %s", vendor, name);
    } else {
        /* Unknown to the table: the numbers, which are at least true. */
        sys_snprintf(dst, dstsz, "%s family %d model %d", vendor,
                     ci->family, ci->model);
    }
}

int cpu_feature_list(cu32 features, char *dst, cu32 dstsz)
{
    static const cu32 BITS[7] = {
        CPU_FEAT_FPU, CPU_FEAT_TSC, CPU_FEAT_CX8, CPU_FEAT_CMOV,
        CPU_FEAT_MMX, CPU_FEAT_SSE, CPU_FEAT_SSE2
    };
    static const char *NAMES[7] = {
        "FPU", "TSC", "CX8", "CMOV", "MMX", "SSE", "SSE2"
    };
    int i, n = 0;
    if (dst == NULL || dstsz == 0u) { return 0; }
    dst[0] = '\0';
    for (i = 0; i < 7; i++) {
        if ((features & BITS[i]) != 0u) {
            if (n > 0) { sys_strlcat(dst, " ", dstsz); }
            sys_strlcat(dst, NAMES[i], dstsz);
            n++;
        }
    }
    if (n == 0) { sys_strlcpy(dst, "none", dstsz); }
    return n;
}
