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
 * Log loading and incremental parsing implementation. Two backends
 * (see logfile.h): the standard heap-and-parse-everything path, and
 * the high-performance mmap + lazy-parse path (-H) that keeps only
 * 8 bytes + 1 bit per line and is intended to become the native
 * implementation. All consumers go through the accessor functions at
 * the bottom so the backends stay interchangeable.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logfile.h"
#include "pipein.h"
#include "util.h"

#define STREAM_CHUNK 65536
#define AUTODETECT_WINDOW 65536

const char *logfile_data(const LogFile *lf)
{
    return lf->hp ? lf->map.data : lf->buf;
}

const Span *logfile_fspans(const LogFile *lf, size_t idx)
{
    return lf->fspans + idx * (size_t)lf->tpl->nfields;
}

const double *logfile_fnums(const LogFile *lf, size_t idx)
{
    return lf->fnums + idx * (size_t)lf->tpl->nfields;
}

/* ------------------------------------------------------------------ */
/* visibility bits (hp backend)                                        */

static int bit_get(const unsigned char *b, size_t i)
{
    return (b[i >> 3] >> (i & 7)) & 1;
}

static void bit_set(unsigned char *b, size_t i, int v)
{
    if (v)
        b[i >> 3] |= (unsigned char)(1u << (i & 7));
    else
        b[i >> 3] &= (unsigned char)~(1u << (i & 7));
}

/* ------------------------------------------------------------------ */
/* standard backend: heap buffer + fully parsed entries                */

static void append_entry(LogFile *lf, size_t off, size_t len)
{
    size_t nf = (size_t)lf->tpl->nfields;
    Entry *e;
    Span *fs;
    double *fn;

    if (len && lf->buf[off + len - 1] == '\r')
        len--;

    if (lf->nents == lf->entcap) {
        size_t nc = lf->entcap ? lf->entcap * 2 : 256;
        lf->ents = xrealloc(lf->ents, nc * sizeof *lf->ents);
        lf->fspans = xrealloc(lf->fspans, nc * nf * sizeof *lf->fspans);
        lf->fnums = xrealloc(lf->fnums, nc * nf * sizeof *lf->fnums);
        lf->entcap = nc;
    }
    e = &lf->ents[lf->nents];
    fs = lf->fspans + lf->nents * nf;
    fn = lf->fnums + lf->nents * nf;

    e->raw.off = off;
    e->raw.len = len;
    e->lineno = lf->nents + 1;
    e->matched =
        (unsigned char)template_match(lf->tpl, lf->buf + off, len, fs, fn);
    e->cont = (unsigned char)(!e->matched && lf->nents > 0);
    e->visible = 1;
    if (e->matched)
        lf->nmatched++;
    lf->nents++;
    lf->nvisible++;
}

static void parse_from(LogFile *lf, size_t start)
{
    size_t i = start;
    lf->unterminated = 0;
    while (i < lf->len) {
        const char *nl = memchr(lf->buf + i, '\n', lf->len - i);
        if (nl) {
            append_entry(lf, i, (size_t)(nl - (lf->buf + i)));
            i = (size_t)(nl - lf->buf) + 1;
        } else {
            append_entry(lf, i, lf->len - i);
            lf->unterminated = 1;
            i = lf->len;
        }
    }
}

/* Parse everything appended to lf->buf since the last parse; a
 * previously unterminated tail line is re-parsed from its start. */
static void parse_appended(LogFile *lf)
{
    size_t start;

    if (lf->unterminated && lf->nents) {
        lf->nents--;
        if (lf->ents[lf->nents].matched)
            lf->nmatched--;
        if (lf->ents[lf->nents].visible)
            lf->nvisible--;
        start = lf->ents[lf->nents].raw.off;
        lf->unterminated = 0;
    } else if (lf->nents) {
        size_t last = lf->ents[lf->nents - 1].raw.off;
        const char *nl = memchr(lf->buf + last, '\n', lf->len - last);
        start = nl ? (size_t)(nl - lf->buf) + 1 : lf->len;
    } else {
        start = 0;
    }
    lf->refresh_from = lf->nents;
    parse_from(lf, start);
}

