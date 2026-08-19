/*
 * test_fuzz.c - The parsers, fed things no encoder would ever write.
 *
 * This system reads four formats it wrote itself -- .CAR archives, .CZ
 * compressed files, .CAPP add-on packages, and its own BMPs -- and each is a
 * hand-rolled parser reading lengths and offsets out of a byte stream. That
 * is the shape where a truncated file, or a length field claiming more than
 * the file holds, walks off the end of a buffer. The abuse harness covers one
 * such case per format, by hand.
 *
 * This covers thousands, deterministically: take something valid, damage it
 * every way a damaged file is actually damaged -- truncated anywhere, a byte
 * flipped anywhere, a length field replaced with something enormous, the
 * whole thing replaced with noise -- and require the parser to REFUSE rather
 * than crash, over-read, or report a success it cannot back up.
 *
 * The generator is seeded, so a failure here is reproducible rather than a
 * story about a run that once went wrong.
 *
 * Worth stating what this can and cannot see. Run normally it catches
 * crashes, hangs, and answers that contradict themselves. It does NOT catch
 * a read one byte past a buffer that happens to land on readable memory --
 * for that it has to run under valgrind, which `make memcheck` does, since
 * that target runs this very binary.
 */
#include "ctest.h"
#include "../src/apps/lzss_core.h"
#include "../src/apps/car_core.h"
#include "castalia/capp.h"
#include "castalia/sys.h"
#include <string.h>

/* A tiny deterministic generator, so every run damages the same bytes. */
static cu32 g_fz = 20260818u;
static cu32 fz_next(void)
{
    g_fz = g_fz * 1103515245u + 12345u;
    return (g_fz >> 8) & 0xFFFFFFu;
}
/* A capacity the decompressed output really reaches. */
#define FZ_CAP 64u

static int fz_pick(int n) { return (n > 0) ? (int)(fz_next() % (cu32)n) : 0; }

/* Damage 'buf' in place, one of the ways a real file is damaged. */
static cu32 fz_damage(unsigned char *buf, cu32 n)
{
    int how = fz_pick(5);
    if (n == 0) { return 0; }
    switch (how) {
    case 0:                                   /* truncate anywhere         */
        return (cu32)fz_pick((int)n);
    case 1:                                   /* flip one byte             */
        buf[fz_pick((int)n)] ^= (unsigned char)(1u << fz_pick(8));
        return n;
    case 2:                                   /* smash a byte outright     */
        buf[fz_pick((int)n)] = (unsigned char)fz_pick(256);
        return n;
    case 3: {                                 /* an enormous length field  */
        int at = fz_pick((int)n);
        cu32 k;
        for (k = 0; k < 4u && (cu32)at + k < n; k++) {
            buf[at + k] = 0xFFu;
        }
        return n;
    }
    default: {                                /* noise, keeping the size   */
        cu32 k;
        for (k = 0; k < n; k++) { buf[k] = (unsigned char)fz_pick(256); }
        return n;
    }
    }
}

