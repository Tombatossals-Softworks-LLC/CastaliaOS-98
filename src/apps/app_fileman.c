/*
 * app_fileman.c - CastaliaOS File Manager (single-pane explorer).
 *
 * Phase 2 flagship app. Browses the filesystem through the platform directory
 * API (plat_opendir/readdir), so it works identically on the host backend and
 * on DOS. Features:
 *   - Details listing (name / type / size), directories first, sorted.
 *   - Navigation: double-click or Enter to open a folder, ".." / Backspace to
 *     go up.
 *   - Keyboard: Up/Down to move the selection, Enter to open, Delete to trash.
 *   - Toolbar: Up, Refresh, New Folder.
 *   - New Folder (plat_mkdir) and safe delete (move to CASTALIA_HOME\TRASH,
 *     which is recoverable -- the Bible's "safe delete to trash" rule).
 *
 * The window owns a heap payload and frees it on WM_MSG_DESTROY. Text editing
 * (inline rename) is a later Phase 2/3 item. The dual-pane commander view is
 * here now (fm_toggle_dual, --dual-demo).
 */
#include "apps.h"
#include "trash_core.h"
#include "grep_core.h"
#include "../sys/mach_core.h"
#include "fsize_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/shell.h"   /* sh_iconpack_icon: optional toolbar icons */
#include "ren_batch.h"        /* Rename Many, shared with the console */
#include "thumb_core.h"
#include "assoc.h"
#include "cz_file.h"
#include "car_core.h"
#include "lzss_core.h"
#include "paint_core.h"       /* pc_scale: the thumbnail resampler        */

#include <string.h>
#include <stdlib.h>

#define FM_MAX_ENTRIES 512
#define FM_ROW_H       16
#define FM_MENUBAR_H   16
#define FM_TOOLBAR_H   28
#define FM_PATHBAR_H   22     /* address bar */
#define FM_HEADER_H    17
#define FM_STATUS_H    18
#define FM_TASKPANE_W  152    /* left XP "Explorer bar" width (single pane) */
#define FM_DBLCLICK_MS 400

typedef struct {
    char  name[CASTALIA_MAX_NAME];
    cbool is_dir;
    long  size;
} FmEntry;

#define FM_SEARCH_MAX   128   /* results a search collects */
#define FM_SEARCH_DEPTH 16    /* recursion cap             */
/*
 * Find looks INSIDE files as well as at their names, which means reading them,
 * which on the machines this targets means a floppy or a CF card. Both limits
 * below are reported in the status line when they bite -- a search that
 * quietly stopped looking is a search that says "no matches" and means "I gave
 * up", and those are not the same answer.
 */
#define FM_GREP_BYTES   65536u  /* read at most this much of any one file */
#define FM_GREP_FILES   400     /* ...and open at most this many files    */

typedef struct FileMan_s {
    char    path[CASTALIA_MAX_PATH];
    FmEntry ent[FM_MAX_ENTRIES];
    int     count;
    int     hot_sb;       /* active scroll-bar part (UI_SB_*)          */
    cbool   sb_drag;      /* dragging the scroll thumb                 */
    int     sel;
    char    ren_from[32]; /* Rename Many: the pattern from the first prompt */
    int     top;          /* first visible row (scroll)                    */
    cu32    last_click_ms;
    int     last_click_row;
    int     view;         /* 0 = Details, 1 = Tiles (large rows)           */
    int     sort_key;     /* FM_SORT_NAME / _SIZE / _TYPE                  */
    cbool   sort_desc;    /* descending order                             */
    /* Right-click context menu (in-window overlay). */
    cbool   ctx_open;
    int     ctx_x, ctx_y; /* menu top-left, client coords                 */
    UiMenu  ctx_menu;
    /* Drag-and-drop (owner pane holds the whole gesture's state). */
    cbool   drag_armed;   /* button down on a row; may become a drag       */
    cbool   drag_active;  /* past the threshold -> a live drag             */
    int     drag_row;     /* source row within the source pane            */
    int     drag_pane;    /* 0 = owner/left, 1 = mate/right (dual mode)    */
    int     down_x, down_y; /* button-down position (client coords)       */
    int     drag_x, drag_y; /* current cursor (client) for the ghost      */
    cbool   drag_isdir;
    char    drag_name[CASTALIA_MAX_NAME];
    char    status[96];
    /* Search-results mode: ent[] holds matches (name = path relative to the
     * search root), spath[] their full paths for opening. */
    cbool   search_mode;
    char    query[64];
    char    spath[FM_SEARCH_MAX][CASTALIA_MAX_PATH];
    int     grep_files;      /* files opened by the last search            */
    int     grep_partial;    /* ...of which were larger than we would read */
    cbool   grep_capped;     /* ...and whether we stopped opening any more */
    /* Dual-pane (commander) mode -- only the window's owner pane sets these.
     * 'mate' is the second pane; 'active' selects which pane input acts on. */
    cbool   dual;
    int     active;       /* 0 = owner (left), 1 = mate (right) */
    struct FileMan_s *mate;
    /* Thumbnails for the Icons view (bounded; see thumb_core.h). */
    ThumbCache thumbs;
    cbool   thumbs_on;
    /* The file a "may I replace that?" question is waiting on an answer
     * about, and which question it was. Dialogs here are asynchronous -- the
     * callback comes back on a later frame -- so what was asked has to
     * survive until it is answered. */
    int     pend_op;      /* FM_PEND_*, FM_PEND_NONE when nothing is asked */
    char    pending[CASTALIA_MAX_PATH];
    /* Which toolbar button the pointer is on / pressed. On the window's owner
     * pane only: there is one toolbar however many panes are under it. */
    UiHot   hot_tb;
} FileMan;

/*
 * The three acts that can destroy a file that was already here: restoring an
 * archive over it, compressing something to a .CZ name already taken, and
 * backing a folder up onto an existing .CAR. They share one pending slot, one
 * confirmation callback and one rule -- ask only when there is something to
 * lose -- because they used to share nothing, and only the first of them
 * asked.
 */
enum { FM_PEND_NONE = 0, FM_PEND_EXPAND, FM_PEND_COMPRESS, FM_PEND_BACKUP };

/* ---- path helpers ---------------------------------------------------- */
/* Joining and climbing live in ui_path.c (ui_path_join, ui_path_parent_rel),
 * where tests/test_path.c can reach every edge case without a window. This
 * file used to carry its own copies, character for character identical to the
 * console's. */
static cbool is_sep(char c) { return (c == '/' || c == '\\') ? CTRUE : CFALSE; }

/* ---- listing --------------------------------------------------------- */
/* Sort context for fm_cmp (set by fm_sort before each pass). Columns: 0=Name,
 * 1=Size, 2=Type. Folders always group ahead of files; ".." pins to the top. */
enum { FM_SORT_NAME = 0, FM_SORT_SIZE, FM_SORT_TYPE };
static int  g_sort_key = FM_SORT_NAME;
static cbool g_sort_desc = CFALSE;

static const char *fm_ext(const char *name)
{
    const char *dot = 0;
    while (*name != '\0') { if (*name == '.') { dot = name; } name++; }
    return dot ? dot + 1 : "";
}

/* Within-group ordering by the active column (before direction is applied). */
static int fm_cmp_key(const FmEntry *a, const FmEntry *b)
{
    int c = 0;
    switch (g_sort_key) {
    case FM_SORT_SIZE:
        if (a->size != b->size) { c = (a->size < b->size) ? -1 : 1; }
        else { c = sys_stricmp(a->name, b->name); }
        break;
    case FM_SORT_TYPE:
        c = sys_stricmp(fm_ext(a->name), fm_ext(b->name));
        if (c == 0) { c = sys_stricmp(a->name, b->name); }
        break;
    case FM_SORT_NAME:
    default:
        c = sys_stricmp(a->name, b->name);
        break;
    }
    return c;
}

static int fm_cmp(const FmEntry *a, const FmEntry *b)
{
    int ad = (strcmp(a->name, "..") == 0);
    int bd = (strcmp(b->name, "..") == 0);
    int c;
    if (ad != bd) { return ad ? -1 : 1; }        /* ".." pinned to the top   */
    if (a->is_dir != b->is_dir) { return a->is_dir ? -1 : 1; } /* folders 1st */
    c = fm_cmp_key(a, b);
    return g_sort_desc ? -c : c;
}

static void fm_sort(FileMan *fm)
{
    int i, j;
    g_sort_key = fm->sort_key;
    g_sort_desc = fm->sort_desc;
    for (i = 1; i < fm->count; i++) {
        FmEntry key = fm->ent[i];
        j = i - 1;
        while (j >= 0 && fm_cmp(&fm->ent[j], &key) > 0) {
            fm->ent[j + 1] = fm->ent[j];
            j--;
        }
        fm->ent[j + 1] = key;
    }
}

static void fm_add(FileMan *fm, const char *name, cbool is_dir, long size)
{
    FmEntry *e;
    if (fm->count >= FM_MAX_ENTRIES) { return; }
    e = &fm->ent[fm->count++];
    sys_strlcpy(e->name, name, sizeof(e->name));
    e->is_dir = is_dir;
    e->size = size;
}

static void fm_load(FileMan *fm, const char *path)
{
    PlatDir *d;
    PlatDirEntry e;
    sys_strlcpy(fm->path, path, sizeof(fm->path));
    fm->count = 0;
    fm->sel = 0;
    fm->top = 0;

    /* Synthetic parent entry (kept sorted to the top). */
    fm_add(fm, "..", CTRUE, 0);

    d = plat_opendir(path);
    if (d != NULL) {
        while (plat_readdir(d, &e)) {
            if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) {
                continue;
            }
            fm_add(fm, e.name, e.is_dir, e.size);
        }
        plat_closedir(d);
    } else {
        sys_snprintf(fm->status, sizeof(fm->status), "Cannot open %s", path);
    }
    fm_sort(fm);
    if (fm->status[0] == '\0' || d != NULL) {
        /*
         * How much is here, not how many -- the cell to the left of this one
         * already says how many, and the two of them saying "3 object(s)" and
         * "3 items" was the same fact twice in one status bar.
         *
         * The byte total is the thing that is actually hard to find out
         * otherwise: it answers "will this fit on a floppy", which on this
         * machine is a question people really do have.
         */
        FsizeSum sum;
        int i;
        fsize_begin(&sum);
        for (i = 0; i < fm->count; i++) {
            if (!fm->ent[i].is_dir) { fsize_add(&sum, fm->ent[i].size); }
        }
        fsize_label(&sum, fm->status, sizeof(fm->status));
    }
    fm->search_mode = CFALSE;
    SYS_LOGI("app", "fileman: %s (%d entries)", path, fm->count);
}

/*
 * Re-read the folder being browsed, keeping the highlight on the same NAME.
 *
 * Not on the same row: a folder that gained a file sorted above the selection
 * would move the highlight onto a neighbour, and the next Delete would take
 * the wrong thing. Reloading is how every operation here finishes, so this is
 * only for the two that are a refresh and nothing else -- the toolbar button
 * and F5.
 */
static void fm_refresh(FileMan *fm)
{
    char keep[CASTALIA_MAX_NAME];
    int i;
    keep[0] = '\0';
    if (fm->sel >= 0 && fm->sel < fm->count) {
        sys_strlcpy(keep, fm->ent[fm->sel].name, sizeof(keep));
    }
    fm_load(fm, fm->path);          /* also exits search mode */
    for (i = 0; keep[0] != '\0' && i < fm->count; i++) {
        if (strcmp(fm->ent[i].name, keep) == 0) { fm->sel = i; break; }
    }
}

/* ---- recursive name search ------------------------------------------- */
static int fm_lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

/* Case-insensitive: does 'name' contain 'needle'? */
static cbool fm_name_matches(const char *name, const char *needle)
{
    int nl = (int)sys_strnlen(needle, 64);
    int i, k;
    if (nl == 0) { return CTRUE; }
    for (i = 0; name[i] != '\0'; i++) {
        for (k = 0; k < nl; k++) {
            if (name[i + k] == '\0') { return CFALSE; }
            if (fm_lower((unsigned char)name[i + k]) !=
                fm_lower((unsigned char)needle[k])) { break; }
        }
        if (k == nl) { return CTRUE; }
    }
    return CFALSE;
}

/* Walk 'dir' recursively; 'rel' is dir's path relative to the search root.
 * Each name containing 'needle' becomes a result (ent name = childrel, spath =
 * full path). Bounded by FM_SEARCH_MAX results and FM_SEARCH_DEPTH depth. Each
 * PlatDir carries its own search state, so nesting is safe on DOS too. */
/*
 * Does this file contain 'needle'? Returns the 1-based line, or 0.
 *
 * The buffer is static and shared: one search runs at a time, and 64K on the
 * stack is not something to do on the DOS target.
 */
static int fm_file_has(FileMan *fm, const char *path, const char *needle)
{
    static char buf[FM_GREP_BYTES];
    PlatFile *f;
    cu32 got;
    long size;
    if (fm->grep_files >= FM_GREP_FILES) { fm->grep_capped = CTRUE; return 0; }
    size = plat_file_size(path);
    if (size < 0) { return 0; }
    if ((cu32)size > FM_GREP_BYTES) { fm->grep_partial++; }
    f = plat_fopen(path, "rb");
    if (f == NULL) { return 0; }
    fm->grep_files++;
    got = plat_fread(f, buf, FM_GREP_BYTES);
    plat_fclose(f);
    return grep_find_line(buf, got, needle);
}

static void fm_search_walk(FileMan *fm, const char *dir, const char *rel,
                           const char *needle, int depth)
{
    PlatDir *d;
    PlatDirEntry e;
    if (depth > FM_SEARCH_DEPTH || fm->count >= FM_SEARCH_MAX) { return; }
    d = plat_opendir(dir);
    if (d == NULL) { return; }
    while (plat_readdir(d, &e) && fm->count < FM_SEARCH_MAX) {
        char child[CASTALIA_MAX_PATH];
        char childrel[CASTALIA_MAX_PATH];
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        ui_path_join(child, sizeof(child), dir, e.name);
        if (rel[0] != '\0') { ui_path_join(childrel, sizeof(childrel), rel, e.name); }
        else { sys_strlcpy(childrel, e.name, sizeof(childrel)); }

        {
            /*
             * By name, or by what is in it. One Find that answers both is the
             * point: you remember one or the other, rarely which.
             *
             * The name is tried first because it costs nothing -- a file whose
             * name matches is never opened.
             */
            cbool hit = fm_name_matches(e.name, needle);
            int line = 0;
            if (!hit && !e.is_dir) {
                line = fm_file_has(fm, child, needle);
                hit = (line > 0) ? CTRUE : CFALSE;
            }
            if (hit) {
                FmEntry *ent = &fm->ent[fm->count];
                if (line > 0) {
                    sys_snprintf(ent->name, sizeof(ent->name), "%s (line %d)",
                                 childrel, line);
                } else {
                    sys_strlcpy(ent->name, childrel, sizeof(ent->name));
                }
                ent->is_dir = e.is_dir;
                ent->size = e.size;
                sys_strlcpy(fm->spath[fm->count], child, sizeof(fm->spath[0]));
                fm->count++;
            }
        }
        if (e.is_dir) { fm_search_walk(fm, child, childrel, needle, depth + 1); }
    }
    plat_closedir(d);
}

static void fm_run_search(FileMan *fm, const char *needle)
{
    sys_strlcpy(fm->query, needle, sizeof(fm->query));
    fm->count = 0;
    fm->sel = 0;
    fm->top = 0;
    fm->search_mode = CTRUE;
    fm->grep_files = 0;
    fm->grep_partial = 0;
    fm->grep_capped = CFALSE;
    fm_search_walk(fm, fm->path, "", needle, 0);
    sys_snprintf(fm->status, sizeof(fm->status),
                 "%d match%s for '%s' (%d file%s read)%s",
                 fm->count, (fm->count == 1) ? "" : "es", needle,
                 fm->grep_files, (fm->grep_files == 1) ? "" : "s",
                 (fm->count >= FM_SEARCH_MAX) ? " -- list full" : "");
    /* Every limit that bit is named. "No matches" and "I stopped looking"
     * are different answers and must not read the same. */
    if (fm->grep_capped) {
        sys_strlcat(fm->status, " -- stopped after 400 files",
                    sizeof(fm->status));
    }
    if (fm->grep_partial > 0) {
        char more[48];
        sys_snprintf(more, sizeof(more), " -- %d too big to read fully",
                     fm->grep_partial);
        sys_strlcat(fm->status, more, sizeof(fm->status));
    }
    SYS_LOGI("app", "fileman: search '%s' -> %d matches, %d files read",
             needle, fm->count, fm->grep_files);
}

/* The pane input currently acts on: the mate in dual mode when it is active,
 * otherwise the window's owner pane. */
static FileMan *fm_active(FileMan *o)
{
    return (o->dual && o->active == 1 && o->mate != NULL) ? o->mate : o;
}

