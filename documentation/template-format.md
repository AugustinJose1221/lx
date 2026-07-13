# lx template format (`.lxt`)

A template tells lx how a single log entry is structured: which named
components it contains, their data types, units, and allowed values.
Field names double as identifiers in the filter language.

Templates are plain text files, conventionally with the `.lxt`
extension, loaded with `lx file.log -T my.lxt` (or at runtime with
`:template my.lxt`).

You don't have to write them by hand: `lx file.log -g my.lxt` starts an
interactive wizard that splits a sample line, suggests names, types,
timestamp formats and colours, validates the result against the log,
and writes the file for you.

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

A template may declare **several `entry:` lines** (up to 4). They are
tried in order and the first one that matches wins — useful when a
logger emits a couple of line shapes. The built-in `serilog` does this:

```
entry: %{timestamp} [%{level}]: %{message}
entry: %{timestamp} [%{level}] %{message}
```

Within a pattern:

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
| `color=`    | all         | display colour for the field's text as a hex code: `#RRGGBB`, `RRGGBB` or `#RGB`. Mapped to the nearest xterm-256 colour, so it works in every 256-colour terminal (including Terminal.app). |

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
## Colours

Every built-in template ships with a default colour set (grey
timestamps, blue hosts, teal process/logger names, ...), and custom
templates can colour any field with `color=#RRGGBB`.

Severity always wins over field colours:

* If the template has a severity field — one named `level`, `severity`,
  `lvl`, `loglevel`, `priority` or `pri`, or one marked `severity=yes` —
  its value classifies the entry.
* **Error** entries are tinted red and **fatal/critical** entries bold
  red across the whole line, overriding field colours so problems stand
  out when scrolling.
* On other entries the severity field itself is coloured by class
  (warning yellow, debug cyan, trace grey) while the remaining fields
  use their `color=` values.
* Continuation lines are dimmed.

Example:

```
field time: type=timestamp format="%H:%M:%S.%f" color=#8A8A8A
field module: type=word color=#5FAFD7
field level: type=enum values=INFO|WARN|ERROR    # severity: no color= needed
```

## Built-in templates

Run `lx -l`. The built-ins (`plain`, `syslog`, `syslog-iso`, `dmesg`,
`serilog`, `python`, `apache`) are written in this same format — their
definitions in [`source/template.c`](../source/template.c) are good
starting points for custom templates, as is
[`examples/myapp.lxt`](examples/myapp.lxt).
