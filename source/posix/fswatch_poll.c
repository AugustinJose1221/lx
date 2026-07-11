/* Fallback for POSIX systems without inotify/kqueue: no watch fd; the
 * UI polls the file size on a timer while following. */
#include "fswatch.h"

int fswatch_open(const char *path)
{
    (void)path;
    return -1;
}

void fswatch_drain(int fd)
{
    (void)fd;
}

void fswatch_close(int fd)
{
    (void)fd;
}
