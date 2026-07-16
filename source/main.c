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
 * lx entry point: command-line parsing and dispatch to the viewer,
 * print mode (-P), the template wizard (-g), template listing (-l)
 * and export (-e).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "logfile.h"
#include "lx.h"
#include "pipein.h"
#include "template.h"
#include "term.h"
#include "ui.h"
#include "wizard.h"

static void usage(FILE *o)
{
    fputs("usage: lx [options] [logfile]\n"
          "       <producer> | lx [options] [-]\n"
          "\n"
          "options:\n"
          "  -t <name>   parse with a built-in template (see -l)\n"
          "  -T <file>   parse with a template loaded from <file> (.lxt)\n"
          "  -g <file>   interactively create a template for the log and\n"
          "              write it to <file> (.lxt), then offer to open the\n"
          "              log with it\n"
          "  -f <expr>   apply a filter expression at startup,\n"
          "              e.g. -f 'level==ERR && message ~ timeout'\n"
          "  -F          start in follow mode (like tail -f)\n"
          "  -H          high-performance mode for huge logs (1-15 GB):\n"
          "              the file is memory-mapped and parsed on demand\n"
          "              instead of being loaded and parsed up front\n"
          "  -d <n>      in the entry inspector (Enter), show up to <n>\n"
          "              wrapped lines per field value; without -d, values\n"
          "              are capped at 500 characters\n"
          "  -P          non-interactive: print the (filtered) lines to\n"
          "              stdout and exit\n"
          "  -e <name>   export the built-in template <name> as .lxt text\n"
          "              to stdout, e.g.: lx -e serilog > my.lxt\n"
          "  -l          list built-in templates\n"
          "  -h, --help  show this help\n"
          "  -V, --version  show version\n"
          "\n"
          "Without -t/-T the template is auto-detected from the file\n"
          "contents. With no logfile (or '-') the log is read from a\n"
          "pipe on standard input, e.g. 'journalctl -b | lx' or\n"
          "'log show --last 5m | lx'; the keyboard then comes from the\n"
          "controlling terminal. See lx(1) and\n"
          "documentation/template-format.md for the template file format\n"
          "and the filter language.\n",
          o);
}

static void list_templates(void)
{
    int i;
    for (i = 0; i < template_builtin_count(); i++) {
        const Template *t = template_builtin_at(i);
        printf("%-12s %s\n", t->name, t->desc);
    }
}

int main(int argc, char **argv)
{
    const char *file = NULL, *tname = NULL, *tfile = NULL, *fexpr = NULL;
    const char *genfile = NULL;
    int follow = 0, print = 0, detail_lines = 0, hp = 0, i, rc;
    static Template custom;
    const Template *tpl = NULL;
    static LogFile lf;
    FNode *fn = NULL;
    char err[256];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            if (!strcmp(a, "-t") && i + 1 < argc) {
                tname = argv[++i];
            } else if (!strcmp(a, "-T") && i + 1 < argc) {
                tfile = argv[++i];
            } else if (!strcmp(a, "-g") && i + 1 < argc) {
                genfile = argv[++i];
            } else if (!strcmp(a, "-f") && i + 1 < argc) {
                fexpr = argv[++i];
            } else if (!strcmp(a, "-F")) {
                follow = 1;
            } else if (!strcmp(a, "-H")) {
                hp = 1;
            } else if (!strcmp(a, "-d") && i + 1 < argc) {
                char *end;
                long v = strtol(argv[++i], &end, 10);
                if (*end || v < 1 || v > 10000) {
                    fprintf(stderr,
                            "lx: -d expects a line count between 1 and "
                            "10000\n");
                    return 2;
                }
                detail_lines = (int)v;
            } else if (!strcmp(a, "-P")) {
                print = 1;
            } else if (!strcmp(a, "-e") && i + 1 < argc) {
                static char buf[8192];
                const char *nm = argv[++i];
                const Template *t = template_builtin(nm);
                if (!t) {
                    fprintf(stderr,
                            "lx: unknown template '%s' (use -l to list "
                            "them)\n", nm);
                    return 2;
                }
                if (template_export(t, buf, sizeof buf)) {
                    fprintf(stderr, "lx: template too large to export\n");
                    return 1;
                }
                fputs(buf, stdout);
                return 0;
            } else if (!strcmp(a, "-l")) {
                list_templates();
                return 0;
            } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
                usage(stdout);
                return 0;
            } else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
                printf("lx %s\n"
                       "Copyright (C) 2026 Augustin Jose\n"
                       "License GPLv2+: GNU GPL version 2 or later.\n"
                       "This is free software: you are free to change and "
                       "redistribute it.\n"
                       "There is NO WARRANTY, to the extent permitted by "
                       "law.\n"
                       "Written by Augustin Jose.\n", LX_VERSION);
                return 0;
            } else {
                fprintf(stderr, "lx: unknown option '%s'\n\n", a);
                usage(stderr);
                return 2;
            }
        } else {
            if (file) {
                fprintf(stderr, "lx: only one file can be opened\n");
                return 2;
            }
            file = a;
        }
    }

    if (!file) {
        if (term_stdin_redirected()) {
            file = "-"; /* piped input: producer | lx */
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (!strcmp(file, "-") && !term_stdin_redirected()) {
        fprintf(stderr,
                "lx: '-' reads the log from a pipe, e.g.: "
                "journalctl -b | lx -\n");
        return 2;
    }

    if (hp && !strcmp(file, "-")) {
        fprintf(stderr, "lx: -H needs a file to map; reading the pipe "
                        "in standard mode\n");
        hp = 0;
    }

    if (genfile) {
        int wrc = wizard_run(file, genfile);
        if (wrc < 0)
            return 1;
        if (wrc == 1)
            return 0;
        tfile = genfile; /* open the log with the new template */
    }

    if (tfile) {
        if (template_load_file(&custom, tfile, err, sizeof err)) {
            fprintf(stderr, "lx: %s: %s\n", tfile, err);
            return 2;
        }
        tpl = &custom;
    } else if (tname) {
        tpl = template_builtin(tname);
        if (!tpl) {
            fprintf(stderr,
                    "lx: unknown template '%s' (use -l to list them)\n",
                    tname);
            return 2;
        }
    }

    if (logfile_load(&lf, file, tpl, hp)) {
        fprintf(stderr, "lx: cannot read '%s'\n", file);
        return 1;
    }

    if (fexpr) {
        fn = filter_compile(fexpr, lf.tpl, err, sizeof err);
        if (!fn) {
            fprintf(stderr, "lx: filter: %s\n", err);
            return 2;
        }
    }

    /* -P on a pipe behaves like grep: consume everything, then print */
    if (print && lf.stream) {
        while (!lf.stream_eof) {
            pipein_wait(1000);
            logfile_refresh(&lf);
        }
    }
    filter_apply(fn, &lf);

    if (print) {
        size_t k;
        for (k = 0; k < lf.nents; k++) {
            if (logfile_is_visible(&lf, k)) {
                Span r = logfile_raw(&lf, k);
                fwrite(logfile_data(&lf) + r.off, 1, r.len, stdout);
                fputc('\n', stdout);
            }
        }
        logfile_free(&lf);
        filter_free(fn);
        return 0;
    }

    if (!term_is_tty()) {
        fprintf(stderr,
                "lx: interactive mode needs a terminal; use -P to print "
                "to a pipe\n");
        return 2;
    }

    rc = ui_run(&lf, fexpr ? fexpr : "", fn, follow, detail_lines);
    logfile_free(&lf);
    return rc;
}
