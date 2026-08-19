/*
 * ctest.h - Tiny assertion harness for the host unit tests. No dependencies
 * beyond stdio; each suite is a plain function that calls CHECK*.
 */
#ifndef CASTALIA_CTEST_H
#define CASTALIA_CTEST_H

#include <stdio.h>

extern int g_checks;
extern int g_fails;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fails++; \
        fprintf(stderr, "  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQI(a, b) do { \
    long _va = (long)(a), _vb = (long)(b); \
    g_checks++; \
    if (_va != _vb) { g_fails++; \
        fprintf(stderr, "  FAIL %s:%d  %s==%s  (%ld != %ld)\n", \
                __FILE__, __LINE__, #a, #b, _va, _vb); } \
} while (0)

#define CHECK_STR(a, b) do { \
    g_checks++; \
    if (test_strcmp((a), (b)) != 0) { g_fails++; \
        fprintf(stderr, "  FAIL %s:%d  \"%s\"==\"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); } \
} while (0)

int test_strcmp(const char *a, const char *b);

/* Suites. */
void test_str(void);
void test_rect(void);
void test_region(void);
void test_ini(void);
void test_install(void);
void test_snd(void);
void test_color(void);
void test_capp(void);
void test_capp_loader(void);
void test_net(void);
void test_clip(void);
void test_snap(void);
void test_bmp(void);
void test_mines(void);
void test_sheet(void);
void test_write(void);
void test_scroll(void);
void test_eq(void);
void test_agenda(void);
void test_paint(void);
void test_stack(void);
void test_theme(void);
void test_path(void);
void test_thumb(void);
void test_assoc(void);
void test_hist(void);
void test_map(void);
void test_lzss(void);
void test_lastgood(void);
void test_cpu(void);
void test_vbe(void);
void test_vbe_report(void);
void test_vbe_bank(void);
void test_sw(void);
void test_sol(void);
void test_wrap(void);
void test_fuzz(void);
void test_cmap(void);
void test_fsize(void);
void test_ring(void);
void test_fc(void);
void test_rev(void);
void test_trash(void);
void test_diff(void);
void test_grep(void);
void test_ren(void);
void test_undo(void);
void test_move(void);
void test_ctrl(void);
void test_meter(void);
void test_font(void);
void test_mem(void);
void test_blit(void);
void test_scale_map(void);
void test_iconnav(void);
void test_mach(void);
void test_hex(void);
void test_cal(void);
void test_calc(void);
void test_le(void);
void test_home(void);
void test_car(void);

#endif /* CASTALIA_CTEST_H */
