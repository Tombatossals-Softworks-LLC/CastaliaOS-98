/*
 * snd_host.c - Null sound backend for the host build.
 *
 * There is no audio device in CI, and none is wanted: this backend reports
 * "unavailable" and turns every beep into a debug log line, so the whole
 * stack builds and runs silently. snd_available() returning CFALSE is the
 * signal to the rest of the system to run without sound.
 */
#include "castalia/snd.h"
#include "castalia/sys.h"

CResult snd_init(void)
{
    SYS_LOGI("snd", "host null sound backend (no audio device)");
    return CE_OK;
}

void snd_shutdown(void) { }

cbool snd_available(void) { return CFALSE; }

void snd_beep(int freq_hz, int ms)
{
    if (!snd_enabled() || freq_hz <= 0) { return; }
    SYS_LOGD("snd", "beep %d Hz for %d ms (null backend, silent)", freq_hz, ms);
}

CResult snd_pcm_play(const void *pcm, cu32 bytes, int rate, int channels, int bits)
{
    CASTALIA_UNUSED(pcm);
    SYS_LOGD("snd", "pcm play %lu bytes @ %d Hz x%d %d-bit (null backend, silent)",
             (unsigned long)bytes, rate, channels, bits);
    return CE_OK;   /* silent success: the player still animates + times out */
}

void  snd_pcm_stop(void) { }
cbool snd_pcm_busy(void) { return CFALSE; }

SndDevice   snd_device(void)      { return SND_DEV_NONE; }
const char *snd_device_name(void) { return "None (null audio)"; }
