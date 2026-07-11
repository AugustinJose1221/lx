# lx — a terminal log viewer and analyzer

`lx` opens log files (`.log`, `.txt`) in an interactive, vi-style pager,
parses every line against a *log template* (built-in or your own), and
lets you navigate, search and **filter on the named fields** of each
entry — like a lightweight, file-based `journalctl`.

Written entirely in C99 with the standard library; the only
platform-specific code (raw terminal mode, file-change notification) is
isolated per platform under `source/posix`, `source/linux`,
`source/darwin` and `source/windows`.

```
lx app.log                 # open with template auto-detection
lx kern.log -t dmesg       # parse as Linux kernel log
lx app.log -T myapp.lxt    # parse with a custom template
lx svc.log -F              # follow a growing file (tail -f)
lx app.log -P -f 'level==ERR && message ~ timeout'   # pipe mode
```

## Features

- **Vi/less-style navigation** — `j/k`, half/full pages, `g/G`,
  horizontal scrolling, `/` search with `n/N`.
- **Log templates** describe the structure of an entry: each component
  is a *named field* with a type (`string`, `word`, `int`, `float`,
  `timestamp`, `enum`), an optional unit, allowed values, and a
  timestamp format. See
  [documentation/template-format.md](documentation/template-format.md).
- **Built-in templates**: `plain`, `syslog` (RFC 3164), `syslog-iso`
  (rsyslog), `dmesg` (kernel), `serilog` (.NET), `python` (logging),
  `apache` (common log format) — auto-detected when none is given
  (`lx -l` lists them).
- **Field filtering**: `:filter level==ERR || level==WRN`,
  `timestamp >= "2026-06-01 19:00:00"`, `status >= 500`,
  `message ~ timeout`, with `&& || ! ()` logic. Unparsed lines (stack
  traces) are treated as continuations and follow their parent entry.
- **Entry inspector**: press `Enter` on a line to see every parsed
  field with its type, unit and value.
- **Severity colouring** driven by the template's `level` field.
- **Follow mode** (`-F` or `F`): inotify on Linux, kqueue on macOS,
  polling elsewhere; handles file truncation/rotation.
- **Pipe mode** (`-P`): print filtered lines to stdout for scripts.

## Building

```
make            # builds build/lx
make test       # builds and runs the unit tests
make install    # installs lx + man page (PREFIX=/usr/local)
```

Requires only a C99 compiler and make. Linux and macOS build out of the
box; other POSIX systems fall back to a generic backend
(`source/posix/fswatch_poll.c`). Windows sources live in
`source/windows` (Win32 console + VT output).

## Try it

```
make
./build/lx tests/data/python.log            # auto-detects 'python'
./build/lx tests/data/serilog.log -f 'level==ERR'
./build/lx documentation/examples/myapp.log -T documentation/examples/myapp.lxt
```

Inside the viewer: `?` shows all key bindings, `Enter` inspects an
entry, `:filter level==ERROR` filters, `:clear` unfilters, `q` quits.

## Repository layout

```
source/              core C sources (C99, standard library only)
source/posix/        termios/ANSI terminal backend, poll fallback
source/linux/        inotify file watching
source/darwin/       kqueue file watching
source/windows/      Win32 console backend
tests/               unit tests + sample logs (make test)
documentation/       man page, template format reference, examples
```

## Documentation

- `man ./documentation/lx.1` — full manual
- [documentation/template-format.md](documentation/template-format.md)
  — writing custom templates
- [documentation/examples/](documentation/examples/) — a worked
  custom-template example
