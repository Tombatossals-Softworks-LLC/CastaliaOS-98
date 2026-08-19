/*
 * test_str.c - Safe string helpers: truncation, termination, trim, icmp.
 */
#include "ctest.h"
#include "castalia/sys.h"

void test_str(void)
{
    char buf[8];
    cu32 n;

    printf("- sys_str\n");

    /* strlcpy always terminates and reports the full source length. */
    n = sys_strlcpy(buf, "hello", sizeof(buf));
    CHECK_EQI(n, 5);
    CHECK_STR(buf, "hello");

    /* Truncation: dst holds 7 chars + NUL; source length returned. */
    n = sys_strlcpy(buf, "abcdefghij", sizeof(buf));
    CHECK_EQI(n, 10);          /* would-be length */
    CHECK_EQI(buf[7], '\0');   /* terminated within bounds */
    CHECK_STR(buf, "abcdefg");

    /* strlcat appends within bounds. */
    sys_strlcpy(buf, "ab", sizeof(buf));
    n = sys_strlcat(buf, "cd", sizeof(buf));
    CHECK_EQI(n, 4);
    CHECK_STR(buf, "abcd");
    /* Overflowing cat still terminates. */
    sys_strlcat(buf, "xyzuvw", sizeof(buf));
    CHECK_EQI(buf[7], '\0');

    /* snprintf clamps and terminates. */
    sys_snprintf(buf, sizeof(buf), "%d-%d", 12, 3456);
    CHECK_EQI(buf[7], '\0');

    /* strnlen bounds a missing terminator. */
    CHECK_EQI(sys_strnlen("hi", 8), 2);
    CHECK_EQI(sys_strnlen("abcdef", 3), 3);

    /* case-insensitive compare */
    CHECK_EQI(sys_stricmp("Castalia", "castalia"), 0);
    CHECK(sys_stricmp("abc", "abd") < 0);

    /* trim */
    {
        char t[16];
        sys_strlcpy(t, "  hi there  ", sizeof(t));
        sys_strtrim(t);
        CHECK_STR(t, "hi there");
    }
}
