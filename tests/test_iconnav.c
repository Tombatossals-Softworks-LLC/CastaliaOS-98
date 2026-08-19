/*
 * test_iconnav.c - Arrow keys across a desktop of icons (icon_nav.c).
 *
 * The desktops here are laid out by hand, which is the point: icons are
 * draggable and their positions persist, so after somebody rearranges their
 * desktop the array order says nothing about what is next to what. Every case
 * below is a layout where "the next icon in the array" and "the next icon on
 * the screen" are different answers.
 */
#include "ctest.h"
#include "../src/shell/icon_nav.h"

/* A 76x56 icon like the shell's, at a top-left corner. */
static CRect at(int x, int y) { return crect_make(x, y, 76, 56); }

void test_iconnav(void)
{
    printf("- desktop icon navigation\n");

    /* ---- the default layout: one column -------------------------------- */
    {
        CRect col[4];
        col[0] = at(22, 20);
        col[1] = at(22, 94);
        col[2] = at(22, 168);
        col[3] = at(22, 242);
        CHECK_EQI(icon_nav_next(col, 4, 0, ICON_NAV_DOWN), 1);
        CHECK_EQI(icon_nav_next(col, 4, 1, ICON_NAV_DOWN), 2);
        CHECK_EQI(icon_nav_next(col, 4, 3, ICON_NAV_UP), 2);
        /* The ends stop rather than wrapping: a selection that leaps to the
         * far side of the screen is one you have to go and find again. */
        CHECK_EQI(icon_nav_next(col, 4, 3, ICON_NAV_DOWN), -1);
        CHECK_EQI(icon_nav_next(col, 4, 0, ICON_NAV_UP), -1);
        /* Nothing is to the left or right of a single column. */
        CHECK_EQI(icon_nav_next(col, 4, 1, ICON_NAV_LEFT), -1);
        CHECK_EQI(icon_nav_next(col, 4, 1, ICON_NAV_RIGHT), -1);
        /* From nothing selected, any arrow lands on the top-left icon. */
        CHECK_EQI(icon_nav_next(col, 4, -1, ICON_NAV_DOWN), 0);
        CHECK_EQI(icon_nav_next(col, 4, -1, ICON_NAV_UP), 0);
        CHECK_EQI(icon_nav_next(col, 4, -1, ICON_NAV_RIGHT), 0);
    }

    /* ---- a grid, with the array order deliberately scrambled ----------- */
    /*
     *   [2] [0]
     *   [3] [1]
     * Array order is meaningless here; only the geometry decides.
     */
    {
        CRect g[4];
        g[0] = at(200, 20);
        g[1] = at(200, 120);
        g[2] = at(20, 20);
        g[3] = at(20, 120);
        CHECK_EQI(icon_nav_next(g, 4, 2, ICON_NAV_RIGHT), 0);
        CHECK_EQI(icon_nav_next(g, 4, 0, ICON_NAV_LEFT), 2);
        CHECK_EQI(icon_nav_next(g, 4, 2, ICON_NAV_DOWN), 3);
        CHECK_EQI(icon_nav_next(g, 4, 1, ICON_NAV_UP), 0);
        CHECK_EQI(icon_nav_next(g, 4, 1, ICON_NAV_LEFT), 3);
        CHECK_EQI(icon_nav_next(g, 4, 3, ICON_NAV_RIGHT), 1);
        /* ...and the corners still stop. */
        CHECK_EQI(icon_nav_next(g, 4, 0, ICON_NAV_UP), -1);
        CHECK_EQI(icon_nav_next(g, 4, 1, ICON_NAV_DOWN), -1);
    }

    /* ---- the same row beats a nearer icon off to the side -------------- */
    /*
     * THE reason a plain nearest-centre metric is not enough. From icon 0,
     * pressing Right must reach the icon on the same row -- even though the
     * one below-right is closer as the crow flies -- because that is what an
     * arrow key means.
     */
    {
        CRect r[3];
        r[0] = at(0, 100);
        r[1] = at(300, 100);   /* same row, far    */
        r[2] = at(90, 260);    /* below, nearer    */
        CHECK_EQI(icon_nav_next(r, 3, 0, ICON_NAV_RIGHT), 1);
        /* ...and Down from 0 reaches the one below rather than the one across,
         * for the same reason mirrored. */
        CHECK_EQI(icon_nav_next(r, 3, 0, ICON_NAV_DOWN), 2);
    }

    /* ---- level icons are not "past" each other ------------------------- */
    /* An icon at exactly the same centre-x is not to the right of this one,
     * however far down the screen it is. Without that rule, Right and Down
     * would reach the same icon in a plain column. */
    {
        CRect r[2];
        r[0] = at(50, 10);
        r[1] = at(50, 300);
        CHECK_EQI(icon_nav_next(r, 2, 0, ICON_NAV_RIGHT), -1);
        CHECK_EQI(icon_nav_next(r, 2, 0, ICON_NAV_LEFT), -1);
        CHECK_EQI(icon_nav_next(r, 2, 0, ICON_NAV_DOWN), 1);
    }

    /* ---- a walk that has to come back to where it started -------------- */
    /*
     * Driven as a sequence rather than as single calls: moving right and then
     * left again has to land on the icon it left, or the desktop drifts under
     * the user's hands. Nothing in the single-step checks above would catch a
     * metric that was subtly asymmetric.
     */
    {
        CRect g[6];
        int here;
        g[0] = at(20, 20);   g[1] = at(120, 20);  g[2] = at(220, 20);
        g[3] = at(20, 120);  g[4] = at(120, 120); g[5] = at(220, 120);
        here = 0;
        here = icon_nav_next(g, 6, here, ICON_NAV_RIGHT);  CHECK_EQI(here, 1);
        here = icon_nav_next(g, 6, here, ICON_NAV_RIGHT);  CHECK_EQI(here, 2);
        here = icon_nav_next(g, 6, here, ICON_NAV_DOWN);   CHECK_EQI(here, 5);
        here = icon_nav_next(g, 6, here, ICON_NAV_LEFT);   CHECK_EQI(here, 4);
        here = icon_nav_next(g, 6, here, ICON_NAV_LEFT);   CHECK_EQI(here, 3);
        here = icon_nav_next(g, 6, here, ICON_NAV_UP);     CHECK_EQI(here, 0);
    }

    /* ---- refusals ------------------------------------------------------ */
    {
        CRect one[1];
        one[0] = at(10, 10);
        CHECK_EQI(icon_nav_next(one, 1, 0, ICON_NAV_DOWN), -1);   /* alone   */
        CHECK_EQI(icon_nav_next(one, 0, -1, ICON_NAV_DOWN), -1);  /* none    */
        CHECK_EQI(icon_nav_next(NULL, 4, 0, ICON_NAV_DOWN), -1);
        /* An index off the end is treated as "nothing selected". */
        CHECK_EQI(icon_nav_next(one, 1, 9, ICON_NAV_DOWN), 0);
        CHECK_EQI(icon_nav_next(one, 1, -7, ICON_NAV_UP), 0);
    }
}
