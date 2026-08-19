/*
 * clip.c - System text clipboard implementation (see clip.h).
 *
 * One fixed buffer, no allocation. Bounded copies only.
 */
#include "castalia/clip.h"

#include <string.h>

static char g_clip[CLIP_MAX];
static int  g_len = 0;

void clip_set_text(const char *text, int len)
{
    int n;
    if (text == NULL) { clip_clear(); return; }
    if (len < 0) { len = (int)strlen(text); }
    n = len;
    if (n > CLIP_MAX - 1) { n = CLIP_MAX - 1; }
    if (n < 0) { n = 0; }
    memcpy(g_clip, text, (size_t)n);
    g_clip[n] = '\0';
    g_len = n;
}

const char *clip_get_text(void) { return g_clip; }

int clip_length(void) { return g_len; }

cbool clip_has_text(void) { return (g_len > 0) ? CTRUE : CFALSE; }

void clip_clear(void)
{
    g_clip[0] = '\0';
    g_len = 0;
}
