#ifndef LX_FILTER_H
#define LX_FILTER_H

#include "logfile.h"
#include "template.h"

/* Filter expressions operate on the named fields of a template:
 *
 *   level == ERR
 *   level == ERR || level == WRN
 *   message ~ "connection timeout" && not level == DBG
 *   timestamp >= "2026-06-01 19:00:00" and timestamp < "2026-06-01 20:00:00"
 *   status >= 500
 *   raw ~ "kernel"        (raw: the unparsed line)
 *   line > 1000           (line: the line number)
 *
 * Operators: == != ~ (contains, case-insensitive) !~ > >= < <=
 * Logic:     && || ! (or: and, or, not), parentheses.
 * Values:    barewords, "quoted", 'quoted', numbers, timestamps (parsed
 *            with the field's own format, falling back to ISO forms).
 *
 * Timestamp values always compare chronologically, never as strings
 * (an unparseable value is a compile error; only ~ / !~ stay textual):
 *   timestamp == "2026-07-13"        the whole day
 *   timestamp == "2026-07-13 09:15"  that minute
 *   timestamp >= "09:18:00"          by time of day, on any date */

typedef struct FNode FNode;

FNode *filter_compile(const char *expr, const Template *t, char *err,
                      size_t errsz);
void filter_free(FNode *n);

/* Evaluate against entry idx of lf (visibility flags are not consulted). */
int filter_eval_entry(const FNode *n, const LogFile *lf, size_t idx);

/* Set the visible flag on every entry. A NULL filter shows everything.
 * Continuation lines inherit the visibility of their parent entry. */
void filter_apply(const FNode *n, LogFile *lf);

#endif
