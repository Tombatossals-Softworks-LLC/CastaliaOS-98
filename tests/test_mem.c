/*
 * test_mem.c - The accounted allocator, and the numbers the budget rests on.
 *
 * This suite exists because of a measurement. Making sys_mem_peak_bytes()
 * return a constant zero -- so the shell's footprint became unmeasurable --
 * broke NOTHING: no unit check, no demo scene, no abuse world. And yet
 * `--mem-check` is the guard on this project's central constraint (a four
 * megabyte machine), the README quotes its reading as a fact, and the capture
 * scene added this week asserts that saving a screenshot does not cost a
 * megabyte. All three were reading an instrument nobody had ever checked.
 *
 * So the checks below are about the instrument, not about any particular
 * allocation: live must move by exactly what was asked for, peak must be a
 * high-water mark that does not fall, and the count must follow. A test that
 * only asked "does sys_alloc return non-NULL" would have passed against the
 * broken accounting too.
 *
 * Everything is measured as a DELTA from wherever the counters happen to be,
 * because other suites have already run and this one has no business assuming
 * it starts from an empty heap.
 */
#include "ctest.h"
#include "castalia/sys.h"

void test_mem(void)
{
    cu32 base_live, base_peak, base_count;
    void *a, *b;

    printf("- accounted allocator\n");

    base_live  = sys_mem_live_bytes();
    base_peak  = sys_mem_peak_bytes();
    base_count = sys_mem_alloc_count();

    /* ---- live follows the bytes actually asked for --------------------- */
    a = sys_alloc(1000u);
    CHECK(a != NULL);
    CHECK_EQI((long)(sys_mem_live_bytes() - base_live), 1000);
    CHECK_EQI((long)(sys_mem_alloc_count() - base_count), 1);

    b = sys_alloc(2500u);
    CHECK(b != NULL);
    CHECK_EQI((long)(sys_mem_live_bytes() - base_live), 3500);

    sys_free(b, 2500u);
    CHECK_EQI((long)(sys_mem_live_bytes() - base_live), 1000);
    sys_free(a, 1000u);
    CHECK_EQI((long)(sys_mem_live_bytes() - base_live), 0);
    CHECK_EQI((long)(sys_mem_alloc_count() - base_count), 0);

    /* ---- peak is a high-water mark ------------------------------------- */
    /*
     * THE property --mem-check depends on. Live returning to where it started
     * must not take peak down with it, or the budget would only ever see
     * whatever happened to be allocated at the moment it looked.
     */
    {
        cu32 peak_before;
        void *big = sys_alloc(200000u);
        CHECK(big != NULL);
        CHECK(sys_mem_peak_bytes() >= base_live + 200000u);
        peak_before = sys_mem_peak_bytes();
        sys_free(big, 200000u);
        CHECK_EQI((long)(sys_mem_live_bytes() - base_live), 0);
        CHECK_EQI((long)sys_mem_peak_bytes(), (long)peak_before);  /* held */
        /* A smaller allocation afterwards must not raise it either. */
        big = sys_alloc(100u);
        CHECK_EQI((long)sys_mem_peak_bytes(), (long)peak_before);
        sys_free(big, 100u);
    }

    /* ---- the awkward sizes --------------------------------------------- */
    /* A zero-size request still yields a usable pointer, and is accounted as
     * the one byte it really takes rather than as nothing. */
    {
        cu32 live_before = sys_mem_live_bytes();
        void *z = sys_alloc(0u);
        CHECK(z != NULL);
        CHECK_EQI((long)(sys_mem_live_bytes() - live_before), 1);
        sys_free(z, 0u);
    }
    /* Freeing NULL is a no-op, not a crash and not an accounting change. */
    {
        cu32 live_before = sys_mem_live_bytes();
        sys_free(NULL, 1234u);
        CHECK_EQI((long)sys_mem_live_bytes(), (long)live_before);
    }

    /* ---- calloc zeroes, and refuses a product that would wrap ---------- */
    {
        unsigned char *c = (unsigned char *)sys_calloc(1u, 64u);
        int i, nonzero = 0;
        CHECK(c != NULL);
        for (i = 0; i < 64; i++) { if (c[i] != 0) { nonzero++; } }
        CHECK_EQI(nonzero, 0);
        sys_free(c, 64u);
    }
    {
        /*
         * count * size wraps in 32 bits, and a wrapped product used to become
         * a one-byte allocation returned as a non-NULL pointer the caller
         * believed addressed four gigabytes. 65536 * 65536 is exactly 2^32.
         *
         * Every caller in the tree passes a literal 1, so this was latent --
         * but the refusal has to be checked here, because the place it would
         * be discovered otherwise is a corrupted heap.
         */
        cu32 live_before = sys_mem_live_bytes();
        CHECK(sys_calloc(65536u, 65536u) == NULL);
        CHECK(sys_calloc(0xFFFFFFFFUL, 2u) == NULL);
        CHECK(sys_calloc(3u, 0x60000000UL) == NULL);
        /* ...and a refusal costs nothing. */
        CHECK_EQI((long)sys_mem_live_bytes(), (long)live_before);
        /* A product that fits is still served. */
        {
            void *ok = sys_calloc(4u, 16u);
            CHECK(ok != NULL);
            CHECK_EQI((long)(sys_mem_live_bytes() - live_before), 64);
            sys_free(ok, 64u);
        }
        /* Zero of anything is legal and takes the one-byte floor. */
        {
            void *none = sys_calloc(0u, 32u);
            CHECK(none != NULL);
            sys_free(none, 0u);
        }
    }

    /* ---- realloc moves the accounting, it does not add to it ----------- */
    {
        cu32 live_before = sys_mem_live_bytes();
        void *p = sys_alloc(500u);
        CHECK(p != NULL);
        CHECK_EQI((long)(sys_mem_live_bytes() - live_before), 500);
        p = sys_realloc(p, 500u, 900u);
        CHECK(p != NULL);
        CHECK_EQI((long)(sys_mem_live_bytes() - live_before), 900);
        p = sys_realloc(p, 900u, 100u);
        CHECK(p != NULL);
        CHECK_EQI((long)(sys_mem_live_bytes() - live_before), 100);
        sys_free(p, 100u);
        CHECK_EQI((long)sys_mem_live_bytes(), (long)live_before);
        /* Growing from nothing is an allocation, and is accounted as one. */
        p = sys_realloc(NULL, 0u, 300u);
        CHECK(p != NULL);
        CHECK_EQI((long)(sys_mem_live_bytes() - live_before), 300);
        sys_free(p, 300u);
    }

    /* ---- a wrong size on free clamps rather than wrapping -------------- */
    /*
     * Passing the size back is what keeps this allocator header-free, and it
     * puts the burden on the caller. A caller that gets it wrong must leave
     * the counter merely inaccurate, not underflowed to four billion -- which
     * on an unsigned counter is what "live -= too much" produces, and would
     * make the budget check report an impossible number and pass.
     */
    {
        void *p = sys_alloc(10u);
        CHECK(p != NULL);
        sys_free(p, 1000000u);              /* far more than is live */
        CHECK(sys_mem_live_bytes() < 1000000u);
    }
}
