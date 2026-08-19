/*
 * snd.h - Layer 1 optional sound interface (Castalia-owned).
 *
 * Sound is OPTIONAL and must never be required (Project Bible): the shell
 * works identically with it absent. Backends implement this seam:
 *   - src/platform/host/snd_host.c  a null driver (logs, no audio) so CI and
 *     the host build never depend on an audio device.
 *   - src/platform/dos/snd_dos.c    the PC speaker (8253 timer 2 + port 61h),
 *     which every PC of the target era has -- no Sound Blaster needed.
 *   - src/platform/dos/snd_blaster.c a Sound Blaster-class DAC path that slots
 *     in BEHIND this same interface: snd_dos.c prefers it when a card is
 *     detected (from the BLASTER environment variable + a DSP reset probe) and
 *     transparently falls back to the speaker otherwise. The UI cues
 *     (snd_startup/snd_error) upgrade to the DAC automatically.
 *
 * Nothing above this layer programs a timer, DMA channel, or I/O port.
 */
#ifndef CASTALIA_SND_H
#define CASTALIA_SND_H

#include "castalia/ctypes.h"

/* Bring the sound backend up. Returns CE_OK, or CE_UNSUPPORTED if no device
 * is available -- callers must treat that as "run silently", not an error. */
CResult snd_init(void);

/* Tear down (silences the speaker). Symmetric with snd_init. */
void    snd_shutdown(void);

/* Is a working output available? (CFALSE on the null driver.) */
cbool   snd_available(void);

/* Enable/disable sound at runtime (the Control Center / INI can mute it). */
void    snd_set_enabled(cbool enabled);
cbool   snd_enabled(void);

/* Play a single square-wave tone of 'freq_hz' for 'ms' milliseconds. Blocking
 * but short; a no-op when muted or unavailable. freq_hz <= 0 is silence. */
void    snd_beep(int freq_hz, int ms);

/* Named UI cues, mapped to short tones by the backend. All are short and are
 * no-ops when muted/unavailable; on the DOS PC speaker they fire only at
 * discrete events (never per-frame) so they never stutter the UI. */
void    snd_startup(void);  /* a brief rising chime at shell start           */
void    snd_error(void);    /* a low buzz for errors / the crash boundary     */
void    snd_menu(void);     /* a soft blip when a menu opens                  */
void    snd_open(void);     /* a light rising two-note when a window opens     */
void    snd_close(void);    /* a light falling two-note when a window closes   */
void    snd_click(void);    /* a tiny tick for a button/commit action          */

/* ---------------------------------------------------------------------- */
/* PCM playback (optional, best-effort -- the Media Player uses it)        */
/* ---------------------------------------------------------------------- */
/*
 * Start playing an interleaved PCM buffer. The caller keeps 'pcm' valid until
 * playback ends (snd_pcm_busy() -> CFALSE) or snd_pcm_stop() is called. This is
 * deliberately best-effort: the host/null backend is a silent no-op that
 * returns CE_OK (the Media Player still animates from the sample data and the
 * wall clock), a Sound Blaster DAC can render it on DOS, and the PC-speaker
 * backend returns CE_UNSUPPORTED. 'bits' is 8 or 16; 'channels' 1 or 2.
 */
CResult snd_pcm_play(const void *pcm, cu32 bytes, int rate, int channels, int bits);
/* Stop any PCM playback started by snd_pcm_play (safe if none is active). */
void    snd_pcm_stop(void);
/* Is PCM audio still playing? (Always CFALSE on the null/speaker backend.) */
cbool   snd_pcm_busy(void);

/* ---------------------------------------------------------------------- */
/* Device reporting (for System Info / diagnostics)                       */
/* ---------------------------------------------------------------------- */
typedef enum {
    SND_DEV_NONE = 0,   /* muted / null backend -- no output device       */
    SND_DEV_SPEAKER,    /* PC speaker (8253 timer 2)                       */
    SND_DEV_DAC         /* Sound Blaster-class DAC (DMA PCM)               */
} SndDevice;

/* Which output the backend actually brought up. Implemented per backend. */
SndDevice   snd_device(void);
/* Human-readable device string, e.g. "Sound Blaster (A220 I5 D1)" or
 * "PC speaker". Never NULL. Implemented per backend. */
const char *snd_device_name(void);

/* ---------------------------------------------------------------------- */
/* Sound Blaster configuration parsing (portable, host-testable)          */
/* ---------------------------------------------------------------------- */
/*
 * The DOS DAC path locates the card from the standard BLASTER environment
 * variable, e.g. "BLASTER=A220 I5 D1 H5 T6":
 *   A = I/O base port (hex)   I = IRQ (dec)   D = 8-bit DMA channel (dec)
 *   H = 16-bit DMA (dec)      T = card type (dec); P/E are ignored.
 * The parser is pure logic with no hardware access, so it is unit-tested on
 * the host and reused by snd_blaster.c on DOS.
 */
typedef struct {
    int   base;   /* I/O base port, e.g. 0x220 (0 if absent)               */
    int   irq;    /* IRQ number      (-1 if absent)                        */
    int   dma;    /* 8-bit DMA channel (-1 if absent)                      */
    int   hdma;   /* 16-bit DMA channel (-1 if absent)                     */
    int   type;   /* card type from Tx (0 if absent)                       */
    cbool valid;  /* CTRUE iff base, irq and dma were all present          */
} SndBlaster;

/* Parse a BLASTER-style string into 'out'. Case-insensitive, order-free.
 * A NULL/empty/base-less string yields out->valid == CFALSE. Returns
 * out->valid. */
cbool snd_parse_blaster(const char *env, SndBlaster *out);

#endif /* CASTALIA_SND_H */
