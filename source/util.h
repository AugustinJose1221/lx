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
#ifndef LX_UTIL_H
#define LX_UTIL_H

#include <stddef.h>

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Grow *p (element size esz) so it can hold at least `need` elements.
 * *cap is updated. Returns the (possibly moved) buffer. */
void *xgrow(void *p, size_t *cap, size_t need, size_t esz);

/* Find `nee` inside `hay`. Returns byte index or -1. nocase != 0 for a
 * case-insensitive search. */
long find_sub(const char *hay, size_t hlen, const char *nee, size_t nlen,
              int nocase);

/* Case-insensitive full-string equality. */
int str_ieq(const char *a, const char *b);

/* Trim leading/trailing whitespace in place; returns the advanced pointer. */
char *trim(char *s);

/* Bounded string copy with guaranteed NUL termination (strlcpy-like).
 * Truncation is silent and intentional. */
void xcopy(char *dst, size_t dstsz, const char *src);

#endif
