#include <string.h>

#include "test.h"
#include "wizard.h"

static int piece_is(const char *line, const WPiece *p, int is_field,
                    FieldType t, const char *text)
{
    if (p->is_field != is_field)
        return 0;
    if (is_field && p->type != t)
        return 0;
    return p->len == strlen(text) && !memcmp(line + p->off, text, p->len);
}

int main(void)
{
    WPiece p[WIZ_MAX_PIECES];
    const char *line;
    int n;

    /* --- pipe-separated app log -------------------------------------- */
    line = "19:18:23.123 | INFO  | app | service starting";
    n = wizard_segment(line, strlen(line), p, WIZ_MAX_PIECES);
    OK(n == 7);
    OK(piece_is(line, &p[0], 1, FT_TIMESTAMP, "19:18:23.123"));
    OK(!strcmp(p[0].tsfmt, "%H:%M:%S.%f"));
    OK(piece_is(line, &p[1], 0, 0, " | "));
    OK(piece_is(line, &p[2], 1, FT_ENUM, "INFO"));
    OK(p[2].values && strstr(p[2].values, "WARNING") != NULL);
    OK(piece_is(line, &p[4], 1, FT_WORD, "app"));
    OK(piece_is(line, &p[6], 1, FT_STRING, "service starting"));

    /* --- syslog -------------------------------------------------------- */
    line = "Jun  1 19:18:23 myhost sshd[4123]: Accepted publickey for u";
    n = wizard_segment(line, strlen(line), p, WIZ_MAX_PIECES);
    OK(n == 9);
    OK(piece_is(line, &p[0], 1, FT_TIMESTAMP, "Jun  1 19:18:23"));
    OK(!strcmp(p[0].tsfmt, "%b %e %H:%M:%S"));
    OK(piece_is(line, &p[2], 1, FT_WORD, "myhost"));
    OK(piece_is(line, &p[4], 1, FT_WORD, "sshd"));
    OK(piece_is(line, &p[5], 0, 0, "["));
    OK(piece_is(line, &p[6], 1, FT_INT, "4123"));
    OK(piece_is(line, &p[7], 0, 0, "]: "));
    OK(piece_is(line, &p[8], 1, FT_STRING, "Accepted publickey for u"));

    /* --- dmesg: essentially [%{time}] %{message} ----------------------- */
    line = "[   12.345678] usb 1-1: new high-speed USB device";
    n = wizard_segment(line, strlen(line), p, WIZ_MAX_PIECES);
    OK(n == 4);
    OK(piece_is(line, &p[0], 0, 0, "[   "));
    OK(piece_is(line, &p[1], 1, FT_FLOAT, "12.345678"));
    OK(piece_is(line, &p[2], 0, 0, "] "));
    OK(piece_is(line, &p[3], 1, FT_STRING,
                "usb 1-1: new high-speed USB device"));

    /* --- macOS unified log --------------------------------------------- */
    line = "2026-06-01 19:18:23.123456+0200 0x16b3     Default     0x0"
           "                  501    0    kernel: en0: link up";
    n = wizard_segment(line, strlen(line), p, WIZ_MAX_PIECES);
    OK(piece_is(line, &p[0], 1, FT_TIMESTAMP,
                "2026-06-01 19:18:23.123456+0200"));
    /* hex thread id must be a word, not a number */
    OK(piece_is(line, &p[2], 1, FT_WORD, "0x16b3"));
    OK(piece_is(line, &p[4], 1, FT_ENUM, "Default"));
    OK(p[4].values && strstr(p[4].values, "Fault") != NULL);
    OK(piece_is(line, &p[8], 1, FT_INT, "501"));
    OK(piece_is(line, &p[10], 1, FT_INT, "0"));

    /* --- python: " - " separators don't get eaten by the message ------- */
    line = "2026-06-01 19:18:23,123 - app.db - WARNING - pool exhausted";
    n = wizard_segment(line, strlen(line), p, WIZ_MAX_PIECES);
    OK(piece_is(line, &p[0], 1, FT_TIMESTAMP, "2026-06-01 19:18:23,123"));
    OK(!strcmp(p[0].tsfmt, "%Y-%m-%d %H:%M:%S,%f"));
    OK(piece_is(line, &p[1], 0, 0, " - "));
    OK(piece_is(line, &p[2], 1, FT_WORD, "app.db"));
    OK(piece_is(line, &p[4], 1, FT_ENUM, "WARNING"));
    OK(piece_is(line, &p[6], 1, FT_STRING, "pool exhausted"));

    /* --- words with embedded dashes stay whole -------------------------- */
    line = "connect db-01 failed";
    n = wizard_segment(line, strlen(line), p, WIZ_MAX_PIECES);
    OK(n == 1);
    OK(piece_is(line, &p[0], 1, FT_STRING, "connect db-01 failed"));

    return TEST_REPORT("test_wizard");
}
