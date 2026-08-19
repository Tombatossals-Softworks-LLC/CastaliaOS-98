/*
 * wav.c - Portable WAV probing + compact mono decode (see wav.h).
 */
#include "wav.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

/* ---- little-endian readers ------------------------------------------- */
/* Read exactly 'n' bytes; CTRUE only if all were read. */
static cbool read_exact(PlatFile *f, void *buf, cu32 n)
{
    return (plat_fread(f, buf, n) == n) ? CTRUE : CFALSE;
}

/* Discard 'n' bytes from the stream (no seek API on DOS -> read + drop). */
static cbool skip_bytes(PlatFile *f, cu32 n)
{
    unsigned char scratch[512];
    while (n > 0) {
        cu32 want = (n > sizeof(scratch)) ? (cu32)sizeof(scratch) : n;
        cu32 got = plat_fread(f, scratch, want);
        if (got == 0) { return CFALSE; }
        n -= got;
    }
    return CTRUE;
}

/* Duration in ms for 'frames' at 'rate', overflow-safe (no 64-bit needed). */
static cu32 frames_to_ms(cu32 frames, int rate)
{
    if (rate <= 0) { return 0; }
    return (frames / (cu32)rate) * 1000u +
           ((frames % (cu32)rate) * 1000u) / (cu32)rate;
}

/*
 * Open 'path', scan RIFF chunks, and stop at the data chunk. Fills 'info' from
 * the fmt chunk and the data size. On success returns the open file positioned
 * at the first PCM byte (caller reads/closes it); on failure closes the file and
 * returns NULL. 'info->valid' reflects whether the format is supported.
 */
static PlatFile *open_and_parse(const char *path, WavInfo *info)
{
    PlatFile *f;
    unsigned char hdr[12];
    cbool have_fmt = CFALSE;

    memset(info, 0, sizeof(*info));
    if (path == NULL || path[0] == '\0') { return NULL; }
    f = plat_fopen(path, "rb");
    if (f == NULL) { return NULL; }

    if (!read_exact(f, hdr, 12) ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        plat_fclose(f);
        return NULL;
    }

    for (;;) {
        unsigned char ch[8];
        cu32 csz;
        if (!read_exact(f, ch, 8)) { break; }
        csz = sys_le32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            cu32 take = (csz < 16) ? csz : 16;
            if (!read_exact(f, fmt, take)) { break; }
            if (take >= 16) {
                cu16 afmt = sys_le16(fmt + 0);
                info->channels = (int)sys_le16(fmt + 2);
                info->rate     = (int)sys_le32(fmt + 4);
                info->bits     = (int)sys_le16(fmt + 14);
                /* 1 = PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE (PCM subformat). */
                if ((afmt == 1 || afmt == 0xFFFE) &&
                    (info->channels == 1 || info->channels == 2) &&
                    (info->bits == 8 || info->bits == 16) &&
                    info->rate > 0) {
                    have_fmt = CTRUE;
                }
            }
            if (csz > take) {
                if (!skip_bytes(f, csz - take)) { break; }
            }
            if (csz & 1u) { skip_bytes(f, 1); } /* word-align padding */
        } else if (memcmp(ch, "data", 4) == 0) {
            info->data_bytes = csz;
            if (have_fmt) {
                int fsz = info->channels * (info->bits / 8);
                if (fsz > 0) {
                    info->frames = csz / (cu32)fsz;
                    info->ms = frames_to_ms(info->frames, info->rate);
                    info->valid = CTRUE;
                    return f; /* positioned at first PCM byte */
                }
            }
            break; /* data reached but fmt unsupported/missing */
        } else {
            if (!skip_bytes(f, csz)) { break; }
            if (csz & 1u) { skip_bytes(f, 1); }
        }
    }
    plat_fclose(f);
    return NULL;
}

