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
 * A loaded log with two backends behind one accessor API:
 *
 * standard: the whole file is copied to the heap and every line is
 * parsed up front (Entry + field spans per line). Fast for the
 * megabyte range, prohibitive for gigabytes.
 *
 * high-performance (hp, the -H flag): the file is mmap()ed read-only,
 * the only per-line storage is one byte offset (8 B) plus one
 * visibility bit, and template parsing happens on demand when a line
 * is viewed or filtered. This is what makes 1-15 GB files usable and
 * is intended to become the native implementation; new code must go
 * through logfile_view()/logfile_raw()/logfile_*_visible() rather than
 * touching Entry arrays, so both backends stay interchangeable.
 */
#ifndef LX_LOGFILE_H
#define LX_LOGFILE_H

#include <stddef.h>

#include "filemap.h"
#include "lx.h"
#include "template.h"

typedef struct {
    Span raw;      /* whole line, offsets into LogFile.buf */
    size_t lineno; /* 1-based line number in the file */
    unsigned char matched; /* line matched the template */
    unsigned char visible; /* passes the active filter */
    unsigned char cont;    /* continuation of the previous entry */
} Entry;

/* A read-only view of one entry, valid until the next logfile_view()
 * call on the same LogFile. In hp mode the fields are parsed on
 * demand; in standard mode they point at the precomputed arrays. */
typedef struct {
    Span raw;      /* offsets into logfile_data() */
    size_t lineno; /* == idx + 1 */
    int matched, cont, visible;
    const Span *fsp;    /* per-field spans, relative to the line start */
    const double *fnum; /* per-field numeric values (NAN when n/a) */
} EView;

typedef struct {
    char *path;
    /* standard backend */
    char *buf;
    size_t len, cap;
    Entry *ents;
    size_t entcap;
    Span *fspans;  /* nents * tpl->nfields slots */
    double *fnums;
    /* hp backend */
    int hp;
    FileMap map;
    size_t *lineoff;        /* start offset of each line */
    size_t linecap;
    unsigned char *visbits; /* one visibility bit per line */
    size_t visbytes;
    Span hp_fsp[TPL_MAX_FIELDS];   /* scratch for the current view */
    double hp_fnum[TPL_MAX_FIELDS];
    size_t hp_vidx;                /* index the scratch holds, or -1 */
    int hp_vmatched;
    /* shared */
    size_t nents;
    const Template *tpl;
    size_t nmatched, nvisible; /* nmatched is unknown (0) in hp mode */
    size_t refresh_from; /* first entry (re)parsed by the last refresh */
    int unterminated;    /* last line has no trailing newline (yet) */
    int stream;          /* reading a pipe on stdin instead of a file */
    int stream_eof;      /* the pipe reached end of stream */
} LogFile;

/* Load a file. When tpl is NULL, autodetect a built-in template.
 * The path "-" reads piped data from standard input instead: the
 * initial burst is consumed (waiting for the producer to pause or
 * close), further data arrives via logfile_refresh(). hp selects the
 * high-performance backend (ignored for streams, which have no file
 * to map). Returns 0 on success, -1 on I/O error. */
int logfile_load(LogFile *lf, const char *path, const Template *tpl,
                 int hp);

/* Re-read the source: appended file data (or newly piped bytes in
 * stream mode) is parsed incrementally; a truncated file is reloaded.
 * Sets refresh_from for incremental re-filtering.
 * Returns 1 if anything changed, 0 otherwise, -1 on error. */
int logfile_refresh(LogFile *lf);

/* Re-parse every line with a different template. */
int logfile_reparse(LogFile *lf, const Template *tpl);

void logfile_free(LogFile *lf);

/* Base pointer line offsets refer to (heap buffer or mapping). */
const char *logfile_data(const LogFile *lf);

/* Fill *v for entry idx (parses on demand in hp mode). */
void logfile_view(LogFile *lf, size_t idx, EView *v);

/* The line's byte span only - never parses; cheap in both modes. */
Span logfile_raw(const LogFile *lf, size_t idx);

int logfile_is_visible(const LogFile *lf, size_t idx);
void logfile_set_visible(LogFile *lf, size_t idx, int v);

const Span *logfile_fspans(const LogFile *lf, size_t idx);
const double *logfile_fnums(const LogFile *lf, size_t idx);

#endif
