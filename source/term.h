/* Terminal abstraction. Implemented per platform:
 *   posix/term_posix.c    termios + ANSI escape sequences (Linux, macOS)
 *   windows/term_windows.c Win32 console with VT processing
 * The UI layer emits ANSI escape sequences through term_write(); the
 * platform layer only handles raw mode, sizing and key decoding. */
#ifndef LX_TERM_H
#define LX_TERM_H

#include <stddef.h>

enum {
    TKEY_NONE = -1, /* timeout */
    TKEY_UP = 1000,
    TKEY_DOWN,
    TKEY_LEFT,
    TKEY_RIGHT,
    TKEY_PGUP,
    TKEY_PGDN,
    TKEY_HOME,
    TKEY_END,
    TKEY_DEL,
    TKEY_RESIZE, /* terminal was resized */
    TKEY_FSEVENT /* extra_fd became readable (file watch) */
};

/* Device the keyboard falls back to when stdin carries piped log data. */
#ifdef _WIN32
#define TERM_TTY_DEVICE "CONIN$"
#else
#define TERM_TTY_DEVICE "/dev/tty"
#endif

/* Interactive use is possible: stdout is a terminal, and keyboard input
 * is available either on stdin or via TERM_TTY_DEVICE (so piped logs
 * still allow an interactive session). */
int term_is_tty(void);

/* stdin is not a terminal (piped or redirected input). */
int term_stdin_redirected(void);

int term_init(void);
void term_shutdown(void);
void term_get_size(int *rows, int *cols);

/* Wait up to timeout_ms (-1 = forever) for a key press. When extra_fd is
 * >= 0 and becomes readable, returns TKEY_FSEVENT without reading it. */
int term_read_key(int timeout_ms, int extra_fd);

/* Buffered output; flushed explicitly once per frame. */
void term_write(const char *s, size_t n);
void term_writes(const char *s);
void term_flush(void);
void term_show_cursor(int on);

#endif
