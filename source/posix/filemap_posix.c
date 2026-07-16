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
/* POSIX read-only file mapping via mmap (Linux, macOS). */
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filemap.h"

int filemap_open(FileMap *fm, const char *path)
{
    struct stat st;
    int fd = open(path, O_RDONLY);

    fm->data = NULL;
    fm->len = 0;
    fm->os = NULL;
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) || st.st_size < 0) {
        close(fd);
        return -1;
    }
    fm->len = (size_t)st.st_size;
    if (fm->len) {
        void *p = mmap(NULL, fm->len, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            close(fd);
            fm->len = 0;
            return -1;
        }
        fm->data = p;
    }
    close(fd);
    return 0;
}

int filemap_remap(FileMap *fm, const char *path)
{
    FileMap next;
    if (filemap_open(&next, path))
        return -1;
    filemap_close(fm);
    *fm = next;
    return 0;
}

void filemap_close(FileMap *fm)
{
    if (fm->data && fm->len)
        munmap((void *)fm->data, fm->len);
    fm->data = NULL;
    fm->len = 0;
    fm->os = NULL;
}

long long filemap_size(const char *path)
{
    struct stat st;
    if (stat(path, &st))
        return -1;
    return (long long)st.st_size;
}