/* ---- operations ------------------------------------------------------ */
/* Put an archive back: a .CZ restores its single member under the name its
 * header remembers, a .CAR restores all of its members here.
 *
 * Both extensions map to ASSOC_ARCHIVE, so this is where they part company --
 * and it is worth being explicit, because handing a .CAR to the .CZ path is
 * exactly the bug that shipped when .CZ was added and the open path had no
 * case for it at all. Shared by the context menu and by opening one, so the
 * two routes cannot disagree about what "restore" means.
 */
static void fm_expand(FileMan *fm, const char *path)
{
    CResult rc;
    if (ui_path_match_ext(path, "CAR")) {
        int files = 0;
        rc = car_unpack(path, fm->path, &files);
        if (rc == CE_OK) {
            sys_snprintf(fm->status, sizeof(fm->status),
                         "Restored %d file(s) here", files);
        } else if (rc == CE_INVALID) {
            sys_strlcpy(fm->status, "That archive is damaged or names a file "
                        "it may not write", sizeof(fm->status));
        } else {
            sys_strlcpy(fm->status, "Could not restore that archive",
                        sizeof(fm->status));
        }
    } else {
        char got[CASTALIA_MAX_NAME];
        rc = cz_decompress_file(path, fm->path, got, sizeof(got));
        if (rc == CE_OK) {
            sys_snprintf(fm->status, sizeof(fm->status), "Restored %s", got);
        } else if (rc == CE_INVALID) {
            sys_strlcpy(fm->status, "Not a valid .CZ file (or damaged)",
                        sizeof(fm->status));
        } else if (rc == CE_OVERFLOW) {
            sys_strlcpy(fm->status, "That archive is too large to expand",
                        sizeof(fm->status));
        } else {
            sys_strlcpy(fm->status, "Could not restore that archive",
                        sizeof(fm->status));
        }
    }
    SYS_LOGI("app", "fileman: %s", fm->status);
}

/*
 * Compress one file to a .CZ beside it, and back one folder up into a .CAR
 * beside itself. Both report the outcome in the status line -- including the
 * ratio, which is the only number that answers "was that worth doing".
 *
 * They live out here rather than inside the context-menu handler because the
 * confirmation callback has to be able to perform them too, and a body that
 * exists once cannot drift from the copy the other route runs.
 */
static void fm_do_compress(FileMan *fm, const char *src)
{
    char arc[CASTALIA_MAX_NAME], dst[CASTALIA_MAX_PATH];
    long saved = 0;
    CResult rc;
    if (!cz_archive_name(ui_path_base(src), arc, sizeof(arc))) {
        sys_strlcpy(fm->status, "Cannot name a compressed copy of that",
                    sizeof(fm->status));
        return;
    }
    ui_path_join(dst, sizeof(dst), fm->path, arc);
    rc = cz_compress_file(src, dst, &saved);
    if (rc == CE_OK) {
        long orig = plat_file_size(src);
        /* Not (saved * 100L) / orig: that product passes 2^31 once a file
         * shrinks by more than 20.5 MB, and on the 32-bit target it wrapped
         * to a negative percentage. */
        long pct = (orig > 0) ? (long)ui_percent((cs32)saved, (cs32)orig) : 0;
        if (saved > 0) {
            sys_snprintf(fm->status, sizeof(fm->status),
                         "%s -- %ld bytes smaller (%ld%%)", arc, saved, pct);
        } else {
            sys_snprintf(fm->status, sizeof(fm->status),
                         "%s -- stored, it does not compress", arc);
        }
    } else if (rc == CE_OVERFLOW) {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "Too large to compress (limit %luK)",
                     (unsigned long)(CZ_FILE_MAX / 1024u));
    } else {
        sys_strlcpy(fm->status, "Could not compress that file",
                    sizeof(fm->status));
    }
    SYS_LOGI("app", "fileman: %s", fm->status);
}

/* The name a folder's backup takes: the first eight characters of its name,
 * plus .CAR. Two folders can share one -- which is exactly why the caller has
 * to look before it writes. */
static void fm_backup_name(const char *dir, char *out, cu32 outsz)
{
    int k = 0;
    sys_strlcpy(out, ui_path_base(dir), outsz);
    while (out[k] != '\0' && out[k] != '.' && k < 8) { k++; }
    out[k] = '\0';
    sys_strlcat(out, ".CAR", outsz);
}

static void fm_do_backup(FileMan *fm, const char *dir)
{
    /* The archive lands beside the folder, not inside it -- writing it into
     * the folder being read would put it in its own backup on the next run. */
    char arc[CASTALIA_MAX_PATH], nm[CASTALIA_MAX_NAME];
    int files = 0;
    long saved = 0;
    CResult rc;
    fm_backup_name(dir, nm, sizeof(nm));
    ui_path_join(arc, sizeof(arc), fm->path, nm);
    rc = car_pack_dir(dir, arc, &files, &saved);
    if (rc == CE_OK) {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "%s -- %d file(s), %ld bytes saved", nm, files, saved);
    } else if (rc == CE_NOTFOUND) {
        sys_strlcpy(fm->status, "Nothing in that folder to back up",
                    sizeof(fm->status));
    } else {
        sys_strlcpy(fm->status, "Could not back up that folder",
                    sizeof(fm->status));
    }
    SYS_LOGI("app", "fileman: %s", fm->status);
}

/*
 * How many files already here a pending operation would REPLACE, and -- when
 * that is exactly one and its name is known -- what it is called.
 *
 * Restoring, compressing and backing up all write with "wb", so this is the
 * difference between the act somebody asked for and the quiet destruction of
 * something else. Returns -1 when the source cannot be read at all, which is
 * not "nothing to lose": that case is left to the operation itself to report,
 * since it can say why.
 */
static int fm_would_replace(FileMan *fm, int op, const char *path,
                            char *one, cu32 onesz)
{
    char probe[CASTALIA_MAX_PATH], nm[CASTALIA_MAX_NAME];
    if (one != NULL && onesz > 0u) { one[0] = '\0'; }
    if (op == FM_PEND_EXPAND && ui_path_match_ext(path, "CAR")) {
        /* A .CAR names its members, so the count is the honest answer and no
         * single name stands for it. */
        return car_clashes(path, fm->path);
    }
    if (op == FM_PEND_EXPAND) {
        if (!cz_member_name(path, nm, sizeof(nm))) { return -1; }
    } else if (op == FM_PEND_COMPRESS) {
        if (!cz_archive_name(ui_path_base(path), nm, sizeof(nm))) { return -1; }
    } else if (op == FM_PEND_BACKUP) {
        fm_backup_name(path, nm, sizeof(nm));
    } else {
        return -1;
    }
    ui_path_join(probe, sizeof(probe), fm->path, nm);
    if (!plat_file_exists(probe)) { return 0; }
    if (one != NULL) { sys_strlcpy(one, nm, onesz); }
    return 1;
}

static void fm_perform(FileMan *fm, int op, const char *path)
{
    if (op == FM_PEND_EXPAND)        { fm_expand(fm, path); }
    else if (op == FM_PEND_COMPRESS) { fm_do_compress(fm, path); }
    else if (op == FM_PEND_BACKUP)   { fm_do_backup(fm, path); }
    else                             { return; }
    fm_load(fm, fm->path);
}

/* The answer to "that would replace X -- go ahead?". */
static void on_replace_confirm(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (fm == NULL) { return; }
    if (result == UI_DR_YES && fm->pending[0] != '\0') {
        fm_perform(fm, fm->pend_op, fm->pending);
    } else {
        sys_strlcpy(fm->status, "Cancelled -- nothing was replaced",
                    sizeof(fm->status));
    }
    fm->pend_op = FM_PEND_NONE;
    fm->pending[0] = '\0';
    wm_invalidate(win, NULL);
}

/*
 * Do it, or ask first if doing it would destroy a file that is already here.
 * Every route into restoring, compressing and backing up comes through here,
 * so none of them can be quieter than the others -- which is how three of the
 * four of them came to be silent while the fourth asked.
 */
static void fm_ask_replace(WmWindow *win, FileMan *fm, int op, const char *path)
{
    char one[CASTALIA_MAX_NAME], msg[192];
    int clash = fm_would_replace(fm, op, path, one, sizeof(one));

    if (clash <= 0) {                    /* nothing to lose, or nothing to read */
        fm_perform(fm, op, path);
        wm_invalidate(win, NULL);
        return;
    }
    fm->pend_op = op;
    sys_strlcpy(fm->pending, path, sizeof(fm->pending));
    if (one[0] != '\0') {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "'%s' is already here", one);
        sys_snprintf(msg, sizeof(msg),
                     "'%s' is already here and will be replaced.\n\n"
                     "Go ahead?", one);
    } else {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "%d file(s) here would be replaced", clash);
        sys_snprintf(msg, sizeof(msg),
                     "%d file(s) here will be replaced by that archive.\n\n"
                     "Restore anyway?", clash);
    }
    SYS_LOGI("app", "fileman: %s", fm->status);
    ui_msgbox(op == FM_PEND_EXPAND ? "Restore" : "Replace", msg, UI_MB_YESNO,
              on_replace_confirm, win);
    wm_invalidate(win, NULL);
}

/* Open one file with whatever the association table says owns it. Anything
 * unclaimed goes to the Viewer, which can always show something -- an image if
 * it decodes, a hex dump otherwise -- rather than refusing. */
static void fm_launch(WmWindow *win, FileMan *fm, const char *path,
                      const char *name)
{
    AssocKind kind = assoc_for(name);
    switch (kind) {
    case ASSOC_TEXT:    app_notepad_open_file(path); break;
    case ASSOC_IMAGE:   app_paint_open_file(path);   break;
    case ASSOC_SHEET:   app_sheet_open_file(path);   break;
    case ASSOC_WRITE:   app_write_open_file(path);   break;
    case ASSOC_AUDIO:   app_media_open_file(path);   break;
    case ASSOC_THEME:   app_notepad_open_file(path); break;
    case ASSOC_PROGRAM:
        /* A DOS program is the one thing the shell must not open blind: it
         * would take over the machine. Say what it is and let the user run it
         * deliberately from the DOS Program dialog. */
        sys_snprintf(fm->status, sizeof(fm->status),
                     "'%s' is a DOS program -- use Start / DOS Program", name);
        return;
    case ASSOC_ARCHIVE:
        /*
         * A .CZ holds one file, so opening it means getting that file back --
         * there is nothing to look at, and the container's bytes are not the
         * point.
         *
         * A .CAR holds a folder, and opening one used to unpack every member
         * into wherever you were standing, replacing anything already there
         * under the same name, before telling you what it had contained. That
         * is a lot to do on a double-click. It opens the Archive window now,
         * which lists the members and says how many of them already exist
         * here; Extract is still one press away. The context menu's "Restore
         * from Archive" is unchanged -- somebody who asked for a restore
         * asked for it.
         *
         * The .CZ still expands on the double-click, but through the same
         * gate everything else uses: getting one file back is worth a click,
         * losing a newer file of the same name to it is not.
         */
        if (ui_path_match_ext(path, "CAR")) {
            app_archive_open(path);
            sys_snprintf(fm->status, sizeof(fm->status),
                         "Opened '%s' -- press Extract All to restore it", name);
            return;
        }
        fm_ask_replace(win, fm, FM_PEND_EXPAND, path);
        return;
    default:            app_view_open_file(path);    break;
    }
    sys_snprintf(fm->status, sizeof(fm->status), "Opened '%s' in %s", name,
                 assoc_app_name(kind));
    SYS_LOGI("app", "fileman: '%s' -> %s", name, assoc_app_name(kind));
}

static void fm_open_sel(WmWindow *win, FileMan *fm)
{
    if (fm->search_mode) {
        FmEntry *e;
        if (fm->sel < 0 || fm->sel >= fm->count) { return; }
        e = &fm->ent[fm->sel];
        if (e->is_dir) {
            char full[CASTALIA_MAX_PATH];
            sys_strlcpy(full, fm->spath[fm->sel], sizeof(full));
            fm_load(fm, full); /* exits search mode */
        } else {
            fm_launch(win, fm, fm->spath[fm->sel], e->name);
        }
        return;
    }
    {
    FmEntry *e;
    char np[CASTALIA_MAX_PATH];
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    e = &fm->ent[fm->sel];
    if (!e->is_dir) {
        char fp[CASTALIA_MAX_PATH];
        ui_path_join(fp, sizeof(fp), fm->path, e->name);
        fm_launch(win, fm, fp, e->name);
        return;
    }
    if (strcmp(e->name, "..") == 0) {
        ui_path_parent_rel(fm->path, np, sizeof(np));
    } else {
        ui_path_join(np, sizeof(np), fm->path, e->name);
    }
    fm->status[0] = '\0';
    fm_load(fm, np);
    }
}

static void fm_create_folder(FileMan *fm, const char *name)
{
    char np[CASTALIA_MAX_PATH];
    CResult r;
    if (name == NULL || name[0] == '\0') { return; }
    ui_path_join(np, sizeof(np), fm->path, name);
    r = plat_mkdir(np);
    fm_load(fm, fm->path);
    if (r == CE_OK) {
        sys_snprintf(fm->status, sizeof(fm->status), "Created '%s'", name);
    } else if (r == CE_BUSY) {
        sys_snprintf(fm->status, sizeof(fm->status), "'%s' already exists", name);
    } else {
        sys_snprintf(fm->status, sizeof(fm->status), "Could not create folder");
    }
}

/* ---- the Recycle Bin's index ------------------------------------------ *
 *
 * TRASH/TRASH.IDX, plain text, one 'stored|original folder' line per item
 * (trash_core.c). Loaded and rewritten whole: it is a handful of short lines,
 * so there is no partial-update state to get wrong, and a text file is
 * repairable by hand -- which is the property you want most in the one folder
 * that holds things somebody wishes they had not deleted.
 */
#define FM_IDX_MAX 8192

/*
 * Is anything at all sitting at 'path' -- file or folder?
 *
 * plat_file_exists opens the path, which answers for files but not for
 * folders on every platform (DOS will not open one). Deleted FOLDERS go into
 * the bin too, and both the delete probe and the restore probe are asking
 * "is this name free", where a folder in the way counts.
 */
static cbool fm_path_taken(const char *path)
{
    PlatDir *d;
    if (plat_file_exists(path)) { return CTRUE; }
    d = plat_opendir(path);
    if (d == NULL) { return CFALSE; }
    plat_closedir(d);
    return CTRUE;
}

static void fm_trash_idx_path(char *out, cu32 cap)
{
    char trash[CASTALIA_MAX_PATH];
    sys_home_path(trash, (cu32)sizeof(trash), "TRASH");
    sys_snprintf(out, cap, "%s/TRASH.IDX", trash);
}

static void fm_trash_idx_load(char *buf, cu32 cap)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    cu32 got = 0;
    buf[0] = '\0';
    fm_trash_idx_path(path, sizeof(path));
    f = plat_fopen(path, "rb");
    if (f == NULL) { return; }
    got = plat_fread(f, buf, cap - 1u);
    plat_fclose(f);
    if (got >= cap) { got = cap - 1u; }
    buf[got] = '\0';
}

static void fm_trash_idx_save(const char *buf)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    fm_trash_idx_path(path, sizeof(path));
    f = plat_fopen(path, "wb");
    if (f == NULL) { return; }
    plat_fwrite(f, buf, sys_strnlen(buf, FM_IDX_MAX));
    plat_fclose(f);
}

static void fm_trash_record(const char *stored, const char *orig)
{
    static char idx[FM_IDX_MAX];
    fm_trash_idx_load(idx, sizeof(idx));
    if (trash_idx_add(idx, sizeof(idx), stored, orig)) {
        fm_trash_idx_save(idx);
    }
}

static void fm_trash_forget(const char *stored)
{
    static char idx[FM_IDX_MAX];
    fm_trash_idx_load(idx, sizeof(idx));
    trash_idx_remove(idx, sizeof(idx), stored);
    fm_trash_idx_save(idx);
}

/* Is this pane looking at the Recycle Bin itself? Restore is offered there and
 * nowhere else -- it is the only folder where "where did this come from" has
 * an answer. */
static cbool fm_in_trash(const FileMan *fm)
{
    char trash[CASTALIA_MAX_PATH];
    int i = 0;
    sys_home_path(trash, (cu32)sizeof(trash), "TRASH");
    for (;;) {
        char a = trash[i], b = fm->path[i];
        /* Same folder written two ways is still the same folder: DOS is blind
         * to case and this system accepts either slash. */
        if (a == '\\') { a = '/'; }
        if (b == '\\') { b = '/'; }
        if (fm_lower((unsigned char)a) != fm_lower((unsigned char)b)) {
            return CFALSE;
        }
        if (a == '\0') { return CTRUE; }
        i++;
    }
}

