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
/* Interactive template creation ("lx <log> -g <out.lxt>").
 *
 * Modelled on prompt-driven tools like `git add -p` and debconf: every
 * question shows its default in [brackets], Enter accepts it, and the
 * result is validated against the actual log file before anything is
 * written. Runs in normal cooked terminal mode (no raw TUI). */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "term.h"
#include "tstamp.h"
#include "util.h"
#include "wizard.h"

/* ------------------------------------------------------------------ */
/* line segmentation heuristics                                        */

/* candidate timestamp layouts, tried longest-consumption-first;
 * %Z variants must come after their base so ties prefer the base */
static const char *TS_CANDIDATES[] = {
    "%Y-%m-%d %H:%M:%S.%f%z", "%Y-%m-%d %H:%M:%S.%f%z%Z",
    "%Y-%m-%d %H:%M:%S.%f",   "%Y-%m-%d %H:%M:%S,%f",
    "%Y-%m-%d %H:%M:%S%z",    "%Y-%m-%d %H:%M:%S",
    "%Y-%m-%dT%H:%M:%S.%f%z", "%Y-%m-%dT%H:%M:%S.%f",
    "%Y-%m-%dT%H:%M:%S%z",    "%Y-%m-%dT%H:%M:%S",
    "%d/%b/%Y:%H:%M:%S %z",   "%b %e %H:%M:%S",
    "%H:%M:%S.%f",            "%H:%M:%S",
};

/* common severity vocabularies offered as enum suggestions */
static const char *SEV_SETS[] = {
    "DEBUG|INFO|WARNING|ERROR|CRITICAL",
    "TRACE|DEBUG|INFO|WARN|ERROR|FATAL",
    "VRB|DBG|INF|WRN|ERR|FTL",
    "Default|Info|Debug|Error|Fault",
    "TRC|DBG|INF|WRN|ERR|FTL",
};

static int is_litc(char c)
{
    return strchr("|[](){}\",;:=", c) != NULL;
}

/* Try every timestamp layout at s[pos..]; returns consumed length
 * (ignoring trailing whitespace-only wins) and sets fmt64. */
static size_t try_timestamp(const char *s, size_t n, size_t pos,
                            char *fmt64)
{
    size_t best = 0, k;
    for (k = 0; k < sizeof TS_CANDIDATES / sizeof TS_CANDIDATES[0]; k++) {
        double v;
        size_t c;
        if (ts_parse(s + pos, n - pos, TS_CANDIDATES[k], &v, &c) == 0) {
            while (c > 0 && (s[pos + c - 1] == ' ' || s[pos + c - 1] == '\t'))
                c--;
            if (c > best) {
                best = c;
                xcopy(fmt64, 64, TS_CANDIDATES[k]);
            }
        }
    }
    /* a bare number is not a timestamp */
    return best >= 8 ? best : 0;
}

/* Does the rest of the line look like one free-text message? */
static int tail_is_message(const char *s, size_t pos, size_t n)
{
    size_t i, firstsp = n;
    int spaces = 0;

    for (i = pos; i < n; i++) {
        if (strchr("|[](){}\"=", s[i]))
            return 0; /* structure ahead */
        if (i + 1 < n && s[i] == ' ' && s[i + 1] == ' ')
            return 0; /* aligned columns ahead */
        if (s[i] == ' ') {
            if (firstsp == n)
                firstsp = i;
            spaces++;
        }
    }
    if (!spaces)
        return 0;
    if (find_sub(s + pos, n - pos, " - ", 3, 0) >= 0)
        return 0; /* dash-separated columns ahead */
    for (i = pos; i < firstsp; i++)
        if (s[i] == ':')
            return 0; /* likely "proc: message" */
    return 1;
}

