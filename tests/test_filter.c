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
/*
 * Unit tests for the filter language (filter.c): operators, boolean
 * logic, pseudo-fields, semantic timestamp comparison, and compile
 * errors.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "logfile.h"
#include "template.h"
#include "test.h"

#define TMPFILE "lx_test_filter.tmp"

static const char *SAMPLE =
    "2026-06-01 19:18:23.123+2.00 UTC [INF]: Application started\n"
    "2026-06-01 19:18:24.001+2.00 UTC [DBG]: Cache warm-up begun\n"
    "2026-06-01 19:18:25.512+2.00 UTC [WRN]: Disk usage at 85%\n"
    "2026-06-01 19:18:26.700+2.00 UTC [ERR]: Failed: timeout\n"
    "    retry 1 of 3\n"
    "    retry 2 of 3\n"
    "2026-06-01 19:18:30.002+2.00 UTC [INF]: Connected to db-02\n"
    "2026-06-01 19:18:31.444+2.00 UTC [FTL]: Shutting down\n"
    "2026-06-02 08:05:00.000+2.00 UTC [INF]: Next day, morning\n"
    "2026-06-02 19:18:26.000+2.00 UTC [INF]: Next day, evening\n";

static size_t count_visible(const LogFile *lf)
{
    size_t i, n = 0;
    for (i = 0; i < lf->nents; i++)
        n += lf->ents[i].visible;
    return n;
}

static size_t apply(LogFile *lf, const char *expr)
{
    char err[256];
    FNode *f = filter_compile(expr, lf->tpl, err, sizeof err);
    size_t n;
    if (!f) {
        printf("  compile failed: %s (%s)\n", expr, err);
        return (size_t)-1;
    }
    filter_apply(f, lf);
    n = count_visible(lf);
    filter_free(f);
    return n;
}

int main(void)
{
    LogFile lf;
    FILE *fp;
    char err[256];

    fp = fopen(TMPFILE, "wb");
    OK(fp != NULL);
    fputs(SAMPLE, fp);
    fclose(fp);

    OK(logfile_load(&lf, TMPFILE, template_builtin("serilog"), 0) == 0);
    OK(lf.nents == 10);
    OK(lf.nmatched == 8);

    /* equality and enum values */
    OK(apply(&lf, "level==ERR") == 3);            /* + 2 continuations */
    OK(apply(&lf, "level == INF") == 4);
    OK(apply(&lf, "level != INF") == 6);
    OK(apply(&lf, "level=WRN") == 1);

    /* contains (case-insensitive) */
    OK(apply(&lf, "message ~ TIMEOUT") == 3);
    OK(apply(&lf, "message !~ timeout") == 7);
    OK(apply(&lf, "raw ~ retry") == 0); /* continuations inherit parent */

    /* boolean logic */
    OK(apply(&lf, "level==ERR || level==FTL") == 4);
    OK(apply(&lf, "level==ERR or level==FTL") == 4);
    OK(apply(&lf, "level==INF && message ~ db") == 1);
    OK(apply(&lf, "not level==INF") == 6);
    OK(apply(&lf, "!(level==INF || level==DBG)") == 5);

    /* line number pseudo field */
    OK(apply(&lf, "line <= 2") == 2);
    OK(apply(&lf, "line > 6") == 4);

    /* timestamp ranges (wall clock, fallback format without fraction) */
    OK(apply(&lf, "timestamp >= \"2026-06-01 19:18:25\"") == 8);
    OK(apply(&lf, "timestamp < '2026-06-01 19:18:24'") == 1);
    OK(apply(&lf,
             "timestamp >= \"2026-06-01 19:18:24\" && "
             "timestamp < \"2026-06-01 19:18:31\"") == 6);

    /* semantic timestamp equality: the value names a granule */
    OK(apply(&lf, "timestamp == \"2026-06-01\"") == 8);       /* whole day */
    OK(apply(&lf, "timestamp == \"2026-06-02\"") == 2);
    OK(apply(&lf, "timestamp != \"2026-06-01\"") == 2);
    OK(apply(&lf, "timestamp == \"2026-06-01 19:18\"") == 8); /* minute */
    OK(apply(&lf, "timestamp == \"2026-06-01 19:18:26\"") == 3); /* second */
    OK(apply(&lf, "timestamp == \"2026-06-01 19:18:23.123\"") == 1); /* exact */

    /* a time without a date compares by time of day, on any date */
    OK(apply(&lf, "timestamp >= \"19:18:26\"") == 6);
    OK(apply(&lf, "timestamp < \"09:00\"") == 1);   /* the 08:05 entry */
    OK(apply(&lf, "timestamp == \"19:18\"") == 9);  /* that minute, any day */

    /* ~ stays textual; ==/order ops never fall back to strings */
    OK(apply(&lf, "timestamp ~ \"19:18:2\"") == 7);
    OK(filter_compile("timestamp == junk", lf.tpl, err, sizeof err) ==
       NULL);

    /* compile errors */
    OK(filter_compile("bogusfield == 1", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("level ==", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("level == ERR &&", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("(level == ERR", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("timestamp > junk", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("line ~ 3", lf.tpl, err, sizeof err) == NULL);

    /* NULL filter shows everything */
    filter_apply(NULL, &lf);
    OK(count_visible(&lf) == 10);

    logfile_free(&lf);
    remove(TMPFILE);
    return TEST_REPORT("test_filter");
}
