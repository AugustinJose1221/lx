#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "logfile.h"
#include "template.h"
#include "test.h"

#define TMPFILE "lx_test_filter.tmp"

static const char *SAMPLE =
    "2026-06-01 19:18:23.123+2.00 UTC [INF]: Application started\n"
    "2026-06-01 19:18:24.001+2.00 UTC [DBG]: Cache warm-up begun\n"
    "2026-06-01 19:18:25.512+2.00 UTC [WRN]: Disk usage at 85%\n"
    "2026-06-01 19:18:26.700+2.00 UTC [ERR]: Failed: timeout\n"
    "    retry 1 of 3\n"
    "    retry 2 of 3\n"
    "2026-06-01 19:18:30.002+2.00 UTC [INF]: Connected to db-02\n"
    "2026-06-01 19:18:31.444+2.00 UTC [FTL]: Shutting down\n";

static size_t count_visible(const LogFile *lf)
{
    size_t i, n = 0;
    for (i = 0; i < lf->nents; i++)
        n += lf->ents[i].visible;
    return n;
}

static size_t apply(LogFile *lf, const char *expr)
{
    char err[256];
    FNode *f = filter_compile(expr, lf->tpl, err, sizeof err);
    size_t n;
    if (!f) {
        printf("  compile failed: %s (%s)\n", expr, err);
        return (size_t)-1;
    }
    filter_apply(f, lf);
    n = count_visible(lf);
    filter_free(f);
    return n;
}

int main(void)
{
    LogFile lf;
    FILE *fp;
    char err[256];

    fp = fopen(TMPFILE, "wb");
    OK(fp != NULL);
    fputs(SAMPLE, fp);
    fclose(fp);

    OK(logfile_load(&lf, TMPFILE, template_builtin("serilog")) == 0);
    OK(lf.nents == 8);
    OK(lf.nmatched == 6);

    /* equality and enum values */
    OK(apply(&lf, "level==ERR") == 3);            /* + 2 continuations */
    OK(apply(&lf, "level == INF") == 2);
    OK(apply(&lf, "level != INF") == 6);
    OK(apply(&lf, "level=WRN") == 1);

    /* contains (case-insensitive) */
    OK(apply(&lf, "message ~ TIMEOUT") == 3);
    OK(apply(&lf, "message !~ timeout") == 5);
    OK(apply(&lf, "raw ~ retry") == 0); /* continuations inherit parent */

    /* boolean logic */
    OK(apply(&lf, "level==ERR || level==FTL") == 4);
    OK(apply(&lf, "level==ERR or level==FTL") == 4);
    OK(apply(&lf, "level==INF && message ~ db") == 1);
    OK(apply(&lf, "not level==INF") == 6);
    OK(apply(&lf, "!(level==INF || level==DBG)") == 5);

    /* line number pseudo field */
    OK(apply(&lf, "line <= 2") == 2);
    OK(apply(&lf, "line > 6") == 2);

    /* timestamp ranges (wall clock, fallback format without fraction) */
    OK(apply(&lf, "timestamp >= \"2026-06-01 19:18:25\"") == 6);
    OK(apply(&lf, "timestamp < '2026-06-01 19:18:24'") == 1);
    OK(apply(&lf,
             "timestamp >= \"2026-06-01 19:18:24\" && "
             "timestamp < \"2026-06-01 19:18:31\"") == 6);

    /* compile errors */
    OK(filter_compile("bogusfield == 1", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("level ==", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("level == ERR &&", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("(level == ERR", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("timestamp > junk", lf.tpl, err, sizeof err) == NULL);
    OK(filter_compile("line ~ 3", lf.tpl, err, sizeof err) == NULL);

    /* NULL filter shows everything */
    filter_apply(NULL, &lf);
    OK(count_visible(&lf) == 8);

    logfile_free(&lf);
    remove(TMPFILE);
    return TEST_REPORT("test_filter");
}
