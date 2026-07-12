/* POSIX streamed stdin: non-blocking reads, select() to wait. */
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "pipein.h"

static int g_eof = 0;

int pipein_start(void)
{
    int fl = fcntl(0, F_GETFL, 0);
    if (fl < 0)
        return -1;
    if (fcntl(0, F_SETFL, fl | O_NONBLOCK) < 0)
        return -1;
    return 0;
}

int pipein_fd(void)
{
    return 0;
}

long pipein_read(char *dst, size_t n)
{
    ssize_t r;
    if (g_eof)
        return -1;
    r = read(0, dst, n);
    if (r > 0)
        return (long)r;
    if (r == 0) {
        g_eof = 1;
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    g_eof = 1;
    return -1;
}

int pipein_wait(int ms)
{
    fd_set rf;
    struct timeval tv, *tvp = NULL;

    if (g_eof)
        return 1;
    FD_ZERO(&rf);
    FD_SET(0, &rf);
    if (ms >= 0) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        tvp = &tv;
    }
    return select(1, &rf, NULL, NULL, tvp) > 0;
}

int pipein_eof(void)
{
    return g_eof;
}
