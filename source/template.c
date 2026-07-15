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
 * Template engine implementation: .lxt definition parsing, the
 * built-in templates, auto-detection, line matching against entry
 * pattern variants, severity classes, colours, and .lxt export.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "template.h"
#include "tstamp.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */

const char *field_type_name(FieldType ft)
{
    switch (ft) {
    case FT_STRING:    return "string";
    case FT_WORD:      return "word";
    case FT_INT:       return "int";
    case FT_FLOAT:     return "float";
    case FT_TIMESTAMP: return "timestamp";
    case FT_ENUM:      return "enum";
    }
    return "?";
}

static int type_from_name(const char *s)
{
    if (str_ieq(s, "string") || str_ieq(s, "text"))
        return FT_STRING;
    if (str_ieq(s, "word"))
        return FT_WORD;
    if (str_ieq(s, "int") || str_ieq(s, "integer"))
        return FT_INT;
    if (str_ieq(s, "float") || str_ieq(s, "double") || str_ieq(s, "number"))
        return FT_FLOAT;
    if (str_ieq(s, "timestamp"))
        return FT_TIMESTAMP;
    if (str_ieq(s, "enum"))
        return FT_ENUM;
    return -1;
}

int field_type_parse(const char *name)
{
    return type_from_name(name);
}

int template_field_index(const Template *t, const char *name, size_t namelen)
{
    int i;
    for (i = 0; i < t->nfields; i++) {
        if (strlen(t->fields[i].name) == namelen &&
            !strncmp(t->fields[i].name, name, namelen))
            return i;
    }
    return -1;
}

static int field_get_or_create(Template *t, const char *name, size_t nl,
                               char *err, size_t errsz)
{
    TField *f;
    int i = template_field_index(t, name, nl);
    if (i >= 0)
        return i;
    if (t->nfields == TPL_MAX_FIELDS) {
        snprintf(err, errsz, "too many fields (max %d)", TPL_MAX_FIELDS);
        return -1;
    }
    f = &t->fields[t->nfields];
    memset(f, 0, sizeof *f);
    if (nl > sizeof f->name - 1)
        nl = sizeof f->name - 1;
    memcpy(f->name, name, nl);
    f->name[nl] = 0;
    f->type = FT_STRING;
    f->rgb = -1;
    f->c256 = -1;
    return t->nfields++;
}

/* ------------------------------------------------------------------ */
/* colours                                                             */

