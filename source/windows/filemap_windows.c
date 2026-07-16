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
/* Windows read-only file mapping via CreateFileMapping/MapViewOfFile. */
#ifdef _WIN32

#include <windows.h>

#include "filemap.h"

int filemap_open(FileMap *fm, const char *path)
{
    HANDLE f, m;
    LARGE_INTEGER sz;

    fm->data = NULL;
    fm->len = 0;
    fm->os = NULL;

    f = CreateFileA(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    if (!GetFileSizeEx(f, &sz)) {
        CloseHandle(f);
        return -1;
    }
    fm->len = (size_t)sz.QuadPart;
    if (fm->len) {
        m = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!m) {
            CloseHandle(f);
            fm->len = 0;
            return -1;
        }
        fm->data = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(m);
        if (!fm->data) {
            CloseHandle(f);
            fm->len = 0;
            return -1;
        }
    }
    CloseHandle(f);
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
    if (fm->data)
        UnmapViewOfFile((void *)fm->data);
    fm->data = NULL;
    fm->len = 0;
    fm->os = NULL;
}

long long filemap_size(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    LARGE_INTEGER sz;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa))
        return -1;
    sz.HighPart = (LONG)fa.nFileSizeHigh;
    sz.LowPart = fa.nFileSizeLow;
    return (long long)sz.QuadPart;
}

#endif /* _WIN32 */
