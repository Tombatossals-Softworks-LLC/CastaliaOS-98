/*
 * eq_core.c - Three-band equalizer (pure integer DSP, host-tested).
 *
 * See eq_core.h for the band split and why it is written this way.
 */
#include "eq_core.h"

void eq_reset(EqState *st)
{
    if (st == NULL) { return; }
    st->lp1 = 0;
    st->lp2 = 0;
}

cbool eq_is_flat(const int *gain)
{
    int i;
    if (gain == NULL) { return CTRUE; }
    for (i = 0; i < EQ_BANDS; i++) {
        if (gain[i] != EQ_UNITY) { return CFALSE; }
    }
    return CTRUE;
}

void eq_process(EqState *st, const signed char *in, signed char *out,
                cu32 n, const int *gain)
{
    cu32 i;
    int gl, gm, gh;
    if (st == NULL || in == NULL || out == NULL || gain == NULL) { return; }
    gl = gain[0]; gm = gain[1]; gh = gain[2];

    for (i = 0; i < n; i++) {
        long x = (long)in[i] << 8;          /* Q8 */
        long low, mid, high, y;

        st->lp1 += ((x - st->lp1) * EQ_A_LOW) >> 8;
        st->lp2 += ((x - st->lp2) * EQ_A_MID) >> 8;

        low  = st->lp1;
        mid  = st->lp2 - st->lp1;
        high = x - st->lp2;               /* low + mid + high == x exactly  */

        y = (low * gl + mid * gm + high * gh) / EQ_UNITY;
        y >>= 8;
        if (y > 127)  { y = 127; }
        if (y < -128) { y = -128; }
        out[i] = (signed char)y;
    }
}

long eq_power(const signed char *buf, cu32 n)
{
    cu32 i;
    long acc = 0;
    if (buf == NULL || n == 0) { return 0; }
    for (i = 0; i < n; i++) {
        long v = buf[i];
        acc += v * v;
    }
    return acc / (long)n;
}
