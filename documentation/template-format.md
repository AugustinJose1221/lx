# lx template format (`.lxt`)

A template tells lx how a single log entry is structured: which named
components it contains, their data types, units, and allowed values.
Field names double as identifiers in the filter language.

Templates are plain text files, conventionally with the `.lxt`
extension, loaded with `lx file.log -T my.lxt` (or at runtime with
`:template my.lxt`).

## Anatomy

```
# Comments start with '#'. Blank lines are ignored.

name: myapp
description: MyApp console output

# The shape of one log line. Literal text + %{field} placeholders.
entry: %{time} | %{level} | %{module} | %{message}

# One declaration per field: attributes as key=value pairs.
field time: type=timestamp format="%H:%M:%S.%f"
field level: type=enum values=TRACE|DEBUG|INFO|WARN|ERROR
field module: type=word
field message: type=string
```

This parses lines such as:

```
19:18:23.123 | WARN  | net | retry in 5 s
```

into `time=19:18:23.123`, `level=WARN`, `module=net`,
`message=retry in 5 s` — and `lx -f 'level==ERROR && module==net'`
filters on those names.

## Keys

| key            | meaning                                              |
|----------------|------------------------------------------------------|
| `name:`        | template name (shown in the status bar)              |
| `description:` | one-line description (shown by `lx -l`)              |
| `entry:`       | the entry pattern (required)                         |
| `field NAME:`  | attributes for the field NAME                        |

## The entry pattern

* `%{name}` — a field placeholder. Fields not declared elsewhere
  default to `type=string`.
* `%{name:type}` — placeholder with an inline type
  (`entry: %{n:int} %{rest:string}`).
* `%%` — a literal percent sign.
* Any other text must appear literally in the log line. A **space in
  the pattern matches one or more spaces/tabs**, so column-aligned logs
  (`Jun  1 ...`, `WARN  |`) match naturally.

## Field attributes

| attribute   | applies to  | meaning                                          |
|-------------|-------------|--------------------------------------------------|
| `type=`     | all         | `string`, `word`, `int`, `float`, `timestamp`, `enum` |
| `format=`   | timestamp   | timestamp layout (see directives below); quoted since it contains spaces. Setting `format=` implies `type=timestamp`. |
| `values=`   | enum        | allowed values, separated by `\|` (or commas inside quotes). Setting `values=` implies `type=enum`. |
| `unit=`     | int, float  | informational unit (`s`, `ms`, `bytes`, ...); shown in the Enter field inspector. |
| `severity=` | all         | `severity=yes` marks this field as the severity field driving per-line colouring, when its name alone (`level`, `severity`, ...) would not identify it — e.g. the `type` column of the macOS unified log. |

### Field types

* `string` — greedy text. Consumes up to the next literal in the
  pattern, or to the end of the line when it is the last piece.
* `word` — like `string` but never crosses whitespace. Good for
  hostnames, process names (`sshd[42]`), loggers.
* `int` — an optionally signed integer; filters compare it numerically.
* `float` — a floating-point number (leading spaces allowed, so
  dmesg's `[   12.345678]` works); numeric comparisons.
* `timestamp` — parsed with `format=`; filters compare chronologically.
* `enum` — exactly one of `values=`, with a word-boundary check
  (`INF` will not half-match `INFO`).

### Timestamp format directives

```
%Y 4-digit year      %y 2-digit year      %m month 01-12
%b/%B month name     %d day               %e day, space padded
%H hour 00-23        %I hour 01-12        %p AM/PM
%M minute            %S second            %f fractional seconds
%j day of year       %a/%A weekday name (skipped)
%z UTC offset: +HH, +HHMM, +HH:MM, +H.MM, or Z
%Z timezone name (e.g. "UTC"; optional, skipped)
%% literal percent   space = one or more spaces/tabs
```

Missing components default to 1970-01-01 00:00:00, so time-only or
day-only timestamps still compare consistently *within one file*.
Offsets (`%z`) are validated and consumed but **not applied**: filters
compare wall-clock time exactly as printed in the log, which is what a
human typing `timestamp >= "2026-06-01 19:00:00"` means.

## Matching rules

* Literals must match (with elastic whitespace); every field must
  parse; the whole line must be consumed (trailing whitespace ok).
* A line that does not match is a **continuation** of the previous
  entry (stack traces, wrapped messages). Continuations inherit their
  parent's filter visibility and are dimmed in the UI.
* If the template declares a field named `level`, `severity`, `lvl`,
  `loglevel`, `priority` or `pri`, its value drives per-line colouring
  (fatal/error red, warning yellow, debug cyan, trace grey).

## Built-in templates

Run `lx -l`. The built-ins (`plain`, `syslog`, `syslog-iso`, `dmesg`,
`serilog`, `python`, `apache`) are written in this same format — their
definitions in [`source/template.c`](../source/template.c) are good
starting points for custom templates, as is
[`examples/myapp.lxt`](examples/myapp.lxt).
