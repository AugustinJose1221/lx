/* Linux file watching via inotify. */
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "fswatch.h"

int fswatch_open(const char *path)
{
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)
        return -1;
    if (inotify_add_watch(fd, path,
                          IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF |
                          IN_ATTRIB) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void fswatch_drain(int fd)
{
    char buf[4096];
    while (read(fd, buf, sizeof buf) > 0)
        ;
}

void fswatch_close(int fd)
{
    if (fd >= 0)
        close(fd);
}