static void fm_trash_sel(FileMan *fm)
{
    FmEntry *e;
    char trash[CASTALIA_MAX_PATH];
    char src[CASTALIA_MAX_PATH];
    char dst[CASTALIA_MAX_PATH];
    char stored[CASTALIA_MAX_NAME];
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    e = &fm->ent[fm->sel];
    if (strcmp(e->name, "..") == 0) { return; }

    sys_home_path(trash, (cu32)sizeof(trash), "TRASH");
    plat_mkdir(trash); /* ensure it exists (CE_BUSY if already there) */

    ui_path_join(src, sizeof(src), fm->path, e->name);
    /*
     * Find a FREE slot in the bin instead of clearing an occupied one.
     *
     * This used to be plat_file_remove(dst) -- "clear any prior copy so the
     * rename can land". Deleting NOTES.TXT from two different folders
     * therefore destroyed the first one, permanently, while the status line
     * said "Moved to Trash" and Help promised that deleting "moves to the
     * Recycle Bin rather than destroying anything". A data-loss path that
     * announces success is the worst shape a bug can have.
     */
    {
        int n;
        for (n = 0; n < 1000; n++) {
            trash_candidate(stored, sizeof(stored), e->name, n);
            sys_snprintf(dst, sizeof(dst), "%s/%s", trash, stored);
            if (!fm_path_taken(dst)) { break; }
        }
    }
    if (plat_file_rename(src, dst) == CE_OK) {
        /* Remember where it came from -- the whole path, so a restore gets the
         * NAME back too and not the ~1 slot it happened to land in. An index
         * that cannot be written is not fatal (the file is safe either way),
         * but then the restore has to ask. */
        fm_trash_record(stored, src);
        sys_snprintf(fm->status, sizeof(fm->status),
                     "Moved '%s' to Trash", e->name);
        sh_recyclebin_touch();   /* the desktop bin now shows as full */
    } else {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "Could not move '%s' to Trash", e->name);
    }
    fm_load(fm, fm->path);
}

/* ---- putting something back ------------------------------------------ *
 *
 * A bin you cannot restore from is a folder called TRASH. Restore moves an
 * item back to the path the index wrote down -- but two refusals matter more
 * than the happy path:
 *
 *  - it never writes over what is there NOW. The folder has had time to
 *    acquire another NOTES.TXT, and overwriting that would be the very bug
 *    this bin was fixed for, running the other way. A name that is taken gets
 *    the same ~1 treatment a delete gets, and the status line says so.
 *  - it does not invent a destination. A file dropped into TRASH by hand has
 *    no entry, and picking a folder for it is how a file ends up somewhere
 *    nobody thinks to look. The user is asked instead.
 */
static cbool fm_undelete_to(FileMan *fm, const char *stored, const char *want)
{
    char src[CASTALIA_MAX_PATH];
    char dir[CASTALIA_MAX_PATH];
    char dst[CASTALIA_MAX_PATH];
    char base[CASTALIA_MAX_NAME];
    char used[CASTALIA_MAX_NAME];
    PlatDir *d;
    int n;

    ui_path_join(src, sizeof(src), fm->path, stored);
    ui_path_parent_rel(want, dir, sizeof(dir));
    sys_strlcpy(base, ui_path_base(want), sizeof(base));
    if (base[0] == '\0') {
        sys_strlcpy(fm->status, "That is not a folder to restore into",
                    sizeof(fm->status));
        return CFALSE;
    }

    /* The folder it came from may itself be gone. Recreating a whole tree is a
     * larger act than "put this back", so say so and leave the file safe in
     * the bin rather than improvise. */
    d = plat_opendir(dir);
    if (d == NULL) {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "'%s' no longer exists", dir);
        return CFALSE;
    }
    plat_closedir(d);

    used[0] = '\0';
    for (n = 0; n < 1000; n++) {
        trash_candidate(used, sizeof(used), base, n);
        ui_path_join(dst, sizeof(dst), dir, used);
        if (!fm_path_taken(dst)) { break; }
    }

    if (plat_file_rename(src, dst) != CE_OK) {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "Could not restore '%s'", stored);
        return CFALSE;
    }
    fm_trash_forget(stored);
    if (sys_stricmp(used, base) == 0) {
        sys_snprintf(fm->status, sizeof(fm->status), "Restored to %s", dst);
    } else {
        /* Renamed rather than overwritten -- and the user is told, because a
         * file that came back under a different name is a file they will not
         * find by looking for the old one. */
        sys_snprintf(fm->status, sizeof(fm->status),
                     "'%s' was taken -- restored as %s", base, used);
    }
    SYS_LOGI("app", "fileman: %s", fm->status);
    sh_recyclebin_touch();
    fm_load(fm, fm->path);
    return CTRUE;
}

/* The prompt shown when the index does not know where an item belongs. */
static void on_undelete_where(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    char want[CASTALIA_MAX_PATH];
    if (!ok || fm == NULL || text[0] == '\0') { return; }
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    /* The typed part is a FOLDER; the name stays whatever the bin holds,
     * because that is the only name anyone still knows. */
    ui_path_join(want, sizeof(want), text, fm->ent[fm->sel].name);
    fm_undelete_to(fm, fm->ent[fm->sel].name, want);
    wm_invalidate(win, NULL);
}

static void fm_undelete_sel(WmWindow *win, FileMan *fm)
{
    static char idx[FM_IDX_MAX];
    char want[CASTALIA_MAX_PATH];
    const char *stored;
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    stored = fm->ent[fm->sel].name;
    if (strcmp(stored, "..") == 0) { return; }
    if (trash_is_index(stored)) {
        sys_strlcpy(fm->status, "That is the bin's own index, not a file "
                    "you deleted", sizeof(fm->status));
        wm_invalidate(win, NULL);
        return;
    }
    fm_trash_idx_load(idx, sizeof(idx));
    if (trash_idx_find(idx, stored, want, sizeof(want))) {
        fm_undelete_to(fm, stored, want);
        wm_invalidate(win, NULL);
        return;
    }
    {
        char home[CASTALIA_MAX_PATH];
        sys_home_path(home, (cu32)sizeof(home), "DOCS");
        sys_snprintf(fm->status, sizeof(fm->status),
                     "Where '%s' came from was not recorded", stored);
        ui_prompt("Restore", "Restore into which folder:", home,
                  on_undelete_where, win);
    }
}

/* ---- clipboard: copy / cut / paste ----------------------------------- */
/* One shared file clipboard for all File Manager windows. */
static struct {
    cbool active;
    cbool cut;
    cbool is_dir;
    char  path[CASTALIA_MAX_PATH];
} g_clip;

static CResult fm_copy_file(const char *src, const char *dst)
{
    PlatFile *in, *out;
    char buf[2048];
    cu32 n;
    in = plat_fopen(src, "rb");
    if (in == NULL) { return CE_IO; }
    out = plat_fopen(dst, "wb");
    if (out == NULL) { plat_fclose(in); return CE_IO; }
    while ((n = plat_fread(in, buf, (cu32)sizeof(buf))) > 0) {
        if (plat_fwrite(out, buf, n) != n) {
            plat_fclose(in); plat_fclose(out); return CE_IO;
        }
    }
    plat_fclose(in);
    plat_fclose(out);
    return CE_OK;
}

/* Recursively copy a file or directory tree. Depth-guarded against cycles. */
static CResult fm_copy_tree(const char *src, const char *dst, cbool is_dir,
                            int depth)
{
    PlatDir *d;
    PlatDirEntry e;
    CResult rc = CE_OK;
    if (depth > 32) { return CE_FAIL; }
    if (!is_dir) { return fm_copy_file(src, dst); }
    plat_mkdir(dst); /* CE_BUSY if it already exists -> merge into it */
    d = plat_opendir(src);
    if (d == NULL) { return CE_IO; }
    while (plat_readdir(d, &e)) {
        char s2[CASTALIA_MAX_PATH], d2[CASTALIA_MAX_PATH];
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        ui_path_join(s2, sizeof(s2), src, e.name);
        ui_path_join(d2, sizeof(d2), dst, e.name);
        if (fm_copy_tree(s2, d2, e.is_dir, depth + 1) != CE_OK) { rc = CE_IO; }
    }
    plat_closedir(d);
    return rc;
}

static void fm_clip_set(FileMan *fm, cbool cut)
{
    FmEntry *e;
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    e = &fm->ent[fm->sel];
    if (strcmp(e->name, "..") == 0) { return; }
    g_clip.active = CTRUE;
    g_clip.cut = cut;
    g_clip.is_dir = e->is_dir;
    ui_path_join(g_clip.path, sizeof(g_clip.path), fm->path, e->name);
    sys_snprintf(fm->status, sizeof(fm->status), "%s '%s'",
                 cut ? "Cut" : "Copied", e->name);
}

/* Perform the paste into the current directory (overwrite already resolved). */
static void fm_do_paste(FileMan *fm)
{
    const char *base;
    char dst[CASTALIA_MAX_PATH];
    CResult r;
    if (!g_clip.active) { return; }
    base = ui_path_base(g_clip.path);
    ui_path_join(dst, sizeof(dst), fm->path, base);
    if (g_clip.cut) {
        /* A same-volume rename moves either a file or a whole directory. */
        r = plat_file_rename(g_clip.path, dst);
        if (r != CE_OK && !g_clip.is_dir) {
            /* Cross-volume file move: copy then delete. (Cross-volume
             * directory moves are left as a limitation.) */
            r = fm_copy_file(g_clip.path, dst);
            if (r == CE_OK) { plat_file_remove(g_clip.path); }
        }
        if (r == CE_OK) { g_clip.active = CFALSE; }
        fm_load(fm, fm->path);
        sys_snprintf(fm->status, sizeof(fm->status),
                     (r == CE_OK) ? "Moved '%s'" : "Move of '%s' failed", base);
    } else {
        r = fm_copy_tree(g_clip.path, dst, g_clip.is_dir, 0);
        fm_load(fm, fm->path);
        sys_snprintf(fm->status, sizeof(fm->status),
                     (r == CE_OK) ? "Copied '%s'" : "Copy of '%s' failed", base);
    }
}

/* ---- dialog callbacks ------------------------------------------------ */
static void on_delete_confirm(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (result == UI_DR_YES && fm != NULL) {
        fm_trash_sel(fm);
        wm_invalidate(win, NULL);
    }
}

static void on_overwrite(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (result == UI_DR_YES && fm != NULL) {
        fm_do_paste(fm);
        wm_invalidate(win, NULL);
    }
}

/*
 * Rename Many: two prompts, then the whole batch or none of it.
 *
 * F2 renames one file. Renaming two hundred photos one at a time is the job a
 * computer should be doing, and `ren *.JPG HOLIDAY*` is how DOS always spelled
 * it -- so the same patterns work here as at the console, through the same
 * ren_batch.c, and neither can drift into allowing a batch the other refuses.
 *
 * Asking twice rather than parsing one line keeps the two patterns apart
 * without inventing a quoting rule for names with spaces in them.
 */
static void on_renmany_to(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    RenBatch r;
    if (!ok || fm == NULL || text == NULL || text[0] == '\0') { return; }
    {
        cbool done = ren_batch_run(fm->path, fm->ren_from, text, &r);
        fm_load(fm, fm->path);   /* ...which writes its own status line */
        if (done) {
            sys_snprintf(fm->status, sizeof(fm->status),
                         "%d file(s) renamed to '%s'", r.renamed, text);
        } else {
            sys_strlcpy(fm->status, r.why, sizeof(fm->status));
        }
    }
    wm_invalidate(win, NULL);
}

static void on_renmany_from(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (!ok || fm == NULL || text == NULL || text[0] == '\0') { return; }
    sys_strlcpy(fm->ren_from, text, sizeof(fm->ren_from));
    ui_prompt("Rename Many", "...to this pattern:", "*.BAK",
              on_renmany_to, win);
}

static void on_rename(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    FmEntry *e;
    char why[112];
    if (!ok || fm == NULL || text[0] == '\0') { return; }
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    e = &fm->ent[fm->sel];
    /*
     * Through ren_batch_one, which REFUSES to rename onto a file that is
     * already there. plat_file_rename goes straight to the C library and
     * overwrites without complaint, so typing the name of the file next to
     * this one destroyed it -- and the status line said "Renamed to 'X'",
     * which is what it says when nothing went wrong.
     */
    {
        cbool done = ren_batch_one(fm->path, e->name, text, why, sizeof why);
        /* fm_load writes its own status line, so ours goes AFTER it -- the
         * other way round the reload silently replaces the answer. */
        fm_load(fm, fm->path);
        if (done) {
            sys_snprintf(fm->status, sizeof(fm->status), "Renamed to '%s'", text);
        } else {
            sys_strlcpy(fm->status, why, sizeof(fm->status));
        }
    }
    wm_invalidate(win, NULL);
}

static void on_newfolder(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (ok && fm != NULL) {
        fm_create_folder(fm, text);
        wm_invalidate(win, NULL);
    }
}

static void on_find(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (ok && fm != NULL && text[0] != '\0') {
        fm_run_search(fm, text);
        wm_invalidate(win, NULL);
    }
}

/* Address bar: navigate to a typed path if it is an openable directory. */
static void on_navigate(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    FileMan *fm = fm_active((FileMan *)wm_user(win));
    if (ok && fm != NULL && text[0] != '\0') {
        PlatDir *d = plat_opendir(text);
        if (d != NULL) {
            plat_closedir(d);
            fm_load(fm, text);
        } else {
            sys_snprintf(fm->status, sizeof(fm->status), "Cannot open %s", text);
        }
        wm_invalidate(win, NULL);
    }
}

/* ---- operation entry points (open dialogs) --------------------------- */
static void fm_ask_delete(WmWindow *win, FileMan *fm)
{
    FmEntry *e;
    char msg[96];
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    e = &fm->ent[fm->sel];
    if (strcmp(e->name, "..") == 0) { return; }
    sys_snprintf(msg, sizeof(msg), "Move '%s' to Trash?", e->name);
    ui_msgbox("Delete", msg, UI_MB_YESNO, on_delete_confirm, win);
}

static void fm_ask_rename(WmWindow *win, FileMan *fm)
{
    FmEntry *e;
    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    e = &fm->ent[fm->sel];
    if (strcmp(e->name, "..") == 0) { return; }
    ui_prompt("Rename", "New name:", e->name, on_rename, win);
}

static void fm_paste(WmWindow *win, FileMan *fm)
{
    const char *base;
    char dst[CASTALIA_MAX_PATH];
    if (!g_clip.active) {
        sys_snprintf(fm->status, sizeof(fm->status), "Clipboard is empty");
        wm_invalidate(win, NULL);
        return;
    }
    base = ui_path_base(g_clip.path);
    ui_path_join(dst, sizeof(dst), fm->path, base);
    if (sys_stricmp(dst, g_clip.path) == 0) {
        sys_snprintf(fm->status, sizeof(fm->status),
                     "Source and destination are the same");
        wm_invalidate(win, NULL);
        return;
    }
    if (plat_file_exists(dst)) {
        char msg[96];
        sys_snprintf(msg, sizeof(msg), "Overwrite '%s'?", base);
        ui_msgbox("Paste", msg, UI_MB_YESNO, on_overwrite, win);
        return;
    }
    fm_do_paste(fm);
    wm_invalidate(win, NULL);
}

/* ---- layout ---------------------------------------------------------- */
enum { FB_UP, FB_REFRESH, FB_NEW, FB_RENAME, FB_COPY, FB_CUT, FB_PASTE,
       FB_DELETE, FB_FIND, FB_DUAL, FB_COUNT };
static const char *FB_LABEL[FB_COUNT] = {
    "Up", "Refresh", "New", "Rename", "Copy", "Cut", "Paste", "Delete", "Find",
    "2-Pane"
};
static const int FB_WIDTH[FB_COUNT] = { 34, 58, 38, 56, 46, 36, 48, 54, 40, 52 };

/* Optional toolbar icon per button (NULL = text only). When the active icon
 * pack provides the icon, the button becomes a compact icon-only button. */
static const char *const FB_ICON[FB_COUNT] = {
    "tb-up", "tb-refresh", "tb-newfolder", "tb-rename", "tb-copy", "tb-cut",
    "tb-paste", "tb-delete", "tb-find", "tb-dual"
};
#define FM_ICON_BTN_W 24   /* compact width for an icon-only toolbar button */

/* The pack icon for button i, or NULL (no icon mapped, or pack lacks it). */
static const GfxSurface *fm_btn_icon(int i)
{
    return (FB_ICON[i] != 0) ? sh_iconpack_icon(FB_ICON[i]) : (const GfxSurface *)0;
}

/* Draw one toolbar button: an icon-only button when the pack supplies its icon,
 * else the classic text button. */
