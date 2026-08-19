/*
 * cz_file.c - Compressing and restoring a file on disk (see cz_file.h).
 *
 * Every buffer here is bounded and every read is checked against what the
 * header claims, because the input is a file on disk and a file on disk is
 * whatever somebody put there. The header having said "12000 bytes" is not
 * evidence that 12000 bytes follow it.
 */
#include "castalia/ctypes.h"
#include "castalia/ui.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "lzss_core.h"
#include "car_core.h"
#include "cz_file.h"

#include <string.h>

/* Read a whole file into a freshly allocated buffer. The caller frees it with
 * sys_free and the size this reports. */
CResult cz_slurp_public(const char *path, unsigned char **out, cu32 *out_n)
{
    PlatFile *f;
    long size;
    unsigned char *buf;
    cu32 n, got;

    *out = NULL;
    *out_n = 0;
    size = plat_file_size(path);
    if (size < 0) { return CE_NOTFOUND; }
    if ((cu32)size > CZ_FILE_MAX) { return CE_OVERFLOW; }
    n = (cu32)size;
    buf = (unsigned char *)sys_alloc(n > 0u ? n : 1u);
    if (buf == NULL) { return CE_NOMEM; }
    f = plat_fopen(path, "rb");
    if (f == NULL) { sys_free(buf, n > 0u ? n : 1u); return CE_NOTFOUND; }
    got = (n > 0u) ? plat_fread(f, buf, n) : 0u;
    plat_fclose(f);
    if (got != n) { sys_free(buf, n > 0u ? n : 1u); return CE_IO; }
    *out = buf;
    *out_n = n;
    return CE_OK;
}

CResult cz_spit_public(const char *path, const void *data, cu32 n)
{
    PlatFile *f = plat_fopen(path, "wb");
    cu32 put;
    if (f == NULL) { return CE_IO; }
    put = (n > 0u) ? plat_fwrite(f, data, n) : 0u;
    if (plat_fclose(f) != CE_OK || put != n) { return CE_IO; }
    return CE_OK;
}

CResult cz_compress_file(const char *src_path, const char *dst_path,
                         long *out_saved)
{
    unsigned char *in = NULL, *out = NULL;
    cu32 n = 0, cap, body, total;
    CzHeader h;
    CResult rc;

    if (src_path == NULL || dst_path == NULL) { return CE_INVALID; }
    rc = cz_slurp_public(src_path, &in, &n);
    if (rc != CE_OK) { return rc; }

    cap = (cu32)CZ_HEADER + lz_worst_case(n);
    out = (unsigned char *)sys_alloc(cap);
    if (out == NULL) { sys_free(in, n > 0u ? n : 1u); return CE_NOMEM; }

    body = lz_compress(in, n, out + CZ_HEADER, cap - (cu32)CZ_HEADER);
    memset(&h, 0, sizeof(h));
    h.original = n;
    sys_strlcpy(h.name, ui_path_base(src_path), sizeof(h.name));
    /* Compression that grew the file is stored instead. Writing a "compressed"
     * copy larger than the original would be a lie the size column tells. */
    if (n == 0u || body == 0u || body >= n) {
        h.method = CZ_STORED;
        h.stored = n;
        if (n > 0u) { memcpy(out + CZ_HEADER, in, (size_t)n); }
    } else {
        h.method = CZ_LZSS;
        h.stored = body;
    }
    cz_write_header(&h, out, cap);
    total = (cu32)CZ_HEADER + h.stored;

    rc = cz_spit_public(dst_path, out, total);
    if (rc == CE_OK && out_saved != NULL) {
        *out_saved = (long)n - (long)total;
    }
    sys_free(in, n > 0u ? n : 1u);
    sys_free(out, cap);
    return rc;
}

CResult cz_decompress_file(const char *src_path, const char *dst_dir,
                           char *out_name, cu32 namesz)
{
    unsigned char *in = NULL, *out = NULL;
    cu32 n = 0, got;
    CzHeader h;
    CResult rc;
    char path[CASTALIA_MAX_PATH];

    if (src_path == NULL || dst_dir == NULL) { return CE_INVALID; }
    rc = cz_slurp_public(src_path, &in, &n);
    if (rc != CE_OK) { return rc; }

    if (!cz_read_header(in, n, &h)) {
        sys_free(in, n > 0u ? n : 1u);
        return CE_INVALID;
    }
    if (h.original > CZ_FILE_MAX) {
        sys_free(in, n > 0u ? n : 1u);
        return CE_OVERFLOW;
    }
    if (h.name[0] == '\0') {
        sys_free(in, n > 0u ? n : 1u);
        return CE_INVALID;
    }

    out = (unsigned char *)sys_alloc(h.original > 0u ? h.original : 1u);
    if (out == NULL) { sys_free(in, n > 0u ? n : 1u); return CE_NOMEM; }

    if (h.method == CZ_STORED) {
        if (h.original > 0u) { memcpy(out, in + CZ_HEADER, (size_t)h.original); }
        got = h.original;
    } else {
        got = lz_decompress(in + CZ_HEADER, h.stored, out, h.original);
    }
    /* The header said how big it would be. If the stream disagrees, the file
     * is damaged, and a partial restore under the original name is the worst
     * possible outcome -- it looks like the file. */
    if (got != h.original) {
        sys_free(in, n > 0u ? n : 1u);
        sys_free(out, h.original > 0u ? h.original : 1u);
        return CE_INVALID;
    }

    sys_snprintf(path, sizeof(path), "%s/%s", dst_dir, h.name);
    rc = cz_spit_public(path, out, got);
    if (rc == CE_OK && out_name != NULL) {
        sys_strlcpy(out_name, h.name, namesz);
    }
    sys_free(in, n > 0u ? n : 1u);
    sys_free(out, h.original > 0u ? h.original : 1u);
    return rc;
}

