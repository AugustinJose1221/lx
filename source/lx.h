/* lx - a terminal log file viewer and analyzer.
 * Common definitions shared by every translation unit. */
#ifndef LX_H
#define LX_H

#include <stddef.h>

#define LX_VERSION "1.0.0"

/* A byte range inside some backing buffer. Offsets are used instead of
 * pointers because the log buffer may be reallocated while following a
 * growing file. */
typedef struct {
    size_t off;
    size_t len;
} Span;

#endif