static void fm_draw_toolbar_btn(GfxSurface *s, const CRect *br, int i, int state)
{
    const GfxSurface *ic = fm_btn_icon(i);
    if (ic != (const GfxSurface *)0) {
        CRect src = crect_make(0, 0, ic->w, ic->h);
        ui_draw_button(s, br, "", state);
        gfx_blit(s, (br->x0 + br->x1) / 2 - ic->w / 2,
                    (br->y0 + br->y1) / 2 - ic->h / 2, ic, &src, GFX_BLIT_KEYED);
    } else {
        ui_draw_button(s, br, FB_LABEL[i], state);
    }
}

typedef struct {
    CRect menubar;
    CRect toolbar;
    CRect btn[FB_COUNT];
    CRect pathbar;      /* address strip (also the top of the body panes) */
    CRect addr_field;   /* the sunken white path field inside the strip   */
    CRect go_btn;
    CRect taskpane;     /* left XP explorer bar (single-pane mode only)    */
    CRect header, list, vbar, status;
    int   view_i;       /* the "View" menu word rect (index into menu)     */
    int   visible_rows;
    int   row_h;
} FmLayout;

/* The row height for the active view (Tiles uses taller rows). */
static int fm_row_h(const FileMan *fm) { return fm->view == 1 ? 34 : FM_ROW_H; }

/* Icons (grid) view cell metrics for a given list well. */
#define FM_CELL_W 82
#define FM_CELL_H 62
#define FM_THUMB  30   /* thumbnail box inside an Icons cell        */
static void fm_grid_dims(const CRect *list, int *cols, int *rows_vis)
{
    int c = crect_w(list) / FM_CELL_W;
    int r = crect_h(list) / FM_CELL_H;
    *cols = (c < 1) ? 1 : c;
    *rows_vis = (r < 1) ? 1 : r;
}

/* Keep the selection visible in grid view, snapping 'top' to a row boundary. */
static void fm_scroll_grid(FileMan *fm, int cols, int rows_vis)
{
    int vis = cols * rows_vis;
    if (cols < 1) { cols = 1; }
    if (fm->sel < fm->top) { fm->top = (fm->sel / cols) * cols; }
    if (fm->sel >= fm->top + vis) { fm->top = ((fm->sel - vis) / cols + 1) * cols; }
    if (fm->top < 0) { fm->top = 0; }
}

/* Compute client-relative rectangles for the given client size. */
static void fm_layout(const FileMan *fm, int cw, int ch, FmLayout *L)
{
    int x = 4, i, body_y, body_h;
    L->menubar = crect_make(0, 0, cw, FM_MENUBAR_H);
    L->toolbar = crect_make(0, FM_MENUBAR_H, cw, FM_TOOLBAR_H);
    for (i = 0; i < FB_COUNT; i++) {
        int w = (fm_btn_icon(i) != (const GfxSurface *)0) ? FM_ICON_BTN_W : FB_WIDTH[i];
        L->btn[i] = crect_make(x, FM_MENUBAR_H + 3, w, FM_TOOLBAR_H - 6);
        x += w + 3;
    }
    L->pathbar = crect_make(0, FM_MENUBAR_H + FM_TOOLBAR_H, cw, FM_PATHBAR_H);
    /* Address field: after an "Address" caption; a Go button on the right. */
    L->addr_field = crect_make(58, L->pathbar.y0 + 2, cw - 58 - 34, FM_PATHBAR_H - 4);
    L->go_btn     = crect_make(cw - 32, L->pathbar.y0 + 2, 28, FM_PATHBAR_H - 4);

    body_y = L->pathbar.y0 + FM_PATHBAR_H;
    body_h = ch - body_y - FM_STATUS_H;
    if (body_h < FM_ROW_H) { body_h = FM_ROW_H; }

    if (fm != NULL && fm->dual && fm->mate != NULL) {
        /* Two-pane mode: no task pane; the panes fill the whole body. */
        L->taskpane = crect_make(0, body_y, 0, body_h);
        L->header   = crect_make(0, body_y, cw, 0);
        L->list     = crect_make(0, body_y, cw - UI_SB_W, body_h);
        L->vbar     = crect_make(cw - UI_SB_W, body_y, UI_SB_W, body_h);
        L->row_h    = FM_ROW_H;
        L->visible_rows = body_h / FM_ROW_H;
    } else {
        int lx = FM_TASKPANE_W;
        int hh = (fm != NULL && fm->view == 2) ? 0 : FM_HEADER_H; /* Icons: no header */
        L->taskpane = crect_make(0, body_y, FM_TASKPANE_W, body_h);
        L->header   = crect_make(lx, body_y, cw - lx, hh);
        {
            int list_y = body_y + hh;
            int list_h = ch - list_y - FM_STATUS_H;
            int rh = fm ? fm_row_h(fm) : FM_ROW_H;
            if (list_h < rh) { list_h = rh; }
            L->list = crect_make(lx, list_y, cw - lx - UI_SB_W, list_h);
            L->vbar = crect_make(cw - UI_SB_W, list_y, UI_SB_W, list_h);
            L->row_h = rh;
            L->visible_rows = list_h / rh;
        }
    }
    L->status = crect_make(0, ch - FM_STATUS_H, cw, FM_STATUS_H);
    L->view_i = 0;
}

/*
 * The toolbar strip, in SCREEN coordinates: the buttons AND the address bar
 * under them, because Go lives down there and is numbered into the same
 * UiHot. Two call sites -- the pointer arriving on a button and the pointer
 * letting one go -- and only the first of them used to narrow.
 */
static void fm_invalidate_toolbar(WmWindow *win, const FileMan *fm)
{
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    FmLayout L;
    CRect tb;
    fm_layout(fm, crect_w(&client), crect_h(&client), &L);
    tb = crect_make(0, L.toolbar.y0, crect_w(&client),
                    L.go_btn.y1 - L.toolbar.y0 + 1);
    tb = crect_offset(&tb, o.x, o.y);
    wm_invalidate(win, &tb);
}

static void fm_scroll_to_sel(FileMan *fm, int visible)
{
    if (visible < 1) { visible = 1; }
    if (fm->sel < fm->top) { fm->top = fm->sel; }
    if (fm->sel >= fm->top + visible) { fm->top = fm->sel - visible + 1; }
    if (fm->top < 0) { fm->top = 0; }
}

/* ---- painting -------------------------------------------------------- */
/* A richer folder / document glyph, XP-flavored, drawn in a 'sz'-scaled box
 * whose top-left is (x,y). sz 16 = list icon, sz 32 = tiles/large icon. */
static void draw_entry_icon(GfxSurface *s, int x, int y, cbool is_dir)
{
    if (is_dir) {
        /* Manila folder with a lighter open lip and a soft shadow line. */
        CColor body_top = GFX_RGB(0xFF, 0xE7, 0x9B);
        CColor body_bot = GFX_RGB(0xF0, 0xC2, 0x50);
        CColor edge      = GFX_RGB(0xB8, 0x8A, 0x22);
        CRect tab  = crect_make(x + 1, y + 2, 6, 3);
        CRect body = crect_make(x, y + 4, 15, 9);
        gfx_fill_rect(s, &tab, body_bot);
        gfx_frame_rect(s, &tab, edge);
        gfx_vgradient(s, &body, body_top, body_bot);
        gfx_frame_rect(s, &body, edge);
        gfx_hline(s, x + 1, y + 5, 13, GFX_RGB(0xFF, 0xF6, 0xD0));
    } else {
        /* White page with a dog-eared corner and a couple of text ticks. */
        CColor paper = GFX_RGB(0xFF, 0xFF, 0xFF);
        CColor edge  = GFX_RGB(0x78, 0x82, 0x92);
        CColor fold  = GFX_RGB(0xC8, 0xD2, 0xE2);
        CRect body = crect_make(x + 2, y + 1, 11, 13);
        gfx_fill_rect(s, &body, paper);
        gfx_frame_rect(s, &body, edge);
        {
            CRect dog = crect_make(x + 10, y + 1, 3, 3);
            gfx_fill_rect(s, &dog, fold);
            gfx_line(s, x + 10, y + 1, x + 12, y + 3, edge);
        }
        gfx_hline(s, x + 4, y + 6, 6, GFX_RGB(0xA9, 0xB4, 0xC6));
        gfx_hline(s, x + 4, y + 8, 6, GFX_RGB(0xA9, 0xB4, 0xC6));
        gfx_hline(s, x + 4, y + 10, 4, GFX_RGB(0xA9, 0xB4, 0xC6));
    }
}

/* A larger 28px folder/document for the Tiles view. */
static void draw_entry_icon_big(GfxSurface *s, int x, int y, cbool is_dir)
{
    if (is_dir) {
        CColor top = GFX_RGB(0xFF, 0xE7, 0x9B), bot = GFX_RGB(0xEE, 0xBB, 0x3E);
        CColor edge = GFX_RGB(0xA8, 0x7C, 0x18);
        CRect tab  = crect_make(x + 2, y + 3, 12, 5);
        CRect body = crect_make(x, y + 6, 28, 18);
        gfx_fill_rect(s, &tab, bot);
        gfx_frame_rect(s, &tab, edge);
        gfx_vgradient(s, &body, top, bot);
        gfx_frame_rect(s, &body, edge);
        gfx_hline(s, x + 1, y + 8, 26, GFX_RGB(0xFF, 0xF6, 0xD0));
    } else {
        CColor paper = GFX_RGB(0xFF, 0xFF, 0xFF), edge = GFX_RGB(0x78, 0x82, 0x92);
        CRect body = crect_make(x + 4, y + 1, 20, 24);
        int i;
        gfx_fill_rect(s, &body, paper);
        gfx_frame_rect(s, &body, edge);
        {
            CRect dog = crect_make(x + 19, y + 1, 5, 5);
            gfx_fill_rect(s, &dog, GFX_RGB(0xCC, 0xD6, 0xE6));
            gfx_line(s, x + 19, y + 1, x + 23, y + 5, edge);
        }
        for (i = 0; i < 5; i++) {
            gfx_hline(s, x + 7, y + 8 + i * 3, (i == 4) ? 7 : 13,
                      GFX_RGB(0xAC, 0xB7, 0xC9));
        }
    }
}

/* ---- XP "Explorer bar" task pane -------------------------------------- */
enum { LK_HEADER = -100 };
static const struct { int cmd; const char *label; } FM_TASKS[] = {
    { LK_HEADER, "File and Folder Tasks" },
    { FB_NEW,     "Make a new folder" },
    { FB_RENAME,  "Rename this item" },
    { FB_COPY,    "Copy this item" },
    { FB_PASTE,   "Paste item here" },
    { FB_DELETE,  "Delete this item" },
    { LK_HEADER, "Other Places" },
    { FB_UP,      "Up one level" },
    { FB_FIND,    "Search this folder" },
    { FB_REFRESH, "Refresh view" }
};
#define FM_TASK_COUNT ((int)(sizeof(FM_TASKS) / sizeof(FM_TASKS[0])))

/* Lay out one row rect per FM_TASKS entry inside 'pane' (surface coords).
 * Headers get a full-width band; links get an indented row. */
static void fm_taskpane_rows(const CRect *pane, CRect *rows)
{
    int i, y = pane->y0 + 6;
    for (i = 0; i < FM_TASK_COUNT; i++) {
        if (FM_TASKS[i].cmd == LK_HEADER) {
            if (i > 0) { y += 8; }
            rows[i] = crect_make(pane->x0 + 6, y, crect_w(pane) - 12, 18);
            y += 18;
        } else {
            rows[i] = crect_make(pane->x0 + 12, y, crect_w(pane) - 18, 14);
            y += 15;
        }
    }
}

/* ---- dual-pane (commander) mode -------------------------------------- */
static void fm_toggle_dual(WmWindow *win, FileMan *fm)
{
    if (!fm->dual) {
        FileMan *m = (FileMan *)sys_calloc(1, (cu32)sizeof(FileMan));
        if (m == NULL) { return; }
        m->last_click_row = -1;
        tc_init(&m->thumbs);
        m->thumbs_on = CTRUE;
        fm_load(m, fm->path); /* the new pane opens where the owner is */
        fm->mate = m;
        fm->dual = CTRUE;
        fm->active = 0;
        sys_strlcpy(fm->status, "Two-pane mode (Tab switches panes)",
                    sizeof(fm->status));
    } else {
        if (fm->mate != NULL) {
            tc_free(&fm->mate->thumbs);
            sys_free(fm->mate, (cu32)sizeof(FileMan));
        }
        fm->mate = NULL;
        fm->dual = CFALSE;
        fm->active = 0;
        sys_strlcpy(fm->status, "Single-pane mode", sizeof(fm->status));
    }
    SYS_LOGI("app", "fileman: two-pane mode %s", fm->dual ? "ON" : "OFF");
    wm_invalidate(win, NULL);
}

/* Draw one pane (path caption + entry list) into 'well' (surface coords). */
static void fm_draw_pane(GfxSurface *s, CRect well, FileMan *pane, cbool active)
{
    const UiPalette *p = ui_palette();
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CRect cap = crect_make(well.x0, well.y0, crect_w(&well), 14);
    CRect lw = crect_make(well.x0, well.y0 + 14, crect_w(&well), crect_h(&well) - 14);
    int rows = (crect_h(&lw) - 2) / FM_ROW_H;
    int i, maxc;

    gfx_bevel(s, &cap, GFX_BEVEL_RAISED_THIN, p->light, p->dark,
              active ? p->accent : p->face);
    gfx_draw_text(s, GFX_FONT_SYSTEM, cap.x0 + 3, cap.y0 + 3, pane->path,
                  active ? p->accent_text : p->text);
    gfx_bevel(s, &lw, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);

    maxc = (crect_w(&lw) - 26) / 6;
    if (maxc < 1) { maxc = 1; }
    if (maxc > 48) { maxc = 48; }
    for (i = 0; i < rows; i++) {
        int idx = pane->top + i;
        int ry = lw.y0 + 1 + i * FM_ROW_H;
        FmEntry *e;
        CColor tcol = p->text;
        char nm[64];
        if (idx >= pane->count) { break; }
        e = &pane->ent[idx];
        if (idx == pane->sel) {
            CRect hl = crect_make(lw.x0 + 1, ry, crect_w(&lw) - 2, FM_ROW_H);
            gfx_fill_rect(s, &hl, active ? p->accent : p->dark);
            tcol = active ? p->accent_text : p->light;
        }
        draw_entry_icon(s, lw.x0 + 5, ry + 1, e->is_dir);
        sys_strlcpy(nm, e->name, (cu32)(maxc + 1));
        gfx_draw_text(s, GFX_FONT_SYSTEM, lw.x0 + 22, ry + 2, nm, tcol);
    }
}

/* ---- XP chrome helpers ----------------------------------------------- */
static const char *FM_MENU[] = { "File", "Edit", "View", "Favorites",
                                 "Tools", "Help" };
#define FM_MENU_COUNT 6

/* Clickable rect of menu word 'idx' within menubar 'mb' (any coord space). */
static CRect fm_menu_word(const CRect *mb, int idx)
{
    int i, x = mb->x0 + 4;
    for (i = 0; i < idx; i++) {
        x += gfx_text_width(GFX_FONT_SYSTEM, FM_MENU[i]) + 14;
    }
    return crect_make(x, mb->y0, gfx_text_width(GFX_FONT_SYSTEM, FM_MENU[idx]) + 12,
                      crect_h(mb));
}

static void fm_draw_menubar(GfxSurface *s, const CRect *mb)
{
    const UiPalette *p = ui_palette();
    int i;
    gfx_bevel(s, mb, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < FM_MENU_COUNT; i++) {
        CRect w = fm_menu_word(mb, i);
        gfx_draw_text(s, GFX_FONT_SYSTEM, w.x0 + 6, mb->y0 + 4, FM_MENU[i], p->text);
    }
}

/* The address strip: an "Address" caption, a sunken white field with a folder
 * glyph and the current path, and a raised "Go" button. */
