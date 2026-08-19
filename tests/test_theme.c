/*
 * test_theme.c - The theme model (sh_theme_io.c).
 *
 * A theme editor is only trustworthy if what it saves is what comes back. So
 * these tests write a theme out and read it in field by field -- every color,
 * not a sample -- and check the presets, the low-color quantization, and what
 * happens when the file is missing or damaged.
 */
#include "ctest.h"
#include "castalia/shell.h"
#include "castalia/cfg.h"

#include <stdio.h>

#define THEME_PATH "build/test_theme.ini"

static ShTheme g_a, g_b;

/* Every color a theme carries, in one place, so a new field cannot quietly
 * escape the round trip. */
static int theme_differs(const ShTheme *x, const ShTheme *y)
{
    int n = 0;
    if (x->desktop_top      != y->desktop_top)      { n++; }
    if (x->desktop_bottom   != y->desktop_bottom)   { n++; }
    if (x->title_active_l   != y->title_active_l)   { n++; }
    if (x->title_active_r   != y->title_active_r)   { n++; }
    if (x->title_inactive_l != y->title_inactive_l) { n++; }
    if (x->title_inactive_r != y->title_inactive_r) { n++; }
    if (x->title_text       != y->title_text)       { n++; }
    if (x->ui.face          != y->ui.face)          { n++; }
    if (x->ui.light         != y->ui.light)         { n++; }
    if (x->ui.dark          != y->ui.dark)          { n++; }
    if (x->ui.darker        != y->ui.darker)        { n++; }
    if (x->ui.text          != y->ui.text)          { n++; }
    if (x->ui.text_disabled != y->ui.text_disabled) { n++; }
    if (x->ui.accent        != y->ui.accent)        { n++; }
    if (x->ui.accent_text   != y->ui.accent_text)   { n++; }
    if (x->taskbar_face     != y->taskbar_face)     { n++; }
    if (x->title_height     != y->title_height)     { n++; }
    if (x->border_width     != y->border_width)     { n++; }
    if (x->colors           != y->colors)           { n++; }
    return n;
}

static void th_presets(void)
{
    int i;
    /* Every preset produces a usable theme -- named, and not all-black. */
    for (i = 0; i < 5; i++) {
        int bad = 0;
        sh_theme_preset(&g_a, i);
        if (g_a.name[0] == '\0') { bad++; }
        if (g_a.title_height < 8 || g_a.border_width < 1) { bad++; }
        if (g_a.ui.face == g_a.ui.text) { bad++; }   /* unreadable controls  */
        if (sh_theme_preset_name(i)[0] == '\0') { bad++; }
        CHECK_EQI(bad, 0);
    }
    /* Presets really differ from one another. */
    sh_theme_preset(&g_a, 0);
    sh_theme_preset(&g_b, 4);
    CHECK(theme_differs(&g_a, &g_b) > 0);
    /* An unknown id falls back to Classic rather than to garbage. */
    sh_theme_preset(&g_b, 99);
    sh_theme_preset(&g_a, 0);
    CHECK_EQI(theme_differs(&g_a, &g_b), 0);
    /* NULL is a no-op. */
    sh_theme_preset(NULL, 1);
}

/* Which channel a colour leads with -- 0 red, 1 green, 2 blue. Crude on
 * purpose: the question is not "what shade" but "is this the same family of
 * colour as the one next to it". */
static int th_lead(CColor c)
{
    int r = (int)GFX_R(c), g = (int)GFX_G(c), b = (int)GFX_B(c);
    if (g >= r && g >= b) { return 1; }
    if (b >= r && b >= g) { return 2; }
    return 0;
}

/*
 * An inactive title bar wears the SAME family of colour as an active one.
 *
 * It did not. Storm and Forest recoloured the active bar and left the
 * inactive one at whatever Classic had put there -- calm slate blue -- so
 * Forest Green showed the focused window with a green title and every other
 * window with a BLUE one. Two themes on the same screen, and nothing said so,
 * because an inactive title bar drawn in a perfectly valid colour is not an
 * error anywhere.
 *
 * Classic and Aurora were only right because each states its own pair. This
 * asks the question of every preset, so the next one cannot forget.
 */
static void th_inactive_follows_active(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        sh_theme_preset(&g_a, i);
        CHECK_EQI(th_lead(g_a.title_inactive_l), th_lead(g_a.title_active_l));
        CHECK_EQI(th_lead(g_a.title_inactive_r), th_lead(g_a.title_active_r));
        /*
         * ...and it must still be DIMMER, or the focused window stops being
         * the obvious one. A rule that only checked the hue would pass on a
         * theme whose two states were identical.
         */
        CHECK(g_a.title_inactive_l != g_a.title_active_l);
        CHECK((int)GFX_R(g_a.title_inactive_l) + (int)GFX_G(g_a.title_inactive_l) +
              (int)GFX_B(g_a.title_inactive_l) >
              (int)GFX_R(g_a.title_active_l) + (int)GFX_G(g_a.title_active_l) +
              (int)GFX_B(g_a.title_active_l));
    }
}

