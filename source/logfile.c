#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logfile.h"
#include "util.h"

const Span *logfile_fspans(const LogFile *lf, size_t idx)
{
    return lf->fspans + idx * (size_t)lf->tpl->nfields;
}

const double *logfile_fnums(const LogFile *lf, size_t idx)
{
    return lf->fnums + idx * (size_t)lf->tpl->nfields;
}

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

int logfile_load(LogFile *lf, const char *path, const Template *tpl)
{
    FILE *fp;

    memset(lf, 0, sizeof *lf);
    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    lf->path = xstrdup(path);
    if (read_whole(lf, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    lf->tpl = tpl ? tpl : template_autodetect(lf->buf, lf->len);
    parse_from(lf, 0);
    lf->nvisible = lf->nents;
    return 0;
}

int logfile_refresh(LogFile *lf)
{
    FILE *fp;
    long sz;
    size_t start, n;

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
        if (logfile_load(lf, path, tpl)) {
            free(path);
            return -1;
        }
        free(path);
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

    if (lf->unterminated && lf->nents) {
        /* the previous tail line may have been completed: re-parse it */
        lf->nents--;
        if (lf->ents[lf->nents].matched)
            lf->nmatched--;
        start = lf->ents[lf->nents].raw.off;
    } else {
        start = lf->nents ? lf->ents[lf->nents - 1].raw.off : 0;
        if (lf->nents) {
            const char *nl = memchr(lf->buf + start, '\n', lf->len - start);
            start = nl ? (size_t)(nl - lf->buf) + 1 : lf->len;
        }
    }
    parse_from(lf, start);
    return 1;
}

int logfile_reparse(LogFile *lf, const Template *tpl)
{
    size_t i, nf;

    lf->tpl = tpl;
    nf = (size_t)tpl->nfields;
    free(lf->fspans);
    free(lf->fnums);
    lf->fspans = xmalloc((lf->entcap ? lf->entcap : 1) * nf *
                         sizeof *lf->fspans);
    lf->fnums = xmalloc((lf->entcap ? lf->entcap : 1) * nf *
                        sizeof *lf->fnums);
    lf->nmatched = 0;
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
    memset(lf, 0, sizeof *lf);
}