cbool cz_member_name(const char *path, char *out, cu32 outsz)
{
    unsigned char head[CZ_HEADER];
    PlatFile *f;
    CzHeader h;
    long size;
    cu32 got;

    if (out == NULL || outsz == 0u) { return CFALSE; }
    out[0] = '\0';
    if (path == NULL) { return CFALSE; }
    size = plat_file_size(path);
    if (size < (long)CZ_HEADER) { return CFALSE; }
    f = plat_fopen(path, "rb");
    if (f == NULL) { return CFALSE; }
    got = plat_fread(f, head, (cu32)CZ_HEADER);
    plat_fclose(f);
    if (got != (cu32)CZ_HEADER) { return CFALSE; }
    /*
     * The whole file's size, not the 28 bytes just read: cz_read_header's job
     * is partly to check that the header describes a file this big, and
     * handing it 28 would fail every archive that has a body.
     */
    if (!cz_read_header(head, (cu32)size, &h)) { return CFALSE; }
    if (h.name[0] == '\0') { return CFALSE; }
    sys_strlcpy(out, h.name, outsz);
    return CTRUE;
}

/* ---- .CAR: a folder in one file --------------------------------------- */

CResult car_pack_dir(const char *dir, const char *archive_path,
                     int *out_files, long *out_saved)
{
    PlatDir *d;
    PlatDirEntry de;
    PlatFile *out;
    CarEntry ent[CAR_MAX_FILES];
    static unsigned char hdr[CAR_HEADER + CAR_MAX_FILES * CAR_ENTRY];
    unsigned char *in = NULL, *packed = NULL;
    cu32 at, n = 0, cap;
    int count = 0, i;
    long raw_total = 0;
    CResult rc = CE_OK;

    if (dir == NULL || archive_path == NULL) { return CE_INVALID; }
    d = plat_opendir(dir);
    if (d == NULL) { return CE_NOTFOUND; }

    /* First pass: decide the membership, so the directory can be written
     * before the data and the archive stays readable with two seeks. */
    while (plat_readdir(d, &de) && count < CAR_MAX_FILES) {
        if (de.is_dir) { continue; }
        if (!car_name_ok(de.name)) { continue; }
        if (de.size < 0 || (cu32)de.size > CZ_FILE_MAX) { continue; }
        memset(&ent[count], 0, sizeof(ent[count]));
        sys_strlcpy(ent[count].name, de.name, sizeof(ent[count].name));
        ent[count].original = (cu32)de.size;
        count++;
    }
    plat_closedir(d);
    if (count == 0) { return CE_NOTFOUND; }

    out = plat_fopen(archive_path, "wb");
    if (out == NULL) { return CE_IO; }
    /* Reserve the directory; it is rewritten once the offsets are known. */
    memset(hdr, 0, sizeof(hdr));
    at = car_size_for(count, 0u);
    if (plat_fwrite(out, hdr, at) != at) { plat_fclose(out); return CE_IO; }

    for (i = 0; i < count; i++) {
        char path[CASTALIA_MAX_PATH];
        cu32 body;
        sys_snprintf(path, sizeof(path), "%s/%s", dir, ent[i].name);
        if (cz_slurp_public(path, &in, &n) != CE_OK) { rc = CE_IO; break; }
        cap = lz_worst_case(n);
        packed = (unsigned char *)sys_alloc(cap > 0u ? cap : 1u);
        if (packed == NULL) { sys_free(in, n > 0u ? n : 1u); rc = CE_NOMEM; break; }
        body = lz_compress(in, n, packed, cap);
        ent[i].offset = at;
        if (n == 0u || body == 0u || body >= n) {
            /* Storing beats a "compressed" copy that grew. */
            ent[i].method = CZ_STORED;
            ent[i].stored = n;
            if (n > 0u && plat_fwrite(out, in, n) != n) { rc = CE_IO; }
        } else {
            ent[i].method = CZ_LZSS;
            ent[i].stored = body;
            if (plat_fwrite(out, packed, body) != body) { rc = CE_IO; }
        }
        at += ent[i].stored;
        raw_total += (long)n;
        sys_free(in, n > 0u ? n : 1u);
        sys_free(packed, cap > 0u ? cap : 1u);
        in = NULL; packed = NULL;
        if (rc != CE_OK) { break; }
    }
    if (plat_fclose(out) != CE_OK) { rc = CE_IO; }
    if (rc != CE_OK) { plat_file_remove(archive_path); return rc; }

    /* Rewrite the directory now that every offset is known. */
    car_write_header(count, hdr, sizeof(hdr));
    for (i = 0; i < count; i++) {
        if (car_write_entry(&ent[i], i, hdr, sizeof(hdr)) == 0u) {
            plat_file_remove(archive_path);
            return CE_INVALID;
        }
    }
    {
        PlatFile *fix = plat_fopen(archive_path, "r+b");
        cu32 dirlen = car_size_for(count, 0u);
        if (fix == NULL) { plat_file_remove(archive_path); return CE_IO; }
        if (plat_fwrite(fix, hdr, dirlen) != dirlen) {
            plat_fclose(fix); plat_file_remove(archive_path); return CE_IO;
        }
        if (plat_fclose(fix) != CE_OK) { return CE_IO; }
    }

    if (out_files != NULL) { *out_files = count; }
    if (out_saved != NULL) {
        *out_saved = raw_total - (long)plat_file_size(archive_path);
    }
    return CE_OK;
}

