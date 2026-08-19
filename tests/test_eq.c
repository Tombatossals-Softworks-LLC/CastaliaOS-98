/*
 * test_eq.c - Three-band equalizer (eq_core.c).
 *
 * The host has no audio, so "it sounds right" is not available as evidence.
 * These tests measure band energy instead: a slow wave must respond to the
 * LOW slider and barely to the HIGH one, a fast wave the other way round.
 * That is what distinguishes a working filter from a slider wired to nothing.
 */
#include "ctest.h"
#include "eq_core.h"

#define N 2048
static signed char g_in[N];
static signed char g_out[N];

/* A square-ish wave with 'period' samples per cycle: low period = high
 * frequency. Amplitude well under the clamp so gain changes are measurable. */
static void make_wave(int period)
{
    int i;
    for (i = 0; i < N; i++) {
        g_in[i] = (signed char)(((i / (period / 2)) % 2) ? 60 : -60);
    }
}

static void run(const int *gain)
{
    EqState st;
    eq_reset(&st);
    eq_process(&st, g_in, g_out, (cu32)N, gain);
}

/* Skip the filter's start-up transient when measuring. */
static long tail_power(void)
{
    return eq_power(g_out + N / 2, (cu32)(N / 2));
}

static void eq_flat_is_transparent(void)
{
    int flat[EQ_BANDS];
    int i;
    for (i = 0; i < EQ_BANDS; i++) { flat[i] = EQ_UNITY; }
    CHECK(eq_is_flat(flat));

    /* The three bands sum back to the input, so flat must be bit-identical --
     * an equalizer that colours the sound when it is centred is broken. */
    make_wave(64);
    run(flat);
    for (i = 0; i < N; i++) {
        if (g_out[i] != g_in[i]) { break; }
    }
    CHECK_EQI(i, N);

    /* ...and a non-centred band is reported as not flat. */
    flat[1] = EQ_UNITY + 1;
    CHECK(!eq_is_flat(flat));
}

static void eq_bands_are_real(void)
{
    int flat[EQ_BANDS] = { EQ_UNITY, EQ_UNITY, EQ_UNITY };
    int boost_low[EQ_BANDS]  = { EQ_UNITY * 3, EQ_UNITY, EQ_UNITY };
    int boost_high[EQ_BANDS] = { EQ_UNITY, EQ_UNITY, EQ_UNITY * 3 };
    int kill_low[EQ_BANDS]   = { 0, EQ_UNITY, EQ_UNITY };
    int kill_high[EQ_BANDS]  = { EQ_UNITY, EQ_UNITY, 0 };
    long base, boosted, killed;

    /* --- a SLOW wave lives in the low band --- */
    make_wave(512);
    run(flat);       base = tail_power();
    run(boost_low);  boosted = tail_power();
    run(kill_low);   killed = tail_power();
    CHECK(boosted > base);        /* the low slider moves it        */
    CHECK(killed < base / 2);     /* and cutting low guts it        */

    run(boost_high); boosted = tail_power();
    CHECK(boosted < base * 2);    /* the high slider barely touches it */

    /* --- a FAST wave lives in the high band --- */
    make_wave(4);
    run(flat);        base = tail_power();
    run(boost_high);  boosted = tail_power();
    run(kill_high);   killed = tail_power();
    CHECK(boosted > base);
    CHECK(killed < base / 2);

    run(kill_low);    killed = tail_power();
    CHECK(killed > base / 2);     /* cutting LOW leaves it mostly intact */
}

static void eq_clamps_and_guards(void)
{
    int hot[EQ_BANDS] = { EQ_GAIN_MAX, EQ_GAIN_MAX, EQ_GAIN_MAX };
    int mute[EQ_BANDS] = { 0, 0, 0 };
    int i;

    /* A loud input at maximum gain must clamp, never wrap: a wrapped sample
     * flips sign and would be heard as a click. */
    make_wave(64);
    for (i = 0; i < N; i++) { g_in[i] = (signed char)((i & 1) ? 120 : -120); }
    run(hot);
    {   /* one assertion for the whole buffer -- a per-sample CHECK would
         * drown the suite's count in thousands of identical successes */
        int wrapped = 0;
        for (i = N / 2; i < N; i++) {
            if (g_in[i] > 0 && g_out[i] <= 0) { wrapped++; }
            if (g_in[i] < 0 && g_out[i] >= 0) { wrapped++; }
        }
        CHECK_EQI(wrapped, 0);
    }

    /* Every band at zero is silence. */
    run(mute);
    CHECK_EQI((int)eq_power(g_out + N / 2, (cu32)(N / 2)), 0);

    /* NULL arguments and an empty run are no-ops, not crashes. */
    {
        EqState st;
        eq_reset(&st);
        eq_process(&st, NULL, g_out, (cu32)N, hot);
        eq_process(&st, g_in, NULL, (cu32)N, hot);
        eq_process(&st, g_in, g_out, 0, hot);
        eq_process(NULL, g_in, g_out, (cu32)N, hot);
        eq_reset(NULL);
        CHECK_EQI((int)eq_power(NULL, 10), 0);
        CHECK_EQI((int)eq_power(g_in, 0), 0);
        CHECK(eq_is_flat(NULL));
    }
}

/* Filtering in place must match filtering into a separate buffer. */
static void eq_in_place(void)
{
    int g[EQ_BANDS] = { EQ_UNITY * 2, EQ_UNITY / 2, EQ_UNITY };
    static signed char copy[N];
    EqState st;
    int i;

    make_wave(32);
    for (i = 0; i < N; i++) { copy[i] = g_in[i]; }

    run(g);                                  /* in -> g_out */
    eq_reset(&st);
    eq_process(&st, copy, copy, (cu32)N, g); /* in place    */
    for (i = 0; i < N; i++) {
        if (copy[i] != g_out[i]) { break; }
    }
    CHECK_EQI(i, N);
}

void test_eq(void)
{
    printf("- eq\n");
    eq_flat_is_transparent();
    eq_bands_are_real();
    eq_clamps_and_guards();
    eq_in_place();
}
