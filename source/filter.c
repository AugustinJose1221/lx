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
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "tstamp.h"
#include "util.h"

enum { NK_AND, NK_OR, NK_NOT, NK_CMP };
enum { OP_EQ, OP_NE, OP_CONT, OP_NCONT, OP_GT, OP_GE, OP_LT, OP_LE };

#define FLD_RAW  (-1)
#define FLD_LINE (-2)

struct FNode {
    int kind;
    struct FNode *l, *r;
    /* NK_CMP */
    int field; /* index into template fields, FLD_RAW or FLD_LINE */
    int op;
    char *sval;
    size_t slen;
    double nval;
    int has_n;
    /* timestamp comparison semantics */
    int ts_cmp;  /* nval is a parsed timestamp */
    int ts_tod;  /* value had a time but no date: compare time of day */
    double gran; /* equality granularity in seconds: "2026-07-13" means
                    the whole day (86400), "...09:15" that minute (60);
                    0 = exact (fractional seconds given) */
};

typedef struct {
    const char *p;
    const Template *t;
    char *err;
    size_t errsz;
} P;

static FNode *parse_or(P *ps);

static void perr(P *ps, const char *fmt, const char *arg)
{
    if (!ps->err[0])
        snprintf(ps->err, ps->errsz, fmt, arg);
}

static void skipws(P *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t')
        ps->p++;
}

