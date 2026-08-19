/*
 * test_main.c - Host unit-test runner. Exercises the pure logic modules
 * (safe strings, rectangle math, dirty regions, INI parsing) with no
 * platform backend required.
 */
#include "ctest.h"

int g_checks = 0;
int g_fails  = 0;

int test_strcmp(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) { return (*a < *b) ? -1 : 1; } a++; b++; }
    if (*a) { return 1; }
    if (*b) { return -1; }
    return 0;
}

int main(void)
{
    printf("CastaliaOS 98 PE - unit tests\n");
    test_str();
    test_rect();
    test_region();
    test_ini();
    test_install();
    test_snd();
    test_color();
    test_capp();
    test_capp_loader();
    test_net();
    test_clip();
    test_snap();
    test_bmp();
    test_mines();
    test_sheet();
    test_write();
    test_scroll();
    test_eq();
    test_agenda();
    test_paint();
    test_stack();
    test_theme();
    test_path();
    test_thumb();
    test_assoc();
    test_hist();
    test_map();
    test_lzss();
    test_lastgood();
    test_cpu();
    test_vbe();
    test_vbe_report();
    test_vbe_bank();
    test_sw();
    test_sol();
    test_wrap();
    test_fuzz();
    test_cmap();
    test_fsize();
    test_ring();
    test_fc();
    test_rev();
    test_trash();
    test_diff();
    test_grep();
    test_ren();
    test_undo();
    test_move();
    test_ctrl();
    test_meter();
    test_font();
    test_mem();
    test_blit();
    test_scale_map();
    test_iconnav();
    test_mach();
    test_hex();
    test_cal();
    test_calc();
    test_le();
    test_home();
    test_car();
    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    if (g_fails == 0) { printf("ALL PASS\n"); return 0; }
    printf("FAILED\n");
    return 1;
}
