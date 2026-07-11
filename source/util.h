#ifndef LX_UTIL_H
#define LX_UTIL_H

#include <stddef.h>

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Grow *p (element size esz) so it can hold at least `need` elements.
 * *cap is updated. Returns the (possibly moved) buffer. */
void *xgrow(void *p, size_t *cap, size_t need, size_t esz);

/* Find `nee` inside `hay`. Returns byte index or -1. nocase != 0 for a
 * case-insensitive search. */
long find_sub(const char *hay, size_t hlen, const char *nee, size_t nlen,
              int nocase);

/* Case-insensitive full-string equality. */
int str_ieq(const char *a, const char *b);

/* Trim leading/trailing whitespace in place; returns the advanced pointer. */
char *trim(char *s);

#endif
