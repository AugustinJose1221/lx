#ifndef LX_TSTAMP_H
#define LX_TSTAMP_H

#include <stddef.h>

/* Parse a timestamp at the start of s[0..n) according to fmt.
 *
 * Supported directives:
 *   %Y 4-digit year        %y 2-digit year        %m month 01-12
 *   %b/%B month name       %d day 01-31           %e day, space padded
 *   %H hour 00-23          %I hour 01-12          %p AM/PM
 *   %M minute              %S second              %f fractional seconds
 *   %j day of year         %a/%A weekday name (skipped)
 *   %z UTC offset (+HH, +HHMM, +HH:MM, +H.MM, Z)
 *   %Z timezone name (skipped, optional)          %% literal percent
 * A space in fmt matches one or more spaces/tabs in the input.
 *
 * Components that do not appear default to 1970-01-01 00:00:00 UTC so
 * that partial timestamps (e.g. syslog's month/day/time) still compare
 * consistently against each other.
 *
 * On success stores the epoch (seconds, with fraction) in *out and the
 * number of input bytes consumed in *consumed, and returns 0. Returns -1
 * on mismatch.
 *
 * A UTC offset (%z) is validated and consumed but NOT applied to the
 * result: filter comparisons intentionally work on wall-clock time
 * exactly as printed in the log file. */
int ts_parse(const char *s, size_t n, const char *fmt, double *out,
             size_t *consumed);

/* Component flags reported by ts_parse2: which parts of the timestamp
 * were actually present in the parsed input. */
#define TSF_DATE 1u  /* year/month/day (or day of year) */
#define TSF_HOUR 2u
#define TSF_MIN  4u
#define TSF_SEC  8u
#define TSF_FRAC 16u

/* Like ts_parse, additionally reporting the parsed components in
 * *flags (may be NULL). Lets callers implement granularity-aware
 * comparisons (a date-only value means the whole day) and time-of-day
 * matching (a time without a date). */
int ts_parse2(const char *s, size_t n, const char *fmt, double *out,
              size_t *consumed, unsigned *flags);

#endif
