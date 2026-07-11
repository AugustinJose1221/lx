/* File modification watching for follow mode. Implemented per platform:
 *   linux/fswatch_linux.c    inotify
 *   darwin/fswatch_darwin.c  kqueue
 *   posix/fswatch_poll.c     none (UI falls back to timed polling)
 *   windows/fswatch_windows.c none
 * A negative fd from fswatch_open() means "not supported"; the UI then
 * polls the file on a timer instead. */
#ifndef LX_FSWATCH_H
#define LX_FSWATCH_H

/* Returns a selectable fd that becomes readable when path changes,
 * or -1 when watching is unavailable. */
int fswatch_open(const char *path);

/* Consume pending events so the fd can signal again. */
void fswatch_drain(int fd);

void fswatch_close(int fd);

#endif
