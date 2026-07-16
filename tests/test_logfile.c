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
 * Unit tests for log loading (logfile.c): line splitting, incremental
 * refresh, truncation reload, continuations, and stdin streams.
 */
#include <stdio.h>
#include <string.h>

#include "filter.h"
#include "logfile.h"
#include "template.h"
#include "test.h"

#define TMPFILE "lx_test_logfile.tmp"

/* Stream mode: feed a pipe into fd 0 and load with path "-".
 * POSIX-only (pipe/dup2); skipped on Windows builds. */
#ifndef _WIN32
#include <unistd.h>

static void test_stream(void)
{
    LogFile lf;
    int fds[2], old0;

    OK(pipe(fds) == 0);
    old0 = dup(0);
    OK(dup2(fds[0], 0) == 0);

    OK(write(fds[1], "[    1.0] one\n[    2.0] two\n", 28) == 28);
    OK(logfile_load(&lf, "-", template_builtin("dmesg"), 0) == 0);
    OK(lf.stream == 1);
    OK(lf.stream_eof == 0);
    OK(lf.nents == 2);
    OK(lf.nmatched == 2);
    OK(!strcmp(lf.path, "(stdin)"));

    /* a partial line arrives ... */
    OK(write(fds[1], "[    3.0] thr", 13) == 13);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 3);
    OK(lf.unterminated == 1);

    /* ... and is completed by the next chunk */
    OK(write(fds[1], "ee\n[    4.0] four\n", 18) == 18);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 4);
    OK(lf.nmatched == 4);
    OK(lf.ents[2].raw.len == strlen("[    3.0] three"));
    OK(!memcmp(lf.buf + lf.ents[2].raw.off, "[    3.0] three",
               lf.ents[2].raw.len));

    /* nothing new */
    OK(logfile_refresh(&lf) == 0);

    /* producer closes the pipe */
    close(fds[1]);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.stream_eof == 1);
    OK(logfile_refresh(&lf) == 0);
    OK(lf.nents == 4);

    logfile_free(&lf);
    dup2(old0, 0);
    close(old0);
    close(fds[0]);
}
#endif /* !_WIN32 */

/* High-performance backend: everything observable through the accessor
 * API must match the standard backend on the same file. */
static void test_hp(void)
{
    LogFile a, b; /* a = standard, b = hp */
    FILE *fp;
    size_t i;
    char err[256];
    FNode *fn;

    fp = fopen(TMPFILE, "wb");
    fputs("2026-06-01 19:18:23.123+2.00 UTC [INF]: started\n"
          "2026-06-01 19:18:24.000+2.00 UTC [ERR]: failed: timeout\n"
          "    retry 1 of 3\n"
          "2026-06-01 19:18:25.000+2.00 UTC [WRN]: disk at 85%\r\n"
          "2026-06-01 19:18:26.000+2.00 UTC [INF]: tail no newline", fp);
    fclose(fp);

    OK(logfile_load(&a, TMPFILE, template_builtin("serilog"), 0) == 0);
    OK(logfile_load(&b, TMPFILE, template_builtin("serilog"), 1) == 0);
    OK(b.hp == 1);
    OK(a.nents == b.nents);
    OK(b.nents == 5);
    OK(b.unterminated == 1);

    /* per-entry equivalence via the accessor API */
    for (i = 0; i < a.nents; i++) {
        EView va, vb;
        Span ra = logfile_raw(&a, i), rb = logfile_raw(&b, i);
        OK(ra.len == rb.len);
        OK(!memcmp(logfile_data(&a) + ra.off, logfile_data(&b) + rb.off,
                   ra.len));
        logfile_view(&a, i, &va);
        logfile_view(&b, i, &vb);
        OK(va.matched == vb.matched);
        OK(va.cont == vb.cont);
        OK(va.lineno == vb.lineno);
        if (va.matched) {
            int f;
            for (f = 0; f < a.tpl->nfields; f++) {
                OK(va.fsp[f].off == vb.fsp[f].off);
                OK(va.fsp[f].len == vb.fsp[f].len);
            }
        }
    }

    /* filters agree */
    fn = filter_compile("level==ERR", a.tpl, err, sizeof err);
    OK(fn != NULL);
    filter_apply(fn, &a);
    filter_apply(fn, &b);
    OK(a.nvisible == b.nvisible);
    OK(b.nvisible == 2); /* the ERR entry + its continuation line */
    for (i = 0; i < a.nents; i++)
        OK(logfile_is_visible(&a, i) == logfile_is_visible(&b, i));
    filter_free(fn);

    /* append: the unterminated tail is re-evaluated, new lines indexed */
    fp = fopen(TMPFILE, "ab");
    fputs(" now terminated\n"
          "2026-06-01 19:18:27.000+2.00 UTC [FTL]: last\n", fp);
    fclose(fp);
    OK(logfile_refresh(&b) == 1);
    OK(b.nents == 6);
    OK(b.refresh_from == 4); /* the tail line */
    OK(b.unterminated == 0);
    {
        Span r = logfile_raw(&b, 4);
        OK(r.len == strlen("2026-06-01 19:18:26.000+2.00 UTC [INF]: "
                           "tail no newline now terminated"));
    }
    OK(logfile_refresh(&b) == 0);

    /* truncation reloads */
    fp = fopen(TMPFILE, "wb");
    fputs("2026-06-01 19:18:30.000+2.00 UTC [INF]: fresh\n", fp);
    fclose(fp);
    OK(logfile_refresh(&b) == 1);
    OK(b.nents == 1);
    OK(b.hp == 1);
    OK(b.refresh_from == 0);

    /* reparse (template switch) resets visibility */
    OK(logfile_reparse(&b, template_builtin("plain")) == 0);
    OK(b.nvisible == b.nents);

    logfile_free(&a);
    logfile_free(&b);
    remove(TMPFILE);
}