cbool wav_probe(const char *path, WavInfo *out)
{
    WavInfo info;
    PlatFile *f = open_and_parse(path, &info);
    if (f != NULL) { plat_fclose(f); }
    if (out != NULL) { *out = info; }
    return info.valid;
}

/* Convert one interleaved frame at 'p' to a mono int in ~[-128,127]. */
static int frame_to_mono(const unsigned char *p, int channels, int bits)
{
    if (bits == 8) {
        int a = (int)p[0] - 128;
        if (channels == 1) { return a; }
        return (a + ((int)p[1] - 128)) / 2;
    } else {
        int a = (int)(short)sys_le16(p) >> 8;
        if (channels == 1) { return a; }
        return (a + ((int)(short)sys_le16(p + 2) >> 8)) / 2;
    }
}

WavClip *wav_load_clip(const char *path, int target_rate, cu32 max_samples)
{
    WavInfo info;
    PlatFile *f;
    WavClip *clip;
    signed char *mono;
    int fsz, out_rate;
    cu32 remaining, out_count = 0;
    long sum = 0;
    int cnt = 0, acc = 0;
    cbool truncated = CFALSE;
    unsigned char blk[4096];

    f = open_and_parse(path, &info);
    if (f == NULL) { return NULL; }

    fsz = info.channels * (info.bits / 8);
    out_rate = target_rate;
    if (out_rate < 1) { out_rate = 1; }
    if (out_rate > info.rate) { out_rate = info.rate; }
    if (max_samples < 1) { max_samples = 1; }

    mono = (signed char *)sys_alloc(max_samples);
    if (mono == NULL) { plat_fclose(f); return NULL; }

    remaining = info.data_bytes;
    while (remaining > 0 && out_count < max_samples) {
        cu32 want = (cu32)(sizeof(blk) - (sizeof(blk) % (cu32)fsz));
        cu32 got, off;
        if (want > remaining) { want = remaining; }
        got = plat_fread(f, blk, want);
        if (got == 0) { break; }
        remaining -= got;
        for (off = 0; off + (cu32)fsz <= got; off += (cu32)fsz) {
            int m = frame_to_mono(blk + off, info.channels, info.bits);
            sum += m;
            cnt++;
            acc += out_rate;
            if (acc >= info.rate) {
                int avg;
                acc -= info.rate;
                avg = (cnt > 0) ? (int)(sum / cnt) : 0;
                if (avg > 127) { avg = 127; }
                if (avg < -128) { avg = -128; }
                mono[out_count++] = (signed char)avg;
                sum = 0; cnt = 0;
                if (out_count >= max_samples) { truncated = CTRUE; break; }
            }
        }
    }
    if (remaining > 0 && out_count >= max_samples) { truncated = CTRUE; }
    plat_fclose(f);

    clip = (WavClip *)sys_calloc(1, (cu32)sizeof(WavClip));
    if (clip == NULL) { sys_free(mono, max_samples); return NULL; }
    clip->info = info;
    clip->mono = mono;
    clip->count = out_count;
    clip->rate = out_rate;
    clip->ms = truncated ? frames_to_ms(out_count, out_rate) : info.ms;
    clip->alloc = max_samples;   /* accounted size for a correct wav_free */
    clip->truncated = truncated;
    return clip;
}

void wav_free(WavClip *c)
{
    if (c == NULL) { return; }
    if (c->mono != NULL) {
        sys_free(c->mono, c->alloc);
    }
    sys_free(c, (cu32)sizeof(WavClip));
}

cbool wav_is_wav_name(const char *name)
{
    cu32 n;
    if (name == NULL) { return CFALSE; }
    n = sys_strnlen(name, 260);
    if (n < 4) { return CFALSE; }
    return (name[n - 4] == '.' &&
            (name[n - 3] == 'w' || name[n - 3] == 'W') &&
            (name[n - 2] == 'a' || name[n - 2] == 'A') &&
            (name[n - 1] == 'v' || name[n - 1] == 'V')) ? CTRUE : CFALSE;
}