int rgb_to_256(int r, int g, int b)
{
    /* candidate from the 6x6x6 colour cube */
    int ir = r < 48 ? 0 : r < 115 ? 1 : (r - 35) / 40;
    int ig = g < 48 ? 0 : g < 115 ? 1 : (g - 35) / 40;
    int ib = b < 48 ? 0 : b < 115 ? 1 : (b - 35) / 40;
    int cr = ir ? 55 + ir * 40 : 0;
    int cg = ig ? 55 + ig * 40 : 0;
    int cb = ib ? 55 + ib * 40 : 0;
    /* candidate from the grayscale ramp */
    int avg = (r + g + b) / 3;
    int gi = avg > 238 ? 23 : avg < 3 ? 0 : (avg - 3) / 10;
    int gv = 8 + gi * 10;
    long dc = (long)(r - cr) * (r - cr) + (long)(g - cg) * (g - cg) +
              (long)(b - cb) * (b - cb);
    long dg = (long)(r - gv) * (r - gv) + (long)(g - gv) * (g - gv) +
              (long)(b - gv) * (b - gv);
    if (dg < dc)
        return 232 + gi;
    return 16 + 36 * ir + 6 * ig + ib;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* "#RRGGBB", "RRGGBB" or "#RGB" -> 0xRRGGBB, or -1. */
static int parse_hex_color(const char *s)
{
    int d[6], i;
    size_t l;
    if (*s == '#')
        s++;
    l = strlen(s);
    if (l == 3) {
        for (i = 0; i < 3; i++) {
            int v = hexval(s[i]);
            if (v < 0)
                return -1;
            d[i * 2] = d[i * 2 + 1] = v;
        }
    } else if (l == 6) {
        for (i = 0; i < 6; i++) {
            d[i] = hexval(s[i]);
            if (d[i] < 0)
                return -1;
        }
    } else {
        return -1;
    }
    return (d[0] << 20) | (d[1] << 16) | (d[2] << 12) | (d[3] << 8) |
           (d[4] << 4) | d[5];
}

int template_parse_color(const char *s)
{
    return parse_hex_color(s);
}

/* ------------------------------------------------------------------ */
/* template definition parsing                                         */

static int push_lit(TVariant *v, const char *lit, int len, char *err,
                    size_t errsz)
{
    while (len > 0) {
        TToken *tk;
        int c = len < 63 ? len : 63;
        if (v->ntoks == TPL_MAX_TOKENS) {
            snprintf(err, errsz, "entry pattern too complex");
            return -1;
        }
        tk = &v->toks[v->ntoks++];
        tk->field = -1;
        memcpy(tk->lit, lit, (size_t)c);
        tk->lit[c] = 0;
        tk->litlen = c;
        lit += c;
        len -= c;
    }
    return 0;
}

static int parse_pattern(Template *t, const char *val, char *err,
                         size_t errsz)
{
    char lit[64];
    int ll = 0;
    const char *p = val;
    TVariant *v;

    if (t->nvars == TPL_MAX_VARIANTS) {
        snprintf(err, errsz, "too many entry patterns (max %d)",
                 TPL_MAX_VARIANTS);
        return -1;
    }
    v = &t->vars[t->nvars];

    while (*p) {
        if (p[0] == '%' && p[1] == '{') {
            char spec[96];
            char *ty, *nm;
            size_t sl;
            int fi;
            const char *e = strchr(p + 2, '}');

            if (ll) {
                if (push_lit(v, lit, ll, err, errsz))
                    return -1;
                ll = 0;
            }
            if (!e) {
                snprintf(err, errsz, "unterminated %%{...} in entry pattern");
                return -1;
            }
            sl = (size_t)(e - (p + 2));
            if (sl > sizeof spec - 1) {
                snprintf(err, errsz, "field reference too long");
                return -1;
            }
            memcpy(spec, p + 2, sl);
            spec[sl] = 0;
            ty = strchr(spec, ':');
            if (ty)
                *ty++ = 0;
            nm = trim(spec);
            if (!*nm) {
                snprintf(err, errsz, "empty field name in entry pattern");
                return -1;
            }
            fi = field_get_or_create(t, nm, strlen(nm), err, errsz);
            if (fi < 0)
                return -1;
            if (ty) {
                int tt = type_from_name(trim(ty));
                if (tt < 0) {
                    snprintf(err, errsz, "unknown field type '%s'", ty);
                    return -1;
                }
                t->fields[fi].type = (FieldType)tt;
            }
            if (v->ntoks == TPL_MAX_TOKENS) {
                snprintf(err, errsz, "entry pattern too complex");
                return -1;
            }
            v->toks[v->ntoks].field = fi;
            v->toks[v->ntoks].lit[0] = 0;
            v->toks[v->ntoks].litlen = 0;
            v->ntoks++;
            p = e + 1;
        } else if (p[0] == '%' && p[1] == '%') {
            lit[ll++] = '%';
            p += 2;
            if (ll == 63) {
                if (push_lit(v, lit, ll, err, errsz))
                    return -1;
                ll = 0;
            }
        } else {
            lit[ll++] = *p++;
            if (ll == 63) {
                if (push_lit(v, lit, ll, err, errsz))
                    return -1;
                ll = 0;
            }
        }
    }
    if (ll && push_lit(v, lit, ll, err, errsz))
        return -1;
    if (!v->ntoks) {
        snprintf(err, errsz, "empty entry pattern");
        return -1;
    }
    t->nvars++;
    return 0;
}

static void sort_values_by_len(TField *f)
{
    int i, j;
    for (i = 1; i < f->nvalues; i++) {
        char *v = f->values[i];
        size_t vl = strlen(v);
        for (j = i; j > 0 && strlen(f->values[j - 1]) < vl; j--)
            f->values[j] = f->values[j - 1];
        f->values[j] = v;
    }
}

static int parse_field_attrs(Template *t, int fi, const char *val, char *err,
                             size_t errsz)
{
    TField *f = &t->fields[fi];
    const char *p = val;

    while (*p) {
        const char *ks;
        size_t kl, vl = 0;
        char vbuf[256];

        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (!*p)
            break;
        ks = p;
        while (*p && *p != '=' && *p != ' ' && *p != '\t')
            p++;
        if (*p != '=') {
            snprintf(err, errsz, "field %s: expected key=value near '%s'",
                     f->name, ks);
            return -1;
        }
        kl = (size_t)(p - ks);
        p++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && vl < sizeof vbuf - 1)
                vbuf[vl++] = *p++;
            if (*p != '"') {
                snprintf(err, errsz, "field %s: unterminated quote", f->name);
                return -1;
            }
            p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != ',' &&
                   vl < sizeof vbuf - 1)
                vbuf[vl++] = *p++;
        }
        vbuf[vl] = 0;

