/* Interactive vi/less-style viewer. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fswatch.h"
#include "term.h"
#include "ui.h"
#include "util.h"

#define CTRL(c) ((c) & 0x1f)
#define HSTEP 8
#define HMAX 8000

typedef struct {
    LogFile *lf;
    size_t *vis; /* indices of visible entries */
    size_t nvis;
    size_t viscap;
    size_t cur;  /* cursor, index into vis[] */
    size_t top;  /* first row on screen, index into vis[] */
    long hscroll;
    char search[128];
    char filter_str[512];
    FNode *filter;
    Template custom; /* storage for :template file.lxt */
    int has_custom;
    int follow;
    int watch_fd;
    int quit;
    char msg[256];
} UI;

/* ------------------------------------------------------------------ */

static void rebuild(UI *ui)
{
    size_t i;
    ui->vis = xgrow(ui->vis, &ui->viscap,
                    ui->lf->nents ? ui->lf->nents : 1, sizeof *ui->vis);
    ui->nvis = 0;
    for (i = 0; i < ui->lf->nents; i++)
        if (ui->lf->ents[i].visible)
            ui->vis[ui->nvis++] = i;
    if (ui->nvis == 0)
        ui->cur = ui->top = 0;
    else if (ui->cur >= ui->nvis)
        ui->cur = ui->nvis - 1;
}

static int content_rows(void)
{
    int r, c;
    term_get_size(&r, &c);
    return r > 2 ? r - 2 : 1;
}

