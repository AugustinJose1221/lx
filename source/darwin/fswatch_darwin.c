/* macOS file watching via kqueue/EVFILT_VNODE. O_EVTONLY keeps the
 * descriptor from blocking volume unmounts. */
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include "fswatch.h"

static int g_filefd = -1;

int fswatch_open(const char *path)
{
    int kq;
    struct kevent ev;

    g_filefd = open(path, O_EVTONLY);
    if (g_filefd < 0)
        return -1;
    kq = kqueue();
    if (kq < 0) {
        close(g_filefd);
        g_filefd = -1;
        return -1;
    }
    EV_SET(&ev, (uintptr_t)g_filefd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME, 0, NULL);
    if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
        close(kq);
        close(g_filefd);
        g_filefd = -1;
        return -1;
    }
    return kq;
}

void fswatch_drain(int fd)
{
    struct kevent evs[8];
    struct timespec zero = { 0, 0 };
    while (kevent(fd, NULL, 0, evs, 8, &zero) > 0)
        ;
}

void fswatch_close(int fd)
{
    if (fd >= 0)
        close(fd);
    if (g_filefd >= 0) {
        close(g_filefd);
        g_filefd = -1;
    }
}
