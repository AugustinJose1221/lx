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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "lx: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "lx: out of memory\n");
        exit(1);
    }
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

void *xgrow(void *p, size_t *cap, size_t need, size_t esz)
{
    size_t nc;
    if (need <= *cap)
        return p;
    nc = *cap ? *cap * 2 : 64;
    while (nc < need)
        nc *= 2;
    p = xrealloc(p, nc * esz);
    *cap = nc;
    return p;
}

long find_sub(const char *hay, size_t hlen, const char *nee, size_t nlen,
              int nocase)
{
    size_t i, j;
    if (nlen == 0)
        return 0;
    if (nlen > hlen)
        return -1;
    for (i = 0; i + nlen <= hlen; i++) {
        for (j = 0; j < nlen; j++) {
            char a = hay[i + j], b = nee[j];
            if (nocase) {
                a = (char)tolower((unsigned char)a);
                b = (char)tolower((unsigned char)b);
            }
            if (a != b)
                break;
        }
        if (j == nlen)
            return (long)i;
    }
    return -1;
}

int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

void xcopy(char *dst, size_t dstsz, const char *src)
{
    size_t n = strlen(src);
    if (!dstsz)
        return;
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

char *trim(char *s)
{
    size_t l;
    while (*s == ' ' || *s == '\t')
        s++;
    l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' ||
                 s[l - 1] == '\n'))
        s[--l] = 0;
    return s;
}
