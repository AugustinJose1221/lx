#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "logfile.h"
#include "template.h"
#include "test.h"

#define TMPFILE "lx_test_logfile.tmp"

/* Stream mode: feed a pipe into fd 0 and load with path "-". */
static void test_stream(void)
{
    LogFile lf;
    int fds[2], old0;

    OK(pipe(fds) == 0);
    old0 = dup(0);
    OK(dup2(fds[0], 0) == 0);

    OK(write(fds[1], "[    1.0] one\n[    2.0] two\n", 28) == 28);
    OK(logfile_load(&lf, "-", template_builtin("dmesg")) == 0);
    OK(lf.stream == 1);
    OK(lf.stream_eof == 0);
    OK(lf.nents == 2);
    OK(lf.nmatched == 2);
    OK(!strcmp(lf.path, "(stdin)"));

    /* a partial line arrives ... */
    OK(write(fds[1], "[    3.0] thr", 13) == 13);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 3);
    OK(lf.unterminated == 1);

    /* ... and is completed by the next chunk */
    OK(write(fds[1], "ee\n[    4.0] four\n", 18) == 18);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 4);
    OK(lf.nmatched == 4);
    OK(lf.ents[2].raw.len == strlen("[    3.0] three"));
    OK(!memcmp(lf.buf + lf.ents[2].raw.off, "[    3.0] three",
               lf.ents[2].raw.len));

    /* nothing new */
    OK(logfile_refresh(&lf) == 0);

    /* producer closes the pipe */
    close(fds[1]);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.stream_eof == 1);
    OK(logfile_refresh(&lf) == 0);
    OK(lf.nents == 4);

    logfile_free(&lf);
    dup2(old0, 0);
    close(old0);
    close(fds[0]);
}

int main(void)
{
    LogFile lf;
    FILE *fp;

    /* --- load + line splitting (incl. CRLF, no trailing newline) --- */
    fp = fopen(TMPFILE, "wb");
    OK(fp != NULL);
    fputs("[    1.0] one\r\n[    2.0] two\n[    3.0] thr", fp);
    fclose(fp);

    OK(logfile_load(&lf, TMPFILE, template_builtin("dmesg")) == 0);
    OK(lf.nents == 3);
    OK(lf.nmatched == 3);
    OK(lf.unterminated == 1);
    OK(lf.ents[0].raw.len == strlen("[    1.0] one")); /* \r stripped */
    OK(lf.ents[2].lineno == 3);

    /* --- append: the unterminated tail must be re-parsed ------------ */
    fp = fopen(TMPFILE, "ab");
    fputs("ee\n[    4.0] four\n", fp);
    fclose(fp);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 4);
    OK(lf.nmatched == 4);
    OK(lf.unterminated == 0);
    {
        const char *l3 = lf.buf + lf.ents[2].raw.off;
        OK(lf.ents[2].raw.len == strlen("[    3.0] three"));
        OK(!memcmp(l3, "[    3.0] three", lf.ents[2].raw.len));
    }

    /* --- no change ---------------------------------------------------- */
    OK(logfile_refresh(&lf) == 0);

    /* --- truncation triggers a full reload ---------------------------- */
    fp = fopen(TMPFILE, "wb");
    fputs("[    9.0] fresh\n", fp);
    fclose(fp);
    OK(logfile_refresh(&lf) == 1);
    OK(lf.nents == 1);
    OK(lf.ents[0].lineno == 1);

    /* --- continuation flags ------------------------------------------- */
    fp = fopen(TMPFILE, "wb");
    fputs("[    1.0] parent\n  indented continuation\n[    2.0] next\n",
          fp);
    fclose(fp);
    logfile_free(&lf);
    OK(logfile_load(&lf, TMPFILE, template_builtin("dmesg")) == 0);
    OK(lf.nents == 3);
    OK(lf.ents[0].matched == 1 && lf.ents[0].cont == 0);
    OK(lf.ents[1].matched == 0 && lf.ents[1].cont == 1);
    OK(lf.ents[2].matched == 1 && lf.ents[2].cont == 0);

    /* --- template switching -------------------------------------------- */
    OK(logfile_reparse(&lf, template_builtin("plain")) == 0);
    OK(lf.nmatched == 3); /* plain matches everything */

    /* --- empty file ------------------------------------------------------ */
    fp = fopen(TMPFILE, "wb");
    fclose(fp);
    logfile_free(&lf);
    OK(logfile_load(&lf, TMPFILE, NULL) == 0);
    OK(lf.nents == 0);
    OK(lf.tpl != NULL); /* autodetect falls back to plain */

    logfile_free(&lf);
    remove(TMPFILE);

    test_stream();

    return TEST_REPORT("test_logfile");
}
