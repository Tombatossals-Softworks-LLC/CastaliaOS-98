/*
 * cpu_core.h - Turning raw CPUID numbers into something a person can read.
 *
 * System Information reports the video mode, the sound device and the memory
 * budget, and says nothing at all about the processor -- which on a machine
 * this targets is the first thing anyone wants to know, because a 386SX and a
 * Pentium are the same "PC" and a very different experience.
 *
 * Reading CPUID is four instructions and belongs in the platform layer. What
 * does NOT belong there is the part that actually gets things wrong: deciding
 * that family 5 model 4 is a Pentium MMX, that family 6 model 1 from AMD is a
 * K7 and from Intel a Pentium Pro, that a vendor string of twelve arbitrary
 * bytes should be shown as-is rather than guessed at, and which feature bits
 * are worth naming. That is a lookup table with edge cases, so it lives here
 * as pure functions over numbers and is walked by tests/test_cpu.c.
 *
 * Nothing here executes an instruction, allocates, or knows what a CPU is.
 * Feed it numbers and it hands back strings.
 */
#ifndef CASTALIA_CPU_CORE_H
#define CASTALIA_CPU_CORE_H

#include "castalia/ctypes.h"

/* Feature bits of CPUID leaf 1, EDX -- only the ones worth naming on a
 * machine of this era. */
#define CPU_FEAT_FPU   0x00000001UL
#define CPU_FEAT_TSC   0x00000010UL
#define CPU_FEAT_CX8   0x00000100UL
#define CPU_FEAT_CMOV  0x00008000UL
#define CPU_FEAT_MMX   0x00800000UL
#define CPU_FEAT_SSE   0x02000000UL
#define CPU_FEAT_SSE2  0x04000000UL

typedef struct {
    cbool has_cpuid;      /* CFALSE on a 386 or an early 486               */
    char  vendor[13];     /* the raw twelve bytes, NUL terminated          */
    int   family;
    int   model;
    int   stepping;
    cu32  features;       /* leaf 1 EDX                                    */
} CpuInfo;

/*
 * The maker's readable name for a raw vendor string ("GenuineIntel" ->
 * "Intel"). An unrecognised string is NOT guessed at: it comes back as-is if
 * it is printable, and as "Unknown" if it is not, because a vendor field full
 * of control characters means the read went wrong and inventing a name for it
 * would hide that.
 */
const char *cpu_vendor_label(const char *vendor12);

/*
 * A readable processor name -- "Intel Pentium MMX", "AMD K6-2", "486DX".
 * Always writes something: a family this table does not know still produces
 * "Intel family 21 model 3" rather than an empty line, because "we do not
 * recognise it" is information and a blank field is not.
 */
void cpu_describe(const CpuInfo *ci, char *dst, cu32 dstsz);

/*
 * The named feature bits, space separated ("FPU TSC CX8 MMX"), in a fixed
 * order so two machines can be compared by eye. Returns how many were named;
 * writes "none" when a CPU reports no bit we care about.
 */
int cpu_feature_list(cu32 features, char *dst, cu32 dstsz);

#endif /* CASTALIA_CPU_CORE_H */
