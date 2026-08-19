/*
 * clip.h - System text clipboard (Layer 2).
 *
 * A single bounded plain-text buffer shared across the whole shell so cut /
 * copy / paste works BETWEEN apps (copy in Notepad, paste into the Run box or a
 * rename field), not just within one control. This is deliberately text-only
 * and fixed-capacity -- no allocation, no format negotiation -- which is all a
 * repairable retro desktop needs and keeps it honest on the target hardware.
 *
 * Portable C89, host-tested (tests/test_clip.c). One process-wide clipboard.
 */
#ifndef CASTALIA_CLIP_H
#define CASTALIA_CLIP_H

#include "castalia/ctypes.h"

#define CLIP_MAX 8192   /* capacity incl. the NUL terminator */

/* Replace the clipboard with 'text'. If 'len' >= 0 it is a byte count (the text
 * need not be NUL-terminated); if 'len' < 0 the text is treated as a C string.
 * Content is copied and clamped to CLIP_MAX-1 bytes; the stored copy is always
 * NUL-terminated. A NULL 'text' clears the clipboard. */
void clip_set_text(const char *text, int len);

/* The current clipboard text, always NUL-terminated (never NULL; "" if empty). */
const char *clip_get_text(void);

/* Length in bytes of the stored text (excluding the terminator). */
int clip_length(void);

/* CTRUE if the clipboard holds a non-empty string. */
cbool clip_has_text(void);

/* Empty the clipboard. */
void clip_clear(void);

#endif /* CASTALIA_CLIP_H */
