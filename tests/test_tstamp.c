#include <math.h>
#include <string.h>

#include "test.h"
#include "tstamp.h"

/* 2026-06-01 00:00:00 = 1780272000 (wall clock; offsets are not applied) */
#define D20260601 1780272000.0
/* 1970-06-01 00:00:00 = 13046400 */
#define D19700601 13046400.0

static int near(double a, double b)
{
    return fabs(a - b) < 1e-3;
}

int main(void)
{
    double v;
    size_t c;
    const char *s;

    /* ISO with fraction and offset */
    s = "2026-06-01 19:18:23.123+2.00 UTC [INF]";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S.%f%z%Z", &v, &c) == 0);
    OK(near(v, D20260601 + 19 * 3600 + 18 * 60 + 23 + 0.123));
    OK(c == strlen("2026-06-01 19:18:23.123+2.00 UTC"));

    /* offset formats */
    s = "2026-06-01 19:18:23+02:00";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S%z", &v, &c) == 0);
    s = "2026-06-01 19:18:23+0200";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S%z", &v, &c) == 0);
    s = "2026-06-01 19:18:23Z";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S%z", &v, &c) == 0);

    /* syslog style: no year -> 1970, %e handles space padding */
    s = "Jun  1 19:18:23 host";
    OK(ts_parse(s, strlen(s), "%b %e %H:%M:%S", &v, &c) == 0);
    OK(near(v, D19700601 + 19 * 3600 + 18 * 60 + 23));
    OK(c == strlen("Jun  1 19:18:23"));

    /* full month name, 12h clock */
    s = "June 1 2026 7:18 PM";
    OK(ts_parse(s, strlen(s), "%B %d %Y %I:%M %p", &v, &c) == 0);
    OK(near(v, D20260601 + 19 * 3600 + 18 * 60));

    /* python logging: comma fraction */
    s = "2026-06-01 19:18:23,123";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S,%f", &v, &c) == 0);
    OK(near(v, D20260601 + 19 * 3600 + 18 * 60 + 23 + 0.123));

    /* mismatches */
    s = "2026-13-01 00:00:00"; /* bad month */
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S", &v, &c) != 0);
    s = "2026-06-01 25:00:00"; /* bad hour */
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S", &v, &c) != 0);
    s = "not a date";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d", &v, &c) != 0);
    s = "2026-06-01";
    OK(ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S", &v, &c) != 0);

    /* ordering across entries of the same file */
    {
        double a, b;
        s = "2026-06-01 19:18:23.123";
        ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S.%f", &a, &c);
        s = "2026-06-01 19:18:23.124";
        ts_parse(s, strlen(s), "%Y-%m-%d %H:%M:%S.%f", &b, &c);
        OK(a < b);
    }

    return TEST_REPORT("test_tstamp");
}
