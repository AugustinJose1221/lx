#include <math.h>
#include <string.h>

#include "template.h"
#include "test.h"

static int span_eq(const char *line, Span sp, const char *want)
{
    return sp.len == strlen(want) && !memcmp(line + sp.off, want, sp.len);
}

int main(void)
{
    Span sp[TPL_MAX_FIELDS];
    double fn[TPL_MAX_FIELDS];
    char err[256];
    const Template *t;
    const char *line;

    /* --- built-in lookup ------------------------------------------ */
    OK(template_builtin("serilog") != NULL);
    OK(template_builtin("SERILOG") != NULL); /* case-insensitive */
    OK(template_builtin("nope") == NULL);
    OK(template_builtin_count() >= 6);

    /* --- serilog (the format from the requirements) ---------------- */
    t = template_builtin("serilog");
    line = "2026-06-01 19:18:23.123+2.00 UTC [INF]: Some log entry";
    OK(template_match(t, line, strlen(line), sp, fn) == 1);
    {
        int fl = template_field_index(t, "level", 5);
        int fm = template_field_index(t, "message", 7);
        int fts = template_field_index(t, "timestamp", 9);
        OK(fl >= 0 && fm >= 0 && fts >= 0);
        OK(span_eq(line, sp[fl], "INF"));
        OK(span_eq(line, sp[fm], "Some log entry"));
        OK(!isnan(fn[fts]));
        OK(t->level_field == fl);
    }
    line = "2026-06-01 19:18:23.123+2.00 UTC [XXX]: bad level";
    OK(template_match(t, line, strlen(line), sp, fn) == 0);
    line = "no timestamp at all";
    OK(template_match(t, line, strlen(line), sp, fn) == 0);

    /* --- dmesg: float with unit ------------------------------------ */
    t = template_builtin("dmesg");
    line = "[   12.345678] usb 1-1: new device";
    OK(template_match(t, line, strlen(line), sp, fn) == 1);
    {
        int ft = template_field_index(t, "time", 4);
        OK(ft >= 0);
        OK(fabs(fn[ft] - 12.345678) < 1e-9);
        OK(!strcmp(t->fields[ft].unit, "s"));
    }

    /* --- syslog: words with scan-to-literal ------------------------ */
    t = template_builtin("syslog");
    line = "Jun  1 19:18:23 myhost sshd[4123]: Accepted publickey";
    OK(template_match(t, line, strlen(line), sp, fn) == 1);
    OK(span_eq(line, sp[template_field_index(t, "host", 4)], "myhost"));
    OK(span_eq(line, sp[template_field_index(t, "proc", 4)], "sshd[4123]"));
    OK(span_eq(line, sp[template_field_index(t, "message", 7)],
               "Accepted publickey"));

    /* --- python: enum word boundary --------------------------------- */
    t = template_builtin("python");
    line = "2026-06-01 19:18:23,123 - app.db - INFO - hello";
    OK(template_match(t, line, strlen(line), sp, fn) == 1);
    OK(span_eq(line, sp[template_field_index(t, "level", 5)], "INFO"));
    line = "2026-06-01 19:18:23,123 - app.db - INFORMAL - hello";
    OK(template_match(t, line, strlen(line), sp, fn) == 0);

    /* --- macos: unified log via 'log show' -------------------------- */
    t = template_builtin("macos");
    OK(t != NULL);
    line = "2026-06-01 19:18:23.123456+0200 0x16b3     Default     0x0"
           "                  0      0    kernel: en0: link up";
    OK(template_match(t, line, strlen(line), sp, fn) == 1);
    OK(span_eq(line, sp[template_field_index(t, "type", 4)], "Default"));
    OK(span_eq(line, sp[template_field_index(t, "process", 7)], "kernel"));
    OK(span_eq(line, sp[template_field_index(t, "message", 7)],
               "en0: link up"));
    OK(fabs(fn[template_field_index(t, "pid", 3)] - 0.0) < 1e-9);
    /* 'severity=yes' marks 'type' as the severity field */
    OK(t->level_field == template_field_index(t, "type", 4));
    /* the column header line must not match */
    line = "Timestamp                       Thread     Type        "
           "Activity             PID    TTL";
    OK(template_match(t, line, strlen(line), sp, fn) == 0);

    /* --- custom template parsing ------------------------------------ */
    {
        Template c;
        const char *def =
            "# comment\n"
            "name: myapp\n"
            "description: test template\n"
            "entry: %{time} | %{level} | %{module} | %{message}\n"
            "field time: type=timestamp format=\"%H:%M:%S.%f\"\n"
            "field level: type=enum values=TRACE|DEBUG|INFO|WARN|ERROR\n"
            "field module: type=word\n"
            "field message: type=string\n";
        OK(template_parse_text(&c, def, err, sizeof err) == 0);
        OK(!strcmp(c.name, "myapp"));
        OK(c.nfields == 4);
        OK(c.fields[template_field_index(&c, "level", 5)].type == FT_ENUM);
        OK(c.level_field == template_field_index(&c, "level", 5));

        line = "19:18:23.123 | WARN  | net | retry in 5 s";
        OK(template_match(&c, line, strlen(line), sp, fn) == 1);
        OK(span_eq(line, sp[template_field_index(&c, "module", 6)], "net"));
        OK(span_eq(line, sp[template_field_index(&c, "message", 7)],
                   "retry in 5 s"));
        template_free(&c);
    }

    /* --- error reporting -------------------------------------------- */
    {
        Template c;
        OK(template_parse_text(&c, "name: x\n", err, sizeof err) != 0);
        OK(template_parse_text(&c, "entry: %{a\n", err, sizeof err) != 0);
        OK(template_parse_text(&c, "entry: %{a:nosuch}\n", err,
                               sizeof err) != 0);
        OK(template_parse_text(&c, "bogus stuff\n", err, sizeof err) != 0);
    }

    /* --- inline type in the entry pattern ---------------------------- */
    {
        Template c;
        const char *def = "entry: %{n:int} %{rest:string}\n";
        OK(template_parse_text(&c, def, err, sizeof err) == 0);
        line = "42 hello world";
        OK(template_match(&c, line, strlen(line), sp, fn) == 1);
        OK(fabs(fn[0] - 42.0) < 1e-9);
        OK(span_eq(line, sp[1], "hello world"));
        line = "x hello";
        OK(template_match(&c, line, strlen(line), sp, fn) == 0);
        template_free(&c);
    }

    /* --- autodetect --------------------------------------------------- */
    {
        const char *buf =
            "2026-06-01 19:18:23,123 - root - INFO - one\n"
            "2026-06-01 19:18:24,456 - root - ERROR - two\n"
            "Traceback (most recent call last):\n"
            "  File \"x.py\", line 1\n"
            "2026-06-01 19:18:25,789 - root - INFO - three\n";
        const Template *d = template_autodetect(buf, strlen(buf));
        OK(!strcmp(d->name, "python"));
        d = template_autodetect("random text\nmore text\n", 22);
        OK(!strcmp(d->name, "plain"));
    }

    /* --- explicit severity= attribute --------------------------------- */
    {
        Template c;
        const char *def = "entry: %{kind} %{message}\n"
                          "field kind: type=enum values=OK|BAD severity=yes\n";
        OK(template_parse_text(&c, def, err, sizeof err) == 0);
        OK(c.level_field == template_field_index(&c, "kind", 4));
        template_free(&c);
    }

    /* --- field colours ------------------------------------------------ */
    {
        Template c;
        const char *def =
            "entry: %{a} %{b} %{c} %{message}\n"
            "field a: type=word color=#5FAFD7\n"
            "field b: type=word color=FF0000\n"
            "field c: type=word color=#f00\n";
        OK(template_parse_text(&c, def, err, sizeof err) == 0);
        OK(c.fields[0].rgb == 0x5FAFD7);
        OK(c.fields[0].c256 == 74);
        OK(c.fields[1].rgb == 0xFF0000);
        OK(c.fields[1].c256 == 196);
        OK(c.fields[2].rgb == 0xFF0000); /* #RGB expands */
        OK(c.fields[3].rgb == -1);       /* undeclared: no colour */
        template_free(&c);
        OK(template_parse_text(&c,
                               "entry: %{a}\nfield a: color=#12345\n", err,
                               sizeof err) != 0); /* bad hex length */
        OK(template_parse_text(&c,
                               "entry: %{a}\nfield a: color=#GGGGGG\n", err,
                               sizeof err) != 0); /* bad hex digits */
    }

    /* --- rgb -> xterm-256 mapping -------------------------------------- */
    OK(rgb_to_256(0xFF, 0x00, 0x00) == 196);
    OK(rgb_to_256(0x00, 0x00, 0x00) == 16);
    OK(rgb_to_256(0xFF, 0xFF, 0xFF) == 231);
    OK(rgb_to_256(0x8A, 0x8A, 0x8A) == 245); /* grayscale ramp */

    /* --- built-in default palette --------------------------------------- */
    t = template_builtin("syslog");
    OK(t->fields[template_field_index(t, "timestamp", 9)].c256 >= 0);
    OK(t->fields[template_field_index(t, "host", 4)].rgb == 0x5FAFD7);
    OK(t->fields[template_field_index(t, "message", 7)].rgb == -1);
    t = template_builtin("macos");
    OK(t->fields[template_field_index(t, "process", 7)].rgb == 0x00AFAF);
    /* the severity field keeps class-based colouring, not a fixed one */
    OK(t->fields[template_field_index(t, "type", 4)].rgb == -1);

    /* --- severity classes -------------------------------------------- */
    OK(severity_class("ERR", 3) == 4);
    OK(severity_class("error", 5) == 4);
    OK(severity_class("WARNING", 7) == 3);
    OK(severity_class("FTL", 3) == 5);
    OK(severity_class("CRITICAL", 8) == 5);
    OK(severity_class("debug", 5) == 1);
    OK(severity_class("TRACE", 5) == 0);
    OK(severity_class("hello", 5) == -1);
    OK(severity_class("Fault", 5) == 5);
    OK(severity_class("Default", 7) == 2); /* not fatal despite "fault" */

    return TEST_REPORT("test_template");
}