static const char *severity_set_for(const char *word, size_t len)
{
    size_t k;
    char buf[288];
    for (k = 0; k < sizeof SEV_SETS / sizeof SEV_SETS[0]; k++) {
        char *tok, *save;
        xcopy(buf, sizeof buf, SEV_SETS[k]);
        for (tok = buf; tok; tok = save) {
            save = strchr(tok, '|');
            if (save)
                *save++ = 0;
            if (strlen(tok) == len && !strncmp(tok, word, len))
                return SEV_SETS[k];
        }
    }
    return NULL;
}

int wizard_segment(const char *s, size_t n, WPiece *out, int maxp)
{
    size_t pos = 0;
    int np = 0;

    /* trailing whitespace would end up as a trailing literal that some
     * lines lack; the matcher tolerates extra input whitespace anyway */
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        n--;

    while (pos < n && np < maxp) {
        WPiece *p = &out[np];
        size_t st = pos;

        memset(p, 0, sizeof *p);

        /* piece budget nearly exhausted: a template that covers only a
         * prefix of the line can never match, so everything left
         * becomes one free-text field */
        if (np == maxp - 1) {
            p->is_field = 1;
            p->type = FT_STRING;
            p->off = pos;
            p->len = n - pos;
            np++;
            break;
        }

        /* literal separator run: whitespace, punctuation, and dashes
         * that have a space on either side (so "db-01" stays whole) */
        while (pos < n &&
               (s[pos] == ' ' || s[pos] == '\t' || is_litc(s[pos]) ||
                (s[pos] == '-' &&
                 ((pos > st && s[pos - 1] == ' ') ||
                  (pos + 1 < n && s[pos + 1] == ' ')))))
            pos++;
        if (pos > st) {
            const char *colon = memchr(s + st, ':', pos - st);

            p->is_field = 0;
            p->off = st;
            p->len = pos - st;
            np++;

            /* a colon inside a separator marks the "proc: message"
             * convention: what follows is one free-text message, not
             * more columns (log show, syslog, serilog, ...). End the
             * literal right after ": " so message decorations that only
             * some lines carry - e.g. log show's "(sender)" - stay in
             * the message instead of the pattern. */
            if (colon && np < maxp) {
                size_t cut = (size_t)(colon - s) + 1;
                while (cut < pos && (s[cut] == ' ' || s[cut] == '\t'))
                    cut++;
                if (cut < n) {
                    p->len = cut - st;
                    p = &out[np];
                    memset(p, 0, sizeof *p);
                    p->is_field = 1;
                    p->type = FT_STRING;
                    p->off = cut;
                    p->len = n - cut;
                    np++;
                    break;
                }
            }
            continue;
        }

        p->is_field = 1;
        p->off = pos;

        /* 1. timestamp */
        {
            size_t c = try_timestamp(s, n, pos, p->tsfmt);
            if (c) {
                p->type = FT_TIMESTAMP;
                p->len = c;
                pos += c;
                np++;
                continue;
            }
        }

        /* 2. free-text tail message */
        if (tail_is_message(s, pos, n)) {
            p->type = FT_STRING;
            p->len = n - pos;
            pos = n;
            np++;
            continue;
        }

        /* 3. decimal number (ints and floats; "0x16b3" stays a word) */
        {
            size_t q = pos;
            int dots = 0;
            while (q < n && (isdigit((unsigned char)s[q]) ||
                             (s[q] == '.' && dots == 0 &&
                              q + 1 < n && isdigit((unsigned char)s[q + 1]) &&
                              ++dots)))
                q++;
            if (q > pos &&
                (q == n || s[q] == ' ' || s[q] == '\t' || is_litc(s[q]))) {
                p->type = dots ? FT_FLOAT : FT_INT;
                p->len = q - pos;
                pos = q;
                np++;
                continue;
            }
        }

        /* 4. word */
        {
            size_t q = pos;
            while (q < n && s[q] != ' ' && s[q] != '\t' && !is_litc(s[q]))
                q++;
            if (q == pos) /* defensive; is_litc chars belong to literals */
                q = pos + 1;
            p->type = FT_WORD;
            p->len = q - pos;
            p->values = severity_set_for(s + pos, q - pos);
            if (p->values)
                p->type = FT_ENUM;
            pos = q;
            np++;
        }
    }
    return np;
}