static int identch(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

/* Consume a case-insensitive keyword if present. */
static int word_is(P *ps, const char *kw)
{
    size_t l = strlen(kw), k;
    for (k = 0; k < l; k++)
        if (tolower((unsigned char)ps->p[k]) != kw[k])
            return 0;
    if (identch(ps->p[l]))
        return 0;
    ps->p += l;
    return 1;
}

static FNode *mknode(int kind)
{
    FNode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = kind;
    return n;
}

void filter_free(FNode *n)
{
    if (!n)
        return;
    filter_free(n->l);
    filter_free(n->r);
    free(n->sval);
    free(n);
}

static int parse_op(P *ps)
{
    skipws(ps);
    if (!strncmp(ps->p, "==", 2)) { ps->p += 2; return OP_EQ; }
    if (!strncmp(ps->p, "!=", 2)) { ps->p += 2; return OP_NE; }
    if (!strncmp(ps->p, "!~", 2)) { ps->p += 2; return OP_NCONT; }
    if (!strncmp(ps->p, ">=", 2)) { ps->p += 2; return OP_GE; }
    if (!strncmp(ps->p, "<=", 2)) { ps->p += 2; return OP_LE; }
    if (*ps->p == '~') { ps->p += 1; return OP_CONT; }
    if (*ps->p == '>') { ps->p += 1; return OP_GT; }
    if (*ps->p == '<') { ps->p += 1; return OP_LT; }
    if (*ps->p == '=') { ps->p += 1; return OP_EQ; }
    if (word_is(ps, "contains"))
        return OP_CONT;
    return -1;
}

static char *parse_value(P *ps, size_t *outlen)
{
    char buf[512];
    size_t l = 0;

    skipws(ps);
    if (*ps->p == '"' || *ps->p == '\'') {
        char q = *ps->p++;
        while (*ps->p && *ps->p != q && l < sizeof buf - 1)
            buf[l++] = *ps->p++;
        if (*ps->p != q) {
            perr(ps, "unterminated quoted value%s", "");
            return NULL;
        }
        ps->p++;
    } else {
        while (*ps->p && *ps->p != ' ' && *ps->p != '\t' && *ps->p != ')' &&
               *ps->p != '&' && *ps->p != '|' && l < sizeof buf - 1)
            buf[l++] = *ps->p++;
        if (!l) {
            perr(ps, "expected a value%s", "");
            return NULL;
        }
    }
    buf[l] = 0;
    *outlen = l;
    return xstrdup(buf);
}

static int is_order_op(int op)
{
    return op == OP_GT || op == OP_GE || op == OP_LT || op == OP_LE;
}

static const char *TS_FALLBACKS[] = {
    "%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%dT%H:%M:%S.%f",
    "%Y-%m-%d %H:%M:%S",    "%Y-%m-%dT%H:%M:%S",
    "%Y-%m-%d %H:%M",       "%Y-%m-%dT%H:%M",
    "%Y-%m-%d",
    "%b %e %H:%M:%S",       "%b %e %H:%M",
    "%H:%M:%S.%f",          "%H:%M:%S",          "%H:%M",
};

static FNode *parse_cmp(P *ps)
{
    char ident[64];
    size_t il = 0;
    int fidx, op;
    FNode *n;

    skipws(ps);
    while (identch(ps->p[il]) && il < sizeof ident - 1) {
        ident[il] = ps->p[il];
        il++;
    }
    if (!il) {
        perr(ps, "expected a field name near '%s'", ps->p);
        return NULL;
    }
    memcpy(ident + il, "", 1);
    ps->p += il;

    if (!strcmp(ident, "raw")) {
        fidx = FLD_RAW;
    } else if (!strcmp(ident, "line") || !strcmp(ident, "lineno")) {
        fidx = FLD_LINE;
    } else {
        fidx = template_field_index(ps->t, ident, il);
        if (fidx < 0) {
            perr(ps, "unknown field '%s' (see :help or the template)",
                 ident);
            return NULL;
        }
    }

    op = parse_op(ps);
    if (op < 0) {
        perr(ps, "expected an operator after '%s'", ident);
        return NULL;
    }

    n = mknode(NK_CMP);
    n->field = fidx;
    n->op = op;
    n->sval = parse_value(ps, &n->slen);
    if (!n->sval) {
        filter_free(n);
        return NULL;
    }

    if (fidx == FLD_LINE) {
        char *end;
        if (op == OP_CONT || op == OP_NCONT) {
            perr(ps, "'line' supports numeric comparisons only%s", "");
            filter_free(n);
            return NULL;
        }
        n->nval = strtod(n->sval, &end);
        if (end == n->sval || *end) {
            perr(ps, "'line' must be compared with a number, got '%s'",
                 n->sval);
            filter_free(n);
            return NULL;
        }
        n->has_n = 1;
    } else if (fidx >= 0) {
        FieldType ft = ps->t->fields[fidx].type;
        if (ft == FT_INT || ft == FT_FLOAT) {
            char *end;
            double v = strtod(n->sval, &end);
            if (end != n->sval && !*end) {
                n->nval = v;
                n->has_n = 1;
            } else if (is_order_op(op)) {
                perr(ps, "'%s' is numeric; compare it with a number",
                     ps->t->fields[fidx].name);
                filter_free(n);
                return NULL;
            }
        } else if (ft == FT_TIMESTAMP) {
            double v;
            size_t cons, k;
            unsigned fl = 0;
            if (!ts_parse2(n->sval, n->slen, ps->t->fields[fidx].tsfmt, &v,
                           &cons, &fl) && cons == n->slen) {
                n->nval = v;
                n->has_n = 1;
            } else {
                for (k = 0;
                     k < sizeof TS_FALLBACKS / sizeof TS_FALLBACKS[0]; k++) {
                    fl = 0;
                    if (!ts_parse2(n->sval, n->slen, TS_FALLBACKS[k], &v,
                                   &cons, &fl) && cons == n->slen) {
                        n->nval = v;
                        n->has_n = 1;
                        break;
                    }
                }
            }
            if (n->has_n) {
                n->ts_cmp = 1;
                /* a time without a date compares by time of day */
                n->ts_tod = (fl & TSF_HOUR) && !(fl & TSF_DATE);
                /* equality covers the whole granule the value names */
                n->gran = (fl & TSF_FRAC) ? 0.0
                          : (fl & TSF_SEC) ? 1.0
                          : (fl & TSF_MIN) ? 60.0
                          : (fl & TSF_HOUR) ? 3600.0
                                            : 86400.0;
            } else if (op != OP_CONT && op != OP_NCONT) {
                /* never fall back to string comparison for timestamps */
                perr(ps, "cannot parse '%s' as a timestamp", n->sval);
                filter_free(n);
                return NULL;
            }
        }
    }
    return n;
}

static FNode *parse_unary(P *ps)
{
    FNode *n;
    skipws(ps);
    if (*ps->p == '(') {
        ps->p++;
        n = parse_or(ps);
        if (!n)
            return NULL;
        skipws(ps);
        if (*ps->p != ')') {
            perr(ps, "missing ')'%s", "");
            filter_free(n);
            return NULL;
        }
        ps->p++;
        return n;
    }
    if (*ps->p == '!' && ps->p[1] != '=' && ps->p[1] != '~') {
        FNode *not;
        ps->p++;
        n = parse_unary(ps);
        if (!n)
            return NULL;
        not = mknode(NK_NOT);
        not->l = n;
        return not;
    }
    if (word_is(ps, "not")) {
        FNode *not;
        n = parse_unary(ps);
        if (!n)
            return NULL;
        not = mknode(NK_NOT);
        not->l = n;
        return not;
    }
    return parse_cmp(ps);
}

static FNode *parse_and(P *ps)
{
    FNode *l = parse_unary(ps);
    if (!l)
        return NULL;
    for (;;) {
        FNode *r, *n;
        skipws(ps);
        if (!strncmp(ps->p, "&&", 2))
            ps->p += 2;
        else if (!word_is(ps, "and"))
            return l;
        r = parse_unary(ps);
        if (!r) {
            filter_free(l);
            return NULL;
        }
        n = mknode(NK_AND);
        n->l = l;
        n->r = r;
        l = n;
    }
}

static FNode *parse_or(P *ps)
{
    FNode *l = parse_and(ps);
    if (!l)
        return NULL;
    for (;;) {
        FNode *r, *n;
        skipws(ps);
        if (!strncmp(ps->p, "||", 2))
            ps->p += 2;
        else if (!word_is(ps, "or"))
            return l;
        r = parse_and(ps);
        if (!r) {
            filter_free(l);
            return NULL;
        }
        n = mknode(NK_OR);
        n->l = l;
        n->r = r;
        l = n;
    }
}

FNode *filter_compile(const char *expr, const Template *t, char *err,
                      size_t errsz)
{
    P ps;
    FNode *n;

    ps.p = expr;
    ps.t = t;
    ps.err = err;
    ps.errsz = errsz;
    err[0] = 0;

    n = parse_or(&ps);
    if (!n)
        return NULL;
    skipws(&ps);
    if (*ps.p) {
        snprintf(err, errsz, "unexpected trailing input: '%s'", ps.p);
        filter_free(n);
        return NULL;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* evaluation                                                          */

static int lexcmp(const char *a, size_t al, const char *b, size_t bl)
{
    size_t m = al < bl ? al : bl;
    int c = memcmp(a, b, m);
    if (c)
        return c;
    return al < bl ? -1 : al > bl ? 1 : 0;
}

static int node_eval(const FNode *n, const LogFile *lf, size_t idx)
{
    const Entry *e = &lf->ents[idx];
    const char *line = lf->buf + e->raw.off;

    switch (n->kind) {
    case NK_AND:
        return node_eval(n->l, lf, idx) && node_eval(n->r, lf, idx);
    case NK_OR:
        return node_eval(n->l, lf, idx) || node_eval(n->r, lf, idx);
    case NK_NOT:
        return !node_eval(n->l, lf, idx);
    }

    /* NK_CMP */
    {
        const char *txt = NULL;
        size_t tl = 0;
        double num = 0;
        int isnum = 0;

        if (n->field == FLD_RAW) {
            txt = line;
            tl = e->raw.len;
        } else if (n->field == FLD_LINE) {
            num = (double)e->lineno;
            isnum = 1;
        } else {
            const Span *sp;
            if (!e->matched)
                return 0;
            sp = &logfile_fspans(lf, idx)[n->field];
            txt = line + sp->off;
            tl = sp->len;
            if (n->has_n && !isnan(logfile_fnums(lf, idx)[n->field])) {
                num = logfile_fnums(lf, idx)[n->field];
                isnum = 1;
            }
        }

        /* semantic timestamp comparison: by time of day when the value
         * carries no date, with equality meaning "within the granule
         * the value names" (a bare date = that whole day) */
        if (n->ts_cmp && isnum) {
            double l = num, r = n->nval;
            int eq;
            if (n->ts_tod) {
                l = fmod(l, 86400.0);
                r = fmod(r, 86400.0);
            }
            eq = n->gran > 0.0 ? (l >= r && l < r + n->gran)
                               : fabs(l - r) < 1e-9;
            switch (n->op) {
            case OP_EQ: return eq;
            case OP_NE: return !eq;
            case OP_GT: return l > r;
            case OP_GE: return l >= r;
            case OP_LT: return l < r;
            case OP_LE: return l <= r;
            }
        }

        switch (n->op) {
        case OP_EQ:
            if (isnum)
                return fabs(num - n->nval) < 1e-9;
            return tl == n->slen && !memcmp(txt, n->sval, tl);
        case OP_NE:
            if (isnum)
                return fabs(num - n->nval) >= 1e-9;
            return !(tl == n->slen && !memcmp(txt, n->sval, tl));
        case OP_CONT:
            return txt && find_sub(txt, tl, n->sval, n->slen, 1) >= 0;
        case OP_NCONT:
            return !(txt && find_sub(txt, tl, n->sval, n->slen, 1) >= 0);
        case OP_GT:
            return isnum ? num > n->nval
                         : lexcmp(txt, tl, n->sval, n->slen) > 0;
        case OP_GE:
            return isnum ? num >= n->nval
                         : lexcmp(txt, tl, n->sval, n->slen) >= 0;
        case OP_LT:
            return isnum ? num < n->nval
                         : lexcmp(txt, tl, n->sval, n->slen) < 0;
        case OP_LE:
            return isnum ? num <= n->nval
                         : lexcmp(txt, tl, n->sval, n->slen) <= 0;
        }
    }
    return 0;
}

int filter_eval_entry(const FNode *n, const LogFile *lf, size_t idx)
{
    if (!n)
        return 1;
    return node_eval(n, lf, idx);
}

void filter_apply(const FNode *n, LogFile *lf)
{
    size_t i;
    int prev = 1;

    lf->nvisible = 0;
    for (i = 0; i < lf->nents; i++) {
        Entry *e = &lf->ents[i];
        int v;
        if (!n)
            v = 1;
        else if (e->cont)
            v = prev; /* continuation lines follow their parent */
        else
            v = node_eval(n, lf, i);
        e->visible = (unsigned char)v;
        prev = v;
        lf->nvisible += (size_t)v;
    }
}
