/*
 * capp.c - .CAPP container parser, validator, and builder (see capp.h).
 *
 * Portable C89, no allocation: capp_parse validates a caller-supplied image and
 * fills a fixed CappInfo. Defensive throughout -- every offset and length is
 * range-checked against the image before it is trusted, and the whole image is
 * CRC-32 verified -- because a package may come from untrusted media.
 */
#include "castalia/capp.h"
#include "castalia/sys.h"

#include <string.h>

/* ---- CRC-32 (reflected, poly 0xEDB88320) ----------------------------- */

static cu32 crc_update(cu32 crc, const unsigned char *p, cu32 len)
{
    cu32 i;
    int k;
    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
        }
    }
    return crc;
}

cu32 capp_crc32(const void *data, cu32 len)
{
    return crc_update(0xFFFFFFFFUL, (const unsigned char *)data, len)
           ^ 0xFFFFFFFFUL;
}

/* CRC over the whole image with the 4-byte crc field (offset 16..19) taken as
 * zero, so the field is self-consistent without a second pass over a copy. */
static cu32 crc_image(const unsigned char *b, cu32 len)
{
    static const unsigned char zero4[4] = { 0, 0, 0, 0 };
    cu32 c = 0xFFFFFFFFUL;
    c = crc_update(c, b, 16);
    c = crc_update(c, zero4, 4);
    if (len > 20) { c = crc_update(c, b + 20, len - 20); }
    return c ^ 0xFFFFFFFFUL;
}

/* ---- little-endian scalar access ------------------------------------- */

static void read_field(char *dst, const unsigned char *src, int max)
{
    int i;
    for (i = 0; i < max; i++) { dst[i] = (char)src[i]; }
    dst[max - 1] = '\0';
}
static void write_field(unsigned char *dst, const char *src, int max)
{
    int i;
    for (i = 0; i < max; i++) { dst[i] = 0; }
    if (src != NULL) {
        for (i = 0; i < max - 1 && src[i] != '\0'; i++) {
            dst[i] = (unsigned char)src[i];
        }
    }
}

/* ---- parse / validate ------------------------------------------------ */

CResult capp_parse(const void *image, cu32 len, CappInfo *out)
{
    const unsigned char *b = (const unsigned char *)image;
    cu32 tbl_off, tbl_end;
    int i;

    if (image == NULL || out == NULL || len < CAPP_HEADER_SIZE) {
        return CE_INVALID;
    }
    if (memcmp(b, CAPP_MAGIC, 4) != 0) { return CE_INVALID; }

    out->format_version = sys_le16(b + 4);
    if (out->format_version != CAPP_FORMAT_VERSION) { return CE_INVALID; }
    if (sys_le16(b + 6) != CAPP_HEADER_SIZE) { return CE_INVALID; }

    out->abi_version   = sys_le16(b + 8);
    out->flags         = sys_le16(b + 10);
    out->total_size    = sys_le32(b + 12);
    out->crc32         = sys_le32(b + 16);
    out->section_count = (int)sys_le16(b + 20);

    if (out->total_size != len) { return CE_INVALID; }
    if (out->section_count < 0 || out->section_count > CAPP_MAX_SECTIONS) {
        return CE_INVALID;
    }

    read_field(out->name,        b + 24,  CAPP_NAME_MAX);
    read_field(out->version,     b + 56,  CAPP_VERSTR_MAX);
    read_field(out->author,      b + 72,  CAPP_AUTHOR_MAX);
    read_field(out->description, b + 104, CAPP_DESC_MAX);

    tbl_off = CAPP_HEADER_SIZE;
    tbl_end = tbl_off + (cu32)out->section_count * CAPP_SECTION_ENTRY_SIZE;
    if (tbl_end > len) { return CE_OVERFLOW; }

    for (i = 0; i < out->section_count; i++) {
        const unsigned char *e = b + tbl_off + (cu32)i * CAPP_SECTION_ENTRY_SIZE;
        CappSection *s = &out->sections[i];
        s->type   = sys_le16(e);
        s->flags  = sys_le16(e + 2);
        s->offset = sys_le32(e + 4);
        s->size   = sys_le32(e + 8);
        /* Payload must sit after the table and stay within the image; the
         * subtraction form avoids offset+size wrapping around cu32. */
        if (s->offset < tbl_end || s->offset > len) { return CE_OVERFLOW; }
        if (s->size > len - s->offset) { return CE_OVERFLOW; }
    }

    if (crc_image(b, len) != out->crc32) { return CE_FAIL; }
    return CE_OK;
}

cbool capp_find_section(const void *image, const CappInfo *info, int type,
                        const void **out_ptr, cu32 *out_size)
{
    const unsigned char *b = (const unsigned char *)image;
    int i;
    if (image == NULL || info == NULL) { return CFALSE; }
    for (i = 0; i < info->section_count; i++) {
        if (info->sections[i].type == (cu16)type) {
            if (out_ptr)  { *out_ptr  = b + info->sections[i].offset; }
            if (out_size) { *out_size = info->sections[i].size; }
            return CTRUE;
        }
    }
    return CFALSE;
}

/* ---- build ----------------------------------------------------------- */

int capp_build(unsigned char *out, cu32 out_cap, const CappInfo *meta,
               const cu16 *sec_types, const cu16 *sec_flags,
               const void *const *sec_data, const cu32 *sec_sizes, int n)
{
    cu32 tbl_off, pay_off, total, po;
    int i;

    if (out == NULL || meta == NULL || n < 0 || n > CAPP_MAX_SECTIONS) {
        return CE_INVALID;
    }
    if (n > 0 && (sec_types == NULL || sec_data == NULL || sec_sizes == NULL)) {
        return CE_INVALID;
    }

    tbl_off = CAPP_HEADER_SIZE;
    pay_off = tbl_off + (cu32)n * CAPP_SECTION_ENTRY_SIZE;
    total = pay_off;
    for (i = 0; i < n; i++) { total += sec_sizes[i]; }
    if (total > out_cap) { return CE_OVERFLOW; }

    memset(out, 0, (size_t)pay_off);
    memcpy(out, CAPP_MAGIC, 4);
    sys_put_le16(out + 4,  CAPP_FORMAT_VERSION);
    sys_put_le16(out + 6,  CAPP_HEADER_SIZE);
    sys_put_le16(out + 8,  meta->abi_version);
    sys_put_le16(out + 10, meta->flags);
    sys_put_le16(out + 20, (cu16)n);
    write_field(out + 24,  meta->name,        CAPP_NAME_MAX);
    write_field(out + 56,  meta->version,     CAPP_VERSTR_MAX);
    write_field(out + 72,  meta->author,      CAPP_AUTHOR_MAX);
    write_field(out + 104, meta->description, CAPP_DESC_MAX);

    po = pay_off;
    for (i = 0; i < n; i++) {
        unsigned char *e = out + tbl_off + (cu32)i * CAPP_SECTION_ENTRY_SIZE;
        sys_put_le16(e,     sec_types[i]);
        sys_put_le16(e + 2, sec_flags ? sec_flags[i] : 0);
        sys_put_le32(e + 4, po);
        sys_put_le32(e + 8, sec_sizes[i]);
        if (sec_sizes[i] > 0 && sec_data[i] != NULL) {
            memcpy(out + po, sec_data[i], (size_t)sec_sizes[i]);
        }
        po += sec_sizes[i];
    }

    sys_put_le32(out + 12, total);
    sys_put_le32(out + 16, 0);                 /* crc field zero before computing */
    sys_put_le32(out + 16, crc_image(out, total));
    return (int)total;
}