#define KEQ(k) (kl == strlen(k) && !strncmp(ks, k, kl))
        if (KEQ("type")) {
            int ty = type_from_name(vbuf);
            if (ty < 0) {
                snprintf(err, errsz, "field %s: unknown type '%s'", f->name,
                         vbuf);
                return -1;
            }
            f->type = (FieldType)ty;
        } else if (KEQ("format")) {
            xcopy(f->tsfmt, sizeof f->tsfmt, vbuf);
            if (f->type == FT_STRING)
                f->type = FT_TIMESTAMP;
        } else if (KEQ("values")) {
            char *tok, *save = vbuf;
            while ((tok = save) != NULL && *save) {
                char *sep = strpbrk(save, "|,");
                if (sep) {
                    *sep = 0;
                    save = sep + 1;
                } else {
                    save += strlen(save);
                }
                tok = trim(tok);
                if (*tok) {
                    if (f->nvalues == TPL_MAX_VALUES) {
                        snprintf(err, errsz, "field %s: too many values",
                                 f->name);
                        return -1;
                    }
                    f->values[f->nvalues++] = xstrdup(tok);
                }
            }
            if (f->type == FT_STRING || f->type == FT_WORD)
                f->type = FT_ENUM;
            sort_values_by_len(f);
        } else if (KEQ("unit")) {
            xcopy(f->unit, sizeof f->unit, vbuf);
        } else if (KEQ("severity")) {
            if (str_ieq(vbuf, "yes") || str_ieq(vbuf, "true") ||
                !strcmp(vbuf, "1"))
                t->level_field = fi;
        } else if (KEQ("color") || KEQ("colour")) {
            int rgb = parse_hex_color(vbuf);
            if (rgb < 0) {
                snprintf(err, errsz,
                         "field %s: bad color '%s' (expected hex like "
                         "#5FAFD7)", f->name, vbuf);
                return -1;
            }
            f->rgb = rgb;
            f->c256 = rgb_to_256((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF,
                                 rgb & 0xFF);
        } else {
            snprintf(err, errsz, "field %s: unknown attribute '%.*s'",
                     f->name, (int)kl, ks);
            return -1;
        }
#undef KEQ
    }
    return 0;
}

int template_parse_text(Template *t, const char *text, char *err,
                        size_t errsz)
{
    const char *p = text;
    int lno = 0, i;
    static const char *LEVEL_NAMES[] = { "level",    "severity", "lvl",
                                         "loglevel", "priority", "pri" };

    memset(t, 0, sizeof *t);
    t->level_field = -1;

    while (*p) {
        char line[512];
        char *s, *c, *key, *val;
        const char *e = strchr(p, '\n');
        size_t ll = e ? (size_t)(e - p) : strlen(p);

        lno++;
        if (ll > sizeof line - 1) {
            snprintf(err, errsz, "line %d: too long", lno);
            return -1;
        }
        memcpy(line, p, ll);
        line[ll] = 0;
        p = e ? e + 1 : p + ll;

        s = trim(line);
        if (!*s || *s == '#')
            continue;
        c = strchr(s, ':');
        if (!c) {
            snprintf(err, errsz, "line %d: expected 'key: value'", lno);
            return -1;
        }
        *c = 0;
        key = trim(s);
        val = trim(c + 1);

        if (!strcmp(key, "name")) {
            xcopy(t->name, sizeof t->name, val);
        } else if (!strcmp(key, "description")) {
            xcopy(t->desc, sizeof t->desc, val);
        } else if (!strcmp(key, "entry")) {
            if (parse_pattern(t, val, err, errsz))
                return -1;
        } else if (!strncmp(key, "field", 5) &&
                   (key[5] == ' ' || key[5] == '\t')) {
            char *fname = trim(key + 5);
            int fi = field_get_or_create(t, fname, strlen(fname), err, errsz);
            if (fi < 0)
                return -1;
            if (parse_field_attrs(t, fi, val, err, errsz))
                return -1;
        } else {
            snprintf(err, errsz, "line %d: unknown key '%s'", lno, key);
            return -1;
        }
    }

    if (!t->nvars) {
        snprintf(err, errsz, "template has no 'entry:' pattern");
        return -1;
    }
    if (!t->name[0])
        xcopy(t->name, sizeof t->name, "custom");

    for (i = 0; i < t->nfields; i++) {
        size_t k;
        if (t->fields[i].type == FT_TIMESTAMP && !t->fields[i].tsfmt[0])
            xcopy(t->fields[i].tsfmt, sizeof t->fields[i].tsfmt,
                  "%Y-%m-%d %H:%M:%S");
        for (k = 0; k < sizeof LEVEL_NAMES / sizeof LEVEL_NAMES[0]; k++) {
            if (t->level_field < 0 &&
                str_ieq(t->fields[i].name, LEVEL_NAMES[k]))
                t->level_field = i;
        }
    }
    return 0;
}

