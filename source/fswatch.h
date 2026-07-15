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
/* File modification watching for follow mode. Implemented per platform:
 *   linux/fswatch_linux.c    inotify
 *   darwin/fswatch_darwin.c  kqueue
 *   posix/fswatch_poll.c     none (UI falls back to timed polling)
 *   windows/fswatch_windows.c none
 * A negative fd from fswatch_open() means "not supported"; the UI then
 * polls the file on a timer instead. */
#ifndef LX_FSWATCH_H
#define LX_FSWATCH_H

/* Returns a selectable fd that becomes readable when path changes,
 * or -1 when watching is unavailable. */
int fswatch_open(const char *path);

/* Consume pending events so the fd can signal again. */
void fswatch_drain(int fd);

void fswatch_close(int fd);

#endif
