/*
 * test_snd.c - Host unit tests for the portable BLASTER parser
 * (snd_parse_blaster in snd_common.c), the logic the DOS DAC path uses to
 * locate a Sound Blaster before it touches any port.
 */
#include "ctest.h"
#include "castalia/snd.h"

/* snd_common.c (where the parser lives) references the backend's snd_beep for
 * its cues; provide a silent stub so the hermetic test binary links without a
 * platform sound backend. */
void snd_beep(int freq_hz, int ms) { (void)freq_hz; (void)ms; }

void test_snd(void)
{
    SndBlaster b;

    /* Canonical, full string. */
    CHECK(snd_parse_blaster("A220 I5 D1 H5 T6", &b));
    CHECK_EQI(b.base, 0x220);
    CHECK_EQI(b.irq, 5);
    CHECK_EQI(b.dma, 1);
    CHECK_EQI(b.hdma, 5);
    CHECK_EQI(b.type, 6);
    CHECK(b.valid);

    /* Lowercase, reordered, and no high-DMA/type: hdma=-1, type=0. */
    CHECK(snd_parse_blaster("d3 i7 a240", &b));
    CHECK_EQI(b.base, 0x240);
    CHECK_EQI(b.irq, 7);
    CHECK_EQI(b.dma, 3);
    CHECK_EQI(b.hdma, -1);
    CHECK_EQI(b.type, 0);

    /* Hex base with letters (A388), plus an ignored MPU port P330 and E620. */
    CHECK(snd_parse_blaster("A388 I5 D1 P330 T6 E620", &b));
    CHECK_EQI(b.base, 0x388);
    CHECK_EQI(b.dma, 1);

    /* Missing base -> invalid (cannot address the card). */
    CHECK(!snd_parse_blaster("I5 D1", &b));
    CHECK(!b.valid);

    /* Missing DMA -> invalid. */
    CHECK(!snd_parse_blaster("A220 I5", &b));
    CHECK(!b.valid);

    /* NULL and empty -> invalid, fields defaulted. */
    CHECK(!snd_parse_blaster(NULL, &b));
    CHECK_EQI(b.base, 0);
    CHECK_EQI(b.irq, -1);
    CHECK(!snd_parse_blaster("", &b));
    CHECK(!b.valid);

    /* Extra whitespace and tabs are tolerated. */
    CHECK(snd_parse_blaster("  A220\tI5   D1  ", &b));
    CHECK_EQI(b.base, 0x220);
    CHECK_EQI(b.irq, 5);
    CHECK_EQI(b.dma, 1);
}