int template_load_file(Template *t, const char *path, char *err,
                       size_t errsz)
{
    FILE *fp = fopen(path, "rb");
    char *buf;
    size_t cap = 4096, len = 0, n;
    int rc;

    if (!fp) {
        snprintf(err, errsz, "cannot open template file");
        return -1;
    }
    buf = xmalloc(cap);
    while ((n = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
        len += n;
        if (cap - len < 2) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    fclose(fp);
    buf[len] = 0;
    rc = template_parse_text(t, buf, err, errsz);
    free(buf);
    return rc;
}

void template_free(Template *t)
{
    int i, j;
    for (i = 0; i < t->nfields; i++)
        for (j = 0; j < t->fields[i].nvalues; j++)
            free(t->fields[i].values[j]);
    memset(t, 0, sizeof *t);
}

/* ------------------------------------------------------------------ */
/* export                                                              */

static int exp_put(char *buf, size_t bufsz, size_t *off, const char *s)
{
    size_t l = strlen(s);
    if (*off + l >= bufsz)
        return -1;
    memcpy(buf + *off, s, l);
    *off += l;
    buf[*off] = 0;
    return 0;
}

int template_export(const Template *t, char *buf, size_t bufsz)
{
    size_t off = 0;
    char tmp[512];
    int vi, ti, i;

#define PUT(s) do { if (exp_put(buf, bufsz, &off, (s))) return -1; } while (0)
    snprintf(tmp, sizeof tmp, "name: %s\n", t->name);
    PUT(tmp);
    if (t->desc[0]) {
        snprintf(tmp, sizeof tmp, "description: %s\n", t->desc);
        PUT(tmp);
    }
    PUT("\n");

    for (vi = 0; vi < t->nvars; vi++) {
        const TVariant *v = &t->vars[vi];
        PUT("entry: ");
        for (ti = 0; ti < v->ntoks; ti++) {
            const TToken *tk = &v->toks[ti];
            if (tk->field >= 0) {
                snprintf(tmp, sizeof tmp, "%%{%s}",
                         t->fields[tk->field].name);
                PUT(tmp);
            } else {
                int k;
                for (k = 0; k < tk->litlen; k++) {
                    if (tk->lit[k] == '%')
                        PUT("%%");
                    else {
                        char c[2] = { tk->lit[k], 0 };
                        PUT(c);
                    }
                }
            }
        }
        PUT("\n");
    }
    PUT("\n");

    for (i = 0; i < t->nfields; i++) {
        const TField *f = &t->fields[i];
        snprintf(tmp, sizeof tmp, "field %s: type=%s", f->name,
                 field_type_name(f->type));
        PUT(tmp);
        if (f->tsfmt[0]) {
            snprintf(tmp, sizeof tmp, " format=\"%s\"", f->tsfmt);
            PUT(tmp);
        }
        if (f->nvalues) {
            int k;
            PUT(" values=");
            for (k = 0; k < f->nvalues; k++) {
                snprintf(tmp, sizeof tmp, "%s%s", k ? "|" : "",
                         f->values[k]);
                PUT(tmp);
            }
        }
        if (f->unit[0]) {
            snprintf(tmp, sizeof tmp, " unit=%s", f->unit);
            PUT(tmp);
        }
        if (f->rgb >= 0) {
            snprintf(tmp, sizeof tmp, " color=#%06X", f->rgb);
            PUT(tmp);
        }
        if (t->level_field == i)
            PUT(" severity=yes");
        PUT("\n");
    }
#undef PUT
    return 0;
}

/* ------------------------------------------------------------------ */
/* line matching                                                       */

/* Match a literal token; a space in the literal matches one or more
 * spaces/tabs in the input (so "Jun  1" style padding still matches). */
static int lit_match(const char *s, size_t n, size_t *i, const char *lit,
                     int litlen)
{
    size_t p = *i;
    int k;
    for (k = 0; k < litlen; k++) {
        if (lit[k] == ' ') {
            size_t st = p;
            while (p < n && (s[p] == ' ' || s[p] == '\t'))
                p++;
            if (p == st)
                return 0;
            while (k + 1 < litlen && lit[k + 1] == ' ')
                k++;
        } else {
            if (p < n && s[p] == lit[k])
                p++;
            else
                return 0;
        }
    }
    *i = p;
    return 1;
}

static int iswordch(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int match_variant(const Template *t, const TVariant *v,
                         const char *s, size_t len, Span *fsp,
                         double *fnum)
{
    size_t i = 0;
    int ti;

    for (ti = 0; ti < t->nfields; ti++) {
        fsp[ti].off = 0;
        fsp[ti].len = 0;
        fnum[ti] = NAN;
    }

    for (ti = 0; ti < v->ntoks; ti++) {
        const TToken *tk = &v->toks[ti];
        const TField *f;
        const TToken *nx;
        size_t start;
        double num = NAN;

        if (tk->field < 0) {
            if (!lit_match(s, len, &i, tk->lit, tk->litlen))
                return 0;
            continue;
        }
        f = &t->fields[tk->field];
        nx = (ti + 1 < v->ntoks) ? &v->toks[ti + 1] : NULL;
        start = i;

        switch (f->type) {
        case FT_INT: {
            size_t p = i, d0;
            int neg = 0;
            double v = 0;
            if (p < len && (s[p] == '+' || s[p] == '-')) {
                neg = s[p] == '-';
                p++;
            }
            d0 = p;
            while (p < len && isdigit((unsigned char)s[p])) {
                v = v * 10 + (s[p] - '0');
                p++;
            }
            if (p == d0)
                return 0;
            num = neg ? -v : v;
            i = p;
            break;
        }
        case FT_FLOAT: {
            char tmp[48], *end;
            size_t p = i, k = 0, q;
            double v;
            while (p < len && (s[p] == ' ' || s[p] == '\t'))
                p++;
            q = p;
            while (q < len && k < sizeof tmp - 1 &&
                   (isdigit((unsigned char)s[q]) || s[q] == '+' ||
                    s[q] == '-' || s[q] == '.' || s[q] == 'e' ||
                    s[q] == 'E'))
                tmp[k++] = s[q++];
            tmp[k] = 0;
            v = strtod(tmp, &end);
            if (end == tmp)
                return 0;
            num = v;
            i = p + (size_t)(end - tmp);
            start = p;
            break;
        }
        case FT_TIMESTAMP: {
            size_t cons;
            double v;
            if (ts_parse(s + i, len - i, f->tsfmt, &v, &cons))
                return 0;
            num = v;
            i += cons;
            break;
        }
        case FT_ENUM: {
            int vi, hit = -1;
            for (vi = 0; vi < f->nvalues; vi++) {
                size_t vl = strlen(f->values[vi]);
                if (i + vl <= len && !memcmp(s + i, f->values[vi], vl)) {
                    /* word boundary so INFO does not half-match INF */
                    if (i + vl < len && iswordch(s[i + vl]) && vl &&
                        iswordch(f->values[vi][vl - 1]))
                        continue;
                    hit = vi;
                    i += vl;
                    break;
                }
            }
            if (hit < 0)
                return 0;
            break;
        }
        case FT_WORD:
        case FT_STRING: {
            if (nx && nx->field < 0) {
                /* non-greedy scan for the next literal */
                size_t p = i;
                int found = 0;
                for (; p <= len; p++) {
                    size_t q = p;
                    if (lit_match(s, len, &q, nx->lit, nx->litlen)) {
                        found = 1;
                        break;
                    }
                    if (f->type == FT_WORD && p < len &&
                        (s[p] == ' ' || s[p] == '\t'))
                        break;
                    if (p == len)
                        break;
                }
                if (!found)
                    return 0;
                if (p == i && f->type == FT_WORD)
                    return 0;
                i = p;
            } else if (f->type == FT_WORD) {
                size_t p = i;
                while (p < len && s[p] != ' ' && s[p] != '\t')
                    p++;
                if (p == i)
                    return 0;
                i = p;
            } else {
                i = len;
            }
            break;
        }
        }

        fsp[tk->field].off = start;
        fsp[tk->field].len = i - start;
        fnum[tk->field] = num;
    }

    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r'))
        i++;
    return i == len;
}

int template_match(const Template *t, const char *s, size_t len, Span *fsp,
                   double *fnum)
{
    int vi;
    for (vi = 0; vi < t->nvars; vi++)
        if (match_variant(t, &t->vars[vi], s, len, fsp, fnum))
            return 1;
    return 0;
}

int severity_class(const char *v, size_t n)
{
    static const struct { const char *pat; int cls; } M[] = {
        /* "default" (macOS unified log) must precede "fault": the
         * substring match would otherwise classify it as fatal */
        { "default", 2 },
        { "ftl", 5 }, { "fatal", 5 }, { "fault", 5 }, { "crit", 5 },
        { "emerg", 5 }, { "alert", 5 }, { "panic", 5 },
        { "err", 4 },
        { "wrn", 3 }, { "warn", 3 },
        { "inf", 2 }, { "notice", 2 },
        { "dbg", 1 }, { "debug", 1 },
        { "trc", 0 }, { "trace", 0 }, { "vrb", 0 }, { "verbose", 0 },
    };
    size_t k;
    for (k = 0; k < sizeof M / sizeof M[0]; k++)
        if (find_sub(v, n, M[k].pat, strlen(M[k].pat), 1) >= 0)
            return M[k].cls;
    return -1;
}

/* ------------------------------------------------------------------ */
/* built-in templates                                                  */

static const char *BI_TEXTS[] = {
    /* plain must stay first: it is the autodetect fallback */
    "name: plain\n"
    "description: unstructured text; the whole line is the message\n"
    "entry: %{message}\n"
    "field message: type=string\n",

    "name: syslog\n"
    "description: BSD syslog / RFC 3164 ('Jun  1 19:18:23 host proc[1]: msg')\n"
    "entry: %{timestamp} %{host} %{proc}: %{message}\n"
    "field timestamp: type=timestamp format=\"%b %e %H:%M:%S\" color=#8A8A8A\n"
    "field host: type=word color=#5FAFD7\n"
    "field proc: type=word color=#00AFAF\n"
    "field message: type=string\n",

    "name: syslog-iso\n"
    "description: rsyslog with ISO timestamps ('2026-06-01T19:18:23.123+02:00 host proc: msg')\n"
    "entry: %{timestamp} %{host} %{proc}: %{message}\n"
    "field timestamp: type=timestamp format=\"%Y-%m-%dT%H:%M:%S.%f%z\" color=#8A8A8A\n"
    "field host: type=word color=#5FAFD7\n"
    "field proc: type=word color=#00AFAF\n"
    "field message: type=string\n",

    "name: dmesg\n"
    "description: Linux kernel ring buffer ('[   12.345678] usb 1-1: ...')\n"
    "entry: [%{time}] %{message}\n"
    "field time: type=float unit=s color=#8A8A8A\n"
    "field message: type=string\n",

    "name: serilog\n"
    "description: Serilog output, '... [INF] msg' and '... UTC [INF]: msg' forms\n"
    "entry: %{timestamp} [%{level}]: %{message}\n"
    "entry: %{timestamp} [%{level}] %{message}\n"
    "field timestamp: type=timestamp format=\"%Y-%m-%d %H:%M:%S.%f%z%Z\" color=#8A8A8A\n"
    "field level: type=enum values=VRB|DBG|INF|WRN|ERR|FTL\n"
    "field message: type=string\n",

    "name: python\n"
    "description: Python logging ('2026-06-01 19:18:23,123 - root - INFO - msg')\n"
    "entry: %{timestamp} - %{logger} - %{level} - %{message}\n"
    "field timestamp: type=timestamp format=\"%Y-%m-%d %H:%M:%S,%f\" color=#8A8A8A\n"
    "field logger: type=word color=#00AFAF\n"
    "field level: type=enum values=DEBUG|INFO|WARNING|ERROR|CRITICAL\n"
    "field message: type=string\n",

    "name: macos\n"
    "description: macOS unified log, as printed by 'log show' / 'log stream'\n"
    "entry: %{timestamp} %{thread} %{type} %{activity} %{pid} %{ttl} %{process}: %{message}\n"
    "field timestamp: type=timestamp format=\"%Y-%m-%d %H:%M:%S.%f%z\" color=#8A8A8A\n"
    "field thread: type=word color=#626262\n"
    "field type: type=enum values=Default|Info|Debug|Error|Fault severity=yes\n"
    "field activity: type=word color=#626262\n"
    "field pid: type=int color=#626262\n"
    "field ttl: type=int color=#626262\n"
    "field process: type=word color=#00AFAF\n"
    "field message: type=string\n",

    "name: apache\n"
    "description: Apache/nginx common log format\n"
    "entry: %{host} %{ident} %{user} [%{timestamp}] \"%{request}\" %{status} %{size}\n"
    "field timestamp: type=timestamp format=\"%d/%b/%Y:%H:%M:%S %z\" color=#8A8A8A\n"
    "field host: type=word color=#5FAFD7\n"
    "field ident: type=word color=#626262\n"
    "field user: type=word color=#626262\n"
    "field request: type=string\n"
    "field status: type=int\n"
    "field size: type=word unit=bytes color=#626262\n",
};

#define N_BUILTIN ((int)(sizeof BI_TEXTS / sizeof BI_TEXTS[0]))

static Template g_bi[N_BUILTIN];
static int g_ready = 0;

static void init_builtins(void)
{
    int i;
    char err[256];
    if (g_ready)
        return;
    for (i = 0; i < N_BUILTIN; i++) {
        if (template_parse_text(&g_bi[i], BI_TEXTS[i], err, sizeof err)) {
            fprintf(stderr, "lx: internal template error: %s\n", err);
            exit(1);
        }
    }
    g_ready = 1;
}

int template_builtin_count(void)
{
    return N_BUILTIN;
}

const Template *template_builtin_at(int i)
{
    init_builtins();
    if (i < 0 || i >= N_BUILTIN)
        return NULL;
    return &g_bi[i];
}

const Template *template_builtin(const char *name)
{
    int i;
    init_builtins();
    for (i = 0; i < N_BUILTIN; i++)
        if (str_ieq(g_bi[i].name, name))
            return &g_bi[i];
    return NULL;
}

const Template *template_autodetect(const char *buf, size_t len)
{
    int ti, besti = -1;
    double best = 0.0;

    init_builtins();
    for (ti = 1; ti < N_BUILTIN; ti++) {
        Span sp[TPL_MAX_FIELDS];
        double fn[TPL_MAX_FIELDS];
        size_t i = 0;
        int total = 0, hit = 0;

        while (i < len && total < 50) {
            const char *nl = memchr(buf + i, '\n', len - i);
            size_t ll = nl ? (size_t)(nl - (buf + i)) : len - i;
            size_t l2 = ll;
            if (l2 && buf[i + l2 - 1] == '\r')
                l2--;
            /* indented lines are almost always continuations (stack
             * traces etc.); don't hold them against a template */
            if (l2 && buf[i] != ' ' && buf[i] != '\t') {
                total++;
                if (template_match(&g_bi[ti], buf + i, l2, sp, fn))
                    hit++;
            }
            if (!nl)
                break;
            i += ll + 1;
        }
        if (total) {
            double r = (double)hit / total;
            if (r > best) {
                best = r;
                besti = ti;
            }
        }
    }
    if (besti >= 0 && best >= 0.6)
        return &g_bi[besti];
    return &g_bi[0];
}
