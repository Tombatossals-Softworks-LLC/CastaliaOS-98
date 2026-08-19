/*
 * ctypes.h - CastaliaOS 98 PE fixed-width scalar types.
 *
 * The core runtime targets C89 and must compile identically on a modern
 * host (for testing and the headless backend) and under Open Watcom for
 * the 32-bit DOS product. C89 has no <stdint.h>, and 16-bit DOS builds
 * make "int" 16 bits, so we never assume the width of int. All sized
 * quantities go through the typedefs below.
 *
 * Widths are chosen so they hold on both ILP32 (Watcom flat DOS) and the
 * historical 16-bit small model:
 *   - short is 16 bits everywhere we care about.
 *   - long  is 32 bits everywhere we care about.
 *
 * This file has no dependencies and pulls in no system headers.
 */
#ifndef CASTALIA_CTYPES_H
#define CASTALIA_CTYPES_H

#include <limits.h>

typedef unsigned char  cu8;
typedef signed char    cs8;
typedef unsigned short cu16;
typedef short          cs16;

/* cu32/cs32 must be EXACTLY 32 bits so CColor is a true 32-bit XRGB pixel and
 * the DOS register frames pack correctly. On our real targets -- gcc/clang on
 * x86-64 and Open Watcom in 32-bit flat mode -- `int` is 32 bits, so we use
 * `unsigned int` there. Only a genuine 16-bit real-mode build (not the flagship
 * product) falls back to `long`. */
#if defined(UINT_MAX) && (UINT_MAX >= 0xFFFFFFFFUL)
typedef unsigned int   cu32;
typedef int            cs32;
#else
typedef unsigned long  cu32;
typedef long           cs32;
#endif

/* Boolean. C89 has no _Bool; use an int with explicit constants. */
typedef int cbool;
#define CTRUE  1
#define CFALSE 0

/* A packed 32-bit color in canonical 0x00RRGGBB form (X in the top byte).
 * The whole UI renders in this format; the platform present() step is the
 * only place that converts down to the hardware's 8bpp or 16bpp mode. */
typedef cu32 CColor;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Result codes returned across subsystem boundaries. Negative == failure;
 * CE_OK == success. Keep the list small and stable. */
typedef enum {
    CE_OK              =  0,
    CE_FAIL            = -1,   /* generic, unclassified failure           */
    CE_NOMEM           = -2,   /* allocation failed                       */
    CE_INVALID         = -3,   /* invalid argument                        */
    CE_NOTFOUND        = -4,   /* file / key / entity not found           */
    CE_IO              = -5,   /* I/O error                               */
    CE_UNSUPPORTED     = -6,   /* not supported on this platform/mode     */
    CE_OVERFLOW        = -7,   /* buffer / range overflow                 */
    CE_BUSY            = -8,   /* resource busy / already in use          */
    CE_EOF             = -9    /* end of stream                           */
} CResult;

#define CASTALIA_UNUSED(x) ((void)(x))

/* Global fixed limits, kept here so every subsystem header can use them
 * without pulling in the umbrella header. Modest for a P2/128MB machine. */
#define CASTALIA_MAX_WINDOWS 64
#define CASTALIA_MAX_PATH    260
#define CASTALIA_MAX_NAME    64

#endif /* CASTALIA_CTYPES_H */
