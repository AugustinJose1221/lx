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
int filter_eval_entry(const FNode *n, LogFile *lf, size_t idx);

/* Set the visible flag on every entry. A NULL filter shows everything.
 * Continuation lines inherit the visibility of their parent entry. */
void filter_apply(const FNode *n, LogFile *lf);

/* Re-evaluate only entries from `from` onward (used after a refresh
 * appended new lines, so a follow tick does not rescan the whole log). */
void filter_apply_from(const FNode *n, LogFile *lf, size_t from);

#endif
