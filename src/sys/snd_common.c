/*
 * snd_common.c - Portable parts of the sound subsystem.
 *
 * Owns the enable flag and the named UI cues (startup chime, error buzz),
 * expressed as sequences of snd_beep() so they sound the same however the
 * backend renders a tone. The actual tone generation + device bring-up live
 * in the per-platform snd_host.c / snd_dos.c.
 */
#include "castalia/snd.h"

static cbool g_enabled = CTRUE;

void snd_set_enabled(cbool enabled)
{
    g_enabled = enabled;
    if (!enabled) { snd_beep(0, 0); } /* ensure the speaker is silenced */
}

cbool snd_enabled(void) { return g_enabled; }

/* A brief rising four-note chime. */
void snd_startup(void)
{
    snd_beep(587, 60);
    snd_beep(784, 60);
    snd_beep(988, 70);
    snd_beep(1319, 120);
}

/* A short low buzz for errors / the crash boundary. */
void snd_error(void)
{
    snd_beep(220, 180);
}

/* A soft, very short blip when a menu opens. */
void snd_menu(void)
{
    snd_beep(1245, 22);
}

/* A light rising two-note when a window opens. */
void snd_open(void)
{
    snd_beep(784, 34);
    snd_beep(1047, 40);
}

/* A light falling two-note when a window closes. */
void snd_close(void)
{
    snd_beep(1047, 30);
    snd_beep(740, 40);
}

/* A tiny tick for a button press / commit. */
void snd_click(void)
{
    snd_beep(1600, 12);
}

/* ---------------------------------------------------------------------- */
/* BLASTER environment parsing (pure; host-tested in tests/test_snd.c)    */
/* ---------------------------------------------------------------------- */

static int snd_upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* Read a run of digits in 'base' (10 or 16) from *pp, advancing it. Returns
 * the value, or -1 if no digit was present. */
static int snd_parse_num(const char **pp, int base)
{
    const char *p = *pp;
    long v = 0;
    int got = 0;
    for (;;) {
        int c = (unsigned char)*p;
        int d;
        if (c >= '0' && c <= '9')                 { d = c - '0'; }
        else if (base == 16 && c >= 'a' && c <= 'f') { d = c - 'a' + 10; }
        else if (base == 16 && c >= 'A' && c <= 'F') { d = c - 'A' + 10; }
        else { break; }
        v = v * base + d;
        got = 1;
        p++;
    }
    *pp = p;
    return got ? (int)v : -1;
}

cbool snd_parse_blaster(const char *env, SndBlaster *out)
{
    const char *p;
    if (out == NULL) { return CFALSE; }
    out->base = 0;
    out->irq  = -1;
    out->dma  = -1;
    out->hdma = -1;
    out->type = 0;
    out->valid = CFALSE;
    if (env == NULL) { return CFALSE; }

    for (p = env; *p != '\0'; ) {
        int key, val, base;
        if (*p == ' ' || *p == '\t') { p++; continue; }
        key = snd_upper((unsigned char)*p);
        p++;
        /* A/P/E carry hex port addresses; I/D/H/T are decimal. */
        base = (key == 'A' || key == 'P' || key == 'E') ? 16 : 10;
        val = snd_parse_num(&p, base);
        switch (key) {
        case 'A': if (val >= 0) { out->base = val; } break;
        case 'I': if (val >= 0) { out->irq  = val; } break;
        case 'D': if (val >= 0) { out->dma  = val; } break;
        case 'H': if (val >= 0) { out->hdma = val; } break;
        case 'T': if (val >= 0) { out->type = val; } break;
        default:
            /* Unknown key: skip its value and any trailing non-space junk. */
            while (*p != '\0' && *p != ' ' && *p != '\t') { p++; }
            break;
        }
    }
    out->valid = (out->base > 0 && out->irq >= 0 && out->dma >= 0) ? CTRUE
                                                                   : CFALSE;
    return out->valid;
}