static void clamp_view(UI *ui)
{
    size_t rows = (size_t)content_rows();
    if (ui->nvis == 0) {
        ui->cur = ui->top = 0;
        return;
    }
    if (ui->cur >= ui->nvis)
        ui->cur = ui->nvis - 1;
    if (ui->top > ui->cur)
        ui->top = ui->cur;
    if (ui->cur >= ui->top + rows)
        ui->top = ui->cur - rows + 1;
    if (ui->nvis > rows && ui->top > ui->nvis - rows)
        ui->top = ui->nvis - rows;
    if (ui->nvis <= rows)
        ui->top = 0;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */

/* SGR for one character cell.
 *   cl:  0 = literal text between fields, f+1 = inside field f
 *   sev: severity class of the whole entry (-1 unknown)
 *   rev: reverse video (cursor line, toggled inside search matches)
 * Continuations render dim. Error/fatal entries are tinted red as a
 * whole; otherwise each field uses its template colour and the severity
 * field is coloured by its class. */
static void cell_sgr(char *out, size_t outsz, const Template *t,
                     const Entry *e, int sev, int cl, int rev)
{
    char col[24] = "";

    if (!e->matched) {
        if (e->cont)
            strcpy(col, ";2");
    } else if (sev >= 5) {
        strcpy(col, ";1;31");
    } else if (sev == 4) {
        strcpy(col, ";31");
    } else if (cl > 0) {
        int f = cl - 1;
        if (f == t->level_field) {
            if (sev == 3)
                strcpy(col, ";33");
            else if (sev == 1)
                strcpy(col, ";36");
            else if (sev == 0)
                strcpy(col, ";90");
        } else if (t->fields[f].c256 >= 0) {
            snprintf(col, sizeof col, ";38;5;%d", t->fields[f].c256);
        }
    }
    snprintf(out, outsz, "\x1b[0%s%sm", col, rev ? ";7" : "");
}

static void draw_entry(UI *ui, size_t ei, int iscur, int cols)
{
    static char ex[16384];
    static unsigned char cls[16384]; /* per-cell field class */
    static unsigned char mat[16384]; /* per-cell search-match flag */
    const LogFile *lf = ui->lf;
    const Template *t = lf->tpl;
    const Entry *e = &lf->ents[ei];
    const char *s = lf->buf + e->raw.off;
    const Span *fsp = e->matched ? logfile_fspans(lf, ei) : NULL;
    size_t n = e->raw.len, i, k, xl = 0;
    size_t need = (size_t)ui->hscroll + (size_t)cols;
    size_t wstart, wlen, slen;
    int sev = -1;
    char cur[32] = "", want[32];

    if (need > sizeof ex - 1)
        need = sizeof ex - 1;

    /* expand tabs and replace control bytes, tracking which field each
     * output cell belongs to */
    for (i = 0; i < n && xl < need; i++) {
        char c = s[i];
        unsigned char cl = 0;
        if (fsp) {
            int f;
            for (f = 0; f < t->nfields; f++) {
                if (fsp[f].len && i >= fsp[f].off &&
                    i < fsp[f].off + fsp[f].len) {
                    cl = (unsigned char)(f + 1);
                    break;
                }
            }
        }
        if (c == '\t') {
            do {
                ex[xl] = ' ';
                cls[xl] = cl;
                xl++;
            } while (xl % 8 && xl < need);
        } else {
            ex[xl] = (unsigned char)c < 32 ? '?' : c;
            cls[xl] = cl;
            xl++;
        }
    }

    if (fsp && t->level_field >= 0) {
        const Span *sp = &fsp[t->level_field];
        sev = severity_class(s + sp->off, sp->len);
    }

    /* mark search matches */
    slen = strlen(ui->search);
    if (xl)
        memset(mat, 0, xl);
    if (slen) {
        size_t pos = 0;
        long r;
        while (pos < xl &&
               (r = find_sub(ex + pos, xl - pos, ui->search, slen, 1)) >=
                   0) {
            size_t st = pos + (size_t)r, en = st + slen;
            if (en > xl)
                en = xl;
            for (k = st; k < en; k++)
                mat[k] = 1;
            pos = st + 1;
        }
    }

    wstart = (size_t)ui->hscroll < xl ? (size_t)ui->hscroll : xl;
    wlen = xl - wstart;
    if (wlen > (size_t)cols)
        wlen = (size_t)cols;

    for (k = 0; k < wlen; k++) {
        size_t g = wstart + k;
        int rev = mat[g] ? !iscur : iscur;
        cell_sgr(want, sizeof want, t, e, sev, cls[g], rev);
        if (strcmp(want, cur)) {
            term_writes(want);
            strcpy(cur, want);
        }
        term_write(ex + g, 1);
    }
    if (iscur && wlen == 0)
        term_writes("\x1b[7m \x1b[0m"); /* keep empty cursor lines visible */
    term_writes("\x1b[0m");
}

static void draw(UI *ui)
{
    int R, C, r, rows;
    char buf[600];
    int off;

    clamp_view(ui);
    term_get_size(&R, &C);
    rows = R > 2 ? R - 2 : 1;
    if (C > 550)
        C = 550;

    for (r = 0; r < rows; r++) {
        size_t vi = ui->top + (size_t)r;
        int n = snprintf(buf, sizeof buf, "\x1b[%d;1H\x1b[0m\x1b[K", r + 1);
        term_write(buf, (size_t)n);
        if (vi < ui->nvis)
            draw_entry(ui, ui->vis[vi], vi == ui->cur, C);
        else
            term_writes("\x1b[90m~\x1b[0m");
    }

    /* status bar; CLAMP keeps `off` a valid offset even when snprintf
     * truncates (it returns the would-be length) */
#define CLAMP_OFF() do { if (off > (int)sizeof buf - 1) \
                             off = (int)sizeof buf - 1; } while (0)
    {
        /* fall back to the basename when the full path would crowd out
         * the rest of the status line */
        const char *path = ui->lf->path;
        if ((int)strlen(path) > C / 3) {
            const char *slash = strrchr(path, '/');
            if (slash && slash[1])
                path = slash + 1;
        }
        off = snprintf(buf, sizeof buf, " %s  [%s]", path,
                       ui->lf->tpl->name);
    }
    CLAMP_OFF();
    if (ui->nvis) {
        off += snprintf(buf + off, sizeof buf - (size_t)off,
                        "  line %zu  %zu/%zu (%zu%%)",
                        ui->lf->ents[ui->vis[ui->cur]].lineno, ui->cur + 1,
                        ui->nvis, (ui->cur + 1) * 100 / ui->nvis);
    } else {
        off += snprintf(buf + off, sizeof buf - (size_t)off, "  0/0");
    }
    CLAMP_OFF();
    off += snprintf(buf + off, sizeof buf - (size_t)off,
                    "  parsed %zu/%zu", ui->lf->nmatched, ui->lf->nents);
    CLAMP_OFF();
    if (ui->filter)
        off += snprintf(buf + off, sizeof buf - (size_t)off,
                        "  filter: %s", ui->filter_str);
    CLAMP_OFF();
    if (ui->search[0])
        off += snprintf(buf + off, sizeof buf - (size_t)off, "  /%s",
                        ui->search);
    CLAMP_OFF();
    if (ui->follow)
        off += snprintf(buf + off, sizeof buf - (size_t)off, "  FOLLOW");
    CLAMP_OFF();
#undef CLAMP_OFF
    if (off > C)
        off = C;
    {
        char pos[16];
        int n = snprintf(pos, sizeof pos, "\x1b[%d;1H", R - 1);
        term_write(pos, (size_t)n);
    }
    term_writes("\x1b[0;7m");
    term_write(buf, (size_t)off);
    while (off++ < C)
        term_write(" ", 1);
    term_writes("\x1b[0m");

    /* message / hint row */
    {
        char pos[16];
        int n = snprintf(pos, sizeof pos, "\x1b[%d;1H", R);
        term_write(pos, (size_t)n);
    }
    term_writes("\x1b[K");
    if (ui->msg[0]) {
        size_t ml = strlen(ui->msg);
        if (ml > (size_t)C)
            ml = (size_t)C;
        term_write(ui->msg, ml);
    } else {
        const char *hint =
            "q quit  j/k move  / search  :filter EXPR  Enter fields  ? help";
        size_t hl = strlen(hint);
        if (hl > (size_t)C)
            hl = (size_t)C;
        term_writes("\x1b[2m");
        term_write(hint, hl);
        term_writes("\x1b[0m");
    }
    term_flush();
}

