/*
 * sys_str.c - Safe, bounded string helpers.
 *
 * The core never uses strcpy/strcat/sprintf. Everything here always
 * NUL-terminates within the destination size and reports truncation, so a
 * bad config value or a long path cannot smash a fixed buffer.
 */
#include "castalia/sys.h"

#include <stdarg.h>
#include <stdio.h>

cu32 sys_strlcpy(char *dst, const char *src, cu32 dstsz)
{
    cu32 n = 0;
    if (dst == NULL || dstsz == 0) {
        /* Still compute source length for the caller's truncation check. */
        cu32 len = 0;
        if (src != NULL) { while (src[len] != '\0') { len++; } }
        return len;
    }
    if (src == NULL) { dst[0] = '\0'; return 0; }

    while (src[n] != '\0' && n + 1 < dstsz) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
    /* Report the full length that *would* have been copied. */
    {
        cu32 full = n;
        while (src[full] != '\0') { full++; }
        return full;
    }
}

cu32 sys_strlcat(char *dst, const char *src, cu32 dstsz)
{
    cu32 dlen = 0;
    if (dst == NULL || dstsz == 0) { return sys_strlcpy(NULL, src, 0); }
    while (dlen < dstsz && dst[dlen] != '\0') { dlen++; }
    if (dlen == dstsz) {
        /* dst was not terminated within the buffer; treat as full. */
        return dstsz + sys_strlcpy(NULL, src, 0);
    }
    return dlen + sys_strlcpy(dst + dlen, src, dstsz - dlen);
}

int sys_snprintf(char *dst, cu32 dstsz, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (dst == NULL || dstsz == 0) { return 0; }
    va_start(ap, fmt);
    /* vsnprintf is C99, but every host and Open Watcom provides it. It
     * always NUL-terminates when dstsz > 0. */
    r = vsnprintf(dst, (size_t)dstsz, fmt, ap);
    va_end(ap);
    if (r < 0) { dst[0] = '\0'; return 0; }
    if ((cu32)r >= dstsz) { return (int)(dstsz - 1); } /* clamped */
    return r;
}

cu32 sys_strnlen(const char *s, cu32 maxlen)
{
    cu32 n = 0;
    if (s == NULL) { return 0; }
    while (n < maxlen && s[n] != '\0') { n++; }
    return n;
}

static int ci_lower(int c)
{
    if (c >= 'A' && c <= 'Z') { return c - 'A' + 'a'; }
    return c;
}

int sys_stricmp(const char *a, const char *b)
{
    if (a == NULL) { a = ""; }
    if (b == NULL) { b = ""; }
    while (*a != '\0' && *b != '\0') {
        int ca = ci_lower((unsigned char)*a);
        int cb = ci_lower((unsigned char)*b);
        if (ca != cb) { return ca - cb; }
        a++; b++;
    }
    return ci_lower((unsigned char)*a) - ci_lower((unsigned char)*b);
}

static cbool is_ws(int c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
            c == '\v');
}

char *sys_strtrim(char *s)
{
    char *start;
    cu32 len;
    if (s == NULL) { return s; }
    start = s;
    while (*start != '\0' && is_ws((unsigned char)*start)) { start++; }
    /* Shift left if we skipped leading whitespace. */
    if (start != s) {
        cu32 i = 0;
        while (start[i] != '\0') { s[i] = start[i]; i++; }
        s[i] = '\0';
    }
    len = 0;
    while (s[len] != '\0') { len++; }
    while (len > 0 && is_ws((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
    return s;
}