void test_fuzz(void)
{
    int round;
    int lz_bad = 0, car_bad = 0, capp_bad = 0, capp_ok = 0;

    printf("- parsers on damaged input\n");

    /* ---- the LZSS decompressor ------------------------------------------
     *
     * It is handed a byte stream and a capacity. The requirement is simply
     * that it never writes more than the capacity it was given, whatever the
     * stream says -- a decompressor that trusts its input is how a small
     * file becomes an overwritten stack.
     */
    for (round = 0; round < 400; round++) {
        unsigned char src[256], packed[512], out[256];
        cu32 n, plen, got, i;
        n = (cu32)(1 + fz_pick(200));
        for (i = 0; i < n; i++) {
            /* Repetitive data, so the compressor produces real back-references
             * rather than a stream of literals. */
            src[i] = (unsigned char)((i / 3) % 7);
        }
        plen = lz_compress(src, n, packed, (cu32)sizeof packed);
        if (plen == 0) { continue; }
        plen = fz_damage(packed, plen);
        /*
         * The capacity is deliberately SMALLER than the data compressed --
         * 64 bytes against up to 200 -- because the overrun this is looking
         * for only happens when the output actually reaches the limit.
         *
         * The first version of this passed a capacity larger than any
         * possible output, so `out` never came within eighteen bytes of it,
         * the guard was never approached, and deleting that guard entirely
         * failed nothing. Measured before and after: with a capacity of 255
         * the mutation causes 0 overruns, and with 64 it causes 142.
         */
        memset(out, 0xA5, sizeof out);
        got = lz_decompress(packed, plen, out, FZ_CAP);
        if (got > FZ_CAP) { lz_bad++; }
        /* Nothing at all beyond the capacity may have been touched. */
        for (i = FZ_CAP; i < (cu32)sizeof out; i++) {
            if (out[i] != 0xA5u) { lz_bad++; break; }
        }
    }
    CHECK_EQI(lz_bad, 0);

    /* ---- the .CAR archive directory --------------------------------------
     *
     * car_read_count and car_read_entry both take the total size, so the
     * question is whether they honour it. An entry index inside a damaged
     * header must not produce a name read from past the end.
     */
    for (round = 0; round < 400; round++) {
        unsigned char arc[512];
        cu32 total;
        int count, k;
        CarEntry e;
        memset(arc, 0, sizeof arc);
        if (car_write_header(3, arc, (cu32)sizeof arc) == 0) { continue; }
        {
            CarEntry w;
            memset(&w, 0, sizeof w);
            sys_strlcpy(w.name, "FILE.TXT", sizeof w.name);
            w.original = 10; w.stored = 10; w.offset = 128; w.method = 0;
            for (k = 0; k < 3; k++) {
                (void)car_write_entry(&w, k, arc, (cu32)sizeof arc);
            }
        }
        /*
         * The archive's size is header + entries, which is what car_size_for
         * exists to say. car_write_entry returns the size of ONE entry, and
         * taking the largest of those gave 32 where 104 was needed -- so
         * car_read_count refused every archive before it was even damaged and
         * this whole loop tested nothing. It was found by counting how many
         * damaged archives got past the count, seeing zero, and checking the
         * UNDAMAGED case.
         */
        total = car_size_for(3, 0);
        if (total > (cu32)sizeof arc) { continue; }
        total = fz_damage(arc, total);
        count = car_read_count(arc, total);
        /* A count is either a refusal or a number the file could hold. */
        if (count > 0) {
            cu32 need = car_size_for(count, 0);
            if (need > (cu32)sizeof arc * 64u) { car_bad++; }
            for (k = 0; k < count && k < 8; k++) {
                if (car_read_entry(arc, total, k, &e)) {
                    /* A name that came back must be terminated inside its
                     * own field, not run on into whatever follows. */
                    if (e.name[sizeof e.name - 1] != '\0') { car_bad++; }
                }
            }
        }
    }
    CHECK_EQI(car_bad, 0);

    /* ---- .CAPP packages ---------------------------------------------------
     *
     * capp_parse documents four distinct refusals -- bad magic, bad
     * structure, a section running past the image, a CRC mismatch. What must
     * never happen is CE_OK on an image that cannot support it.
     *
     * Read the second loop below before believing this one proves much. The
     * CRC covers the whole image and is checked LAST, so every structural
     * test in capp_parse does run on damaged bytes here -- which is worth
     * having under valgrind -- but the verdict is always a refusal, and the
     * assertions inside the `== CE_OK` arm below are unreachable. Counted:
     * 0 of 400 accepted. That is the same flaw the LZ and CAR loops had, and
     * finding it a third time is why the count is now measured rather than
     * assumed.
     */
    for (round = 0; round < 400; round++) {
        static unsigned char pkg[1024];
        CappInfo meta, got;
        cu16 stypes[1], sflags[1];
        const void *sdata[1];
        cu32 ssizes[1];
        static const unsigned char code[] = { 'C','B','L','T','h','i','\0' };
        int plen;

        memset(&meta, 0, sizeof meta);
        meta.abi_version = 1;
        sys_strlcpy(meta.name, "Fuzz", sizeof meta.name);
        sys_strlcpy(meta.version, "1.0.0", sizeof meta.version);
        sys_strlcpy(meta.author, "T", sizeof meta.author);
        sys_strlcpy(meta.description, "d", sizeof meta.description);
        stypes[0] = CAPP_SEC_CODE; sflags[0] = 0;
        sdata[0] = code; ssizes[0] = (cu32)sizeof code;
        plen = capp_build(pkg, (cu32)sizeof pkg, &meta, stypes, sflags,
                          sdata, ssizes, 1);
        if (plen <= 0) { continue; }
        {
            cu32 n = fz_damage(pkg, (cu32)plen);
            memset(&got, 0, sizeof got);
            if (capp_parse(pkg, n, &got) == CE_OK) {
                /* Accepted: then every string it reports must be terminated
                 * and its section table must fit inside what it was given. */
                if (got.name[sizeof got.name - 1] != '\0') { capp_bad++; }
                if (got.section_count < 0) { capp_bad++; }
            }
        }
    }
    CHECK_EQI(capp_bad, 0);

    /* ---- .CAPP packages that pass their own CRC ---------------------------
     *
     * The loop above never reaches CE_OK, so it never checks what capp_parse
     * REPORTS -- only that it refuses. A checksum is not a validator, though:
     * it proves an image is the one that was written, not that the thing
     * written made sense. Anyone who can damage a package can recompute its
     * CRC, and then every structural check in the parser is the only thing
     * standing between a lying header and a read past the end.
     *
     * So: damage, repair total_size if the damage truncated, recompute the
     * CRC exactly as capp_build does (the field itself taken as zero), and
     * hand THAT to the parser. Now images are accepted, and an accepted image
     * makes a promise -- every section it reports lies inside the bytes it was
     * given -- which is checked here by fetching each section and touching its
     * first and last byte. Under valgrind that turns "the parser says it is in
     * bounds" into "it is in bounds", which is the only version worth having.
     */
    for (round = 0; round < 400; round++) {
        static unsigned char pkg[1024];
        CappInfo meta, got;
        cu16 stypes[2], sflags[2];
        const void *sdata[2];
        cu32 ssizes[2];
        static const unsigned char code[] = { 'C','B','L','T','h','i','\0' };
        static const unsigned char data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        int plen, i;
        cu32 n;

        memset(&meta, 0, sizeof meta);
        meta.abi_version = 1;
        sys_strlcpy(meta.name, "Fuzz", sizeof meta.name);
        sys_strlcpy(meta.version, "1.0.0", sizeof meta.version);
        sys_strlcpy(meta.author, "T", sizeof meta.author);
        sys_strlcpy(meta.description, "d", sizeof meta.description);
        stypes[0] = CAPP_SEC_CODE; sflags[0] = 0;
        sdata[0] = code; ssizes[0] = (cu32)sizeof code;
        stypes[1] = CAPP_SEC_RESOURCE; sflags[1] = 0;
        sdata[1] = data; ssizes[1] = (cu32)sizeof data;
        plen = capp_build(pkg, (cu32)sizeof pkg, &meta, stypes, sflags,
                          sdata, ssizes, 2);
        if (plen <= 0) { continue; }

        n = fz_damage(pkg, (cu32)plen);
        if (n < 24u) { continue; }        /* below a header, nothing to fix  */
        /* Truncation moves the end, so make the length field agree -- else
         * the image is refused at the total_size test and we are back to
         * proving nothing. */
        sys_put_le32(pkg + 12, n);
        /* The CRC as capp_build computes it: over the whole image with its
         * own four bytes read as zero. */
        sys_put_le32(pkg + 16, 0);
        sys_put_le32(pkg + 16, capp_crc32(pkg, n));

        memset(&got, 0, sizeof got);
        if (capp_parse(pkg, n, &got) != CE_OK) { continue; }
        capp_ok++;

        /* Every string it hands back is terminated inside its own field. */
        if (got.name[sizeof got.name - 1] != '\0') { capp_bad++; }
        if (got.version[sizeof got.version - 1] != '\0') { capp_bad++; }
        if (got.author[sizeof got.author - 1] != '\0') { capp_bad++; }
        if (got.description[sizeof got.description - 1] != '\0') { capp_bad++; }
        if (got.section_count < 0 ||
            got.section_count > CAPP_MAX_SECTIONS) { capp_bad++; }

        for (i = 0; i < got.section_count; i++) {
            const void *p = NULL;
            cu32 sz = 0;
            if (!capp_find_section(pkg, &got, (int)got.sections[i].type,
                                   &p, &sz)) {
                continue;
            }
            /* Arithmetic first: the span it names must fit the image. */
            if (got.sections[i].offset > n ||
                sz > n - got.sections[i].offset) { capp_bad++; continue; }
            /* Then actually touch both ends of it, because an offset that
             * merely LOOKS in range and one that IS are different claims and
             * only the read tells them apart. */
            if (sz > 0u) {
                const unsigned char *q = (const unsigned char *)p;
                volatile unsigned char t;
                t = q[0];
                t = q[sz - 1u];
                (void)t;
            }
        }
    }
    /* The loop above is worthless if nothing gets through it -- which is
     * exactly how the LZ and CAR loops managed to test nothing -- so the
     * acceptance count is itself a check. */
    CHECK(capp_ok > 50);
    CHECK_EQI(capp_bad, 0);

    /* A zero-length and a one-byte image, which every parser sees eventually
     * because a file can always be empty. */
    {
        unsigned char tiny[4];
        CappInfo info;
        CarEntry e;
        unsigned char out[16];
        tiny[0] = 0;
        CHECK(capp_parse(tiny, 0, &info) != CE_OK);
        CHECK(capp_parse(tiny, 1, &info) != CE_OK);
        CHECK(capp_parse(NULL, 10, &info) != CE_OK);
        CHECK(car_read_count(tiny, 0) <= 0);
        CHECK(!car_read_entry(tiny, 0, 0, &e));
        CHECK_EQI((int)lz_decompress(tiny, 0, out, (cu32)sizeof out), 0);
    }
}
