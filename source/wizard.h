#ifndef LX_WIZARD_H
#define LX_WIZARD_H

#include <stddef.h>

#include "template.h"

#define WIZ_MAX_PIECES 24

/* One piece of a sample log line, as proposed by the auto-segmenter. */
typedef struct {
    int is_field;     /* 0 = literal separator text */
    FieldType type;   /* suggested type (when is_field) */
    size_t off, len;  /* span within the sample line */
    char tsfmt[64];   /* suggested timestamp format (FT_TIMESTAMP) */
    const char *values; /* suggested enum value set, or NULL */
} WPiece;

/* Split a sample log line into literal and field pieces using
 * heuristics (timestamps, severity keywords, numbers, words, trailing
 * free-text message). Exposed for unit testing. Returns the number of
 * pieces written to out. */
int wizard_segment(const char *s, size_t n, WPiece *out, int maxp);

/* Interactive template creation (lx <log> -g <out.lxt>): walks the user
 * through naming and typing the pieces of a sample line, validates the
 * result against the log file, and writes the .lxt template.
 * Returns 0 when written and the user wants to open the log with it,
 * 1 when written only, -1 when aborted or on error. */
int wizard_run(const char *logpath, const char *outpath);

#endif
