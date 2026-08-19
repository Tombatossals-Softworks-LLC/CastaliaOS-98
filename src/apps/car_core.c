/*
 * car_core.c - The .CAR archive directory (see car_core.h).
 *
 * Reading is where the care goes. Every field that will later become a file
 * path or a memory range is checked here, once, against the actual size of the
 * buffer -- not against another field of the same untrusted header.
 */
#include "castalia/ctypes.h"
#include "castalia/sys.h"
#include "lzss_core.h"
#include "car_core.h"

static void car_put16(unsigned char *p, cu32 v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static cu32 car_get16(const unsigned char *p)
{
    return (cu32)p[0] | ((cu32)p[1] << 8);
}

cbool car_name_ok(const char *name)
{
    int i;
    if (name == NULL || name[0] == '\0') { return CFALSE; }
    /* "." and ".." are directories, not members, and both are how an
     * extractor gets talked into walking upwards. */
    if (name[0] == '.' && (name[1] == '\0' ||
        (name[1] == '.' && name[2] == '\0'))) { return CFALSE; }
    for (i = 0; i < CAR_NAME_MAX; i++) {
        char ch = name[i];
        if (ch == '\0') { return CTRUE; }
        /* A separator or a drive letter turns a member name into a path, and
         * a path is how "extract into this folder" becomes "write anywhere". */
        if (ch == '/' || ch == '\\' || ch == ':') { return CFALSE; }
        if (ch < 0x20 || ch == 0x7F) { return CFALSE; }
    }
    return CFALSE;   /* never terminated within the field */
}

cu32 car_size_for(int count, cu32 payload)
{
    if (count < 0) { count = 0; }
    return (cu32)CAR_HEADER + (cu32)count * (cu32)CAR_ENTRY + payload;
}

cu32 car_write_header(int count, void *dst, cu32 cap)
{
    unsigned char *d = (unsigned char *)dst;
    if (dst == NULL || cap < (cu32)CAR_HEADER) { return 0; }
    if (count < 0 || count > CAR_MAX_FILES) { return 0; }
    d[0] = 'C'; d[1] = 'A'; d[2] = 'R'; d[3] = '1';
    car_put16(d + 4, (cu32)count);
    car_put16(d + 6, 0u);
    return (cu32)CAR_HEADER;
}

cu32 car_write_entry(const CarEntry *e, int index, void *dst, cu32 cap)
{
    unsigned char *d;
    cu32 at;
    int i;
    if (e == NULL || dst == NULL || index < 0 || index >= CAR_MAX_FILES) {
        return 0;
    }
    /* Refuse to WRITE what we would refuse to read: an archive this code
     * cannot open is worse than no archive. */
    if (!car_name_ok(e->name)) { return 0; }
    if (e->method != CZ_STORED && e->method != CZ_LZSS) { return 0; }
    at = (cu32)CAR_HEADER + (cu32)index * (cu32)CAR_ENTRY;
    if (cap < at + (cu32)CAR_ENTRY) { return 0; }
    d = (unsigned char *)dst + at;
    /* car_name_ok has already guaranteed a terminator inside the field. */
    for (i = 0; i < CAR_NAME_MAX && e->name[i] != '\0'; i++) {
        d[i] = (unsigned char)e->name[i];
    }
    for (; i < CAR_NAME_MAX; i++) { d[i] = 0; }
    sys_put_le32(d + 14, e->original);
    sys_put_le32(d + 18, e->stored);
    sys_put_le32(d + 22, e->offset);
    d[26] = (unsigned char)e->method;
    for (i = 27; i < CAR_ENTRY; i++) { d[i] = 0; }
    return (cu32)CAR_ENTRY;
}

int car_read_count(const void *src, cu32 total)
{
    const unsigned char *s = (const unsigned char *)src;
    cu32 count;
    if (src == NULL || total < (cu32)CAR_HEADER) { return -1; }
    if (s[0] != 'C' || s[1] != 'A' || s[2] != 'R' || s[3] != '1') { return -1; }
    count = car_get16(s + 4);
    if (count > (cu32)CAR_MAX_FILES) { return -1; }
    /* The directory itself has to fit in the file. A header claiming two
     * hundred members inside forty bytes is the first thing a damaged or
     * hostile archive says. */
    if (car_size_for((int)count, 0u) > total) { return -1; }
    return (int)count;
}

cbool car_read_entry(const void *src, cu32 total, int index, CarEntry *out)
{
    const unsigned char *s = (const unsigned char *)src;
    const unsigned char *d;
    CarEntry e;
    int count, i;
    if (out == NULL) { return CFALSE; }
    count = car_read_count(src, total);
    if (count < 0 || index < 0 || index >= count) { return CFALSE; }
    d = s + CAR_HEADER + (cu32)index * (cu32)CAR_ENTRY;

    for (i = 0; i < CAR_NAME_MAX; i++) { e.name[i] = (char)d[i]; }
    e.name[CAR_NAME_MAX - 1] = '\0';
    if (!car_name_ok(e.name)) { return CFALSE; }

    e.original = sys_le32(d + 14);
    e.stored   = sys_le32(d + 18);
    e.offset   = sys_le32(d + 22);
    e.method   = (int)d[26];
    if (e.method != CZ_STORED && e.method != CZ_LZSS) { return CFALSE; }
    if (e.method == CZ_STORED && e.stored != e.original) { return CFALSE; }

    /* The member has to lie inside the file, and the addition must not wrap.
     * Checking 'offset + stored <= total' alone is not enough: on a 32-bit
     * total, an offset near the top plus a large length wraps to something
     * small and passes. */
    if (e.offset < (cu32)car_size_for(count, 0u)) { return CFALSE; }
    if (e.offset > total) { return CFALSE; }
    if (e.stored > total - e.offset) { return CFALSE; }

    *out = e;
    return CTRUE;
}
