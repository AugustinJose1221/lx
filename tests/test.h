/*
 * This file is part of lx, a terminal log viewer and analyzer.
 * Copyright (C) 2026 Augustin Jose
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY. See the
 * LICENSE file in the repository root for the full license text.
 */
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