/* ------------------------------------------------------------------ */
/* overlays                                                            */

static void overlay(const char **lines, int n)
{
    int R, C, w = 0, i, y0, x0;
    char buf[600];

    term_get_size(&R, &C);
    for (i = 0; i < n; i++) {
        int l = (int)strlen(lines[i]);
        if (l > w)
            w = l;
    }
    if (w > C - 6)
        w = C - 6;
    if (w < 10)
        w = 10;
    if (n > R - 4)
        n = R - 4;
    y0 = (R - (n + 2)) / 2 + 1;
    x0 = (C - (w + 4)) / 2 + 1;
    if (y0 < 1)
        y0 = 1;
    if (x0 < 1)
        x0 = 1;

    snprintf(buf, sizeof buf, "\x1b[0m\x1b[%d;%dH+", y0, x0);
    term_writes(buf);
    for (i = 0; i < w + 2; i++)
        term_write("-", 1);
    term_write("+", 1);
    for (i = 0; i < n; i++) {
        int l = (int)strlen(lines[i]);
        if (l > w)
            l = w;
        snprintf(buf, sizeof buf, "\x1b[%d;%dH| ", y0 + 1 + i, x0);
        term_writes(buf);
        term_write(lines[i], (size_t)l);
        while (l++ < w)
            term_write(" ", 1);
        term_writes(" |");
    }
    snprintf(buf, sizeof buf, "\x1b[%d;%dH+", y0 + 1 + n, x0);
    term_writes(buf);
    for (i = 0; i < w + 2; i++)
        term_write("-", 1);
    term_write("+", 1);
    term_flush();
    term_read_key(-1, -1);
}

static void show_help(void)
{
    static const char *H[] = {
        "lx " LX_VERSION " - key bindings",
        "",
        "  j / k, arrows      move down / up",
        "  d / u              half page down / up",
        "  space / b, PgDn/Up page down / up",
        "  g / G, Home/End    first / last entry",
        "  h / l, arrows      scroll left / right (0 = reset)",
        "  Enter              show the parsed fields of the entry",
        "  /                  search (case-insensitive); n / N next / prev",
        "  F                  toggle follow mode (like tail -f)",
        "",
        "  :filter EXPR       filter entries, e.g. :filter level==ERR",
        "  :clear             remove the filter",
        "  :N                 go to line N",
        "  :template NAME     switch template (built-in or .lxt file)",
        "  :q                 quit    (also plain q)",
        "",
        "  filter operators: == != ~ !~ > >= < <= && || ! ( )",
        "  pseudo fields: raw (whole line), line (line number)",
        "",
        "        press any key to close",
    };
    overlay(H, (int)(sizeof H / sizeof H[0]));
}

