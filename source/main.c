#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "logfile.h"
#include "lx.h"
#include "template.h"
#include "term.h"
#include "ui.h"

static void usage(FILE *o)
{
    fputs("usage: lx [options] <logfile>\n"
          "\n"
          "options:\n"
          "  -t <name>   parse with a built-in template (see -l)\n"
          "  -T <file>   parse with a template loaded from <file> (.lxt)\n"
          "  -f <expr>   apply a filter expression at startup,\n"
          "              e.g. -f 'level==ERR && message ~ timeout'\n"
          "  -F          start in follow mode (like tail -f)\n"
          "  -P          non-interactive: print the (filtered) lines to\n"
          "              stdout and exit\n"
          "  -l          list built-in templates\n"
          "  -h, --help  show this help\n"
          "  -V, --version  show version\n"
          "\n"
          "Without -t/-T the template is auto-detected from the file\n"
          "contents. See lx(1) and documentation/template-format.md for\n"
          "the template file format and the filter language.\n",
          o);
}

static void list_templates(void)
{
    int i;
    for (i = 0; i < template_builtin_count(); i++) {
        const Template *t = template_builtin_at(i);
        printf("%-12s %s\n", t->name, t->desc);
    }
}

int main(int argc, char **argv)
{
    const char *file = NULL, *tname = NULL, *tfile = NULL, *fexpr = NULL;
    int follow = 0, print = 0, i, rc;
    static Template custom;
    const Template *tpl = NULL;
    static LogFile lf;
    FNode *fn = NULL;
    char err[256];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            if (!strcmp(a, "-t") && i + 1 < argc) {
                tname = argv[++i];
            } else if (!strcmp(a, "-T") && i + 1 < argc) {
                tfile = argv[++i];
            } else if (!strcmp(a, "-f") && i + 1 < argc) {
                fexpr = argv[++i];
            } else if (!strcmp(a, "-F")) {
                follow = 1;
            } else if (!strcmp(a, "-P")) {
                print = 1;
            } else if (!strcmp(a, "-l")) {
                list_templates();
                return 0;
            } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
                usage(stdout);
                return 0;
            } else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
                printf("lx %s\n", LX_VERSION);
                return 0;
            } else {
                fprintf(stderr, "lx: unknown option '%s'\n\n", a);
                usage(stderr);
                return 2;
            }
        } else {
            if (file) {
                fprintf(stderr, "lx: only one file can be opened\n");
                return 2;
            }
            file = a;
        }
    }

    if (!file) {
        usage(stderr);
        return 2;
    }

    if (tfile) {
        if (template_load_file(&custom, tfile, err, sizeof err)) {
            fprintf(stderr, "lx: %s: %s\n", tfile, err);
            return 2;
        }
        tpl = &custom;
    } else if (tname) {
        tpl = template_builtin(tname);
        if (!tpl) {
            fprintf(stderr,
                    "lx: unknown template '%s' (use -l to list them)\n",
                    tname);
            return 2;
        }
    }

    if (logfile_load(&lf, file, tpl)) {
        fprintf(stderr, "lx: cannot read '%s'\n", file);
        return 1;
    }

    if (fexpr) {
        fn = filter_compile(fexpr, lf.tpl, err, sizeof err);
        if (!fn) {
            fprintf(stderr, "lx: filter: %s\n", err);
            return 2;
        }
    }
    filter_apply(fn, &lf);

    if (print) {
        size_t k;
        for (k = 0; k < lf.nents; k++) {
            if (lf.ents[k].visible) {
                fwrite(lf.buf + lf.ents[k].raw.off, 1, lf.ents[k].raw.len,
                       stdout);
                fputc('\n', stdout);
            }
        }
        logfile_free(&lf);
        filter_free(fn);
        return 0;
    }

    if (!term_is_tty()) {
        fprintf(stderr,
                "lx: interactive mode needs a terminal; use -P to print "
                "to a pipe\n");
        return 2;
    }

    rc = ui_run(&lf, fexpr ? fexpr : "", fn, follow);
    logfile_free(&lf);
    return rc;
}