int car_list(const char *archive_path, CarEntry *out, int max)
{
    /* Header plus the whole fixed-size directory: the most a .CAR can have in
     * front of its data, and small enough to sit on the stack. */
    static unsigned char dirbuf[CAR_HEADER + CAR_MAX_FILES * CAR_ENTRY];
    PlatFile *f;
    long fsize;
    cu32 want, got;
    int n, i, kept = 0;

    if (archive_path == NULL || out == NULL || max < 1) { return -1; }
    fsize = plat_file_size(archive_path);
    if (fsize < (long)CAR_HEADER) { return -1; }
    want = (cu32)sizeof(dirbuf);
    if ((cu32)fsize < want) { want = (cu32)fsize; }
    f = plat_fopen(archive_path, "rb");
    if (f == NULL) { return -1; }
    got = plat_fread(f, dirbuf, want);
    plat_fclose(f);
    if (got != want) { return -1; }

    n = car_read_count(dirbuf, (cu32)fsize);
    if (n < 0) { return -1; }
    /* The directory has to lie inside what was actually read, or the entries
     * below would be parsed out of bytes nobody wrote. */
    if (car_size_for(n, 0u) > got) { return -1; }
    for (i = 0; i < n && kept < max; i++) {
        /* Validated against the REAL file size even though only the front of
         * it is in memory: car_read_entry reads inside the records and uses
         * the total purely to range-check what it finds there. */
        if (car_read_entry(dirbuf, (cu32)fsize, i, &out[kept])) { kept++; }
    }
    return kept;
}

int car_clashes(const char *archive_path, const char *dir)
{
    static CarEntry ent[CAR_MAX_FILES];
    char probe[CASTALIA_MAX_PATH];
    int n, i, hits = 0;
    if (dir == NULL) { return -1; }
    n = car_list(archive_path, ent, CAR_MAX_FILES);
    if (n < 0) { return -1; }
    for (i = 0; i < n; i++) {
        sys_snprintf(probe, sizeof(probe), "%s/%s", dir, ent[i].name);
        if (plat_file_exists(probe)) { hits++; }
    }
    return hits;
}

CResult car_unpack(const char *archive_path, const char *dir, int *out_files)
{
    unsigned char *in = NULL, *out = NULL;
    cu32 n = 0;
    int count, i, written = 0;
    CResult rc;

    if (archive_path == NULL || dir == NULL) { return CE_INVALID; }
    rc = cz_slurp_public(archive_path, &in, &n);
    if (rc != CE_OK) { return rc; }

    count = car_read_count(in, n);
    if (count < 0) { sys_free(in, n > 0u ? n : 1u); return CE_INVALID; }

    for (i = 0; i < count; i++) {
        CarEntry e;
        char path[CASTALIA_MAX_PATH];
        cu32 got;
        /* Every member is validated against the real file size before any of
         * it is treated as an offset or a name. */
        if (!car_read_entry(in, n, i, &e)) { rc = CE_INVALID; break; }
        if (e.original > CZ_FILE_MAX) { rc = CE_OVERFLOW; break; }
        out = (unsigned char *)sys_alloc(e.original > 0u ? e.original : 1u);
        if (out == NULL) { rc = CE_NOMEM; break; }
        if (e.method == CZ_STORED) {
            if (e.original > 0u) { memcpy(out, in + e.offset, (size_t)e.original); }
            got = e.original;
        } else {
            got = lz_decompress(in + e.offset, e.stored, out, e.original);
        }
        if (got != e.original) {
            sys_free(out, e.original > 0u ? e.original : 1u);
            rc = CE_INVALID;
            break;
        }
        sys_snprintf(path, sizeof(path), "%s/%s", dir, e.name);
        rc = cz_spit_public(path, out, got);
        sys_free(out, e.original > 0u ? e.original : 1u);
        out = NULL;
        if (rc != CE_OK) { break; }
        written++;
    }
    sys_free(in, n > 0u ? n : 1u);
    if (out_files != NULL) { *out_files = written; }
    return rc;
}
