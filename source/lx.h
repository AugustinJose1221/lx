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
/* lx - a terminal log file viewer and analyzer.
 * Common definitions shared by every translation unit. */
#ifndef LX_H
#define LX_H

#include <stddef.h>

#define LX_VERSION "1.0.0"

/* A byte range inside some backing buffer. Offsets are used instead of
 * pointers because the log buffer may be reallocated while following a
 * growing file. */
typedef struct {
    size_t off;
    size_t len;
} Span;

#endif