/* ------------------------------------------------------------------ */
/* prompting helpers                                                   */

static int ask(const char *q, const char *def, char *out, size_t outsz)
{
    char buf[512];
    char *s;

    if (def && *def)
        printf("  %s [%s]: ", q, def);
    else
        printf("  %s: ", q);
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin)) {
        printf("\n");
        return -1;
    }
    s = trim(buf);
    if (!*s && def)
        xcopy(out, outsz, def);
    else
        xcopy(out, outsz, s);
    return 0;
}

static int ask_yn(const char *q, int def)
{
    char buf[64];
    for (;;) {
        if (ask(def ? q : q, def ? "Y/n" : "y/N", buf, sizeof buf))
            return -1;
        if (!strcmp(buf, "Y/n") || !strcmp(buf, "y/N"))
            return def;
        if (buf[0] == 'y' || buf[0] == 'Y')
            return 1;
        if (buf[0] == 'n' || buf[0] == 'N')
            return 0;
        printf("  please answer y or n\n");
    }
}

static int name_valid(const char *s)
{
    size_t i;
    if (!*s || (!isalpha((unsigned char)s[0]) && s[0] != '_'))
        return 0;
    for (i = 0; s[i]; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '_')
            return 0;
    return 1;
}

static void swatch(int rgb)
{
    if (rgb >= 0)
        printf("  colour: \x1b[48;5;%dm      \x1b[0m #%06X\n",
               rgb_to_256((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF,
                          rgb & 0xFF),
               rgb);
}

/* ------------------------------------------------------------------ */
/* wizard state                                                        */

typedef struct {
    int is_field;
    char name[32];
    FieldType type;
    char tsfmt[64];
    char values[256];
    char unit[16];
    int rgb;      /* -1 none */
    int severity; /* marked as the severity field */
    size_t off, len;
} WF;

static void suggest_name(const WF *wf, int n, FieldType t, int is_msg,
                         char *out, size_t outsz)
{
    const char *base = "field";
    char cand[32];
    int suffix = 1, i;

    if (is_msg)
        base = "message";
    else if (t == FT_TIMESTAMP)
        base = "timestamp";
    else if (t == FT_ENUM)
        base = "level";
    else if (t == FT_INT || t == FT_FLOAT)
        base = "value";
    else if (t == FT_STRING)
        base = "message";

    xcopy(cand, sizeof cand, base);
    for (;;) {
        int used = 0;
        for (i = 0; i < n; i++)
            if (wf[i].is_field && !strcmp(wf[i].name, cand))
                used = 1;
        if (!used)
            break;
        snprintf(cand, sizeof cand, "%s%d", base, ++suffix);
    }
    xcopy(out, outsz, cand);
}

static int name_used(const WF *wf, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (wf[i].is_field && !strcmp(wf[i].name, name))
            return 1;
    return 0;
}

/* append to the generated template text, clamped */
static void app(char *buf, size_t bufsz, size_t *off, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (*off >= bufsz - 1)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf + *off, bufsz - *off, fmt, ap);
    va_end(ap);
    if (n > 0)
        *off += (size_t)n;
    if (*off > bufsz - 1)
        *off = bufsz - 1;
}