static void fm_draw_addressbar(GfxSurface *s, const FmLayout *L, int ox, int oy,
                               FileMan *fm)
{
    const UiPalette *p = ui_palette();
    CRect strip = crect_offset(&L->pathbar, ox, oy);
    CRect field = crect_offset(&L->addr_field, ox, oy);
    CRect go    = crect_offset(&L->go_btn, ox, oy);
    CRect tr;
    char pb[CASTALIA_MAX_PATH + 48];
    gfx_bevel(s, &strip, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    gfx_draw_text(s, GFX_FONT_SYSTEM, strip.x0 + 8, strip.y0 + 6, "Address",
                  p->text);
    gfx_bevel(s, &field, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    draw_entry_icon(s, field.x0 + 3, field.y0 + 1, CTRUE);
    tr = field; tr.x0 += 22; tr.x1 -= 4;
    if (fm->search_mode) {
        sys_snprintf(pb, sizeof(pb), "Search '%s' in %s", fm->query, fm->path);
    } else {
        sys_strlcpy(pb, fm->path, sizeof(pb));
    }
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, pb, GFX_RGB(0x10, 0x10, 0x10),
                       GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    /* Numbered past the toolbar so one UiHot covers both strips: the Go
     * button is the only other push button in this window. */
    ui_draw_button(s, &go, "Go",
                   ui_hot_state(&fm->hot_tb, FB_COUNT, UI_BTN_NORMAL));
}

/*
 * A tiny filled diamond bullet with top-left at (x,y). The heights run
 * 1,2,2,1, which is symmetric -- this is a diamond and not, as the comment
 * here used to claim, a right-pointing arrow. Rendered and looked at before
 * deciding which of the two to change: at four pixels the diamond reads
 * cleanly against the task-pane blue, so the art stayed and the description
 * was corrected. Changing artwork that looks right to match a stale comment
 * is the wrong way round.
 */
static void fm_link_bullet(GfxSurface *s, int x, int y, CColor col)
{
    int i;
    for (i = 0; i < 4; i++) {
        int h = (i < 2) ? (i + 1) : (4 - i);   /* 1,2,2,1 -> a small triangle */
        gfx_vline(s, x + i, y + 3 - h, 2 * h, col);
    }
}

/* Draw the XP explorer bar (task pane) into 'pane' (surface coords). */
static void fm_draw_taskpane(GfxSurface *s, const CRect *pane, FileMan *fm)
{
    CColor bg_top = GFX_RGB(0x5C, 0x80, 0xCE), bg_bot = GFX_RGB(0xA6, 0xBD, 0xEB);
    CColor hd_top = GFX_RGB(0xF8, 0xFB, 0xFF), hd_bot = GFX_RGB(0xC2, 0xD4, 0xF2);
    CColor bd_top = GFX_RGB(0xEC, 0xF2, 0xFD), bd_bot = GFX_RGB(0xCB, 0xDB, 0xF5);
    CColor link   = GFX_RGB(0x11, 0x3A, 0x94);
    CColor hdtext = GFX_RGB(0x0A, 0x24, 0x6A);
    CRect rows[FM_TASK_COUNT];
    int i;
    gfx_vgradient(s, pane, bg_top, bg_bot);
    fm_taskpane_rows(pane, rows);
    for (i = 0; i < FM_TASK_COUNT; i++) {
        if (FM_TASKS[i].cmd == LK_HEADER) {
            /* A section: header band + a body panel spanning its links. */
            int j, body_y0 = rows[i].y1, body_y1 = pane->y1 - 6;
            CRect hd, bd;
            for (j = i + 1; j < FM_TASK_COUNT; j++) {
                if (FM_TASKS[j].cmd == LK_HEADER) { body_y1 = rows[j].y0 - 8; break; }
                body_y1 = rows[j].y1 + 4;
            }
            bd = crect_make(rows[i].x0, body_y0, crect_w(&rows[i]), body_y1 - body_y0);
            gfx_fill_round_rect(s, &bd, 5, bd_bot);
            gfx_vgradient(s, &bd, bd_top, bd_bot);
            hd = rows[i];
            gfx_fill_round_rect(s, &hd, 5, hd_bot);
            gfx_vgradient(s, &hd, hd_top, hd_bot);
            gfx_draw_text(s, GFX_FONT_BOLD, hd.x0 + 6, hd.y0 + 5,
                          FM_TASKS[i].label, hdtext);
            /* Chevron circle at the right. */
            gfx_fill_circle(s, hd.x1 - 10, hd.y0 + 9, 6, GFX_RGB(0x2E, 0x54, 0xA8));
            gfx_line(s, hd.x1 - 13, hd.y0 + 8, hd.x1 - 10, hd.y0 + 11, GFX_RGB(0xFF,0xFF,0xFF));
            gfx_line(s, hd.x1 - 7, hd.y0 + 8, hd.x1 - 10, hd.y0 + 11, GFX_RGB(0xFF,0xFF,0xFF));
        } else {
            fm_link_bullet(s, rows[i].x0, rows[i].y0 + 3, link);
            gfx_draw_text(s, GFX_FONT_SYSTEM, rows[i].x0 + 9, rows[i].y0 + 2,
                          FM_TASKS[i].label, link);
        }
    }
    /* Details footer: the selected item. */
    {
        CRect det = crect_make(pane->x0 + 6, pane->y1 - 56, crect_w(pane) - 12, 50);
        char l1[80];
        gfx_fill_round_rect(s, &det, 5, bd_bot);
        gfx_vgradient(s, &det, bd_top, bd_bot);
        {
            CRect dh = crect_make(det.x0, det.y0, crect_w(&det), 16);
            gfx_fill_round_rect(s, &dh, 5, hd_bot);
            gfx_draw_text(s, GFX_FONT_BOLD, dh.x0 + 6, dh.y0 + 4, "Details", hdtext);
        }
        if (fm->sel >= 0 && fm->sel < fm->count) {
            FmEntry *e = &fm->ent[fm->sel];
            sys_strlcpy(l1, e->name, sizeof(l1));
            gfx_draw_text(s, GFX_FONT_BOLD, det.x0 + 6, det.y0 + 20, l1, hdtext);
            if (e->is_dir) {
                sys_strlcpy(l1, "File Folder", sizeof(l1));
            } else if (e->size >= 0) {
                sys_snprintf(l1, sizeof(l1), "%ld bytes", e->size);
            } else {
                sys_strlcpy(l1, "File", sizeof(l1));
            }
            gfx_draw_text(s, GFX_FONT_SYSTEM, det.x0 + 6, det.y0 + 33, l1, link);
        } else {
            sys_snprintf(l1, sizeof(l1), "%d object(s)", fm->count);
            gfx_draw_text(s, GFX_FONT_SYSTEM, det.x0 + 6, det.y0 + 22, l1, link);
        }
    }
}

/* Defined with the drag-and-drop helpers below; used by both paint paths. */
static void fm_draw_ghost(GfxSurface *s, CPoint o, FileMan *fm);

static void fm_paint_dual(WmWindow *win, GfxSurface *s)
{
    FileMan *fm = (FileMan *)wm_user(win);
    const UiPalette *p = ui_palette();
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    int cw = crect_w(&client), ch = crect_h(&client);
    FmLayout L;
    CRect r;
    int i, top, bot, hgap, halfw;

    fm_layout(fm, cw, ch, &L);
    r = crect_offset(&L.menubar, o.x, o.y);
    fm_draw_menubar(s, &r);
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < FB_COUNT; i++) {
        CRect br = crect_offset(&L.btn[i], o.x, o.y);
        fm_draw_toolbar_btn(s, &br, i,
                            ui_hot_state(&fm->hot_tb, i,
                                         (i == FB_DUAL) ? UI_BTN_PRESSED
                                                        : UI_BTN_NORMAL));
    }

    top = L.pathbar.y0; bot = L.status.y0; hgap = 4;
    halfw = (cw - hgap) / 2;
    {
        CRect left  = crect_make(o.x, o.y + top, halfw, bot - top);
        CRect right = crect_make(o.x + halfw + hgap, o.y + top,
                                 cw - halfw - hgap, bot - top);
        /* Two conditions, not one: the pane has to be the active one AND
         * the window has to have the keyboard. Either alone was drawing a
         * live caption on a window nobody was typing into. */
        cbool live = wm_has_focus(win);
        fm_draw_pane(s, left, fm,
                     (live && fm->active == 0) ? CTRUE : CFALSE);
        fm_draw_pane(s, right, fm->mate,
                     (live && fm->active == 1) ? CTRUE : CFALSE);
    }

    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
    {
        FileMan *act = fm_active(fm);
        CRect tr = r; tr.x0 += 5;
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, act->status, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }

    if (fm->drag_active) { fm_draw_ghost(s, o, fm); }
}

/*
 * The selection band and the ink that goes on it.
 *
 * Live blue when this window has the keyboard, a quiet grey with ordinary
 * ink when it does not. All four views ask here so they cannot disagree, and
 * so the answer is written down once: a highlight that stays live in a window
 * you have clicked away from is telling you your typing lands there, and it
 * does not.
 */
static void fm_sel_band(cbool live, CColor *top, CColor *bot, CColor *ink)
{
    const UiPalette *p = ui_palette();
    if (live) {
        *top = GFX_RGB(0x3C, 0x7C, 0xE0);
        *bot = GFX_RGB(0x27, 0x5F, 0xC6);
        *ink = GFX_RGB(0xFF, 0xFF, 0xFF);
    } else {
        *top = gfx_tint(p->face, p->dark, 70);
        *bot = gfx_tint(p->face, p->dark, 110);
        *ink = p->text;
    }
}

/* Draw one file row in Details view. 'ry' is the row top (surface coords). */
static void fm_draw_row_details(GfxSurface *s, const CRect *lw, int ry,
                                FmEntry *e, cbool sel, cbool live)
{
    int size_x = lw->x1 - 150, type_x = lw->x1 - 82;
    CColor tcol = GFX_RGB(0x10, 0x10, 0x10);
    if (sel) {
        CRect hl = crect_make(lw->x0 + 1, ry, crect_w(lw) - 2, FM_ROW_H);
        CColor top, bot;
        fm_sel_band(live, &top, &bot, &tcol);
        gfx_vgradient(s, &hl, top, bot);
    }
    draw_entry_icon(s, lw->x0 + 4, ry + 1, e->is_dir);
    gfx_draw_text(s, GFX_FONT_SYSTEM, lw->x0 + 24, ry + 4, e->name, tcol);
    if (!e->is_dir) {
        char sz[20];
        if (e->size >= 0) { sys_snprintf(sz, sizeof(sz), "%ld", e->size); }
        else { sys_strlcpy(sz, "-", sizeof(sz)); }
        gfx_draw_text(s, GFX_FONT_SYSTEM, size_x, ry + 4, sz, tcol);
    }
    /* The Type column names what actually opens it, from the same table the
     * double-click uses -- so the two can never disagree. */
    gfx_draw_text(s, GFX_FONT_SYSTEM, type_x, ry + 4,
                  e->is_dir ? "File Folder" : assoc_type_name(e->name), tcol);
}

/* Draw one file row in Tiles view (taller, icon + name + subinfo). */
static void fm_draw_row_tiles(GfxSurface *s, const CRect *lw, int ry,
                              FmEntry *e, cbool sel, cbool live)
{
    const UiPalette *pal = ui_palette();
    CColor tcol = GFX_RGB(0x10, 0x10, 0x10), sub = GFX_RGB(0x50, 0x58, 0x64);
    char info[24];
    if (sel) {
        CRect hl = crect_make(lw->x0 + 1, ry, crect_w(lw) - 2, 33);
        CColor top, bot;
        fm_sel_band(live, &top, &bot, &tcol);
        gfx_vgradient(s, &hl, top, bot);
        sub = live ? GFX_RGB(0xD8, 0xE4, 0xFF)
                   : gfx_tint(pal->text, pal->face, 90);
    }
    draw_entry_icon_big(s, lw->x0 + 6, ry + 2, e->is_dir);
    gfx_draw_text(s, GFX_FONT_BOLD, lw->x0 + 42, ry + 6, e->name, tcol);
    if (e->is_dir) { sys_strlcpy(info, "File Folder", sizeof(info)); }
    else if (e->size >= 0) {
        sys_snprintf(info, sizeof(info), "%s, %ld bytes",
                     assoc_type_name(e->name), e->size);
    } else { sys_strlcpy(info, assoc_type_name(e->name), sizeof(info)); }
    gfx_draw_text(s, GFX_FONT_SYSTEM, lw->x0 + 42, ry + 18, info, sub);
}

/* Draw the Icons (grid) view into the list well 'lw' (surface coords). */
/* Decode one image and shrink it to a thumbnail. Anything that is not a
 * bitmap we can read comes back NULL, and the cache remembers that. */
static GfxSurface *fm_thumb_load(const char *path, int size, void *user)
{
    GfxSurface *img, *small;
    int w, h;
    (void)user;
    if (!ui_path_match_ext(path, "BMP")) { return NULL; }
    img = gfx_bmp_load(path);
    if (img == NULL) { return NULL; }
    /* Fit inside the box, keeping the proportions -- a squashed thumbnail is
     * worse than no thumbnail. */
    if (img->w >= img->h) {
        w = size;
        h = (img->h * size) / (img->w > 0 ? img->w : 1);
    } else {
        h = size;
        w = (img->w * size) / (img->h > 0 ? img->h : 1);
    }
    if (w < 1) { w = 1; }
    if (h < 1) { h = 1; }
    small = pc_scale(img, w, h);
    gfx_surface_free(img);
    return small;
}

/* The thumbnail for an entry, or NULL to fall back to the generic icon. */
static const GfxSurface *fm_thumb_for(FileMan *fm, const FmEntry *e)
{
    char path[CASTALIA_MAX_PATH];
    if (!fm->thumbs_on || e->is_dir) { return NULL; }
    if (!ui_path_match_ext(e->name, "BMP")) { return NULL; }
    ui_path_join(path, (cu32)sizeof path, fm->path, e->name);
    return tc_get(&fm->thumbs, path, FM_THUMB, fm_thumb_load, NULL);
}

static void fm_draw_grid(GfxSurface *s, const CRect *lw, FileMan *fm,
                         cbool live)
{
    int cols, rows_vis, i, vis;
    fm_grid_dims(lw, &cols, &rows_vis);
    fm->top -= fm->top % cols;      /* keep 'top' on a row boundary */
    if (fm->top < 0) { fm->top = 0; }
    vis = cols * rows_vis;
    for (i = 0; i < vis; i++) {
        int idx = fm->top + i;
        int col = i % cols, row = i / cols;
        int cx = lw->x0 + 2 + col * FM_CELL_W;
        int cy = lw->y0 + 2 + row * FM_CELL_H;
        FmEntry *e;
        char nm[18];
        int tw;
        if (idx >= fm->count) { break; }
        e = &fm->ent[idx];
        if (idx == fm->sel) {
            const UiPalette *pal = ui_palette();
            CRect hl = crect_make(cx, cy, FM_CELL_W - 4, FM_CELL_H - 4);
            gfx_fill_round_rect(s, &hl, 4,
                                live ? GFX_RGB(0xCC, 0xDD, 0xF6)
                                     : gfx_tint(pal->face, pal->dark, 60));
            gfx_frame_rect(s, &hl,
                           live ? GFX_RGB(0x6F, 0x9D, 0xD9)
                                : gfx_tint(pal->face, pal->dark, 150));
        }
        {
            /* An image shows itself; everything else shows its icon. */
            const GfxSurface *th = fm_thumb_for(fm, e);
            if (th != NULL) {
                CRect src = crect_make(0, 0, th->w, th->h);
                int tx = cx + (FM_CELL_W - 4 - th->w) / 2;
                int ty = cy + 6 + (FM_THUMB - th->h) / 2;
                CRect frame = crect_make(tx - 1, ty - 1, th->w + 2, th->h + 2);
                gfx_frame_rect(s, &frame, GFX_RGB(0x80, 0x80, 0x80));
                gfx_blit(s, tx, ty, th, &src, GFX_BLIT_COPY);
            } else {
                draw_entry_icon_big(s, cx + FM_CELL_W / 2 - 14, cy + 6,
                                    e->is_dir);
            }
        }
        /* Fitted to the cell. Without this the centring below goes negative
         * for any name wider than 82 pixels, and the label runs out of the
         * list on the left, off the window on the right, and into its
         * neighbours in between. */
        ui_text_fit(nm, sizeof(nm), e->name, FM_CELL_W - 6, GFX_FONT_SYSTEM);
        tw = gfx_text_width(GFX_FONT_SYSTEM, nm);
        gfx_draw_text(s, GFX_FONT_SYSTEM, cx + (FM_CELL_W - 4 - tw) / 2, cy + 40,
                      nm, GFX_RGB(0x10, 0x10, 0x10));
    }
}

static void fm_paint(WmWindow *win, GfxSurface *s)
{
    FileMan *fm = (FileMan *)wm_user(win);
    const UiPalette *p = ui_palette();
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    int cw = crect_w(&client);
    int ch = crect_h(&client);
    FmLayout L;
    /* One question, asked once and handed to every view: does this window
     * have the keyboard? */
    cbool live = wm_has_focus(win);
    int i, rh;
    CRect r, lw;
    if (fm == NULL) { return; }
    if (fm->dual && fm->mate != NULL) { fm_paint_dual(win, s); return; }
    fm_layout(fm, cw, ch, &L);
    rh = L.row_h;

    /* Menu bar + toolbar. */
    r = crect_offset(&L.menubar, o.x, o.y);
    fm_draw_menubar(s, &r);
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < FB_COUNT; i++) {
        CRect br = crect_offset(&L.btn[i], o.x, o.y);
        /* Single-pane: nothing is latched. The two-pane paint path draws
         * FB_DUAL pressed; this used to say so with a ternary whose two arms
         * were the same value. */
        fm_draw_toolbar_btn(s, &br, i,
                            ui_hot_state(&fm->hot_tb, i, UI_BTN_NORMAL));
    }

    /* Address bar. */
    fm_draw_addressbar(s, &L, o.x, o.y, fm);

    /* Left XP task pane. */
    r = crect_offset(&L.taskpane, o.x, o.y);
    fm_draw_taskpane(s, &r, fm);

    /* Column header (Details view only). */
    r = crect_offset(&L.header, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    if (fm->view == 0) {
        int size_x = r.x1 - 150, type_x = r.x1 - 82;
        int col_x[3];
        col_x[FM_SORT_NAME] = r.x0 + 24;
        col_x[FM_SORT_SIZE] = size_x;
        col_x[FM_SORT_TYPE] = type_x;
        gfx_draw_text(s, GFX_FONT_BOLD, col_x[FM_SORT_NAME], r.y0 + 4, "Name", p->text);
        gfx_draw_text(s, GFX_FONT_BOLD, col_x[FM_SORT_SIZE], r.y0 + 4, "Size", p->text);
        gfx_draw_text(s, GFX_FONT_BOLD, col_x[FM_SORT_TYPE], r.y0 + 4, "Type", p->text);
        gfx_vline(s, size_x - 8, r.y0 + 3, FM_HEADER_H - 6, p->dark);
        gfx_vline(s, type_x - 8, r.y0 + 3, FM_HEADER_H - 6, p->dark);
        /* Sort indicator: a small up/down triangle after the active column. */
        {
            static const char *LBL[3] = { "Name", "Size", "Type" };
            int ax = col_x[fm->sort_key] + gfx_text_width(GFX_FONT_BOLD, LBL[fm->sort_key]) + 4;
            int ay = r.y0 + 6, j;
            for (j = 0; j < 4; j++) {
                int hw = fm->sort_desc ? j : (3 - j);
                gfx_hline(s, ax + (3 - hw), ay + j, 2 * hw + 1, p->dark);
            }
        }
    } else if (fm->view == 1) {
        gfx_draw_text(s, GFX_FONT_BOLD, r.x0 + 24, r.y0 + 4, "Name", p->text);
    }

    /* List well (white). */
    lw = crect_offset(&L.list, o.x, o.y);
    gfx_bevel(s, &lw, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_RGB(0xFF,0xFF,0xFF));

    if (fm->view == 2) {
        /* A few decodes per frame, then repaint: a folder of a hundred images
         * fills in over a second instead of stalling one long frame. */
        tc_begin_frame(&fm->thumbs, 3);
        fm_draw_grid(s, &lw, fm, live);
        if (tc_pending(&fm->thumbs) > 0) { wm_invalidate(win, NULL); }
    } else {
        for (i = 0; i < L.visible_rows; i++) {
            int idx = fm->top + i;
            int ry = lw.y0 + 1 + i * rh;
            FmEntry *e;
            if (idx >= fm->count) { break; }
            e = &fm->ent[idx];
            if (fm->view == 1) {
                fm_draw_row_tiles(s, &lw, ry, e, idx == fm->sel, live);
            } else {
                fm_draw_row_details(s, &lw, ry, e, idx == fm->sel, live);
            }
        }
    }

    /* Scrollbar indicator when the list overflows (row-based views only). */
    if (fm->view != 2 && fm->count > L.visible_rows && L.visible_rows > 0) {
        int track_x = lw.x1 - 6;
        int track_h = crect_h(&lw) - 2;
        int thumb_h = (L.visible_rows * track_h) / fm->count;
        int thumb_y;
        CRect track, thumb;
        if (thumb_h < 8) { thumb_h = 8; }
        thumb_y = lw.y0 + 1 + (fm->top * (track_h - thumb_h)) /
                  (fm->count - L.visible_rows);
        track = crect_make(track_x, lw.y0 + 1, 5, track_h);
        thumb = crect_make(track_x, thumb_y, 5, thumb_h);
        gfx_fill_rect(s, &track, p->face);
        gfx_bevel(s, &thumb, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    }

    /* Status bar: two cells (item count / free-form status). */
    /* The list's scroll bar: it shows how far through the folder you are. */
    r = crect_offset(&L.vbar, o.x, o.y);
    ui_scrollbar_draw(s, &r, CTRUE, fm->count, L.visible_rows, fm->top,
                      fm->hot_sb);

    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    {
        int split = r.x0 + crect_w(&r) * 2 / 3;
        CRect cell1 = crect_make(r.x0 + 2, r.y0 + 2, split - r.x0 - 4, crect_h(&r) - 4);
        CRect cell2 = crect_make(split + 2, r.y0 + 2, r.x1 - split - 4, crect_h(&r) - 4);
        CRect tr;
        char cnt[32];
        gfx_bevel(s, &cell1, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
        gfx_bevel(s, &cell2, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
        sys_snprintf(cnt, sizeof(cnt), "%d object(s)", fm->count);
        tr = cell1; tr.x0 += 5;
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, cnt, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
        tr = cell2; tr.x0 += 5;
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, fm->status, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }

    /* Right-click context menu overlay (drawn last, over everything). */
    if (fm->ctx_open) {
        int mx = o.x + fm->ctx_x, my = o.y + fm->ctx_y, mw, mh;
        CRect mr;
        ui_menu_measure(&fm->ctx_menu, GFX_FONT_SYSTEM, &mw, &mh);
        mr = crect_make(mx, my, mw, mh);
        gfx_drop_shadow(s, &mr, 4, 90);
        ui_menu_draw(s, &fm->ctx_menu, mx, my, GFX_FONT_SYSTEM);
    }

    if (fm->drag_active) { fm_draw_ghost(s, o, fm); }
}

/* ---- input ----------------------------------------------------------- */
static void fm_toolbar_action(WmWindow *win, FileMan *fm, int id)
{
    FileMan *act = fm_active(fm); /* toolbar acts on the active pane */
    switch (id) {
    case FB_UP: {
        char np[CASTALIA_MAX_PATH];
        ui_path_parent_rel(act->path, np, sizeof(np));
        fm_load(act, np); /* also exits search mode */
        wm_invalidate(win, NULL);
        break;
    }
    case FB_REFRESH:
        fm_refresh(act);
        wm_invalidate(win, NULL);
        break;
    case FB_FIND:
        ui_prompt("Find Files", "Name or text inside (case-insensitive):",
                  act->query,
                  on_find, win);
        break;
    case FB_NEW:
        ui_prompt("New Folder", "Folder name:", "New Folder", on_newfolder, win);
        break;
    case FB_RENAME: fm_ask_rename(win, act); break;
    case FB_COPY:   fm_clip_set(act, CFALSE); wm_invalidate(win, NULL); break;
    case FB_CUT:    fm_clip_set(act, CTRUE);  wm_invalidate(win, NULL); break;
    case FB_PASTE:  fm_paste(win, act); break;
    case FB_DELETE: fm_ask_delete(win, act); break;
    case FB_DUAL:   fm_toggle_dual(win, fm); break;
    default: break;
    }
}

/* Cycle the view mode (Details <-> Tiles). */
static void fm_toggle_view(WmWindow *win, FileMan *fm)
{
    fm->view = (fm->view + 1) % 3;
    fm->top = 0;
    sys_snprintf(fm->status, sizeof(fm->status), "%s view",
                 fm->view == 0 ? "Details" : fm->view == 1 ? "Tiles" : "Icons");
    wm_invalidate(win, NULL);
}

/* Re-sort the current listing by a column, toggling direction if unchanged.
 * Selection follows the item by name across the re-order. */
static void fm_apply_sort(FileMan *fm, int key, int visible)
{
    char selname[CASTALIA_MAX_NAME];
    int i;
    if (fm->search_mode) { return; }   /* search results keep discovery order */
    selname[0] = '\0';
    if (fm->sel >= 0 && fm->sel < fm->count) {
        sys_strlcpy(selname, fm->ent[fm->sel].name, sizeof(selname));
    }
    if (fm->sort_key == key) { fm->sort_desc = !fm->sort_desc; }
    else { fm->sort_key = key; fm->sort_desc = CFALSE; }
    fm_sort(fm);
    for (i = 0; i < fm->count; i++) {
        if (strcmp(fm->ent[i].name, selname) == 0) { fm->sel = i; break; }
    }
    fm_scroll_to_sel(fm, visible);
    sys_snprintf(fm->status, sizeof(fm->status), "Sorted by %s (%s)",
                 key == FM_SORT_SIZE ? "Size" : key == FM_SORT_TYPE ? "Type" : "Name",
                 fm->sort_desc ? "descending" : "ascending");
}

/* ---- right-click context menu (in-window overlay) -------------------- */
#define CTX_OPEN      100  /* menu ids that do not clash with FB_* (0..FB_COUNT) */
#define CTX_PROPS     101  /* properties of the selected item                    */
#define CTX_PROPS_DIR 102  /* properties of the current folder                   */
#define CTX_DISKUSE   103  /* Disk Usage treemap of the folder under the cursor  */
#define CTX_COMPRESS  104  /* write a .CZ copy of the selected file              */
#define CTX_EXPAND    105  /* restore a .CZ back to its recorded name            */
#define CTX_BACKUP    106  /* pack this folder into one .CAR archive             */
#define CTX_RESTORE   107  /* unpack a .CAR into the folder being browsed        */
#define CTX_HEX       108  /* look at the selected file's actual bytes           */
#define CTX_UNDELETE  109  /* put a binned item back where it came from          */
#define CTX_RENMANY   110  /* rename every file matching a pattern               */
                           /* (not CTX_RESTORE -- that one unpacks a .CAR)       */

/* Full path of the selected entry (honors search-results mode). */
static void fm_sel_fullpath(FileMan *fm, char *dst, cu32 dstsz)
{
    if (fm->sel < 0 || fm->sel >= fm->count) { dst[0] = '\0'; return; }
    if (fm->search_mode) { sys_strlcpy(dst, fm->spath[fm->sel], dstsz); }
    else { ui_path_join(dst, dstsz, fm->path, fm->ent[fm->sel].name); }
}

/* Index of the list row under (px,py), or -1 if not on a row. */
static int fm_row_at(FileMan *fm, const FmLayout *L, int px, int py)
{
    if (!crect_contains(&L->list, px, py)) { return -1; }
    if (fm->view == 2) {
        int cols, rv, col, grow;
        fm_grid_dims(&L->list, &cols, &rv);
        col = (px - L->list.x0 - 2) / FM_CELL_W;
        if (col < 0) { col = 0; }
        if (col >= cols) { col = cols - 1; }
        grow = (py - L->list.y0 - 2) / FM_CELL_H;
        return fm->top + grow * cols + col;
    }
    return fm->top + (py - L->list.y0 - 1) / L->row_h;
}

/* The list scroll bar: arrows step, the trough pages, the thumb drags.
 * Returns CTRUE when the click belonged to the bar. */
static cbool fm_scrollbar_click(WmWindow *win, FileMan *fm, int px, int py)
{
    CRect client = wm_client_rect(win);
    FmLayout L;
    int part;
    fm_layout(fm, crect_w(&client), crect_h(&client), &L);
    if (!crect_contains(&L.vbar, px, py)) { return CFALSE; }
    part = ui_scrollbar_hit(&L.vbar, CTRUE, fm->count, L.visible_rows,
                            fm->top, px, py);
    fm->hot_sb = part;
    switch (part) {
    case UI_SB_LINE_UP:   fm->top--; break;
    case UI_SB_LINE_DOWN: fm->top++; break;
    case UI_SB_PAGE_UP:   fm->top -= L.visible_rows; break;
    case UI_SB_PAGE_DOWN: fm->top += L.visible_rows; break;
    case UI_SB_THUMB:     fm->sb_drag = CTRUE; break;
    default: break;
    }
    if (fm->top > fm->count - L.visible_rows) {
        fm->top = fm->count - L.visible_rows;
    }
    if (fm->top < 0) { fm->top = 0; }
    {   /* the rows and the bar; the chrome around them did not move */
        CPoint o = wm_client_origin(win);
        CRect rr = crect_union(&L.list, &L.vbar);
        rr = crect_offset(&rr, o.x, o.y);
        wm_invalidate(win, &rr);
    }
    return CTRUE;
}

static void fm_open_context(WmWindow *win, FileMan *fm, int px, int py)
{
    CRect client = wm_client_rect(win);
    UiMenu *m = &fm->ctx_menu;
    FmLayout L;
    int row, mw, mh;
    if (fm->dual && fm->mate != NULL) { return; } /* single-pane only */
    fm_layout(fm, crect_w(&client), crect_h(&client), &L);
    row = fm_row_at(fm, &L, px, py);
    ui_menu_clear(m);
    if (row >= 0 && row < fm->count && strcmp(fm->ent[row].name, "..") != 0) {
        FmEntry *e = &fm->ent[row];
        fm->sel = row;
        ui_menu_add(m, CTX_OPEN, e->is_dir ? "Open" : "Open in Notepad", CTRUE);
        /* In the bin, putting it back is the first thing anyone wants -- and
         * it is offered nowhere else, because nowhere else recorded where the
         * item was. The index file itself is bookkeeping, not a deleted
         * thing, so it is not offered a way home. */
        if (fm_in_trash(fm) && !trash_is_index(e->name)) {
            ui_menu_add(m, CTX_UNDELETE, "Restore", CTRUE);
        }
        ui_menu_add_separator(m);
        ui_menu_add(m, FB_CUT, "Cut", CTRUE);
        ui_menu_add(m, FB_COPY, "Copy", CTRUE);
        ui_menu_add(m, FB_PASTE, "Paste", g_clip.active);
        ui_menu_add_separator(m);
        ui_menu_add(m, FB_RENAME, "Rename", CTRUE);
        ui_menu_add(m, CTX_RENMANY, "Rename Many...", CTRUE);
        ui_menu_add(m, FB_DELETE, "Delete", CTRUE);
        ui_menu_add_separator(m);
        if (e->is_dir) {
            ui_menu_add(m, CTX_DISKUSE, "Disk Usage", CTRUE);
            ui_menu_add(m, CTX_BACKUP, "Back Up to .CAR", CTRUE);
        } else if (ui_path_match_ext(e->name, "CAR")) {
            ui_menu_add(m, CTX_RESTORE, "Restore from Archive", CTRUE);
        } else if (ui_path_match_ext(e->name, "CZ")) {
            ui_menu_add(m, CTX_EXPAND, "Decompress", CTRUE);
        } else {
            ui_menu_add(m, CTX_COMPRESS, "Compress", CTRUE);
        }
        if (!e->is_dir) {
            /* Offered for every file, not just the ones nothing else can
             * open: "what is really in this" is the question, and a file
             * that DOES open in Notepad can still be the wrong file. */
            ui_menu_add(m, CTX_HEX, "View Bytes", CTRUE);
        }
        ui_menu_add(m, CTX_PROPS, "Properties", CTRUE);
    } else {
        ui_menu_add(m, FB_PASTE, "Paste", g_clip.active);
        ui_menu_add(m, FB_NEW, "New Folder", CTRUE);
        ui_menu_add(m, CTX_RENMANY, "Rename Many...", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, FB_REFRESH, "Refresh", CTRUE);
        ui_menu_add(m, CTX_DISKUSE, "Disk Usage", CTRUE);
        ui_menu_add(m, CTX_PROPS_DIR, "Properties", CTRUE);
    }
    m->highlight = -1;
    ui_menu_measure(m, GFX_FONT_SYSTEM, &mw, &mh);
    fm->ctx_x = px; fm->ctx_y = py;
    if (fm->ctx_x + mw > crect_w(&client)) { fm->ctx_x = crect_w(&client) - mw; }
    if (fm->ctx_y + mh > crect_h(&client)) { fm->ctx_y = crect_h(&client) - mh; }
    if (fm->ctx_x < 0) { fm->ctx_x = 0; }
    if (fm->ctx_y < 0) { fm->ctx_y = 0; }
    fm->ctx_open = CTRUE;
    wm_invalidate(win, NULL);
}

/*
 * Open the context menu on the row that is selected, from the keyboard.
 *
 * fm_open_context takes a POINT because the mouse has one. A keyboard does
 * not, so this works out where the selected row is drawn and hands over a
 * point inside it -- which keeps one implementation of "what goes on the menu
 * for this item" rather than a second one that could drift out of step with
 * what the mouse offers.
 */
static void fm_ctx_show_selected(WmWindow *win, FileMan *fm)
{
    CRect client = wm_client_rect(win);
    FmLayout L;
    int px, py;

    if (fm->sel < 0 || fm->sel >= fm->count) { return; }
    fm_layout(fm, crect_w(&client), crect_h(&client), &L);
    if (fm->view == 2) {
        int cols, rv, col, grow;
        fm_grid_dims(&L.list, &cols, &rv);
        if (cols < 1) { cols = 1; }
        col  = (fm->sel - fm->top) % cols;
        grow = (fm->sel - fm->top) / cols;
        px = L.list.x0 + 2 + col * FM_CELL_W + FM_CELL_W / 2;
        py = L.list.y0 + 2 + grow * FM_CELL_H + FM_CELL_H / 2;
    } else {
        px = L.list.x0 + 8;
        py = L.list.y0 + 1 + (fm->sel - fm->top) * L.row_h + L.row_h / 2;
    }
    /* A selection scrolled out of view has no point to put the menu on, and
     * a menu pinned to the edge would describe a row nobody can see. */
    if (!crect_contains(&L.list, px, py)) { return; }
    fm_open_context(win, fm, px, py);
    /* Land the highlight on the first thing that can be chosen, so the menu
     * arrives ready to use rather than needing a Down press first. */
    if (ui_menu_step(&fm->ctx_menu, 1)) { wm_invalidate(win, NULL); }
}

/* The status line -- the right-hand cell of the status bar, which carries the
 * last message or, on a fresh listing, how much is in the folder. NULL for a
 * window that is not a File Manager. */
/*
 * One row of the last search's results, as it is shown -- which for a match
 * found INSIDE a file carries the line number. A result count says a search
 * happened; only the rows say what it found.
 */
cbool app_fileman_result(WmWindow *win, int i, char *dst, cu32 cap)
{
    FileMan *fm = (FileMan *)wm_user(win);
    if (dst == NULL || cap == 0u) { return CFALSE; }
    dst[0] = '\0';
    fm = fm_active(fm);
    if (fm == NULL || !fm->search_mode || i < 0 || i >= fm->count) {
        return CFALSE;
    }
    sys_strlcpy(dst, fm->ent[i].name, cap);
    return CTRUE;
}

const char *app_fileman_status(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    return (fm != NULL) ? fm->status : NULL;
}

const char *app_fileman_path(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    return (fm != NULL) ? fm->path : NULL;
}

const char *app_fileman_sel_name(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    if (fm == NULL) { return ""; }
    fm = fm_active(fm);
    if (fm->sel < 0 || fm->sel >= fm->count) { return ""; }
    return fm->ent[fm->sel].name;
}

CRect app_fileman_row_rect(WmWindow *win, int row)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    CRect none = crect_make(0, 0, 0, 0), client, lw;
    FmLayout L;
    CPoint o;
    int rh, i;
    if (fm == NULL) { return none; }
    fm = fm_active(fm);
    client = wm_client_rect(win);
    fm_layout(fm, crect_w(&client), crect_h(&client), &L);
    o = wm_client_origin(win);
    rh = L.row_h;
    i = row - fm->top;
    if (i < 0 || i >= L.visible_rows || row >= fm->count) { return none; }
    lw = crect_offset(&L.list, o.x, o.y);
    return crect_make(lw.x0 + 1, lw.y0 + 1 + i * rh, crect_w(&lw) - 2, rh);
}

int app_fileman_row_count(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    return (fm != NULL) ? fm_active(fm)->count : 0;
}

cbool app_fileman_is_dual(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    return (fm != NULL && fm->dual && fm->mate != NULL) ? CTRUE : CFALSE;
}

int app_fileman_active_pane(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    if (fm == NULL) { return -1; }
    return (fm->dual && fm->mate != NULL) ? fm->active : 0;
}

const char *app_fileman_pane_path(WmWindow *win, int pane)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    if (fm == NULL) { return NULL; }
    if (pane == 0) { return fm->path; }
    if (pane == 1 && fm->dual && fm->mate != NULL) { return fm->mate->path; }
    return NULL;
}

CRect app_fileman_btn_rect(WmWindow *win, int which)
{
    FmLayout L;
    CPoint o;
    CRect cr, none = crect_make(0, 0, 0, 0);
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    if (fm == NULL || which < 0 || which >= FB_COUNT) { return none; }
    cr = wm_client_rect(win);
    fm_layout(fm, crect_w(&cr), crect_h(&cr), &L);
    o = wm_client_origin(win);
    return crect_offset(&L.btn[which], o.x, o.y);
}

int app_fileman_btn_dual(void) { return (int)FB_DUAL; }

/* The label of the context menu's highlighted item, or NULL when no menu is
 * open or nothing is highlighted. The headless driver walks the menu by name
 * with this, rather than by counting Down presses -- a count silently starts
 * choosing the wrong command the moment the menu gains an entry. */
const char *app_fileman_ctx_item(WmWindow *win)
{
    FileMan *fm = (win != NULL) ? (FileMan *)wm_user(win) : NULL;
    if (fm == NULL || !fm->ctx_open) { return NULL; }
    if (!ui_menu_selectable(&fm->ctx_menu, fm->ctx_menu.highlight)) {
        return NULL;
    }
    return fm->ctx_menu.items[fm->ctx_menu.highlight].label;
}

static void fm_ctx_dispatch(WmWindow *win, FileMan *fm, int id)
{
    if (id == CTX_OPEN) { fm_open_sel(win, fm); wm_invalidate(win, NULL); }
    else if (id == CTX_PROPS) {
        char full[CASTALIA_MAX_PATH];
        if (fm->sel >= 0 && fm->sel < fm->count &&
            strcmp(fm->ent[fm->sel].name, "..") != 0) {
            FmEntry *e = &fm->ent[fm->sel];
            fm_sel_fullpath(fm, full, sizeof(full));
            if (full[0] != '\0') { app_props_open(full, e->is_dir, e->size); }
        }
    } else if (id == CTX_HEX) {
        char full[CASTALIA_MAX_PATH];
        if (fm->sel >= 0 && fm->sel < fm->count &&
            !fm->ent[fm->sel].is_dir &&
            strcmp(fm->ent[fm->sel].name, "..") != 0) {
            fm_sel_fullpath(fm, full, sizeof(full));
            if (full[0] != '\0') { app_hex_open(full); }
        }
    } else if (id == CTX_UNDELETE) {
        fm_undelete_sel(win, fm);
        wm_invalidate(win, NULL);
    } else if (id == CTX_RENMANY) {
        ui_prompt("Rename Many", "Rename files matching:", "*.TXT",
                  on_renmany_from, win);
    } else if (id == CTX_PROPS_DIR) {
        app_props_open(fm->path, CTRUE, 0);
    } else if (id == CTX_COMPRESS || id == CTX_EXPAND) {
        /* Compress the selected file next to itself, or restore a .CZ under
         * the name its header remembers. */
        char full[CASTALIA_MAX_PATH];
        if (fm->sel < 0 || fm->sel >= fm->count ||
            fm->ent[fm->sel].is_dir ||
            strcmp(fm->ent[fm->sel].name, "..") == 0) { return; }
        fm_sel_fullpath(fm, full, sizeof(full));
        if (full[0] == '\0') { return; }
        fm_ask_replace(win, fm,
                       (id == CTX_COMPRESS) ? FM_PEND_COMPRESS : FM_PEND_EXPAND,
                       full);
    } else if (id == CTX_BACKUP || id == CTX_RESTORE) {
        char full[CASTALIA_MAX_PATH];
        if (fm->sel < 0 || fm->sel >= fm->count ||
            strcmp(fm->ent[fm->sel].name, "..") == 0) { return; }
        fm_sel_fullpath(fm, full, sizeof(full));
        if (full[0] == '\0') { return; }
        fm_ask_replace(win, fm,
                       (id == CTX_BACKUP) ? FM_PEND_BACKUP : FM_PEND_EXPAND,
                       full);
    } else if (id == CTX_DISKUSE) {
        /* On a picked folder if there is one, otherwise on the folder being
         * browsed -- the same rule Properties uses just above. */
        char full[CASTALIA_MAX_PATH];
        full[0] = '\0';
        if (fm->sel >= 0 && fm->sel < fm->count &&
            fm->ent[fm->sel].is_dir &&
            strcmp(fm->ent[fm->sel].name, "..") != 0) {
            fm_sel_fullpath(fm, full, sizeof(full));
        }
        app_diskuse_open_path(full[0] != '\0' ? full : fm->path);
    } else {
        fm_toolbar_action(win, fm, id);
    }
}

/* ---- drag and drop ---------------------------------------------------- */
/* CTRUE if 'inner' equals 'outer' or is a path beneath it. */
static cbool fm_path_inside(const char *outer, const char *inner)
{
    cu32 ol = sys_strnlen(outer, CASTALIA_MAX_PATH), i;
    if (ol == 0 || sys_strnlen(inner, CASTALIA_MAX_PATH) < ol) { return CFALSE; }
    for (i = 0; i < ol; i++) {
        if (fm_lower((unsigned char)outer[i]) != fm_lower((unsigned char)inner[i])) {
            return CFALSE;
        }
    }
    return (inner[ol] == '\0' || is_sep(inner[ol])) ? CTRUE : CFALSE;
}

/* Move the entry 'row' of 'src' into 'dst_dir'; reload affected panes. */
static void fm_move_into(FileMan *owner, FileMan *src, int row, const char *dst_dir)
{
    FmEntry *e;
    char srcp[CASTALIA_MAX_PATH], dstp[CASTALIA_MAX_PATH];
    CResult r;
    if (src->search_mode || row < 0 || row >= src->count) { return; }
    e = &src->ent[row];
    if (strcmp(e->name, "..") == 0) { return; }
    ui_path_join(srcp, sizeof(srcp), src->path, e->name);
    ui_path_join(dstp, sizeof(dstp), dst_dir, e->name);
    if (sys_stricmp(srcp, dstp) == 0) { return; }               /* same place */
    if (e->is_dir && fm_path_inside(srcp, dst_dir)) {
        sys_strlcpy(owner->status, "Cannot move a folder into itself",
                    sizeof(owner->status));
        return;
    }
    if (plat_file_exists(dstp)) {
        sys_snprintf(owner->status, sizeof(owner->status),
                     "'%s' already exists there", e->name);
        return;
    }
    r = plat_file_rename(srcp, dstp);
    if (r != CE_OK && !e->is_dir) {                 /* cross-volume file move */
        r = fm_copy_file(srcp, dstp);
        if (r == CE_OK) { plat_file_remove(srcp); }
    }
    sys_snprintf(owner->status, sizeof(owner->status),
                 (r == CE_OK) ? "Moved '%s'" : "Move of '%s' failed", e->name);
    fm_load(src, src->path);
    if (owner->dual && owner->mate != NULL) {
        FileMan *other = (src == owner) ? owner->mate : owner;
        fm_load(other, other->path);
    }
}

/* Resolve the drop target at (x,y) and perform the move. */
static void fm_drop(WmWindow *win, FileMan *fm, int x, int y)
{
    CRect client = wm_client_rect(win);
    int cw = crect_w(&client), ch = crect_h(&client);
    FmLayout L;
    FileMan *src;
    fm_layout(fm, cw, ch, &L);
    src = (fm->dual && fm->drag_pane == 1 && fm->mate != NULL) ? fm->mate : fm;

    if (fm->dual && fm->mate != NULL) {
        int top = L.pathbar.y0, bot = L.status.y0, hgap = 4, halfw = (cw - hgap) / 2;
        CRect ll = crect_make(0, top + 14, halfw, bot - top - 14);
        CRect rl = crect_make(halfw + hgap, top + 14, cw - halfw - hgap, bot - top - 14);
        FileMan *dest = NULL;
        CRect dw;
        if (crect_contains(&ll, x, y))      { dest = fm; dw = ll; }
        else if (crect_contains(&rl, x, y)) { dest = fm->mate; dw = rl; }
        if (dest == NULL) { return; }
        if (dest != src) { fm_move_into(fm, src, fm->drag_row, dest->path); return; }
        {   /* same pane: only a drop onto a folder row does anything */
            int row = dest->top + (y - dw.y0 - 1) / FM_ROW_H;
            if (row >= 0 && row < dest->count && row != fm->drag_row) {
                FmEntry *te = &dest->ent[row];
                char dd[CASTALIA_MAX_PATH];
                if (strcmp(te->name, "..") == 0) {
                    ui_path_parent_rel(dest->path, dd, sizeof(dd));
                    fm_move_into(fm, src, fm->drag_row, dd);
                } else if (te->is_dir) {
                    ui_path_join(dd, sizeof(dd), dest->path, te->name);
                    fm_move_into(fm, src, fm->drag_row, dd);
                }
            }
        }
    } else {
        int row = fm_row_at(fm, &L, x, y);
        char dd[CASTALIA_MAX_PATH];
        if (row < 0 || row >= fm->count || row == fm->drag_row) { return; }
        {
            FmEntry *te = &fm->ent[row];
            if (strcmp(te->name, "..") == 0) {
                ui_path_parent_rel(fm->path, dd, sizeof(dd));
                fm_move_into(fm, fm, fm->drag_row, dd);
            } else if (te->is_dir) {
                ui_path_join(dd, sizeof(dd), fm->path, te->name);
                fm_move_into(fm, fm, fm->drag_row, dd);
            }
        }
    }
}

/* Arm a potential drag on entry 'row' of the source pane. */
static void fm_drag_arm(FileMan *owner, FileMan *pane, int row, int pane_idx,
                        int px, int py)
{
    if (row < 0 || row >= pane->count || strcmp(pane->ent[row].name, "..") == 0) {
        owner->drag_armed = CFALSE;
        return;
    }
    owner->drag_armed = CTRUE;
    owner->drag_active = CFALSE;
    owner->drag_row = row;
    owner->drag_pane = pane_idx;
    owner->down_x = px; owner->down_y = py;
    owner->drag_isdir = pane->ent[row].is_dir;
    sys_strlcpy(owner->drag_name, pane->ent[row].name, sizeof(owner->drag_name));
}

/* Draw the drag ghost (a small labeled chip) at the cursor. */
static void fm_draw_ghost(GfxSurface *s, CPoint o, FileMan *fm)
{
    const UiPalette *p = ui_palette();
    int gx = o.x + fm->drag_x + 10, gy = o.y + fm->drag_y - 6;
    int w = gfx_text_width(GFX_FONT_SYSTEM, fm->drag_name) + 26;
    CRect box = crect_make(gx, gy, w, 15);
    gfx_bevel(s, &box, GFX_BEVEL_RAISED_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xE0));
    draw_entry_icon(s, gx + 3, gy + 1, fm->drag_isdir);
    gfx_draw_text(s, GFX_FONT_SYSTEM, gx + 22, gy + 4, fm->drag_name, p->text);
}

/* The toolbar button under (px,py) in client coords, or -1. The owner pane's
 * layout, because there is one toolbar however many panes are under it. */
static int fm_tb_at(FileMan *fm, int cw, int ch, int px, int py)
{
    FmLayout L;
    int i;
    fm_layout(fm, cw, ch, &L);
    for (i = 0; i < FB_COUNT; i++) {
        if (crect_contains(&L.btn[i], px, py)) { return i; }
    }
    if (crect_contains(&L.go_btn, px, py)) { return FB_COUNT; }
    return -1;
}

static void fm_on_click(WmWindow *win, int px, int py)
{
    FileMan *fm = (FileMan *)wm_user(win);
    CRect client = wm_client_rect(win);
    int cw = crect_w(&client);
    int ch = crect_h(&client);
    FmLayout L;
    int i;
    if (fm == NULL) { return; }

    /* A left-click while the context menu is up dispatches or dismisses it. */
    if (fm->ctx_open) {
        int hit = ui_menu_hit(&fm->ctx_menu, fm->ctx_x, fm->ctx_y,
                              GFX_FONT_SYSTEM, px, py);
        fm->ctx_open = CFALSE;
        if (hit >= 0) { fm_ctx_dispatch(win, fm, fm->ctx_menu.items[hit].id); }
        wm_invalidate(win, NULL);
        return;
    }

    fm_layout(fm, cw, ch, &L);

    /* Menu bar: "View" cycles the view mode; other words are inert. */
    if (crect_contains(&L.menubar, px, py)) {
        CRect vw = fm_menu_word(&L.menubar, 2); /* "View" */
        if (crect_contains(&vw, px, py)) { fm_toggle_view(win, fm); }
        return;
    }

    for (i = 0; i < FB_COUNT; i++) {
        if (crect_contains(&L.btn[i], px, py)) {
            (void)ui_hot_press(&fm->hot_tb, i);
            fm_toolbar_action(win, fm, i);
            return;
        }
    }

    /* Address field: type a folder path and navigate. "Go" refreshes. */
    if (crect_contains(&L.addr_field, px, py)) {
        ui_prompt("Address", "Go to folder:", fm->path, on_navigate, win);
        return;
    }
    if (crect_contains(&L.go_btn, px, py)) {
        (void)ui_hot_press(&fm->hot_tb, FB_COUNT);
        fm_toolbar_action(win, fm, FB_REFRESH);
        return;
    }

    /* Column header: click a column to sort by it (Details view). */
    if (fm->view == 0 && !(fm->dual && fm->mate != NULL) &&
        crect_contains(&L.header, px, py)) {
        int size_x = L.header.x1 - 150, type_x = L.header.x1 - 82;
        int key = (px >= type_x - 8) ? FM_SORT_TYPE
                : (px >= size_x - 8) ? FM_SORT_SIZE : FM_SORT_NAME;
        fm_apply_sort(fm, key, L.visible_rows);
        wm_invalidate(win, NULL);
        return;
    }

    /* Left task pane action links (single-pane mode only). */
    if (!(fm->dual && fm->mate != NULL) && crect_contains(&L.taskpane, px, py)) {
        CRect rows[FM_TASK_COUNT];
        fm_taskpane_rows(&L.taskpane, rows);
        for (i = 0; i < FM_TASK_COUNT; i++) {
            if (FM_TASKS[i].cmd != LK_HEADER && crect_contains(&rows[i], px, py)) {
                fm_toolbar_action(win, fm, FM_TASKS[i].cmd);
                return;
            }
        }
        return;
    }

    if (fm->dual && fm->mate != NULL) {
        /* Two columns: pick the pane under the click (activating it), then the
         * row within that pane's list well. Coordinates are client-relative. */
        int top = L.pathbar.y0, bot = L.status.y0, hgap = 4;
        int halfw = (cw - hgap) / 2;
        CRect ll = crect_make(0, top + 14, halfw, bot - top - 14);
        CRect rl = crect_make(halfw + hgap, top + 14, cw - halfw - hgap, bot - top - 14);
        CRect lc = crect_make(0, top, halfw, 14);
        CRect rc = crect_make(halfw + hgap, top, cw - halfw - hgap, 14);
        FileMan *pane = NULL;
        CRect lw;
        if (crect_contains(&ll, px, py))      { fm->active = 0; pane = fm; lw = ll; }
        else if (crect_contains(&rl, px, py)) { fm->active = 1; pane = fm->mate; lw = rl; }
        else if (crect_contains(&lc, px, py)) { fm->active = 0; wm_invalidate(win, NULL); return; }
        else if (crect_contains(&rc, px, py)) { fm->active = 1; wm_invalidate(win, NULL); return; }
        if (pane != NULL) {
            int row = pane->top + (py - lw.y0 - 1) / FM_ROW_H;
            if (row >= 0 && row < pane->count) {
                cu32 now = sys_now_ms();
                cbool dbl = (row == pane->last_click_row &&
                             (now - pane->last_click_ms) < FM_DBLCLICK_MS) ? CTRUE : CFALSE;
                pane->sel = row;
                pane->last_click_row = row;
                pane->last_click_ms = now;
                fm_drag_arm(fm, pane, row, fm->active, px, py);
                if (dbl) { fm_open_sel(win, pane); }
            }
            wm_invalidate(win, NULL);
        }
        return;
    }

    if (crect_contains(&L.list, px, py)) {
        int row;
        if (fm->view == 2) {
            int cols, rows_vis, col, grow;
            fm_grid_dims(&L.list, &cols, &rows_vis);
            col = (px - L.list.x0 - 2) / FM_CELL_W;
            grow = (py - L.list.y0 - 2) / FM_CELL_H;
            if (col < 0) { col = 0; }
            if (col >= cols) { col = cols - 1; }
            row = fm->top + grow * cols + col;
        } else {
            row = fm->top + (py - L.list.y0 - 1) / L.row_h;
        }
        if (row >= 0 && row < fm->count) {
            cu32 now = sys_now_ms();
            cbool dbl = (row == fm->last_click_row &&
                         (now - fm->last_click_ms) < FM_DBLCLICK_MS) ? CTRUE : CFALSE;
            fm->sel = row;
            fm->last_click_row = row;
            fm->last_click_ms = now;
            fm_drag_arm(fm, fm, row, 0, px, py);
            if (dbl) { fm_open_sel(win, fm); }
            wm_invalidate(win, NULL);
        }
        return;
    }
}

static void fm_on_key(WmWindow *win, int key)
{
    FileMan *fm = (FileMan *)wm_user(win);
    FileMan *act;
    CRect client = wm_client_rect(win);
    FmLayout L;
    int vis;
    if (fm == NULL) { return; }

    /*
     * An open context menu takes the keyboard.
     *
     * It used to take only Esc and swallow everything else, which made every
     * command on it -- View Bytes, Properties, Compress, Back Up, Rename --
     * reachable with a mouse and by no other means. That is a gap on any
     * system and a hole in this one, whose own hardware verification is
     * carried out with a keyboard alone, no mouse driver loaded.
     *
     * Stepping and selectability are ui_menu_step / ui_menu_selectable, which
     * is the same code the desktop menu and the launcher use.
     */
    if (fm->ctx_open) {
        if (key == PLAT_KEY_ESC) {
            fm->ctx_open = CFALSE;
            wm_invalidate(win, NULL);
        } else if (key == PLAT_KEY_UP || key == PLAT_KEY_DOWN) {
            if (ui_menu_step(&fm->ctx_menu,
                             (key == PLAT_KEY_DOWN) ? 1 : -1)) {
                wm_invalidate(win, NULL);
            }
        } else if (key == PLAT_KEY_ENTER) {
            /* Enter on nothing closes the menu rather than doing nothing at
             * all: arriving here having pressed Enter means the user is
             * finished with it either way. */
            int sel = fm->ctx_menu.highlight;
            fm->ctx_open = CFALSE;
            if (ui_menu_selectable(&fm->ctx_menu, sel)) {
                fm_ctx_dispatch(win, fm, fm->ctx_menu.items[sel].id);
            }
            wm_invalidate(win, NULL);
        }
        return;
    }

    fm_layout(fm, crect_w(&client), crect_h(&client), &L);

    /* Tab switches the active pane in two-pane mode. */
    if (fm->dual && fm->mate != NULL && key == PLAT_KEY_TAB) {
        fm->active ^= 1;
        wm_invalidate(win, NULL);
        return;
    }
    act = fm_active(fm);
    vis = L.visible_rows;
    if (fm->dual) { vis = (L.status.y0 - L.pathbar.y0 - 14) / FM_ROW_H; }
    if (vis < 1) { vis = 1; }

    {
    /* In Icons (grid) view, Up/Down step by a full row of columns and
     * Left/Right by one; other views ignore Left/Right. */
    int grid = (!fm->dual && act->view == 2);
    int cols = 1, rv = 1, step = 1;
    if (grid) { fm_grid_dims(&L.list, &cols, &rv); step = cols; }

    switch (key) {
    case PLAT_KEY_UP:
        act->sel -= step; if (act->sel < 0) { act->sel = 0; }
        if (grid) { fm_scroll_grid(act, cols, rv); } else { fm_scroll_to_sel(act, vis); }
        break;
    case PLAT_KEY_DOWN:
        act->sel += step; if (act->sel > act->count - 1) { act->sel = act->count - 1; }
        if (act->sel < 0) { act->sel = 0; }
        if (grid) { fm_scroll_grid(act, cols, rv); } else { fm_scroll_to_sel(act, vis); }
        break;
    case PLAT_KEY_HOME:
        act->sel = (act->count > 0) ? 0 : -1;
        if (grid) { fm_scroll_grid(act, cols, rv); } else { fm_scroll_to_sel(act, vis); }
        break;
    case PLAT_KEY_END:
        act->sel = act->count - 1;
        if (act->sel < 0) { act->sel = 0; }
        if (grid) { fm_scroll_grid(act, cols, rv); } else { fm_scroll_to_sel(act, vis); }
        break;
    case PLAT_KEY_LEFT:
        if (!grid) { return; }
        if (act->sel > 0) { act->sel--; }
        fm_scroll_grid(act, cols, rv);
        break;
    case PLAT_KEY_RIGHT:
        if (!grid) { return; }
        if (act->sel < act->count - 1) { act->sel++; }
        fm_scroll_grid(act, cols, rv);
        break;
    case PLAT_KEY_ENTER:
        fm_open_sel(win, act);
        break;
    case PLAT_KEY_BACKSP: {
        char np[CASTALIA_MAX_PATH];
        ui_path_parent_rel(act->path, np, sizeof(np));
        fm_load(act, np);
        break;
    }
    case PLAT_KEY_DELETE:
        fm_ask_delete(win, act);  /* opens a confirm dialog */
        return;
    case PLAT_KEY_F2:
        fm_ask_rename(win, act);
        return;
    case PLAT_KEY_F5:
        /* Refresh. The toolbar has carried a Refresh button from the start
         * with no key to match it, so a folder that changed underneath you --
         * a file another window wrote, an archive just restored somewhere
         * else -- had to be left and walked back into. F5 is what every file
         * browser since has used for this. */
        fm_refresh(act);
        if (grid) { fm_scroll_grid(act, cols, rv); }
        else      { fm_scroll_to_sel(act, vis); }
        break;
    case PLAT_KEY_F10:
        /* The context menu, from the keyboard. It had no opener but the right
         * mouse button, which put every command on it out of reach on a
         * machine with no mouse driver -- the configuration this system's own
         * hardware checks run in.
         *
         * F10 rather than F3: F3 is Find, which is what it already means in
         * Notepad, and --cz-demo was pressing it expecting nothing to happen.
         * A Menu key would be the other convention and the 1999 keyboards
         * this targets do not all have one. */
        fm_ctx_show_selected(win, fm);
        return;
    default:
        return;
    }
    wm_invalidate(win, NULL);
    }
}

static cbool fm_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_PAINT:
        fm_paint(win, (GfxSurface *)param);
        return CTRUE;
    case WM_MSG_LBUTTONDOWN: {
        FileMan *fm = (FileMan *)wm_user(win);
        if (fm != NULL && !fm->ctx_open &&
            fm_scrollbar_click(win, fm, (int)a, (int)b)) {
            return CTRUE;
        }
        fm_on_click(win, (int)a, (int)b);
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP: {
        FileMan *fm = (FileMan *)wm_user(win);
        if (fm != NULL && ui_hot_release(&fm->hot_tb)) {
            fm_invalidate_toolbar(win, fm);
        }
        if (fm != NULL && (fm->sb_drag || fm->hot_sb != UI_SB_NONE)) {
            CRect client = wm_client_rect(win);
            CPoint o = wm_client_origin(win);
            FmLayout L;
            CRect r;
            fm->sb_drag = CFALSE;
            fm->hot_sb = UI_SB_NONE;
            /* The bar goes back from held to idle. Nothing else moved. */
            fm_layout(fm, crect_w(&client), crect_h(&client), &L);
            r = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &r);
            return CTRUE;
        }
        if (fm != NULL && (fm->drag_armed || fm->drag_active)) {
            if (fm->drag_active) { fm_drop(win, fm, (int)a, (int)b); }
            fm->drag_armed = CFALSE;
            fm->drag_active = CFALSE;
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        return CFALSE;
    }
    case WM_MSG_RBUTTONDOWN: {
        FileMan *fm = (FileMan *)wm_user(win);
        if (fm != NULL) { fm_open_context(win, fm, (int)a, (int)b); }
        return CTRUE;
    }
    case WM_MSG_MOUSEMOVE: {
        FileMan *fm = (FileMan *)wm_user(win);
        if (fm == NULL) { return CFALSE; }
        if (fm->sb_drag) {
            CRect client = wm_client_rect(win);
            FmLayout L;
            int was_top = fm->top;
            fm_layout(fm, crect_w(&client), crect_h(&client), &L);
            fm->top = ui_scroll_pos_from_coord(crect_h(&L.vbar), fm->count,
                                               L.visible_rows,
                                               (int)b - L.vbar.y0);
            /* The rows and the thumb moved; the menu bar, the toolbar, the
             * address strip, the task pane, the column header and the status
             * line did not. On the transition only. */
            if (fm->top != was_top) {
                CPoint o = wm_client_origin(win);
                CRect r = crect_union(&L.list, &L.vbar);
                r = crect_offset(&r, o.x, o.y);
                wm_invalidate(win, &r);
            }
            return CTRUE;
        }
        if (fm->ctx_open) {
            int h = ui_menu_hit(&fm->ctx_menu, fm->ctx_x, fm->ctx_y,
                                GFX_FONT_SYSTEM, (int)a, (int)b);
            if (h != fm->ctx_menu.highlight) {
                int was = fm->ctx_menu.highlight;
                fm->ctx_menu.highlight = h;
                ui_menu_repaint(win, &fm->ctx_menu, fm->ctx_x, fm->ctx_y,
                                GFX_FONT_SYSTEM, was, h);
            }
            return CTRUE;
        }
        if (fm->drag_armed || fm->drag_active) {
            int dx = (int)a - fm->down_x, dy = (int)b - fm->down_y;
            if (dx < 0) { dx = -dx; }
            if (dy < 0) { dy = -dy; }
            if (!fm->drag_active && (dx > 4 || dy > 4)) { fm->drag_active = CTRUE; }
            if (fm->drag_active) {
                fm->drag_x = (int)a; fm->drag_y = (int)b;
                wm_invalidate(win, NULL);
            }
            return CTRUE;
        }
        /*
         * Nothing else is going on, so the toolbar may light under the
         * pointer. Two things keep this cheap enough to do on a 486: the
         * repaint happens on the TRANSITION only -- ui_hot_move answers
         * CTRUE when what should be drawn changed, not when the pointer
         * moved -- and it repaints the toolbar strip rather than the window,
         * so sliding across ten buttons costs ten 660x28 redraws instead of
         * ten 660x440 ones.
         */
        {
            CRect client = wm_client_rect(win);
            int cw2 = crect_w(&client), ch2 = crect_h(&client);
            if (ui_hot_move(&fm->hot_tb,
                            fm_tb_at(fm, cw2, ch2, (int)a, (int)b))) {
                fm_invalidate_toolbar(win, fm);
            }
        }
        return CFALSE;
    }
    case WM_MSG_MOUSELEAVE: {
        FileMan *fm = (FileMan *)wm_user(win);
        cbool redraw;
        if (fm == NULL) { return CFALSE; }
        redraw = ui_hot_move(&fm->hot_tb, -1);
        if (ui_hot_release(&fm->hot_tb)) { redraw = CTRUE; }
        if (redraw) {
            CRect client = wm_client_rect(win);
            CPoint o = wm_client_origin(win);
            FmLayout L;
            CRect tb;
            fm_layout(fm, crect_w(&client), crect_h(&client), &L);
            tb = crect_make(0, L.toolbar.y0, crect_w(&client),
                            L.go_btn.y1 - L.toolbar.y0 + 1);
            tb = crect_offset(&tb, o.x, o.y);
            wm_invalidate(win, &tb);
        }
        return CTRUE;
    }
    case WM_MSG_FOCUS_LOST: {
        FileMan *fm = (FileMan *)wm_user(win);
        if (fm != NULL) {
            fm->drag_armed = CFALSE; fm->drag_active = CFALSE;
            (void)ui_hot_move(&fm->hot_tb, -1);
            (void)ui_hot_release(&fm->hot_tb);
            if (fm->ctx_open) { fm->ctx_open = CFALSE; }
            wm_invalidate(win, NULL);
        }
        return CFALSE;
    }
    case WM_MSG_MOUSEWHEEL: {
        FileMan *fm = (FileMan *)wm_user(win);
        CRect client;
        FmLayout L;
        if (fm == NULL) { return CFALSE; }
        client = wm_client_rect(win);
        fm_layout(fm, crect_w(&client), crect_h(&client), &L);
        if (ui_scroll_wheel(&fm->top, (int)a, fm->count, L.visible_rows)) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_union(&L.list, &L.vbar);
            r = crect_offset(&r, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:
        fm_on_key(win, (int)a);
        return CTRUE;
    case WM_MSG_DESTROY: {
        FileMan *fm = (FileMan *)wm_user(win);
        if (fm != NULL) {
            tc_free(&fm->thumbs);
            if (fm->mate != NULL) {
                tc_free(&fm->mate->thumbs);
                sys_free(fm->mate, (cu32)sizeof(FileMan));
            }
            sys_free(fm, (cu32)sizeof(FileMan));
        }
        return CTRUE;
    }
    default:
        break;
    }
    return CFALSE;
}

/* Open the File Manager at 'dir' (NULL -> CASTALIA_HOME, else CWD). */
void app_fileman_open_path(const char *dir)
{
    FileMan *fm;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    const char *start;
    int fw = 660, fh = 440;

    fm = (FileMan *)sys_calloc(1, (cu32)sizeof(FileMan));
    if (fm == NULL) { SYS_LOGE("app", "fileman: OOM"); return; }
    fm->last_click_row = -1;
    fm->view = 0;
    tc_init(&fm->thumbs);
    fm->thumbs_on = CTRUE;

    /* Start in a directory with something to see: the caller's dir, else
     * CASTALIA_HOME, else CWD. */
    start = dir;
    if (start == NULL || start[0] == '\0') { start = sys_home(); }
    fm_load(fm, start);

    plat_video_info(&vi);
    frame = wm_place_centered(fw, fh);   /* the work area, not the screen */

    w = wm_create("File Manager", &frame, WM_STYLE_APP, fm_proc, fm);
    if (w == NULL) { sys_free(fm, (cu32)sizeof(FileMan)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}

void app_fileman_open(void) { app_fileman_open_path(NULL); }
