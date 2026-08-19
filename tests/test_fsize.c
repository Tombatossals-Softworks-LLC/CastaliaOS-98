/*
 * test_fsize.c - Totalling file sizes (fsize_core.c).
 *
 * The section that matters is "past two gigabytes". Everything above it is
 * ordinary formatting; that section is a folder whose files come to more than
 * a 32-bit long holds, which is the case the File Manager got wrong on the
 * product target and right on every machine a test could run on.
 *
 * It is reproducible here for exactly one reason: the total is a cs32, which
 * is 32 bits on the host as well as on DOS. Written as a `long` -- as it was
 * -- this file would pass while the shipped binary printed a negative number,
 * which is the failure mode that has now produced five separate bugs in this
 * tree and is invisible to every host test by construction.
 */
#include "ctest.h"
#include "../src/apps/fsize_core.h"

void test_fsize(void)
{
    FsizeSum s;
    char buf[64];

    printf("- file size totals\n");

    /* ---- an empty folder ------------------------------------------------ */
    fsize_begin(&s);
    CHECK_EQI(s.files, 0);
    CHECK(!s.over);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "No files here");

    /* ---- small totals stay exact ---------------------------------------- */
    fsize_begin(&s);
    fsize_add(&s, 100);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "100 bytes in 1 file");
    fsize_add(&s, 23);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "123 bytes in 2 files");

    /* The boundary between "say it exactly" and "round it". */
    fsize_begin(&s);
    fsize_add(&s, 10239L);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "10239 bytes in 1 file");
    fsize_begin(&s);
    fsize_add(&s, 10240L);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "10 KB in 1 file");

    /* ---- sizes that do not count ---------------------------------------- */
    /* plat_file_size returns -1 for "unknown", and an unknown file must not
     * become a file in the count nor a subtraction from the total. */
    fsize_begin(&s);
    fsize_add(&s, 500);
    fsize_add(&s, -1);
    fsize_add(&s, 0);
    CHECK_EQI(s.files, 1);
    CHECK_EQI((int)s.total, 500);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "500 bytes in 1 file");

    /* ---- KB and MB labels ------------------------------------------------ */
    fsize_begin(&s);
    fsize_add(&s, 1024L * 1024L);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "1.0 MB in 1 file");
    fsize_begin(&s);
    fsize_add(&s, 1536L * 1024L);           /* 1.5 MB */
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "1.5 MB in 1 file");

    /* ---- past two gigabytes ---------------------------------------------- *
     *
     * Three files of 800 MB. On the 32-bit target the old code summed these
     * into a `long` and wrapped to a negative number; the status line then
     * took the `bytes < 10240` branch -- which a negative number passes --
     * and printed a NEGATIVE byte count for a folder holding 2.4 GB.
     *
     * Saturating instead: the total stops at what it can hold and the label
     * says "more than", which is true, rather than a number that is false in
     * either direction.
     */
    {
        const long eight_hundred_mb = 800L * 1024L * 1024L;
        fsize_begin(&s);
        fsize_add(&s, eight_hundred_mb);
        CHECK(!s.over);
        fsize_add(&s, eight_hundred_mb);
        CHECK(!s.over);                      /* 1.6 GB still fits */
        CHECK((long)s.total > 0);
        fsize_add(&s, eight_hundred_mb);     /* 2.4 GB does not */
        CHECK(s.over);
        CHECK_EQI(s.files, 3);
        /* The property that actually broke: the total never goes negative. */
        CHECK((long)s.total > 0);
        CHECK_EQI((long)s.total, FSIZE_MAX);
        fsize_label(&s, buf, sizeof buf);
        CHECK_STR(buf, "more than 2047.9 MB in 3 files");
    }

    /* A single file larger than the total can hold saturates on its own. */
    fsize_begin(&s);
    fsize_add(&s, FSIZE_MAX);
    CHECK(!s.over);
    CHECK_EQI((long)s.total, FSIZE_MAX);
    fsize_begin(&s);
    fsize_add(&s, FSIZE_MAX);
    fsize_add(&s, 1);                        /* one byte over the edge */
    CHECK(s.over);
    CHECK((long)s.total > 0);
    CHECK_EQI((long)s.total, FSIZE_MAX);

    /* Once saturated it stays saturated and stays positive, however many
     * more arrive -- a second wrap would be a positive number again, which is
     * the version of this bug that looks plausible instead of obviously
     * wrong. */
    {
        int i;
        for (i = 0; i < 20; i++) { fsize_add(&s, 1024L * 1024L * 1024L); }
        CHECK(s.over);
        CHECK((long)s.total > 0);
        CHECK_EQI((long)s.total, FSIZE_MAX);
        CHECK_EQI(s.files, 22);
    }

    /* A saturated total is never printed as though it were exact. */
    fsize_begin(&s);
    fsize_add(&s, FSIZE_MAX);
    fsize_add(&s, FSIZE_MAX);
    fsize_label(&s, buf, sizeof buf);
    CHECK_STR(buf, "more than 2047.9 MB in 2 files");

    /* ---- refusals -------------------------------------------------------- */
    fsize_begin(NULL);                       /* no crash */
    fsize_add(NULL, 100);
    fsize_label(NULL, buf, sizeof buf);
    CHECK_STR(buf, "No files here");
    fsize_begin(&s);
    fsize_add(&s, 100);
    fsize_label(&s, NULL, 64);               /* no crash */
    fsize_label(&s, buf, 0u);                /* no write at all */
}
