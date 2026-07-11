/* Windows: no selectable watch fd; follow mode polls on a timer. */
#ifdef _WIN32

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

#endif /* _WIN32 */