static void show_detail(UI *ui)
{
    char lines[TPL_MAX_FIELDS + 4][120];
    const char *ptrs[TPL_MAX_FIELDS + 4];
    int n = 0, i;
    const LogFile *lf = ui->lf;
    const Template *t = lf->tpl;
    size_t ei;
    const Entry *e;

    if (!ui->nvis)
        return;
    ei = ui->vis[ui->cur];
    e = &lf->ents[ei];

    snprintf(lines[n], sizeof lines[n], "line %zu  -  %s  [%s]", e->lineno,
             e->matched ? "matched"
                        : (e->cont ? "continuation of previous entry"
                                   : "did not match template"),
             t->name);
    n++;
    snprintf(lines[n], sizeof lines[n], "%s", "");
    n++;

    if (e->matched) {
        const Span *fsp = logfile_fspans(lf, ei);
        for (i = 0; i < t->nfields && n < TPL_MAX_FIELDS + 3; i++) {
            const TField *f = &t->fields[i];
            const char *v = lf->buf + e->raw.off + fsp[i].off;
            int vl = (int)fsp[i].len;
            int trunc = 0;
            if (vl > 70) {
                vl = 70;
                trunc = 1;
            }
            if (f->unit[0])
                snprintf(lines[n], sizeof lines[n],
                         "%-10s (%s, %s) = %.*s%s", f->name,
                         field_type_name(f->type), f->unit, vl, v,
                         trunc ? "..." : "");
            else
                snprintf(lines[n], sizeof lines[n], "%-10s (%s) = %.*s%s",
                         f->name, field_type_name(f->type), vl, v,
                         trunc ? "..." : "");
            n++;
        }
    } else {
        int vl = (int)e->raw.len;
        int trunc = 0;
        if (vl > 70) {
            vl = 70;
            trunc = 1;
        }
        snprintf(lines[n], sizeof lines[n], "raw: %.*s%s", vl,
                 lf->buf + e->raw.off, trunc ? "..." : "");
        n++;
    }

    for (i = 0; i < n; i++)
        ptrs[i] = lines[i];
    overlay(ptrs, n);
}

/* ------------------------------------------------------------------ */
/* prompt + commands                                                   */

static int prompt(UI *ui, char pfx, char *buf, size_t bufsz)
{
    size_t len = 0;
    buf[0] = 0;
    term_show_cursor(1);
    for (;;) {
        int R, C, k;
        char line[600];
        int n;
        term_get_size(&R, &C);
        n = snprintf(line, sizeof line, "\x1b[%d;1H\x1b[0m\x1b[K%c%s", R,
                     pfx, buf);
        term_write(line, (size_t)n);
        term_flush();
        k = term_read_key(-1, -1);
        if (k == '\r' || k == '\n') {
            term_show_cursor(0);
            return 1;
        }
        if (k == 0x1b || k == CTRL('c')) {
            term_show_cursor(0);
            return 0;
        }
        if (k == 127 || k == 8) {
            if (len)
                buf[--len] = 0;
            else {
                term_show_cursor(0);
                return 0;
            }
        } else if (k == CTRL('u')) {
            len = 0;
            buf[0] = 0;
        } else if (k >= 32 && k < 127 && len + 1 < bufsz) {
            buf[len++] = (char)k;
            buf[len] = 0;
        } else if (k == TKEY_RESIZE) {
            draw(ui);
        }
    }
}

static void set_msg(UI *ui, const char *fmt, const char *a1, const char *a2)
{
    snprintf(ui->msg, sizeof ui->msg, fmt, a1, a2);
}

static void do_filter(UI *ui, const char *expr)
{
    char err[256];
    FNode *f = filter_compile(expr, ui->lf->tpl, err, sizeof err);
    if (!f) {
        set_msg(ui, "filter error: %s%s", err, "");
        return;
    }
    filter_free(ui->filter);
    ui->filter = f;
    snprintf(ui->filter_str, sizeof ui->filter_str, "%s", expr);
    filter_apply(f, ui->lf);
    rebuild(ui);
    ui->cur = ui->top = 0;
    snprintf(ui->msg, sizeof ui->msg, "filter matches %zu of %zu entries",
             ui->lf->nvisible, ui->lf->nents);
}

static void clear_filter(UI *ui)
{
    if (!ui->filter) {
        set_msg(ui, "no filter active%s%s", "", "");
        return;
    }
    filter_free(ui->filter);
    ui->filter = NULL;
    ui->filter_str[0] = 0;
    filter_apply(NULL, ui->lf);
    rebuild(ui);
    set_msg(ui, "filter cleared%s%s", "", "");
}

static void goto_line(UI *ui, size_t lineno)
{
    size_t i;
    for (i = 0; i < ui->nvis; i++) {
        if (ui->lf->ents[ui->vis[i]].lineno >= lineno) {
            ui->cur = i;
            return;
        }
    }
    if (ui->nvis)
        ui->cur = ui->nvis - 1;
}

