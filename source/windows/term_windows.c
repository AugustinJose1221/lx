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
/* Windows console backend. Output uses ANSI escape sequences through
 * ENABLE_VIRTUAL_TERMINAL_PROCESSING (Windows 10+); input uses
 * ReadConsoleInput mapped onto the shared TKEY_* codes. */
#ifdef _WIN32

#include <io.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "term.h"

static HANDLE g_in, g_out;
static DWORD g_in_orig, g_out_orig;
static int g_raw = 0;
static int g_in_own = 0; /* g_in is an opened CONIN$, not the std handle */
static char g_ob[65536];
static size_t g_ol = 0;
static int g_last_rows = 0, g_last_cols = 0;

int term_stdin_redirected(void)
{
    return !_isatty(_fileno(stdin));
}

int term_is_tty(void)
{
    HANDLE h;
    if (!_isatty(_fileno(stdout)))
        return 0;
    if (_isatty(_fileno(stdin)))
        return 1;
    /* stdin carries piped data; keyboard comes from the console */
    h = CreateFileA(TERM_TTY_DEVICE, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

void term_flush(void)
{
    DWORD w;
    size_t off = 0;
    while (off < g_ol) {
        if (!WriteFile(g_out, g_ob + off, (DWORD)(g_ol - off), &w, NULL))
            break;
        off += w;
    }
    g_ol = 0;
}

void term_write(const char *s, size_t n)
{
    while (n) {
        size_t sp = sizeof g_ob - g_ol, c;
        if (!sp) {
            term_flush();
            sp = sizeof g_ob;
        }
        c = n < sp ? n : sp;
        memcpy(g_ob + g_ol, s, c);
        g_ol += c;
        s += c;
        n -= c;
    }
}

void term_writes(const char *s)
{
    term_write(s, strlen(s));
}

void term_show_cursor(int on)
{
    term_writes(on ? "\x1b[?25h" : "\x1b[?25l");
}

int term_init(void)
{
    DWORD m;

    if (!term_is_tty())
        return -1;
    if (_isatty(_fileno(stdin))) {
        g_in = GetStdHandle(STD_INPUT_HANDLE);
        g_in_own = 0;
    } else {
        g_in = CreateFileA(TERM_TTY_DEVICE, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
        if (g_in == INVALID_HANDLE_VALUE)
            return -1;
        g_in_own = 1;
    }
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(g_in, &g_in_orig) ||
        !GetConsoleMode(g_out, &g_out_orig))
        return -1;

    m = g_out_orig | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
        DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(g_out, m))
        return -1;
    m = g_in_orig &
        ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                 ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT);
    m |= ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(g_in, m)) {
        SetConsoleMode(g_out, g_out_orig);
        return -1;
    }
    g_raw = 1;
    term_get_size(&g_last_rows, &g_last_cols);
    term_writes("\x1b[?1049h\x1b[?25l\x1b[?7l");
    term_flush();
    return 0;
}

void term_shutdown(void)
{
    if (!g_raw)
        return;
    term_writes("\x1b[?7h\x1b[0m\x1b[?25h\x1b[?1049l");
    term_flush();
    SetConsoleMode(g_in, g_in_orig);
    SetConsoleMode(g_out, g_out_orig);
    if (g_in_own) {
        CloseHandle(g_in);
        g_in_own = 0;
    }
    g_raw = 0;
}

void term_get_size(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (GetConsoleScreenBufferInfo(g_out, &bi)) {
        *rows = bi.srWindow.Bottom - bi.srWindow.Top + 1;
        *cols = bi.srWindow.Right - bi.srWindow.Left + 1;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

int term_read_key(int timeout_ms, int extra_fd)
{
    (void)extra_fd; /* file watching is poll-based on Windows */

    for (;;) {
        DWORD rc = WaitForSingleObject(
            g_in, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
        INPUT_RECORD rec;
        DWORD n;

        if (rc == WAIT_TIMEOUT)
            return TKEY_NONE;
        if (rc != WAIT_OBJECT_0)
            return TKEY_NONE;
        if (!ReadConsoleInputA(g_in, &rec, 1, &n) || n == 0)
            return TKEY_NONE;

        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            int r, c;
            term_get_size(&r, &c);
            if (r != g_last_rows || c != g_last_cols) {
                g_last_rows = r;
                g_last_cols = c;
                return TKEY_RESIZE;
            }
            continue;
        }
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;

        switch (rec.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:    return TKEY_UP;
        case VK_DOWN:  return TKEY_DOWN;
        case VK_LEFT:  return TKEY_LEFT;
        case VK_RIGHT: return TKEY_RIGHT;
        case VK_PRIOR: return TKEY_PGUP;
        case VK_NEXT:  return TKEY_PGDN;
        case VK_HOME:  return TKEY_HOME;
        case VK_END:   return TKEY_END;
        case VK_DELETE: return TKEY_DEL;
        default:
            if (rec.Event.KeyEvent.uChar.AsciiChar)
                return (unsigned char)rec.Event.KeyEvent.uChar.AsciiChar;
            continue;
        }
    }
}

#endif /* _WIN32 */
