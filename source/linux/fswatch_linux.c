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
/* Linux file watching via inotify. */
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "fswatch.h"

int fswatch_open(const char *path)
{
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
        return -1;
    if (inotify_add_watch(fd, path,
                          IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF |
                          IN_ATTRIB) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void fswatch_drain(int fd)
{
    char buf[4096];
    while (read(fd, buf, sizeof buf) > 0)
        ;
}

void fswatch_close(int fd)
{
    if (fd >= 0)
        close(fd);
}
