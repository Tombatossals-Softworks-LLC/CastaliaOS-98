/*
 * test_lzss.c - LZSS compression (lzss_core.c).
 *
 * A compressor has exactly one hard requirement -- what comes out of the
 * decompressor is what went into the compressor, byte for byte, for every
 * input -- and one that matters just as much in practice: a corrupt stream
 * must be refused, not followed. A decompressor that trusts a length field is
 * how an archive format becomes an exploit, so the deliberately-damaged
 * streams below are as much the point of this file as the round trips.
 *
 * The round trips cover the shapes that break real implementations: runs
 * encoded as overlapping matches, data that is entirely incompressible, input
 * of exactly one byte, matches at the very start and the very end, and input
 * long enough to wrap the 4 KB window and exercise the hash chains.
 */
#include "ctest.h"
#include "../src/apps/lzss_core.h"

#include <string.h>

#define BUFMAX 40000

static unsigned char g_in[BUFMAX];
static unsigned char g_cmp[BUFMAX * 2];
static unsigned char g_out[BUFMAX];

/* Compress, decompress, and require the result to be identical. Returns the
 * compressed size so a caller can also assert it actually got smaller. */
static cu32 roundtrip(const unsigned char *data, cu32 n)
{
    cu32 c, d;
    c = lz_compress(data, n, g_cmp, sizeof(g_cmp));
    if (n > 0u && c == 0u) { CHECK(0); return 0u; }
    memset(g_out, 0xAA, sizeof(g_out));
    d = lz_decompress(g_cmp, c, g_out, sizeof(g_out));
    CHECK_EQI((long)d, (long)n);
    if (d == n) { CHECK(memcmp(g_out, data, (size_t)n) == 0); }
    return c;
}

