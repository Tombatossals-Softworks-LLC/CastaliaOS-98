/*
 * test_thumb.c - The thumbnail cache (thumb_core.c).
 *
 * The cache exists to stop two failures the target hardware would not survive:
 * decoding a folder full of bitmaps inside one repaint, and keeping them all
 * in memory afterwards. Both are bounds, so both are tested here -- with a
 * fake loader that counts how often it is asked, which is the only way to see
 * a cache actually caching.
 */
#include "ctest.h"
#include "thumb_core.h"

static int g_loads;

/* Anything ending in ".BMP" is an "image"; everything else is not. */
static GfxSurface *fake_load(const char *path, int size, void *user)
{
    int i, dot = -1;
    (void)user;
    g_loads++;
    for (i = 0; path[i] != '\0'; i++) {
        if (path[i] == '.') { dot = i; }
    }
    if (dot < 0 || sys_stricmp(path + dot, ".BMP") != 0) { return NULL; }
    return gfx_surface_new((size > 0) ? size : 4, (size > 0) ? size : 4);
}

static void tc_hits(void)
{
    ThumbCache c;
    const GfxSurface *a, *b;

    tc_init(&c);
    CHECK_EQI(tc_count(&c), 0);
    g_loads = 0;

    tc_begin_frame(&c, 4);
    a = tc_get(&c, "PHOTO.BMP", 32, fake_load, NULL);
    CHECK(a != NULL);
    CHECK_EQI(a->w, 32);
    CHECK_EQI(g_loads, 1);
    CHECK_EQI(tc_count(&c), 1);

    /* The second ask is a hit: the same surface, and no new decode. */
    b = tc_get(&c, "PHOTO.BMP", 32, fake_load, NULL);
    CHECK(b == a);
    CHECK_EQI(g_loads, 1);

    /* ...and it is still a hit in a later frame. */
    tc_begin_frame(&c, 4);
    CHECK(tc_get(&c, "PHOTO.BMP", 32, fake_load, NULL) == a);
    CHECK_EQI(g_loads, 1);

    /* A file that is not an image is remembered as such and never retried --
     * otherwise every repaint would try to decode every text file again. */
    CHECK(tc_get(&c, "NOTES.TXT", 32, fake_load, NULL) == NULL);
    CHECK_EQI(g_loads, 2);
    CHECK(tc_get(&c, "NOTES.TXT", 32, fake_load, NULL) == NULL);
    CHECK_EQI(g_loads, 2);
    CHECK_EQI(tc_count(&c), 2);

    tc_free(&c);
    CHECK_EQI(tc_count(&c), 0);
}

static void tc_budget(void)
{
    ThumbCache c;
    tc_init(&c);
    g_loads = 0;

    /* Two decodes a frame: the third ask comes back pending, not stalled. */
    tc_begin_frame(&c, 2);
    CHECK(tc_get(&c, "A.BMP", 16, fake_load, NULL) != NULL);
    CHECK(tc_get(&c, "B.BMP", 16, fake_load, NULL) != NULL);
    CHECK(tc_get(&c, "C.BMP", 16, fake_load, NULL) == NULL);
    CHECK(tc_get(&c, "D.BMP", 16, fake_load, NULL) == NULL);
    CHECK_EQI(g_loads, 2);
    CHECK_EQI(tc_pending(&c), 2);

    /* The next frame picks up where it left off, and the two already there
     * cost nothing. */
    tc_begin_frame(&c, 2);
    CHECK_EQI(tc_pending(&c), 0);
    CHECK(tc_get(&c, "A.BMP", 16, fake_load, NULL) != NULL);
    CHECK(tc_get(&c, "B.BMP", 16, fake_load, NULL) != NULL);
    CHECK(tc_get(&c, "C.BMP", 16, fake_load, NULL) != NULL);
    CHECK_EQI(g_loads, 3);
    CHECK_EQI(tc_pending(&c), 0);

    /* A zero budget decodes nothing at all but still serves what is cached. */
    tc_begin_frame(&c, 0);
    CHECK(tc_get(&c, "A.BMP", 16, fake_load, NULL) != NULL);
    CHECK(tc_get(&c, "E.BMP", 16, fake_load, NULL) == NULL);
    CHECK_EQI(g_loads, 3);
    CHECK_EQI(tc_pending(&c), 1);

    tc_free(&c);
}

static void tc_eviction(void)
{
    ThumbCache c;
    char name[32];
    int i;
    const GfxSurface *keep;

    tc_init(&c);
    g_loads = 0;
    tc_begin_frame(&c, 1000);

    /* Fill every slot. */
    for (i = 0; i < TC_SLOTS; i++) {
        sys_snprintf(name, sizeof name, "IMG%02d.BMP", i);
        CHECK(tc_get(&c, name, 8, fake_load, NULL) != NULL);
    }
    CHECK_EQI(tc_count(&c), TC_SLOTS);
    CHECK_EQI(g_loads, TC_SLOTS);

    /* Touch the oldest so it is no longer the oldest. */
    keep = tc_get(&c, "IMG00.BMP", 8, fake_load, NULL);
    CHECK(keep != NULL);
    CHECK_EQI(g_loads, TC_SLOTS);

    /* One more file evicts the least recently used -- which is now IMG01,
     * not IMG00 -- and the cache never grows past its bound. */
    CHECK(tc_get(&c, "NEW.BMP", 8, fake_load, NULL) != NULL);
    CHECK_EQI(tc_count(&c), TC_SLOTS);
    CHECK_EQI(g_loads, TC_SLOTS + 1);
    CHECK(tc_get(&c, "IMG00.BMP", 8, fake_load, NULL) == keep);   /* survived */
    CHECK_EQI(g_loads, TC_SLOTS + 1);
    CHECK(tc_get(&c, "IMG01.BMP", 8, fake_load, NULL) != NULL);   /* re-read  */
    CHECK_EQI(g_loads, TC_SLOTS + 2);

    tc_free(&c);
}

static void tc_guards(void)
{
    ThumbCache c;
    tc_init(&c);
    tc_begin_frame(&c, 4);
    CHECK(tc_get(&c, NULL, 16, fake_load, NULL) == NULL);
    CHECK(tc_get(&c, "", 16, fake_load, NULL) == NULL);
    CHECK(tc_get(&c, "X.BMP", 16, NULL, NULL) == NULL);
    CHECK(tc_get(NULL, "X.BMP", 16, fake_load, NULL) == NULL);
    CHECK_EQI(tc_pending(NULL), 0);
    CHECK_EQI(tc_count(NULL), 0);
    tc_begin_frame(&c, -5);                 /* a negative budget is zero */
    CHECK(tc_get(&c, "Y.BMP", 16, fake_load, NULL) == NULL);
    tc_begin_frame(NULL, 4);
    tc_init(NULL);
    tc_free(NULL);
    tc_free(&c);
}

void test_thumb(void)
{
    printf("- thumbnails\n");
    tc_hits();
    tc_budget();
    tc_eviction();
    tc_guards();
}