static void refilter_after_template_change(UI *ui)
{
    if (ui->filter) {
        char err[256];
        FNode *nf =
            filter_compile(ui->filter_str, ui->lf->tpl, err, sizeof err);
        filter_free(ui->filter);
        ui->filter = nf;
        if (!nf) {
            set_msg(ui, "filter cleared (not valid for new template: %s)%s",
                    err, "");
            ui->filter_str[0] = 0;
        }
    }
    filter_apply(ui->filter, ui->lf);
    rebuild(ui);
}

static void switch_template(UI *ui, const char *arg)
{
    const Template *nt;
    char err[256];

    if (!*arg) {
        set_msg(ui, "usage: :template NAME | file.lxt%s%s", "", "");
        return;
    }
    nt = template_builtin(arg);
    if (!nt) {
        Template tmp;
        if (template_load_file(&tmp, arg, err, sizeof err) == 0) {
            if (ui->has_custom)
                template_free(&ui->custom);
            ui->custom = tmp;
            ui->has_custom = 1;
            nt = &ui->custom;
        } else {
            set_msg(ui, "no template '%s': %s", arg, err);
            return;
        }
    }
    logfile_reparse(ui->lf, nt);
    refilter_after_template_change(ui);
    if (!ui->msg[0])
        snprintf(ui->msg, sizeof ui->msg,
                 "template: %s  (parsed %zu/%zu entries)", nt->name,
                 ui->lf->nmatched, ui->lf->nents);
}

static void exec_command(UI *ui, char *cmd)
{
    char *s = trim(cmd), *arg;
    int alldig = 1;
    char *p;

    if (!*s)
        return;
    for (p = s; *p; p++)
        if (!isdigit((unsigned char)*p))
            alldig = 0;
    if (alldig) {
        goto_line(ui, (size_t)strtoul(s, NULL, 10));
        return;
    }
    arg = s;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg) {
        *arg++ = 0;
        arg = trim(arg);
    }

    if (!strcmp(s, "q") || !strcmp(s, "quit")) {
        ui->quit = 1;
    } else if (!strcmp(s, "f") || !strcmp(s, "filter")) {
        if (!*arg)
            set_msg(ui, "usage: :filter EXPR%s%s", "", "");
        else
            do_filter(ui, arg);
    } else if (!strcmp(s, "clear")) {
        clear_filter(ui);
    } else if (!strcmp(s, "t") || !strcmp(s, "template")) {
        switch_template(ui, arg);
    } else if (!strcmp(s, "help") || !strcmp(s, "h")) {
        show_help();
    } else {
        set_msg(ui, "unknown command: %s%s", s, "");
    }
}

/* ------------------------------------------------------------------ */
/* search                                                              */

static int entry_contains(UI *ui, size_t ei)
{
    const Entry *e = &ui->lf->ents[ei];
    return find_sub(ui->lf->buf + e->raw.off, e->raw.len, ui->search,
                    strlen(ui->search), 1) >= 0;
}

static void search_move(UI *ui, int dir, int inclusive)
{
    long n = (long)ui->nvis, start, k;
    if (!ui->search[0]) {
        set_msg(ui, "no search pattern%s%s", "", "");
        return;
    }
    if (!n)
        return;
    start = (long)ui->cur + (inclusive ? 0 : dir);
    for (k = 0; k < n; k++) {
        long idx = ((start + dir * k) % n + n) % n;
        if (entry_contains(ui, ui->vis[idx])) {
            if ((dir > 0 && idx < (long)ui->cur) ||
                (dir < 0 && idx > (long)ui->cur))
                set_msg(ui, "search wrapped%s%s", "", "");
            ui->cur = (size_t)idx;
            return;
        }
    }
    set_msg(ui, "pattern not found: %s%s", ui->search, "");
}

/* ------------------------------------------------------------------ */
/* follow mode                                                         */

static void set_follow(UI *ui, int on)
{
    if (on == ui->follow)
        return;
    ui->follow = on;
    if (on) {
        ui->watch_fd = fswatch_open(ui->lf->path);
        set_msg(ui, "follow mode on%s%s", "", "");
    } else {
        if (ui->watch_fd >= 0)
            fswatch_close(ui->watch_fd);
        ui->watch_fd = -1;
        set_msg(ui, "follow mode off%s%s", "", "");
    }
}

static void do_refresh(UI *ui)
{
    int rc = logfile_refresh(ui->lf);
    if (rc > 0) {
        filter_apply(ui->filter, ui->lf);
        rebuild(ui);
        if (ui->follow && ui->nvis)
            ui->cur = ui->nvis - 1;
    }
}

