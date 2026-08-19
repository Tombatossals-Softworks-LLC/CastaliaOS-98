/*
 * snd_blaster.h - Sound Blaster-class DAC path for the DOS backend.
 *
 * A thin device layer that sits BEHIND snd.h: snd_dos.c calls blaster_detect()
 * at init and, if a card answers, routes tone cues through blaster_play_tone()
 * (8-bit single-cycle DMA PCM) instead of the PC speaker. Falls back silently
 * to the speaker when no card is present.
 *
 * COMPILED ONLY under CASTALIA_DOS. This is real-hardware I/O (DSP reset
 * handshake, 8237 DMA programming) written to the Sound Blaster spec; like
 * vesa.c it cannot run in the portable host build and is validated on
 * emulator/hardware passes (see docs/TESTING.md). The BLASTER string parser it
 * relies on (snd_parse_blaster) is portable and host-tested.
 */
#ifndef CASTALIA_SND_BLASTER_H
#define CASTALIA_SND_BLASTER_H

#ifdef CASTALIA_DOS

#include "castalia/snd.h"

/* Detect a card: parse BLASTER, reset the DSP, confirm the 0xAA handshake, and
 * allocate a low-memory DMA buffer. On success fills *cfg and returns CTRUE;
 * otherwise returns CFALSE and the caller uses the speaker. */
cbool blaster_detect(SndBlaster *cfg);

/* Free the DMA buffer and stop any playback. Safe if never detected. */
void  blaster_shutdown(void);

/* Play a square-wave tone of 'freq_hz' for 'ms' via the DAC. Blocking for the
 * (short) duration, like the speaker path. No-op if no card was detected. */
void  blaster_play_tone(int freq_hz, int ms);

/* The detected configuration string ("A220 I5 D1"), or "" if none. */
const char *blaster_desc(void);

#endif /* CASTALIA_DOS */
#endif /* CASTALIA_SND_BLASTER_H */