static void gen_lxt(char *buf, size_t bufsz, const char *name,
                    const char *desc, const WF *wf, int nwf,
                    const char *line)
{
    size_t off = 0;
    int i;

    app(buf, bufsz, &off, "name: %s\n", name);
    if (*desc)
        app(buf, bufsz, &off, "description: %s\n", desc);
    app(buf, bufsz, &off, "entry: ");
    for (i = 0; i < nwf; i++) {
        if (wf[i].is_field) {
            app(buf, bufsz, &off, "%%{%s}", wf[i].name);
        } else {
            size_t k;
            for (k = 0; k < wf[i].len; k++) {
                char c = line[wf[i].off + k];
                if (c == '%')
                    app(buf, bufsz, &off, "%%%%");
                else
                    app(buf, bufsz, &off, "%c", c);
            }
        }
    }
    app(buf, bufsz, &off, "\n");
    for (i = 0; i < nwf; i++) {
        if (!wf[i].is_field)
            continue;
        app(buf, bufsz, &off, "field %s: type=%s", wf[i].name,
            field_type_name(wf[i].type));
        if (wf[i].type == FT_TIMESTAMP && wf[i].tsfmt[0])
            app(buf, bufsz, &off, " format=\"%s\"", wf[i].tsfmt);
        if (wf[i].type == FT_ENUM && wf[i].values[0])
            app(buf, bufsz, &off, " values=%s", wf[i].values);
        if (wf[i].unit[0])
            app(buf, bufsz, &off, " unit=%s", wf[i].unit);
        if (wf[i].rgb >= 0)
            app(buf, bufsz, &off, " color=#%06X", wf[i].rgb);
        if (wf[i].severity)
            app(buf, bufsz, &off, " severity=yes");
        app(buf, bufsz, &off, "\n");
    }
}

/* render the sample line coloured with the freshly built template */
static void print_preview(const Template *t, const char *s, size_t n)
{
    Span fsp[TPL_MAX_FIELDS];
    double fn[TPL_MAX_FIELDS];
    size_t i;
    int sev = -1;

    printf("  ");
    if (!template_match(t, s, n, fsp, fn)) {
        printf("%.*s   <- did not match!\n", (int)n, s);
        return;
    }
    if (t->level_field >= 0)
        sev = severity_class(s + fsp[t->level_field].off,
                             fsp[t->level_field].len);
    for (i = 0; i < n; i++) {
        int f, cl = -1;
        for (f = 0; f < t->nfields; f++) {
            if (fsp[f].len && i >= fsp[f].off &&
                i < fsp[f].off + fsp[f].len) {
                cl = f;
                break;
            }
        }
        if (sev >= 4) {
            printf("\x1b[%sm", sev >= 5 ? "1;31" : "31");
        } else if (cl >= 0 && cl == t->level_field) {
            printf("\x1b[%sm", sev == 3 ? "33"
                               : sev == 1 ? "36"
                               : sev == 0 ? "90"
                                          : "0");
        } else if (cl >= 0 && t->fields[cl].c256 >= 0) {
            printf("\x1b[38;5;%dm", t->fields[cl].c256);
        } else {
            printf("\x1b[0m");
        }
        putchar(s[i]);
    }
    printf("\x1b[0m\n");
}

/* ------------------------------------------------------------------ */

/* Read a file, or all of standard input when path is "-" (piped log
 * data; blocks until the producer closes the pipe). */
static char *read_file(const char *path, size_t *lenout)
{
    FILE *fp;
    char *buf;
    size_t cap = 1 << 16, len = 0, n;
    int from_stdin = !strcmp(path, "-");

    fp = from_stdin ? stdin : fopen(path, "rb");
    if (!fp)
        return NULL;
    buf = xmalloc(cap);
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (cap - len < 2) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    if (!from_stdin)
        fclose(fp);
    buf[len] = 0;
    *lenout = len;
    return buf;
}

#define MAX_SAMPLES 200

static int collect_lines(const char *buf, size_t len, Span *lines, int max)
{
    size_t i = 0;
    int nl = 0;

    while (i < len && nl < max) {
        const char *e = memchr(buf + i, '\n', len - i);
        size_t ll = e ? (size_t)(e - (buf + i)) : len - i;
        size_t l2 = ll;
        if (l2 && buf[i + l2 - 1] == '\r')
            l2--;
        if (l2) {
            lines[nl].off = i;
            lines[nl].len = l2;
            nl++;
        }
        if (!e)
            break;
        i += ll + 1;
    }
    return nl;
}