/* Pull whatever the pipe has right now. Returns 1 when bytes arrived
 * or the stream ended. */
static int stream_pull(LogFile *lf)
{
    int changed = 0;

    if (lf->stream_eof)
        return 0;
    for (;;) {
        long r;
        lf->buf = xgrow(lf->buf, &lf->cap, lf->len + STREAM_CHUNK + 1, 1);
        r = pipein_read(lf->buf + lf->len, STREAM_CHUNK);
        if (r > 0) {
            lf->len += (size_t)r;
            changed = 1;
            continue;
        }
        if (r < 0) {
            lf->stream_eof = 1;
            changed = 1;
        }
        break;
    }
    if (changed)
        lf->buf[lf->len] = 0;
    return changed;
}

static int read_whole(LogFile *lf, FILE *fp)
{
    size_t n;
    lf->len = 0;
    if (!lf->buf) {
        lf->cap = 1 << 16;
        lf->buf = xmalloc(lf->cap);
    }
    while ((n = fread(lf->buf + lf->len, 1, lf->cap - lf->len - 1, fp)) > 0) {
        lf->len += n;
        if (lf->cap - lf->len < 2) {
            lf->cap *= 2;
            lf->buf = xrealloc(lf->buf, lf->cap);
        }
    }
    lf->buf[lf->len] = 0;
    return ferror(fp) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* hp backend: mmap + line-offset index + lazy parsing                 */

static void hp_push_line(LogFile *lf, size_t off)
{
    lf->lineoff = xgrow(lf->lineoff, &lf->linecap, lf->nents + 1,
                        sizeof *lf->lineoff);
    if ((lf->nents >> 3) + 1 > lf->visbytes) {
        size_t oldb = lf->visbytes;
        lf->visbits = xgrow(lf->visbits, &lf->visbytes,
                            (lf->nents >> 3) + 4096, 1);
        memset(lf->visbits + oldb, 0, lf->visbytes - oldb);
    }
    lf->lineoff[lf->nents] = off;
    bit_set(lf->visbits, lf->nents, 1);
    lf->nents++;
    lf->nvisible++;
}

/* Index line starts by scanning for newlines from scan_from onward.
 * A start is recorded only when at least one byte follows the newline,
 * so a trailing newline does not create an empty phantom line. */
static void hp_index(LogFile *lf, size_t scan_from)
{
    const char *d = lf->map.data;
    size_t len = lf->map.len, p = scan_from;

    if (lf->nents == 0 && len)
        hp_push_line(lf, 0);
    while (p < len) {
        const char *nl = memchr(d + p, '\n', len - p);
        size_t q;
        if (!nl)
            break;
        q = (size_t)(nl - d) + 1;
        if (q < len)
            hp_push_line(lf, q);
        p = q;
    }
    lf->unterminated = len ? d[len - 1] != '\n' : 0;
}

static int hp_load(LogFile *lf, const char *path, const Template *tpl)
{
    lf->hp = 1;
    lf->hp_vidx = (size_t)-1;
    lf->path = xstrdup(path);
    if (filemap_open(&lf->map, path))
        return -1;
    hp_index(lf, 0);
    lf->tpl = tpl ? tpl
                  : template_autodetect(lf->map.data ? lf->map.data : "",
                                        lf->map.len < AUTODETECT_WINDOW
                                            ? lf->map.len
                                            : AUTODETECT_WINDOW);
    /* nmatched stays 0: counting it would require parsing every line,
     * which is exactly what hp mode avoids */
    return 0;
}

static int hp_refresh(LogFile *lf)
{
    long long sz = filemap_size(lf->path);
    size_t old;

    if (sz < 0)
        return 0;
    if ((size_t)sz < lf->map.len) {
        /* truncated / rotated: reload from scratch */
        const Template *tpl = lf->tpl;
        char *path = lf->path;
        lf->path = NULL;
        filemap_close(&lf->map);
        free(lf->lineoff);
        free(lf->visbits);
        memset(lf, 0, sizeof *lf);
        if (hp_load(lf, path, tpl)) {
            free(path);
            return -1;
        }
        free(path);
        lf->refresh_from = 0;
        return 1;
    }
    if ((size_t)sz == lf->map.len)
        return 0;

    old = lf->map.len;
    if (filemap_remap(&lf->map, lf->path))
        return -1;
    /* the previously unterminated tail line keeps its start but its
     * content changed: re-filter it along with the new lines */
    lf->refresh_from =
        (lf->unterminated && lf->nents) ? lf->nents - 1 : lf->nents;
    hp_index(lf, old ? old - 1 : 0);
    lf->hp_vidx = (size_t)-1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */

int logfile_load(LogFile *lf, const char *path, const Template *tpl,
                 int hp)
{
    FILE *fp;

    memset(lf, 0, sizeof *lf);

    if (!strcmp(path, "-")) {
        /* piped standard input (hp is ignored: there is no file to
         * map): consume the initial burst, then let logfile_refresh()
         * stream the rest */
        lf->stream = 1;
        lf->path = xstrdup("(stdin)");
        if (pipein_start())
            return -1;
        lf->cap = 1 << 16;
        lf->buf = xmalloc(lf->cap);
        for (;;) {
            long r;
            lf->buf = xgrow(lf->buf, &lf->cap, lf->len + STREAM_CHUNK + 1,
                            1);
            r = pipein_read(lf->buf + lf->len, STREAM_CHUNK);
            if (r > 0) {
                lf->len += (size_t)r;
                continue;
            }
            if (r < 0) {
                lf->stream_eof = 1;
                break;
            }
            if (lf->len == 0) {
                pipein_wait(200); /* nothing yet: wait for first data */
                continue;
            }
            /* got a burst; stop once the producer pauses briefly */
            if (!pipein_wait(150))
                break;
        }
        lf->buf[lf->len] = 0;
    } else if (hp) {
        return hp_load(lf, path, tpl);
    } else {
        fp = fopen(path, "rb");
        if (!fp)
            return -1;
        lf->path = xstrdup(path);
        if (read_whole(lf, fp)) {
            fclose(fp);
            return -1;
        }
        fclose(fp);
    }

    lf->tpl = tpl ? tpl : template_autodetect(lf->buf, lf->len);
    parse_from(lf, 0);
    return 0;
}

int logfile_refresh(LogFile *lf)
{
    FILE *fp;
    long sz;
    size_t n;

    if (lf->stream) {
        if (!stream_pull(lf))
            return 0;
        parse_appended(lf);
        return 1;
    }
    if (lf->hp)
        return hp_refresh(lf);

    fp = fopen(lf->path, "rb");
    if (!fp)
        return 0;
    if (fseek(fp, 0, SEEK_END) || (sz = ftell(fp)) < 0) {
        fclose(fp);
        return -1;
    }

    if ((size_t)sz < lf->len) {
        /* truncated / rotated: reload from scratch */
        const Template *tpl = lf->tpl;
        char *path = lf->path;
        fclose(fp);
        lf->path = NULL;
        free(lf->buf);
        free(lf->ents);
        free(lf->fspans);
        free(lf->fnums);
        if (logfile_load(lf, path, tpl, 0)) {
            free(path);
            return -1;
        }
        free(path);
        lf->refresh_from = 0;
        return 1;
    }
    if ((size_t)sz == lf->len) {
        fclose(fp);
        return 0;
    }

    /* appended data */
    lf->buf = xgrow(lf->buf, &lf->cap, (size_t)sz + 1, 1);
    if (fseek(fp, (long)lf->len, SEEK_SET)) {
        fclose(fp);
        return -1;
    }
    while (lf->len < (size_t)sz &&
           (n = fread(lf->buf + lf->len, 1, (size_t)sz - lf->len, fp)) > 0)
        lf->len += n;
    fclose(fp);
    lf->buf[lf->len] = 0;

    parse_appended(lf);
    return 1;
}

int logfile_reparse(LogFile *lf, const Template *tpl)
{
    size_t i, nf;

    lf->tpl = tpl;
    lf->nmatched = 0;

    if (lf->hp) {
        /* nothing is stored per line: just reset visibility */
        if (lf->visbits && lf->nents)
            memset(lf->visbits, 0xFF, (lf->nents >> 3) + 1);
        lf->nvisible = lf->nents;
        lf->hp_vidx = (size_t)-1;
        return 0;
    }

    nf = (size_t)tpl->nfields;
    free(lf->fspans);
    free(lf->fnums);
    lf->fspans = xmalloc((lf->entcap ? lf->entcap : 1) * nf *
                         sizeof *lf->fspans);
    lf->fnums = xmalloc((lf->entcap ? lf->entcap : 1) * nf *
                        sizeof *lf->fnums);
    for (i = 0; i < lf->nents; i++) {
        Entry *e = &lf->ents[i];
        e->matched = (unsigned char)template_match(
            tpl, lf->buf + e->raw.off, e->raw.len, lf->fspans + i * nf,
            lf->fnums + i * nf);
        e->cont = (unsigned char)(!e->matched && i > 0);
        e->visible = 1;
        if (e->matched)
            lf->nmatched++;
    }
    lf->nvisible = lf->nents;
    return 0;
}

void logfile_free(LogFile *lf)
{
    free(lf->path);
    free(lf->buf);
    free(lf->ents);
    free(lf->fspans);
    free(lf->fnums);
    filemap_close(&lf->map);
    free(lf->lineoff);
    free(lf->visbits);
    memset(lf, 0, sizeof *lf);
}

/* ------------------------------------------------------------------ */
/* entry accessors (both backends)                                     */

Span logfile_raw(const LogFile *lf, size_t idx)
{
    Span s;
    size_t end;

    if (!lf->hp)
        return lf->ents[idx].raw;

    s.off = lf->lineoff[idx];
    if (idx + 1 < lf->nents)
        end = lf->lineoff[idx + 1] - 1; /* the newline */
    else
        end = lf->map.len - (lf->unterminated ? 0 : 1);
    s.len = end - s.off;
    if (s.len && lf->map.data[s.off + s.len - 1] == '\r')
        s.len--;
    return s;
}

void logfile_view(LogFile *lf, size_t idx, EView *v)
{
    if (!lf->hp) {
        const Entry *e = &lf->ents[idx];
        v->raw = e->raw;
        v->lineno = e->lineno;
        v->matched = e->matched;
        v->cont = e->cont;
        v->visible = e->visible;
        v->fsp = logfile_fspans(lf, idx);
        v->fnum = logfile_fnums(lf, idx);
        return;
    }

    if (lf->hp_vidx != idx) {
        Span r = logfile_raw(lf, idx);
        lf->hp_vmatched = template_match(lf->tpl, lf->map.data + r.off,
                                         r.len, lf->hp_fsp, lf->hp_fnum);
        lf->hp_vidx = idx;
    }
    v->raw = logfile_raw(lf, idx);
    v->lineno = idx + 1;
    v->matched = lf->hp_vmatched;
    v->cont = !lf->hp_vmatched && idx > 0;
    v->visible = bit_get(lf->visbits, idx);
    v->fsp = lf->hp_fsp;
    v->fnum = lf->hp_fnum;
}

int logfile_is_visible(const LogFile *lf, size_t idx)
{
    return lf->hp ? bit_get(lf->visbits, idx) : lf->ents[idx].visible;
}

void logfile_set_visible(LogFile *lf, size_t idx, int v)
{
    if (lf->hp)
        bit_set(lf->visbits, idx, v);
    else
        lf->ents[idx].visible = (unsigned char)v;
}