void test_lzss(void)
{
    cu32 i, c;

    printf("- lzss\n");

    /* ---- the easy win: a long run of one byte ------------------------- */
    for (i = 0; i < 4000u; i++) { g_in[i] = 'A'; }
    c = roundtrip(g_in, 4000u);
    CHECK(c < 4000u / 8u);          /* a run must compress hugely          */

    /* A run is encoded as a match that overlaps itself (offset 1). If the
     * decoder used a block move instead of a byte loop this is what breaks. */
    for (i = 0; i < 100u; i++) { g_in[i] = 'Z'; }
    roundtrip(g_in, 100u);

    /* ---- repeating structure, the ordinary case ----------------------- */
    for (i = 0; i < 20000u; i++) {
        static const char *line = "CastaliaOS 98 PE -- the vale of Castalia. ";
        g_in[i] = (unsigned char)line[i % 42u];
    }
    c = roundtrip(g_in, 20000u);
    CHECK(c < 20000u / 4u);

    /* ---- incompressible: must still round trip ------------------------ */
    {
        cu32 rng = 12345u;
        for (i = 0; i < 8000u; i++) {
            rng = rng * 1103515245u + 12345u;
            g_in[i] = (unsigned char)((rng >> 16) & 0xFFu);
        }
        c = roundtrip(g_in, 8000u);
        /* It will grow -- one flag bit per literal -- but only a little, and
         * never past the bound the header promises. */
        CHECK(c > 8000u);
        CHECK(c <= lz_worst_case(8000u));
    }

    /* ---- longer than the window: the hash chains get exercised -------- */
    {
        cu32 rng = 99u;
        for (i = 0; i < 30000u; i++) {
            if ((i % 7u) == 0u) {
                rng = rng * 1103515245u + 12345u;
                g_in[i] = (unsigned char)((rng >> 20) & 0x0Fu);
            } else {
                g_in[i] = (unsigned char)('a' + (i % 26u));
            }
        }
        roundtrip(g_in, 30000u);
    }

    /* ---- the small and awkward sizes ---------------------------------- */
    for (i = 0; i < 64u; i++) { g_in[i] = (unsigned char)i; }
    roundtrip(g_in, 1u);
    roundtrip(g_in, 2u);
    roundtrip(g_in, 3u);
    roundtrip(g_in, 8u);
    roundtrip(g_in, 9u);
    roundtrip(g_in, 64u);

    /* A match right at the end, and one right at the earliest legal spot. */
    memcpy(g_in, "abcabc", 6u);
    roundtrip(g_in, 6u);
    memcpy(g_in, "xxabcdefabcdef", 14u);
    roundtrip(g_in, 14u);

    /* Exactly the longest match the format can encode, and one byte more. */
    for (i = 0; i < 64u; i++) { g_in[i] = (unsigned char)('a' + (i % 18u)); }
    roundtrip(g_in, 36u);
    roundtrip(g_in, 37u);

    /* ---- empty input is a valid, empty stream ------------------------- */
    CHECK_EQI((long)lz_compress(g_in, 0u, g_cmp, sizeof(g_cmp)), 0);
    CHECK_EQI((long)lz_decompress(g_cmp, 0u, g_out, sizeof(g_out)), 0);

    /* ---- refusals ----------------------------------------------------- */
    CHECK_EQI((long)lz_compress(NULL, 10u, g_cmp, sizeof(g_cmp)), 0);
    CHECK_EQI((long)lz_compress(g_in, 10u, NULL, 10u), 0);
    CHECK_EQI((long)lz_decompress(NULL, 10u, g_out, sizeof(g_out)), 0);
    CHECK_EQI((long)lz_decompress(g_cmp, 10u, NULL, 10u), 0);

    /* Too small an output buffer fails rather than truncating. */
    for (i = 0; i < 2000u; i++) { g_in[i] = (unsigned char)(i * 7u); }
    CHECK_EQI((long)lz_compress(g_in, 2000u, g_cmp, 4u), 0);

    /* ---- corrupt streams must be refused, never followed -------------- */
    for (i = 0; i < 3000u; i++) { g_in[i] = (unsigned char)('a' + (i % 11u)); }
    c = lz_compress(g_in, 3000u, g_cmp, sizeof(g_cmp));
    CHECK(c > 0u);

    /* Truncated mid-stream: decode what it can, but never read past the end
     * and never claim more than it produced. */
    {
        cu32 got = lz_decompress(g_cmp, c / 2u, g_out, sizeof(g_out));
        CHECK(got <= 3000u);
    }
    /* Truncated to a single flag byte promising items that are not there. */
    {
        unsigned char one[1];
        one[0] = 0x00u;                     /* eight matches, none present  */
        CHECK_EQI((long)lz_decompress(one, 1u, g_out, sizeof(g_out)), 0);
    }
    /* A match pointing back before the start of the output. */
    {
        unsigned char bad[3];
        bad[0] = 0x00u;                     /* first item is a match        */
        bad[1] = 0xFFu;                     /* offset low                   */
        bad[2] = 0xF0u;                     /* offset high, length code 0   */
        CHECK_EQI((long)lz_decompress(bad, 3u, g_out, sizeof(g_out)), 0);
    }
    /* A valid stream decoded into a buffer too small to hold it. */
    CHECK_EQI((long)lz_decompress(g_cmp, c, g_out, 16u), 0);

    /* Random damage anywhere in a valid stream must never crash, never write
     * past the buffer, and never report more than the buffer holds. */
    {
        cu32 rng = 4242u, t;
        for (t = 0; t < 400u; t++) {
            cu32 at, got;
            unsigned char save;
            rng = rng * 1103515245u + 12345u;
            at = (rng >> 8) % c;
            save = g_cmp[at];
            rng = rng * 1103515245u + 12345u;
            g_cmp[at] = (unsigned char)((rng >> 16) & 0xFFu);
            got = lz_decompress(g_cmp, c, g_out, sizeof(g_out));
            CHECK(got <= sizeof(g_out));
            g_cmp[at] = save;
        }
    }

    /* ---- the size bound holds ----------------------------------------- */
    CHECK_EQI((long)lz_worst_case(0u), 1);
    CHECK_EQI((long)lz_worst_case(8u), 8 + 1 + 1);
    CHECK(lz_worst_case(1000u) >= 1000u);

    /* ---- the .CZ container -------------------------------------------- */
    {
        CzHeader h, r;
        unsigned char buf[64];
        memset(&h, 0, sizeof(h));
        h.original = 123456u;
        h.stored   = 45678u;
        h.method   = CZ_LZSS;
        memcpy(h.name, "NOTES.TXT", 10u);
        CHECK_EQI((long)cz_write_header(&h, buf, sizeof(buf)), CZ_HEADER);
        CHECK(cz_read_header(buf, CZ_HEADER + 45678u, &r));
        CHECK_EQI((long)r.original, 123456);
        CHECK_EQI((long)r.stored, 45678);
        CHECK_EQI(r.method, CZ_LZSS);
        CHECK_STR(r.name, "NOTES.TXT");

        /* A header claiming more data than the file holds is a truncated or
         * doctored file, and must be refused rather than trusted. */
        CHECK(!cz_read_header(buf, CZ_HEADER + 45677u, &r));
        CHECK(cz_read_header(buf, CZ_HEADER + 45679u, &r));   /* trailing ok */

        /* Bad magic, bad method. */
        buf[1] = 'X';
        CHECK(!cz_read_header(buf, sizeof(buf) + 45678u, &r));
        buf[1] = 'Z';
        buf[12] = 9;
        CHECK(!cz_read_header(buf, sizeof(buf) + 45678u, &r));
        buf[12] = CZ_STORED;
        /* Stored means the two sizes must agree; this one says otherwise. */
        CHECK(!cz_read_header(buf, CZ_HEADER + 45678u, &r));

        /* Too short to hold a header at all. */
        CHECK(!cz_read_header(buf, 4u, &r));
        CHECK(!cz_read_header(NULL, 100u, &r));
        CHECK(!cz_read_header(buf, 100u, NULL));
        CHECK_EQI((long)cz_write_header(&h, buf, 4u), 0);
        CHECK_EQI((long)cz_write_header(NULL, buf, sizeof(buf)), 0);
    }

    /* The archive name: 8.3 means NOTES.TXT cannot become NOTES.TXT.CZ. */
    {
        char nm[16];
        CHECK(cz_archive_name("NOTES.TXT", nm, sizeof(nm)));
        CHECK_STR(nm, "NOTES.CZ");
        CHECK(cz_archive_name("VERYLONGNAME.DOC", nm, sizeof(nm)));
        CHECK_STR(nm, "VERYLONG.CZ");
        CHECK(cz_archive_name("README", nm, sizeof(nm)));
        CHECK_STR(nm, "README.CZ");
        CHECK(!cz_archive_name(".HIDDEN", nm, sizeof(nm)));   /* no stem     */
        CHECK(!cz_archive_name("NOTES.TXT", nm, 8u));
        CHECK(!cz_archive_name(NULL, nm, sizeof(nm)));
        CHECK(!cz_archive_name("NOTES.TXT", NULL, sizeof(nm)));
    }

    /* ---- a full container round trip, the way the app does it --------- */
    {
        CzHeader h, r;
        static unsigned char file[BUFMAX];
        cu32 body, total, back;
        for (i = 0; i < 12000u; i++) {
            g_in[i] = (unsigned char)('A' + (i % 13u) + ((i / 500u) % 3u));
        }
        body = lz_compress(g_in, 12000u, file + CZ_HEADER,
                           sizeof(file) - CZ_HEADER);
        CHECK(body > 0u && body < 12000u);
        memset(&h, 0, sizeof(h));
        h.original = 12000u;
        h.stored = body;
        h.method = CZ_LZSS;
        memcpy(h.name, "BIG.DAT", 8u);
        cz_write_header(&h, file, sizeof(file));
        total = CZ_HEADER + body;

        CHECK(cz_read_header(file, total, &r));
        CHECK_EQI((long)r.original, 12000);
        CHECK_STR(r.name, "BIG.DAT");
        back = lz_decompress(file + CZ_HEADER, r.stored, g_out, sizeof(g_out));
        CHECK_EQI((long)back, 12000);
        CHECK(memcmp(g_out, g_in, 12000u) == 0);
    }
}