static const char *DEF_COLORS[] = { "#8A8A8A",  /* timestamp */
                                    "#626262",  /* numbers */
                                    "#5FAFD7",  /* first word */
                                    "#00AFAF" }; /* later words */

int wizard_run(const char *logpath, const char *outpath)
{
    char *buf;
    size_t blen;
    Span lines[MAX_SAMPLES];
    int nlines, li, np, i, nwordf = 0, have_sev = 0;
    WPiece pieces[WIZ_MAX_PIECES];
    WF wf[WIZ_MAX_PIECES];
    const char *line;
    size_t llen;
    char name[64], desc[128], in[512];
    static char lxt[8192];
    Template tpl;
    char err[256];

    int from_stdin = !strcmp(logpath, "-");

    if (!term_is_tty()) {
        fprintf(stderr, "lx: the template wizard needs a terminal\n");
        return -1;
    }
    buf = read_file(logpath, &blen);
    if (!buf) {
        fprintf(stderr, "lx: cannot read '%s'\n", logpath);
        return -1;
    }
    if (from_stdin) {
        /* the pipe is exhausted; prompts read from the terminal */
        if (!freopen(TERM_TTY_DEVICE, "r", stdin)) {
            fprintf(stderr,
                    "lx: cannot open the terminal for wizard prompts\n");
            free(buf);
            return -1;
        }
        logpath = "(stdin)";
    }
    nlines = collect_lines(buf, blen, lines, MAX_SAMPLES);
    if (!nlines) {
        fprintf(stderr, "lx: '%s' has no usable lines\n", logpath);
        free(buf);
        return -1;
    }

    printf("lx template wizard\n");
    printf("==================\n");
    printf("Building '%s' from '%s'.\n", outpath, logpath);
    printf("Enter accepts the [default] of every question; Ctrl-D aborts.\n");

    /* -------- pick a sample line -------------------------------- */
    li = 0;
    while (li < nlines &&
           (buf[lines[li].off] == ' ' || buf[lines[li].off] == '\t'))
        li++; /* skip indented continuation lines */
    if (li == nlines)
        li = 0;
    for (;;) {
        printf("\nSample line %d of %d:\n\n    %.*s\n\n", li + 1, nlines,
               (int)lines[li].len, buf + lines[li].off);
        if (ask("use this line? y=yes, n=next, or a line number",
                "y", in, sizeof in))
            goto aborted;
        if (in[0] == 'y' || in[0] == 'Y')
            break;
        if (in[0] == 'n' || in[0] == 'N') {
            li = (li + 1) % nlines;
        } else if (isdigit((unsigned char)in[0])) {
            int v = atoi(in);
            if (v >= 1 && v <= nlines)
                li = v - 1;
            else
                printf("  no such line\n");
        } else if (in[0] == 'q') {
            goto aborted;
        }
    }
    line = buf + lines[li].off;
    llen = lines[li].len;

    /* -------- name and description ------------------------------- */
    {
        char defname[64];
        const char *b = strrchr(outpath, '/');
        const char *dot;
        b = b ? b + 1 : outpath;
        xcopy(defname, sizeof defname, b);
        dot = strrchr(defname, '.');
        if (dot)
            defname[dot - defname] = 0;
        printf("\n");
        if (ask("template name", defname, name, sizeof name))
            goto aborted;
        if (ask("description (optional)", "", desc, sizeof desc))
            goto aborted;
    }

    /* -------- segment + present pieces --------------------------- */
    np = wizard_segment(line, llen, pieces, WIZ_MAX_PIECES);
    printf("\nThe line splits into these pieces:\n\n");
    for (i = 0; i < np; i++) {
        printf("  %2d  %-36.*s %s", i + 1, (int)pieces[i].len,
               line + pieces[i].off,
               pieces[i].is_field ? field_type_name(pieces[i].type)
                                  : "literal");
        if (pieces[i].is_field && pieces[i].type == FT_TIMESTAMP)
            printf("  (format \"%s\")", pieces[i].tsfmt);
        if (pieces[i].is_field && pieces[i].values)
            printf("  (%s)", pieces[i].values);
        printf("\n");
    }
    printf("\nFor each field piece: press Enter to accept the suggested\n"
           "name, type a new one, '-' to turn it into literal text, or\n"
           "'*' to take everything from here to the end of the line as\n"
           "one final field.\n");

    /* -------- per-piece prompts ----------------------------------- */
    memset(wf, 0, sizeof wf);
    for (i = 0; i < np; i++) {
        WF *w = &wf[i];
        char sug[32], tname[32];

        w->is_field = pieces[i].is_field;
        w->off = pieces[i].off;
        w->len = pieces[i].len;
        w->type = pieces[i].type;
        w->rgb = -1;
        xcopy(w->tsfmt, sizeof w->tsfmt, pieces[i].tsfmt);
        if (pieces[i].values)
            xcopy(w->values, sizeof w->values, pieces[i].values);
        if (!w->is_field)
            continue;

        printf("\npiece %d: \"%.*s\"\n", i + 1, (int)w->len,
               line + w->off);
        suggest_name(wf, i, w->type,
                     w->type == FT_STRING, sug, sizeof sug);
        for (;;) {
            if (ask("field name ('-' literal, '*' rest of line)", sug,
                    w->name, sizeof w->name))
                goto aborted;
            if (!strcmp(w->name, "-") || !strcmp(w->name, "*") ||
                (name_valid(w->name) && !name_used(wf, i, w->name)))
                break;
            printf("  invalid or duplicate name (letters, digits, _)\n");
        }
        if (!strcmp(w->name, "-")) {
            w->is_field = 0;
            continue;
        }
        if (!strcmp(w->name, "*")) {
            w->is_field = 1;
            w->type = FT_STRING;
            w->len = llen - w->off;
            w->tsfmt[0] = w->values[0] = 0;
            suggest_name(wf, i, FT_STRING, 1, sug, sizeof sug);
            for (;;) {
                if (ask("field name", sug, w->name, sizeof w->name))
                    goto aborted;
                if (name_valid(w->name) && !name_used(wf, i, w->name))
                    break;
                printf("  invalid or duplicate name\n");
            }
            np = i + 1; /* drop the remaining pieces */
        }

        /* type */
        for (;;) {
            int tt;
            if (ask("type (string/word/int/float/timestamp/enum)",
                    field_type_name(w->type), tname, sizeof tname))
                goto aborted;
            tt = field_type_parse(tname);
            if (tt >= 0) {
                w->type = (FieldType)tt;
                break;
            }
            printf("  unknown type '%s'\n", tname);
        }

        /* type-specific attributes */
        if (w->type == FT_TIMESTAMP) {
            for (;;) {
                double v;
                size_t c;
                if (ask("timestamp format", w->tsfmt[0] ? w->tsfmt : NULL,
                        in, sizeof in))
                    goto aborted;
                if (ts_parse(line + w->off, w->len, in, &v, &c) == 0) {
                    xcopy(w->tsfmt, sizeof w->tsfmt, in);
                    break;
                }
                printf("  '%s' does not parse \"%.*s\"", in, (int)w->len,
                       line + w->off);
                {
                    int keep = ask_yn("keep it anyway?", 0);
                    if (keep < 0)
                        goto aborted;
                    if (keep) {
                        xcopy(w->tsfmt, sizeof w->tsfmt, in);
                        break;
                    }
                }
            }
        } else if (w->type == FT_ENUM) {
            char defv[256];
            if (w->values[0]) {
                xcopy(defv, sizeof defv, w->values);
            } else {
                snprintf(defv, sizeof defv, "%.*s", (int)w->len,
                         line + w->off);
            }
            if (ask("allowed values, separated by '|'", defv, w->values,
                    sizeof w->values))
                goto aborted;
            if (!have_sev) {
                int dy = severity_class(line + w->off, w->len) >= 0;
                int r = ask_yn("use this field for severity colouring?",
                               dy);
                if (r < 0)
                    goto aborted;
                if (r) {
                    w->severity = 1;
                    have_sev = 1;
                }
            }
        } else if (w->type == FT_INT || w->type == FT_FLOAT) {
            if (ask("unit (informational, e.g. s, ms, bytes; optional)",
                    "", w->unit, sizeof w->unit))
                goto aborted;
        }

        /* colour */
        if (!w->severity) {
            const char *defc;
            if (w->type == FT_TIMESTAMP)
                defc = DEF_COLORS[0];
            else if (w->type == FT_INT || w->type == FT_FLOAT)
                defc = DEF_COLORS[1];
            else if (w->type == FT_WORD)
                defc = DEF_COLORS[nwordf++ ? 3 : 2];
            else
                defc = "-";
            for (;;) {
                int rgb;
                if (ask("colour hex ('-' for none)", defc, in, sizeof in))
                    goto aborted;
                if (!strcmp(in, "-") || !in[0]) {
                    w->rgb = -1;
                    break;
                }
                rgb = template_parse_color(in);
                if (rgb >= 0) {
                    w->rgb = rgb;
                    swatch(rgb);
                    break;
                }
                printf("  bad colour (expected hex like #5FAFD7)\n");
            }
        }
    }

    /* -------- generate, validate, report -------------------------- */
    gen_lxt(lxt, sizeof lxt, name, desc, wf, np, line);
    if (template_parse_text(&tpl, lxt, err, sizeof err)) {
        fprintf(stderr, "lx: generated template is invalid: %s\n", err);
        free(buf);
        return -1;
    }

    {
        Span fsp[TPL_MAX_FIELDS];
        double fn[TPL_MAX_FIELDS];
        int total = 0, hit = 0, shown = 0;
        printf("\nPreview:\n\n");
        print_preview(&tpl, line, llen);
        for (i = 0; i < nlines; i++) {
            if (buf[lines[i].off] == ' ' || buf[lines[i].off] == '\t')
                continue; /* continuation lines are expected to differ */
            total++;
            if (template_match(&tpl, buf + lines[i].off, lines[i].len,
                               fsp, fn))
                hit++;
            else if (shown < 2) {
                if (!shown)
                    printf("\nLines that do NOT match:\n");
                printf("    %.*s\n", (int)(lines[i].len > 76
                                               ? 76
                                               : lines[i].len),
                       buf + lines[i].off);
                shown++;
            }
        }
        printf("\nTemplate matches %d of %d sampled lines "
               "(indented continuation lines excluded).\n", hit, total);
    }

    printf("\nGenerated template:\n\n");
    {
        const char *p = lxt;
        while (*p) {
            const char *e = strchr(p, '\n');
            printf("    %.*s\n", e ? (int)(e - p) : (int)strlen(p), p);
            if (!e)
                break;
            p = e + 1;
        }
    }

    {
        int r = ask_yn("\nwrite this template?", 1);
        if (r <= 0) {
            template_free(&tpl);
            free(buf);
            if (r < 0)
                goto aborted;
            printf("Nothing written.\n");
            return -1;
        }
    }
    {
        FILE *fp = fopen(outpath, "wb");
        if (!fp) {
            fprintf(stderr, "lx: cannot write '%s'\n", outpath);
            template_free(&tpl);
            free(buf);
            return -1;
        }
        fputs(lxt, fp);
        fclose(fp);
    }
    template_free(&tpl);
    free(buf);

    if (from_stdin) {
        /* the piped data is consumed; it cannot be re-opened */
        printf("Wrote %s. Use it with: <producer> | lx -T %s\n", outpath,
               outpath);
        return 1;
    }
    printf("Wrote %s. Use it with: lx %s -T %s\n", outpath, logpath,
           outpath);
    {
        int r = ask_yn("open the log with the new template now?", 1);
        if (r == 1)
            return 0;
        return 1;
    }

aborted:
    printf("Aborted; nothing written.\n");
    free(buf);
    return -1;
}
