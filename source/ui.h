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
#ifndef LX_UI_H
#define LX_UI_H

#include "filter.h"
#include "logfile.h"

/* Run the interactive viewer. Takes ownership of `filter` (may be NULL);
 * filter_str is the expression it was compiled from (may be "").
 * detail_lines limits each value in the entry inspector to that many
 * wrapped lines (0 = default cap of 500 characters). Returns the
 * process exit code. */
int ui_run(LogFile *lf, const char *filter_str, FNode *filter, int follow,
           int detail_lines);

#endif
