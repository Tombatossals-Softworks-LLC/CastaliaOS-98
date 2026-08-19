/*
 * snd_dos.c - Sound backend for DOS (Open Watcom): PC speaker + optional DAC.
 *
 * The always-available path uses the 8253/8254 channel-2 timer and port 61h --
 * present on every PC of the target era, so sound works with no Sound Blaster
 * and no driver. At init we ALSO probe for a Sound Blaster-class DAC
 * (snd_blaster.c); when one answers, tone cues render through it instead, and
 * snd_device() reports SND_DEV_DAC. Either way sound stays optional and
 * snd_beep() is blocking only for the (short) cue duration, never in the frame
 * loop. Compiled only under CASTALIA_DOS.
 */
#ifdef CASTALIA_DOS

#include "castalia/snd.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "snd_blaster.h"

#include <conio.h> /* inp / outp */

#define PIT_FREQ   1193180L
#define PORT_PIT_CMD  0x43
#define PORT_PIT_CH2  0x42
#define PORT_SPEAKER  0x61

static cbool g_dac = CFALSE;   /* CTRUE once a Sound Blaster DAC is up */

static void speaker_on(int freq_hz)
{
    unsigned int divisor;
    unsigned char v;
    if (freq_hz < 20)   { freq_hz = 20; }
    if (freq_hz > 20000){ freq_hz = 20000; }
    divisor = (unsigned int)(PIT_FREQ / freq_hz);
    /* Channel 2, access lo/hi, mode 3 (square wave). */
    outp(PORT_PIT_CMD, 0xB6);
    outp(PORT_PIT_CH2, (int)(divisor & 0xFF));
    outp(PORT_PIT_CH2, (int)((divisor >> 8) & 0xFF));
    /* Gate the timer to the speaker (bits 0 and 1). */
    v = (unsigned char)inp(PORT_SPEAKER);
    outp(PORT_SPEAKER, (int)(v | 0x03));
}

static void speaker_off(void)
{
    unsigned char v = (unsigned char)inp(PORT_SPEAKER);
    outp(PORT_SPEAKER, (int)(v & 0xFC));
}

CResult snd_init(void)
{
    speaker_off();
    g_dac = blaster_detect(NULL);   /* falls back to the speaker if absent */
    return CE_OK;
}

void snd_shutdown(void)
{
    speaker_off();
    if (g_dac) { blaster_shutdown(); g_dac = CFALSE; }
}

cbool snd_available(void) { return CTRUE; }

void snd_beep(int freq_hz, int ms)
{
    if (!snd_enabled()) { speaker_off(); return; }
    if (freq_hz <= 0) { speaker_off(); return; }
    if (g_dac) {
        blaster_play_tone(freq_hz, ms > 0 ? ms : 1);
        return;
    }
    speaker_on(freq_hz);
    plat_sleep_ms((cu32)(ms > 0 ? ms : 1));
    speaker_off();
}

SndDevice snd_device(void)
{
    return g_dac ? SND_DEV_DAC : SND_DEV_SPEAKER;
}

const char *snd_device_name(void)
{
    static char name[48];
    if (g_dac) {
        sys_snprintf(name, sizeof(name), "Sound Blaster (%s)", blaster_desc());
        return name;
    }
    return "PC speaker";
}

/* PCM streaming is a documented next step for the Sound Blaster DMA path; the
 * Media Player is fully usable without it (it animates from the sample data and
 * the wall clock). Report best-effort "unsupported" so callers stay silent. */
CResult snd_pcm_play(const void *pcm, cu32 bytes, int rate, int channels, int bits)
{
    CASTALIA_UNUSED(pcm); CASTALIA_UNUSED(bytes); CASTALIA_UNUSED(rate);
    CASTALIA_UNUSED(channels); CASTALIA_UNUSED(bits);
    return CE_UNSUPPORTED;
}

void  snd_pcm_stop(void) { }
cbool snd_pcm_busy(void) { return CFALSE; }

#endif /* CASTALIA_DOS */
