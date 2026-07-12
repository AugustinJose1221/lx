/* Streamed standard-input reading for piped logs, e.g.:
 *     journalctl -b | lx
 *     log show --last 5m | lx
 * Implemented per platform:
 *   posix/pipein_posix.c     non-blocking read + select (Linux, macOS)
 *   windows/pipein_windows.c PeekNamedPipe polling
 * Keyboard input meanwhile comes from the controlling terminal
 * (/dev/tty resp. CONIN$), handled by the term backend. */
#ifndef LX_PIPEIN_H
#define LX_PIPEIN_H

#include <stddef.h>

/* Prepare stdin for streaming (e.g. switch to non-blocking).
 * Returns 0, -1 on error. */
int pipein_start(void);

/* A selectable fd usable as term_read_key()'s extra_fd so new data
 * wakes the UI, or -1 when unsupported (Windows; the UI then polls
 * on a timer). */
int pipein_fd(void);

/* Read available bytes: >0 count, 0 nothing available right now,
 * -1 end of stream. Never blocks. */
long pipein_read(char *dst, size_t n);

/* Wait up to ms milliseconds (-1 = forever) for data.
 * Returns 1 when readable/EOF, 0 on timeout. */
int pipein_wait(int ms);

int pipein_eof(void);

#endif