static void th_round_trip(void)
{
    remove(THEME_PATH);

    /* A theme with a deliberately distinct value in every field, so a mixed-up
     * key would show as a mismatch rather than passing by luck. */
    sh_theme_preset(&g_a, 0);
    sys_strlcpy(g_a.name, "Round Trip", sizeof g_a.name);
    g_a.desktop_top      = GFX_RGB(0x11, 0x22, 0x33);
    g_a.desktop_bottom   = GFX_RGB(0x44, 0x55, 0x66);
    g_a.title_active_l   = GFX_RGB(0x77, 0x88, 0x99);
    g_a.title_active_r   = GFX_RGB(0xAA, 0xBB, 0xCC);
    g_a.title_inactive_l = GFX_RGB(0x10, 0x20, 0x30);
    g_a.title_inactive_r = GFX_RGB(0x40, 0x50, 0x60);
    g_a.title_text       = GFX_RGB(0xF0, 0xE0, 0xD0);
    g_a.ui.face          = GFX_RGB(0x01, 0x02, 0x03);
    g_a.ui.light         = GFX_RGB(0x04, 0x05, 0x06);
    g_a.ui.dark          = GFX_RGB(0x07, 0x08, 0x09);
    g_a.ui.darker        = GFX_RGB(0x0A, 0x0B, 0x0C);
    g_a.ui.text          = GFX_RGB(0x0D, 0x0E, 0x0F);
    g_a.ui.text_disabled = GFX_RGB(0x21, 0x31, 0x41);
    g_a.ui.accent        = GFX_RGB(0xC0, 0xFF, 0xEE);
    g_a.ui.accent_text   = GFX_RGB(0xDE, 0xAD, 0xBE);
    g_a.taskbar_face     = GFX_RGB(0xFE, 0xED, 0x0C);
    g_a.title_height     = 23;
    g_a.border_width     = 3;
    g_a.colors           = 256;

    CHECK_EQI((int)sh_theme_save(&g_a, THEME_PATH), CE_OK);
    CHECK_EQI((int)sh_theme_load(&g_b, THEME_PATH), CE_OK);
    CHECK_EQI(theme_differs(&g_a, &g_b), 0);
    CHECK_STR(g_b.name, "Round Trip");

    /* Saving over an existing file replaces its values, it does not merge. */
    g_a.ui.accent = GFX_RGB(0x12, 0x34, 0x56);
    CHECK_EQI((int)sh_theme_save(&g_a, THEME_PATH), CE_OK);
    CHECK_EQI((int)sh_theme_load(&g_b, THEME_PATH), CE_OK);
    CHECK(g_b.ui.accent == GFX_RGB(0x12, 0x34, 0x56));

    /* Guards. */
    CHECK_EQI((int)sh_theme_save(NULL, THEME_PATH), CE_INVALID);
    CHECK_EQI((int)sh_theme_save(&g_a, NULL), CE_INVALID);
    CHECK_EQI((int)sh_theme_load(NULL, THEME_PATH), CE_INVALID);
    remove(THEME_PATH);
}

static void th_missing_and_partial(void)
{
    FILE *f;
    remove(THEME_PATH);
    /* A file that is not there leaves the caller with the default theme, and
     * says so -- the shell must still be able to draw. */
    CHECK_EQI((int)sh_theme_load(&g_b, THEME_PATH), CE_NOTFOUND);
    sh_theme_default(&g_a);
    CHECK_EQI(theme_differs(&g_a, &g_b), 0);

    /* A partial or damaged file keeps the defaults for what it does not say,
     * and takes only what it does. */
    f = fopen(THEME_PATH, "w");
    if (f != NULL) {
        fprintf(f, "[Theme]\nName=Half Written\n"
                   "[Colors]\nAccent=#FF8800\nFace=not-a-color\n"
                   "[Metrics]\nTitleBarHeight=26\n");
        fclose(f);
        CHECK_EQI((int)sh_theme_load(&g_b, THEME_PATH), CE_OK);
        CHECK_STR(g_b.name, "Half Written");
        CHECK(g_b.ui.accent == GFX_RGB(0xFF, 0x88, 0x00));
        CHECK(g_b.ui.face == g_a.ui.face);        /* malformed -> default   */
        CHECK_EQI(g_b.title_height, 26);
        CHECK_EQI(g_b.border_width, g_a.border_width);
    }
    remove(THEME_PATH);
}

static void th_quantize(void)
{
    /* Native (0) leaves every color exactly as authored. */
    sh_theme_preset(&g_a, 4);
    g_b = g_a;
    g_b.colors = 0;
    sh_theme_quantize(&g_b);
    CHECK_EQI(theme_differs(&g_a, &g_b), 0);

    /* A 16-color target flattens the gradients -- an interpolated midtone
     * would land outside the palette it is supposed to stay inside. */
    g_b = g_a;
    g_b.colors = 16;
    sh_theme_quantize(&g_b);
    CHECK(g_b.desktop_bottom == g_b.desktop_top);
    CHECK(g_b.title_active_r == g_b.title_active_l);
    CHECK(g_b.title_inactive_r == g_b.title_inactive_l);
    CHECK(g_b.ui.text_disabled == g_b.ui.dark);

    /* 256 keeps the gradients but still moves the colors onto the palette. */
    g_b = g_a;
    g_b.colors = 256;
    sh_theme_quantize(&g_b);
    CHECK(theme_differs(&g_a, &g_b) > 0);
    sh_theme_quantize(NULL);
}

void test_theme(void)
{
    printf("- theme\n");
    th_presets();
    th_inactive_follows_active();
    th_round_trip();
    th_missing_and_partial();
    th_quantize();
}
