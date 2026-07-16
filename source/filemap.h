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
/*
 * Read-only file mapping for high-performance mode: the log stays on
 * disk and the OS pages it in on demand, so multi-gigabyte files open
 * without copying them into the heap. Implemented per platform:
 *   posix/filemap_posix.c     mmap (Linux, macOS)
 *   windows/filemap_windows.c CreateFileMapping/MapViewOfFile
 */
#ifndef LX_FILEMAP_H
#define LX_FILEMAP_H

#include <stddef.h>

typedef struct {
    const char *data; /* read-only mapping (NULL when len == 0) */
    size_t len;
    void *os;         /* platform bookkeeping */
} FileMap;

/* Map path read-only. Returns 0, -1 on error. */
int filemap_open(FileMap *fm, const char *path);

/* Re-map after the file grew (follow mode). The data pointer may move;
 * byte offsets remain valid because log files grow by appending.
 * Returns 0, -1 on error. */
int filemap_remap(FileMap *fm, const char *path);

void filemap_close(FileMap *fm);

/* Current on-disk size of path, or -1. Unlike ftell(), works for
 * files larger than 2 GiB on every platform. */
long long filemap_size(const char *path);

#endif