/* ------------------------------------------------------------------ */

int ui_run(LogFile *lf, const char *filter_str, FNode *filter, int follow)
{
    UI ui;
    char inbuf[512];

    memset(&ui, 0, sizeof ui);
    ui.lf = lf;
    ui.filter = filter;
    ui.watch_fd = -1;
    if (filter)
        snprintf(ui.filter_str, sizeof ui.filter_str, "%s", filter_str);
    rebuild(&ui);

    if (term_init()) {
        fprintf(stderr, "lx: failed to initialise the terminal\n");
        return 1;
    }
    snprintf(ui.msg, sizeof ui.msg,
             "%zu entries, %zu parsed with template '%s'%s", lf->nents,
             lf->nmatched, lf->tpl->name,
             ui.filter ? " (filter active)" : "");
    if (follow) {
        set_follow(&ui, 1);
        if (ui.nvis)
            ui.cur = ui.nvis - 1;
    }

    while (!ui.quit) {
        size_t half = (size_t)content_rows() / 2 + 1;
        size_t page = (size_t)content_rows();
        int k;

        draw(&ui);
        k = term_read_key(ui.follow ? 500 : -1,
                          ui.follow ? ui.watch_fd : -1);
        if (k != TKEY_NONE && k != TKEY_FSEVENT && k != TKEY_RESIZE)
            ui.msg[0] = 0;

        switch (k) {
        case 'q':
        case CTRL('c'):
            ui.quit = 1;
            break;
        case 'j':
        case TKEY_DOWN:
            if (ui.cur + 1 < ui.nvis)
                ui.cur++;
            break;
        case 'k':
        case TKEY_UP:
            if (ui.cur)
                ui.cur--;
            break;
        case 'd':
        case CTRL('d'):
            ui.cur = ui.cur + half < ui.nvis ? ui.cur + half
                                             : (ui.nvis ? ui.nvis - 1 : 0);
            break;
        case 'u':
        case CTRL('u'):
            ui.cur = ui.cur > half ? ui.cur - half : 0;
            break;
        case ' ':
        case CTRL('f'):
        case TKEY_PGDN:
            ui.cur = ui.cur + page < ui.nvis ? ui.cur + page
                                             : (ui.nvis ? ui.nvis - 1 : 0);
            break;
        case 'b':
        case CTRL('b'):
        case TKEY_PGUP:
            ui.cur = ui.cur > page ? ui.cur - page : 0;
            break;
        case 'g':
        case TKEY_HOME:
            ui.cur = 0;
            break;
        case 'G':
        case TKEY_END:
            if (ui.nvis)
                ui.cur = ui.nvis - 1;
            break;
        case 'h':
        case TKEY_LEFT:
            ui.hscroll = ui.hscroll > HSTEP ? ui.hscroll - HSTEP : 0;
            break;
        case 'l':
        case TKEY_RIGHT:
            if (ui.hscroll < HMAX)
                ui.hscroll += HSTEP;
            break;
        case '0':
            ui.hscroll = 0;
            break;
        case '/':
            if (prompt(&ui, '/', inbuf, sizeof inbuf) && inbuf[0]) {
                snprintf(ui.search, sizeof ui.search, "%s", inbuf);
                search_move(&ui, 1, 1);
            }
            break;
        case 'n':
            search_move(&ui, 1, 0);
            break;
        case 'N':
            search_move(&ui, -1, 0);
            break;
        case ':':
            if (prompt(&ui, ':', inbuf, sizeof inbuf))
                exec_command(&ui, inbuf);
            break;
        case '\r':
        case '\n':
            show_detail(&ui);
            break;
        case '?':
            show_help();
            break;
        case 'F':
            set_follow(&ui, !ui.follow);
            if (ui.follow) {
                do_refresh(&ui);
                if (ui.nvis)
                    ui.cur = ui.nvis - 1;
            }
            break;
        case TKEY_FSEVENT:
            if (ui.watch_fd >= 0)
                fswatch_drain(ui.watch_fd);
            do_refresh(&ui);
            break;
        case TKEY_NONE: /* follow-mode poll tick */
            if (ui.follow)
                do_refresh(&ui);
            break;
        default:
            break;
        }
    }

    set_follow(&ui, 0);
    term_shutdown();
    filter_free(ui.filter);
    if (ui.has_custom)
        template_free(&ui.custom);
    free(ui.vis);
    return 0;
}
