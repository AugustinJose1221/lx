/* Minimal assert-based test harness shared by all test programs. */
#ifndef LX_TEST_H
#define LX_TEST_H

#include <stdio.h>

static int t_fail = 0, t_run = 0;

#define OK(cond)                                                       \
    do {                                                               \
        t_run++;                                                       \
        if (!(cond)) {                                                 \
            t_fail++;                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
        }                                                              \
    } while (0)

#define TEST_REPORT(name)                                              \
    (printf("%-16s %d/%d passed\n", name, t_run - t_fail, t_run),      \
     t_fail ? 1 : 0)

#endif
