/*
 * capp.h - The .CAPP package container: format, parser, validator, builder.
 *
 * A .CAPP file is CastaliaOS's add-on package: a small, self-describing binary
 * container holding a manifest (name/version/author/description) plus typed
 * sections (code, resources, icon, help). It is designed to be validated
 * cheaply and defensively on a DOS-class machine before anything inside it is
 * trusted -- every offset is bounds-checked and the whole image is CRC-32
 * protected.
 *
 * This header + capp.c are portable C89 and fully host-tested (tests/test_capp
 * builds a package and parses it back, and checks that malformed images are
 * rejected). This file owns only the container format + validation; the plugin
 * execution ABI a code section targets lives in capp_abi.h, and the loader that
 * runs it in capp_loader.h. Format and validation are the deliverable here, so
 * packages can be authored (tools/mkcapp), shipped, discovered, and inspected
 * safely before any code in them runs.
 *
 * On-disk layout (all little-endian):
 *   off  size  field
 *   0    4     magic "CAPP"
 *   4    2     format_version
 *   6    2     header_size (= CAPP_HEADER_SIZE; forward-compat marker)
 *   8    2     abi_version (plugin ABI a code section targets)
 *   10   2     flags
 *   12   4     total_size (whole-file size)
 *   16   4     crc32 (over the whole file with these 4 bytes taken as zero)
 *   20   2     section_count
 *   22   2     reserved
 *   24   32    name        (NUL-padded)
 *   56   16    version     (NUL-padded, e.g. "1.0.0")
 *   72   32    author      (NUL-padded)
 *   104  64    description (NUL-padded)
 *   168  ...   section table: section_count * { u16 type; u16 flags;
 *                                                u32 offset; u32 size }
 *              followed by the section payloads.
 */
#ifndef CASTALIA_CAPP_H
#define CASTALIA_CAPP_H

#include "castalia/ctypes.h"

#define CAPP_MAGIC          "CAPP"
#define CAPP_FORMAT_VERSION 1
#define CAPP_HEADER_SIZE    168
#define CAPP_SECTION_ENTRY_SIZE 12

#define CAPP_NAME_MAX    32
#define CAPP_VERSTR_MAX  16
#define CAPP_AUTHOR_MAX  32
#define CAPP_DESC_MAX    64
#define CAPP_MAX_SECTIONS 16

/* Section types. */
enum {
    CAPP_SEC_CODE     = 1,  /* loadable plugin code (targets capp_abi.h)     */
    CAPP_SEC_RESOURCE = 2,  /* arbitrary resource blob                       */
    CAPP_SEC_ICON     = 3,  /* launcher/desktop icon bitmap                  */
    CAPP_SEC_HELP     = 4   /* help text                                     */
};

/* Package flags (header 'flags'). */
#define CAPP_FLAG_SIGNED 0x0001  /* reserved for a future signature section  */

typedef struct {
    cu16 type;
    cu16 flags;
    cu32 offset;   /* from the start of the image                           */
    cu32 size;
} CappSection;

/* Parsed, validated view of a package (no pointers into the image). */
typedef struct {
    cu16 format_version;
    cu16 abi_version;
    cu16 flags;
    cu32 total_size;
    cu32 crc32;
    int  section_count;
    char name[CAPP_NAME_MAX];
    char version[CAPP_VERSTR_MAX];
    char author[CAPP_AUTHOR_MAX];
    char description[CAPP_DESC_MAX];
    CappSection sections[CAPP_MAX_SECTIONS];
} CappInfo;

/* ---- CRC-32 (reflected, poly 0xEDB88320) ----------------------------- */
cu32 capp_crc32(const void *data, cu32 len);

/* ---- Parse / validate ------------------------------------------------ */
/*
 * Validate a package image of 'len' bytes and fill 'out'. Checks the magic,
 * the format version, header_size, that total_size matches 'len', that the
 * section table and every section [offset,offset+size) fit inside the image
 * (no overflow), and that the CRC-32 matches. Returns:
 *   CE_OK        valid
 *   CE_INVALID   bad magic / unsupported version / bad structure
 *   CE_OVERFLOW  a section or the table runs past the image
 *   CE_FAIL      CRC mismatch (corrupt)
 */
CResult capp_parse(const void *image, cu32 len, CappInfo *out);

/* Locate the first section of 'type'. On success sets *out_ptr into 'image'
 * and *out_size, and returns CTRUE. Returns CFALSE if absent. */
cbool capp_find_section(const void *image, const CappInfo *info, int type,
                        const void **out_ptr, cu32 *out_size);

/* ---- Build (used by tools/mkcapp and the tests) ---------------------- */
/*
 * Assemble a complete package image into 'out' (out_cap bytes). Metadata comes
 * from 'meta' (name/version/author/description/abi_version/flags; its
 * section_count and sections[] are ignored). The 'n' sections are described by
 * parallel arrays: sec_types[i]/sec_flags[i] and the payload sec_data[i] of
 * sec_sizes[i] bytes. Fills in the section table offsets/sizes, stamps
 * total_size and crc32. Returns the total image length, or a negative CResult.
 */
int capp_build(unsigned char *out, cu32 out_cap, const CappInfo *meta,
               const cu16 *sec_types, const cu16 *sec_flags,
               const void *const *sec_data, const cu32 *sec_sizes, int n);

/* ---- Discovery (capp_scan.c) ----------------------------------------- */
/*
 * Enumerate '*.CAPP' files in 'dir' (e.g. C:\CASTALIA\APPS), validate each with
 * capp_parse, log every valid package's manifest and every rejected file, and
 * return the count of valid packages. This NEVER executes package code -- it is
 * safe discovery/inspection only. Uses the platform file API, so it works on
 * both backends. Oversized files (> CAPP_SCAN_MAX_BYTES) are skipped.
 */
#define CAPP_SCAN_MAX_BYTES (1024UL * 1024UL)
int capp_scan_dir(const char *dir);

#endif /* CASTALIA_CAPP_H */
