/*
 * test_clip.c - Host unit tests for the system text clipboard (clip.c).
 */
#include "ctest.h"
#include "castalia/clip.h"

#include <string.h>

void test_clip(void)
{
    int i;
    static char big[CLIP_MAX + 100];

    /* Empty to start after clear. */
    clip_clear();
    CHECK(!clip_has_text());
    CHECK_EQI(clip_length(), 0);
    CHECK_STR(clip_get_text(), "");

    /* C-string set (len < 0). */
    clip_set_text("hello", -1);
    CHECK(clip_has_text());
    CHECK_EQI(clip_length(), 5);
    CHECK_STR(clip_get_text(), "hello");

    /* Explicit length that stops before a NUL-less region. */
    clip_set_text("abcdef", 3);
    CHECK_EQI(clip_length(), 3);
    CHECK_STR(clip_get_text(), "abc");

    /* NULL clears. */
    clip_set_text(NULL, 5);
    CHECK(!clip_has_text());
    CHECK_STR(clip_get_text(), "");

    /* Oversized input is clamped and still NUL-terminated. */
    for (i = 0; i < (int)sizeof(big); i++) { big[i] = 'A'; }
    clip_set_text(big, (int)sizeof(big));
    CHECK_EQI(clip_length(), CLIP_MAX - 1);
    CHECK_EQI((int)strlen(clip_get_text()), CLIP_MAX - 1);

    clip_clear();
}
