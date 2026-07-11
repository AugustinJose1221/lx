#ifndef LX_TEMPLATE_H
#define LX_TEMPLATE_H

#include <stddef.h>

#include "lx.h"

#define TPL_MAX_FIELDS 16
#define TPL_MAX_TOKENS 48
#define TPL_MAX_VALUES 24

typedef enum {
    FT_STRING = 0, /* greedy text (up to the next literal / end of line) */
    FT_WORD,       /* text without whitespace */
    FT_INT,        /* integer */
    FT_FLOAT,      /* floating point number */
    FT_TIMESTAMP,  /* timestamp, parsed with `tsfmt` */
    FT_ENUM        /* one of a fixed set of values */
} FieldType;

typedef struct {
    char name[32];
    FieldType type;
    char unit[16];                /* informational unit, e.g. "s", "bytes" */
    char tsfmt[64];               /* timestamp format (FT_TIMESTAMP) */
    char *values[TPL_MAX_VALUES]; /* allowed values (FT_ENUM), longest first */
    int nvalues;
    int rgb;  /* display colour 0xRRGGBB from color=#..., or -1 */
    int c256; /* the same colour as an xterm-256 index, or -1 */
} TField;

typedef struct {
    int field; /* index into fields[], or -1 for a literal token */
    char lit[64];
    int litlen;
} TToken;

typedef struct {
    char name[32];
    char desc[96];
    TField fields[TPL_MAX_FIELDS];
    int nfields;
    TToken toks[TPL_MAX_TOKENS];
    int ntoks;
    int level_field; /* index of the severity field, or -1 */
} Template;

/* Built-in templates. */
const Template *template_builtin(const char *name);
int template_builtin_count(void);
const Template *template_builtin_at(int i);

/* Pick the built-in template that best matches the buffer contents.
 * Falls back to "plain". Never returns NULL. */
const Template *template_autodetect(const char *buf, size_t len);

/* Parse a template definition. Returns 0 on success; on error returns -1
 * and writes a message into err. */
int template_parse_text(Template *t, const char *text, char *err,
                        size_t errsz);
int template_load_file(Template *t, const char *path, char *err,
                       size_t errsz);
void template_free(Template *t);

/* Index of a named field, or -1. */
int template_field_index(const Template *t, const char *name,
                         size_t namelen);

/* Match one log line against the template. On success fills fsp[] (spans
 * relative to the start of the line) and fnum[] (numeric value for
 * int/float/timestamp fields, NAN otherwise) for every field and returns 1.
 * Returns 0 when the line does not match. */
int template_match(const Template *t, const char *s, size_t len, Span *fsp,
                   double *fnum);

const char *field_type_name(FieldType ft);

/* Classify a severity value: 0 trace, 1 debug, 2 info, 3 warning,
 * 4 error, 5 fatal; -1 unknown. */
int severity_class(const char *v, size_t n);

/* Nearest xterm-256 palette index for a 24-bit colour. */
int rgb_to_256(int r, int g, int b);

#endif
