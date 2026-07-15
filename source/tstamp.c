#include <ctype.h>
#include <string.h>

#include "tstamp.h"

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
 * Avoids mktime/timegm, which are not portable/standard. */
static long days_from_civil(int y, int m, int d)
{
    long era;
    unsigned yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static int getnum(const char *s, size_t n, size_t *i, int minw, int maxw,
                  int *out)
{
    int v = 0, w = 0;
    while (*i < n && w < maxw && isdigit((unsigned char)s[*i])) {
        v = v * 10 + (s[*i] - '0');
        (*i)++;
        w++;
    }
    if (w < minw)
        return -1;
    *out = v;
    return 0;
}

static int cieq(const char *a, const char *b, size_t n)
{
    size_t k;
    for (k = 0; k < n; k++)
        if (tolower((unsigned char)a[k]) != tolower((unsigned char)b[k]))
            return 0;
    return 1;
}

static const char *MONTHS[12] = {
    "january", "february", "march",     "april",   "may",      "june",
    "july",    "august",   "september", "october", "november", "december"
};

static int match_month(const char *s, size_t n, size_t *i, int *mon)
{
    int m;
    /* full names first so "June" is not left with a trailing 'e' */
    for (m = 0; m < 12; m++) {
        size_t l = strlen(MONTHS[m]);
        if (n - *i >= l && cieq(s + *i, MONTHS[m], l)) {
            *i += l;
            *mon = m + 1;
            return 0;
        }
    }
    for (m = 0; m < 12; m++) {
        if (n - *i >= 3 && cieq(s + *i, MONTHS[m], 3)) {
            *i += 3;
            *mon = m + 1;
            return 0;
        }
    }
    return -1;
}

int ts_parse2(const char *s, size_t n, const char *fmt, double *out,
              size_t *consumed, unsigned *flags)
{
    int Y = 1970, mo = 1, d = 1, H = 0, Mi = 0, Se = 0;
    int pm = -1, yday = -1, v;
    unsigned fl = 0;
    double frac = 0.0;
    long tz = 0, days;
    size_t i = 0;
    const char *f;

    for (f = fmt; *f; f++) {
        if (*f == '%') {
            f++;
            switch (*f) {
            case 'Y':
                if (getnum(s, n, &i, 4, 4, &v))
                    return -1;
                Y = v;
                fl |= TSF_DATE;
                break;
            case 'y':
                if (getnum(s, n, &i, 2, 2, &v))
                    return -1;
                Y = v < 70 ? 2000 + v : 1900 + v;
                fl |= TSF_DATE;
                break;
            case 'm':
                if (getnum(s, n, &i, 1, 2, &v) || v < 1 || v > 12)
                    return -1;
                mo = v;
                fl |= TSF_DATE;
                break;
            case 'b':
            case 'h':
            case 'B':
                if (match_month(s, n, &i, &mo))
                    return -1;
                fl |= TSF_DATE;
                break;
            case 'd':
                if (getnum(s, n, &i, 1, 2, &v) || v < 1 || v > 31)
                    return -1;
                d = v;
                fl |= TSF_DATE;
                break;
            case 'e':
                while (i < n && s[i] == ' ')
                    i++;
                if (getnum(s, n, &i, 1, 2, &v) || v < 1 || v > 31)
                    return -1;
                d = v;
                fl |= TSF_DATE;
                break;
            case 'H':
                if (getnum(s, n, &i, 1, 2, &v) || v > 23)
                    return -1;
                H = v;
                fl |= TSF_HOUR;
                break;
            case 'I':
                if (getnum(s, n, &i, 1, 2, &v) || v < 1 || v > 12)
                    return -1;
                H = v;
                fl |= TSF_HOUR;
                break;
            case 'M':
                if (getnum(s, n, &i, 1, 2, &v) || v > 59)
                    return -1;
                Mi = v;
                fl |= TSF_MIN;
                break;
            case 'S':
                if (getnum(s, n, &i, 1, 2, &v) || v > 60)
                    return -1;
                Se = v;
                fl |= TSF_SEC;
                break;
            case 'f': {
                double sc = 0.1;
                int w = 0;
                frac = 0.0;
                while (i < n && isdigit((unsigned char)s[i]) && w < 9) {
                    frac += (s[i] - '0') * sc;
                    sc /= 10.0;
                    i++;
                    w++;
                }
                if (!w)
                    return -1;
                fl |= TSF_FRAC;
                break;
            }
            case 'j':
                if (getnum(s, n, &i, 1, 3, &v) || v < 1 || v > 366)
                    return -1;
                yday = v;
                fl |= TSF_DATE;
                break;
            case 'p':
                if (n - i >= 2 && (s[i] == 'A' || s[i] == 'a') &&
                    (s[i + 1] == 'M' || s[i + 1] == 'm')) {
                    pm = 0;
                    i += 2;
                } else if (n - i >= 2 && (s[i] == 'P' || s[i] == 'p') &&
                           (s[i + 1] == 'M' || s[i + 1] == 'm')) {
                    pm = 1;
                    i += 2;
                } else {
                    return -1;
                }
                break;
            case 'a':
            case 'A': {
                size_t st = i;
                while (i < n && isalpha((unsigned char)s[i]))
                    i++;
                if (i == st)
                    return -1;
                break;
            }
            case 'z': {
                int sign, hh, mm = 0;
                while (i < n && s[i] == ' ')
                    i++;
                if (i < n && (s[i] == 'Z' || s[i] == 'z')) {
                    i++;
                    tz = 0;
                    break;
                }
                if (i >= n || (s[i] != '+' && s[i] != '-'))
                    return -1;
                sign = s[i] == '-' ? -1 : 1;
                i++;
                if (getnum(s, n, &i, 1, 2, &hh))
                    return -1;
                if (i < n && (s[i] == ':' || s[i] == '.')) {
                    i++;
                    if (getnum(s, n, &i, 2, 2, &mm))
                        return -1;
                } else if (i + 1 < n && isdigit((unsigned char)s[i]) &&
                           isdigit((unsigned char)s[i + 1])) {
                    if (getnum(s, n, &i, 2, 2, &mm))
                        return -1;
                }
                tz = sign * (hh * 3600L + mm * 60L);
                break;
            }
            case 'Z': {
                /* optional zone name; if no alphabetic name follows,
                 * consume nothing (the skipped spaces may belong to a
                 * literal separator after the timestamp) */
                size_t save = i, a;
                while (i < n && s[i] == ' ')
                    i++;
                a = i;
                while (i < n && isalpha((unsigned char)s[i]))
                    i++;
                if (i == a)
                    i = save;
                break;
            }
            case '%':
                if (i < n && s[i] == '%')
                    i++;
                else
                    return -1;
                break;
            default:
                return -1; /* unknown directive */
            }
        } else if (*f == ' ') {
            size_t st = i;
            while (i < n && (s[i] == ' ' || s[i] == '\t'))
                i++;
            if (i == st)
                return -1;
            while (f[1] == ' ')
                f++;
        } else {
            if (i < n && s[i] == *f)
                i++;
            else
                return -1;
        }
    }

    if (pm == 1 && H < 12)
        H += 12;
    if (pm == 0 && H == 12)
        H = 0;

    if (yday > 0)
        days = days_from_civil(Y, 1, 1) + (yday - 1);
    else
        days = days_from_civil(Y, mo, d);

    /* tz was validated and consumed but is deliberately NOT applied:
     * comparisons work on wall-clock time as printed in the log, which
     * is what filters typed by a human refer to. */
    (void)tz;
    *out = (double)days * 86400.0 + H * 3600 + Mi * 60 + Se + frac;
    if (consumed)
        *consumed = i;
    if (flags)
        *flags = fl;
    return 0;
}

int ts_parse(const char *s, size_t n, const char *fmt, double *out,
             size_t *consumed)
{
    return ts_parse2(s, n, fmt, out, consumed, NULL);
}
