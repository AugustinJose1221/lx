#ifndef LX_LOGFILE_H
#define LX_LOGFILE_H

#include <stddef.h>

#include "lx.h"
#include "template.h"

typedef struct {
    Span raw;      /* whole line, offsets into LogFile.buf */
    size_t lineno; /* 1-based line number in the file */
    unsigned char matched; /* line matched the template */
    unsigned char visible; /* passes the active filter */
    unsigned char cont;    /* continuation of the previous entry */
} Entry;

typedef struct {
    char *path;
    char *buf;
    size_t len, cap;
    Entry *ents;
    size_t nents, entcap;
    /* Per-entry field data: nents * tpl->nfields slots. */
    Span *fspans;
    double *fnums;
    const Template *tpl;
    size_t nmatched, nvisible;
    int unterminated; /* last line has no trailing newline (yet) */
} LogFile;

/* Load a file. When tpl is NULL, autodetect a built-in template.
 * Returns 0 on success, -1 on I/O error. */
int logfile_load(LogFile *lf, const char *path, const Template *tpl);

/* Re-read the file: appended data is parsed incrementally, a truncated
 * file is reloaded. Returns 1 if anything changed, 0 otherwise, -1 on
 * error. */
int logfile_refresh(LogFile *lf);

/* Re-parse every line with a different template. */
int logfile_reparse(LogFile *lf, const Template *tpl);

void logfile_free(LogFile *lf);

const Span *logfile_fspans(const LogFile *lf, size_t idx);
const double *logfile_fnums(const LogFile *lf, size_t idx);

#endif
