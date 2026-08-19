/*
 * wav.h - Portable WAV (RIFF/PCM) probing and compact decoding.
 *
 * Used by the Media Player. wav_probe reads just the header (cheap). wav_load_clip
 * streams the PCM data chunk and downmixes + downsamples it into a small, bounded
 * mono buffer -- so a whole song's waveform fits in a few hundred KB and drives
 * the visualizer + seek bar without holding the raw file in memory. Only
 * uncompressed PCM (8-bit unsigned / 16-bit signed, 1-2 channels) is supported;
 * everything else is reported as unsupported rather than mis-decoded.
 *
 * Pure logic over the platform file API (plat_fopen/fread/fclose), so it builds
 * and runs identically on the host and on DOS.
 */
#ifndef CASTALIA_WAV_H
#define CASTALIA_WAV_H

#include "castalia/ctypes.h"

typedef struct {
    cbool valid;
    int   rate;        /* original sample rate (Hz)                */
    int   channels;    /* 1 or 2                                   */
    int   bits;        /* 8 or 16                                  */
    cu32  data_bytes;  /* size of the PCM data chunk               */
    cu32  frames;      /* total sample frames                      */
    cu32  ms;          /* duration in milliseconds                 */
} WavInfo;

/* Read only the header/metadata. Returns CTRUE and fills 'out' for a supported
 * PCM WAV, else CFALSE (out->valid is also set to match). */
cbool wav_probe(const char *path, WavInfo *out);

/* A compact decoded mono clip: signed 8-bit samples at a reduced rate. Owns its
 * sample buffer; release with wav_free. */
typedef struct {
    WavInfo      info;        /* original file metadata                    */
    signed char *mono;        /* decoded mono samples, signed [-128,127]   */
    cu32         count;       /* number of mono samples                    */
    int          rate;        /* reduced sample rate of 'mono' (Hz)        */
    cu32         ms;          /* duration represented by 'mono'            */
    cu32         alloc;       /* accounted allocation size of 'mono'       */
    cbool        truncated;   /* CTRUE if the song exceeded max_samples     */
} WavClip;

/* Load 'path', decoding + downmixing to mono + downsampling to 'target_rate'
 * Hz, capped at 'max_samples' output samples. Returns a heap WavClip (free with
 * wav_free) or NULL on failure / unsupported format. */
WavClip *wav_load_clip(const char *path, int target_rate, cu32 max_samples);
void     wav_free(WavClip *c);

/* Is 'name' a WAV file by extension (case-insensitive ".wav")? */
cbool wav_is_wav_name(const char *name);

#endif /* CASTALIA_WAV_H */
