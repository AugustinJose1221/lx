/* POSIX terminal backend: termios raw mode + ANSI escape sequences.
 * Shared by Linux, macOS and other Unix systems. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "term.h"

static struct termios g_orig;
static int g_raw = 0;
static int g_in = 0; /* keyboard fd: stdin, or /dev/tty for piped logs */
static volatile sig_atomic_t g_winch = 0;
static char g_ob[65536];
static size_t g_ol = 0;

static void on_winch(int sig)
{
    (void)sig;
    g_winch = 1;
}

int term_stdin_redirected(void)
{
    return !isatty(0);
}

int term_is_tty(void)
{
    int fd;
    if (!isatty(1))
        return 0;
    if (isatty(0))
        return 1;
    /* stdin carries piped data; the keyboard needs the controlling
     * terminal instead */
    fd = open(TERM_TTY_DEVICE, O_RDONLY);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

void term_flush(void)
{
    size_t off = 0;
    while (off < g_ol) {
        ssize_t w = write(1, g_ob + off, g_ol - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        off += (size_t)w;
    }
    g_ol = 0;
}

void term_write(const char *s, size_t n)
{
    while (n) {
        size_t sp = sizeof g_ob - g_ol;
        size_t c;
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
    struct termios t;
    struct sigaction sa;

    if (!isatty(1))
        return -1;
    if (isatty(0)) {
        g_in = 0;
    } else {
        g_in = open(TERM_TTY_DEVICE, O_RDONLY);
        if (g_in < 0)
            return -1;
    }
    if (tcgetattr(g_in, &g_orig))
        return -1;
    t = g_orig;
    t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    t.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(g_in, TCSAFLUSH, &t))
        return -1;
    g_raw = 1;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    /* alternate screen, hide cursor, disable autowrap */
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
    tcsetattr(g_in, TCSAFLUSH, &g_orig);
    if (g_in > 2)
        close(g_in);
    g_in = 0;
    g_raw = 0;
}

void term_get_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/* Returns: >= 0 byte, -1 timeout, -2 error/eof, -3 interrupted by signal.
 * Sets *from_extra when extra_fd fired instead of stdin. */
static int read_byte(int timeout_ms, int extra_fd, int *from_extra)
{
    fd_set rf;
    struct timeval tv, *tvp = NULL;
    int maxfd = g_in, rc;
    unsigned char b;
    ssize_t n;

    FD_ZERO(&rf);
    FD_SET(g_in, &rf);
    if (extra_fd >= 0) {
        FD_SET(extra_fd, &rf);
        if (extra_fd > maxfd)
            maxfd = extra_fd;
    }
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    rc = select(maxfd + 1, &rf, NULL, NULL, tvp);
    if (rc < 0)
        return errno == EINTR ? -3 : -2;
    if (rc == 0)
        return -1;
    if (extra_fd >= 0 && FD_ISSET(extra_fd, &rf) &&
        !FD_ISSET(g_in, &rf)) {
        *from_extra = 1;
        return 0;
    }
    n = read(g_in, &b, 1);
    if (n <= 0)
        return -2;
    return b;
}

int term_read_key(int timeout_ms, int extra_fd)
{
    int fe = 0, b, b1, b2;

    if (g_winch) {
        g_winch = 0;
        return TKEY_RESIZE;
    }
    b = read_byte(timeout_ms, extra_fd, &fe);
    if (g_winch) {
        g_winch = 0;
        return TKEY_RESIZE;
    }
    if (fe)
        return TKEY_FSEVENT;
    if (b < 0)
        return TKEY_NONE;
    if (b != 0x1b)
        return b;

    /* decode an escape sequence; a lone ESC returns 0x1b */
    b1 = read_byte(25, -1, &fe);
    if (b1 < 0)
        return 0x1b;
    if (b1 == '[') {
        b2 = read_byte(25, -1, &fe);
        if (b2 < 0)
            return 0x1b;
        if (b2 >= '0' && b2 <= '9') {
            int num = b2 - '0', b3 = read_byte(25, -1, &fe);
            while (b3 >= '0' && b3 <= '9') {
                num = num * 10 + (b3 - '0');
                b3 = read_byte(25, -1, &fe);
            }
            if (b3 == '~') {
                switch (num) {
                case 1: case 7: return TKEY_HOME;
                case 4: case 8: return TKEY_END;
                case 3: return TKEY_DEL;
                case 5: return TKEY_PGUP;
                case 6: return TKEY_PGDN;
                }
            }
            return TKEY_NONE;
        }
        switch (b2) {
        case 'A': return TKEY_UP;
        case 'B': return TKEY_DOWN;
        case 'C': return TKEY_RIGHT;
        case 'D': return TKEY_LEFT;
        case 'H': return TKEY_HOME;
        case 'F': return TKEY_END;
        }
        return TKEY_NONE;
    }
    if (b1 == 'O') {
        b2 = read_byte(25, -1, &fe);
        switch (b2) {
        case 'A': return TKEY_UP;
        case 'B': return TKEY_DOWN;
        case 'C': return TKEY_RIGHT;
        case 'D': return TKEY_LEFT;
        case 'H': return TKEY_HOME;
        case 'F': return TKEY_END;
        }
        return TKEY_NONE;
    }
    return 0x1b;
}
