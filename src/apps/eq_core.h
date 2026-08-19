/*
 * eq_core.h - A three-band equalizer over decoded PCM (no gfx/wm/platform).
 *
 * The DSP behind the Media Player's equalizer, kept pure so the host unit
 * tests can prove it actually filters (tests/test_eq.c) -- the same split
 * sheet_core.c and write_core.c use. That matters here more than anywhere
 * else: the host has a null sound backend, so a slider that only *looked*
 * connected would be indistinguishable from one that worked. The tests
 * measure band energy instead of trusting the ear.
 *
 * The split is two one-pole low-pass filters run in parallel:
 *
 *   lp1 = lowpass(x, a_low)          low  = lp1
 *   lp2 = lowpass(x, a_mid)          mid  = lp2 - lp1
 *                                    high = x  - lp2
 *
 * so the three bands sum back to exactly x. With every gain at EQ_UNITY the
 * output is therefore bit-identical to the input -- a property the tests pin,
 * because "flat" must never colour the sound.
 *
 * Integer throughout (Q8): this has to run on a 386 with no FPU.
 */
#ifndef CASTALIA_EQ_CORE_H
#define CASTALIA_EQ_CORE_H

#include "castalia/ctypes.h"

#define EQ_BANDS  3
#define EQ_UNITY  16      /* gain of 16 = x1.0 (unity)                     */
#define EQ_GAIN_MAX 48    /* x3.0 -- beyond this the clamp dominates       */

/* Filter poles, expressed as fractions of the sample rate so the equalizer
 * behaves the same whatever rate the clip was decoded to. */
#define EQ_A_LOW  32      /* corner near rate/50                           */
#define EQ_A_MID  160     /* corner near rate/10                           */

/* Filter memory. Zero it (eq_reset) before each independent stream. */
typedef struct {
    long lp1, lp2;        /* Q8 */
} EqState;

void eq_reset(EqState *st);

/* Filter 'n' samples from 'in' to 'out' (they may be the same buffer).
 * 'gain' holds EQ_BANDS values: EQ_UNITY is flat, 0 mutes that band.
 * Output is clamped to the signed 8-bit range rather than wrapping. */
void eq_process(EqState *st, const signed char *in, signed char *out,
                cu32 n, const int *gain);

/* Mean square level of a buffer -- what the tests compare, and what a level
 * meter would read. Returns 0 for an empty buffer. */
long eq_power(const signed char *buf, cu32 n);

/* CTRUE when every band is at unity (the app can then skip processing). */
cbool eq_is_flat(const int *gain);

#endif /* CASTALIA_EQ_CORE_H */