int main(void)
{
    LogFile lf;
    FILE *fp;

    /* --- load + line splitting (incl. CRLF, no trailing newline) --- */
    fp = fopen(TMPFILE, "wb");
    OK(fp != NULL);
    fputs("[    1.0] one\r\n[    2.0] two\n[    3.0] thr", fp);
    fclose(fp);

    OK(logfile_load(&lf, TMPFILE, template_builtin("dmesg"), 0) == 0);
    OK(lf.nents == 3);
    OK(lf.nmatched == 3);
    OK(lf.unterminated == 1);
    OK(lf.ents[0].raw.len == strlen("[    1.0] one")); /* \r stripped */
    OK(lf.ents[2].lineno == 3);

    /* --- append: the unterminated tail must be re-parsed ------------ */
    fp = fopen(TMPFILE, "ab");
    fputs("ee\n[    4.0] four\n", fp);
    fclose(fp);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 4);
    OK(lf.nmatched == 4);
    OK(lf.unterminated == 0);
    {
        const char *l3 = lf.buf + lf.ents[2].raw.off;
        OK(lf.ents[2].raw.len == strlen("[    3.0] three"));
        OK(!memcmp(l3, "[    3.0] three", lf.ents[2].raw.len));
    }

    /* --- no change ---------------------------------------------------- */
    OK(logfile_refresh(&lf) == 0);

    /* --- truncation triggers a full reload ---------------------------- */
    fp = fopen(TMPFILE, "wb");
    fputs("[    9.0] fresh\n", fp);
    fclose(fp);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 1);
    OK(lf.ents[0].lineno == 1);

    /* --- continuation flags ------------------------------------------- */
    fp = fopen(TMPFILE, "wb");
    fputs("[    1.0] parent\n  indented continuation\n[    2.0] next\n",
          fp);
    fclose(fp);
    logfile_free(&lf);
    OK(logfile_load(&lf, TMPFILE, template_builtin("dmesg"), 0) == 0);
    OK(lf.nents == 3);
    OK(lf.ents[0].matched == 1 && lf.ents[0].cont == 0);
    OK(lf.ents[1].matched == 0 && lf.ents[1].cont == 1);
    OK(lf.ents[2].matched == 1 && lf.ents[2].cont == 0);

    /* --- template switching -------------------------------------------- */
    OK(logfile_reparse(&lf, template_builtin("plain")) == 0);
    OK(lf.nmatched == 3); /* plain matches everything */

    /* --- empty file ------------------------------------------------------ */
    fp = fopen(TMPFILE, "wb");
    fclose(fp);
    logfile_free(&lf);
    OK(logfile_load(&lf, TMPFILE, NULL, 0) == 0);
    OK(lf.nents == 0);
    OK(lf.tpl != NULL); /* autodetect falls back to plain */

    logfile_free(&lf);
    remove(TMPFILE);

    test_hp();

#ifndef _WIN32
    test_stream();
#endif

    return TEST_REPORT("test_logfile");
}
