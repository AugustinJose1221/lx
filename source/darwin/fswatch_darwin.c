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
/* macOS file watching via kqueue/EVFILT_VNODE. O_EVTONLY keeps the
 * descriptor from blocking volume unmounts. */
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include "fswatch.h"

static int g_filefd = -1;

int fswatch_open(const char *path)
{
    int kq;
    struct kevent ev;

    g_filefd = open(path, O_EVTONLY);
    if (g_filefd < 0)
        return -1;
    kq = kqueue();
    if (kq < 0) {
        close(g_filefd);
        g_filefd = -1;
        return -1;
    }
    EV_SET(&ev, (uintptr_t)g_filefd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME, 0, NULL);
    if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
        close(kq);
        close(g_filefd);
        g_filefd = -1;
        return -1;
    }
    return kq;
}

void fswatch_drain(int fd)
{
    struct kevent evs[8];
    struct timespec zero = { 0, 0 };
    while (kevent(fd, NULL, 0, evs, 8, &zero) > 0)
        ;
}

void fswatch_close(int fd)
{
    if (fd >= 0)
        close(fd);
    if (g_filefd >= 0) {
        close(g_filefd);
        g_filefd = -1;
    }
}
