/* Windows streamed stdin. Pipes cannot be select()ed together with
 * console input here, so pipein_fd() returns -1 and the UI polls on a
 * timer (~500 ms) instead; PeekNamedPipe keeps the reads non-blocking. */
#ifdef _WIN32

#include <windows.h>

#include "pipein.h"

static HANDLE g_h = INVALID_HANDLE_VALUE;
static int g_eof = 0;

int pipein_start(void)
{
    g_h = GetStdHandle(STD_INPUT_HANDLE);
    return g_h == INVALID_HANDLE_VALUE ? -1 : 0;
}

int pipein_fd(void)
{
    return -1; /* poll via timeout */
}

long pipein_read(char *dst, size_t n)
{
    DWORD avail = 0, got = 0;

    if (g_eof)
        return -1;
    if (GetFileType(g_h) == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(g_h, NULL, 0, NULL, &avail, NULL)) {
            g_eof = 1; /* broken pipe = end of stream */
            return -1;
        }
        if (!avail)
            return 0;
        if ((size_t)avail > n)
            avail = (DWORD)n;
        if (!ReadFile(g_h, dst, avail, &got, NULL) || !got) {
            g_eof = 1;
            return -1;
        }
        return (long)got;
    }
    /* redirected regular file */
    if (!ReadFile(g_h, dst, (DWORD)n, &got, NULL) || got == 0) {
        g_eof = 1;
        return got ? (long)got : -1;
    }
    return (long)got;
}

int pipein_wait(int ms)
{
    DWORD r;
    if (g_eof)
        return 1;
    r = WaitForSingleObject(g_h, ms < 0 ? INFINITE : (DWORD)ms);
    return r == WAIT_OBJECT_0;
}

int pipein_eof(void)
{
    return g_eof;
}

#endif /* _WIN32 */
