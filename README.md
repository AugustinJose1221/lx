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
lx app.log -g myapp.lxt    # create a template interactively (wizard)
lx svc.log -F              # follow a growing file (tail -f)
lx app.log -P -f 'level==ERR && message ~ timeout'   # print mode

journalctl -b | lx         # read the log from a pipe ...
log show --last 1h | lx    # ... (keyboard comes from the terminal)
journalctl -f | lx -F      # live-follow a stream interactively
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
  `macos` (unified log via `log show` / `log stream`), `apache` (common
  log format) — auto-detected when none is given (`lx -l` lists them).
- **Field filtering**: `:filter level==ERR || level==WRN`,
  `timestamp >= "2026-06-01 19:00:00"`, `status >= 500`,
  `message ~ timeout`, with `&& || ! ()` logic. Unparsed lines (stack
  traces) are treated as continuations and follow their parent entry.
- **Entry inspector**: press `Enter` on a line to see every parsed
  field with its type, unit and value. Long values wrap over multiple
  lines (500 characters by default; `-d n` allows up to `n` wrapped
  lines per value).
- **Template export** (`-e name`): print any built-in template as
  editable `.lxt` text — `lx -e serilog > mine.lxt` is a quick starting
  point for a custom template.
- **Template wizard** (`-g out.lxt`): builds a `.lxt` interactively in
  the style of `git add -p` — the sample line is auto-split into
  timestamps, levels, numbers and words; every prompt has a default,
  colours are previewed live, and the result is validated against the
  log (match rate reported) before it is written.
- **Colour**: every field can carry a `color=#RRGGBB` hex code in its
  template (mapped to the nearest xterm-256 colour); all built-ins ship
  with a default palette. Severity (from the template's `level`/`type`
  field) overrides: error lines red, fatal lines bold red, warnings
  yellow on the level field, continuations dimmed.
- **Follow mode** (`-F` or `F`): auto-scrolls to the newest entry like
  `tail -f` — and *only* in follow mode; otherwise the view never moves
  on its own. Navigating while following pauses the auto-scroll (press
  `F` to resume). Backed by inotify on Linux, kqueue on macOS, polling
  elsewhere; handles file truncation/rotation.
- **Huge files** (`-H`): logs in the 1-15 GB range open in seconds -
  the file is memory-mapped and parsed on demand (8 bytes + 1 bit per
  line instead of hundreds), with every feature working as usual.
  Planned to become the default implementation.
- **Piped input**: `journalctl -b | lx`, `log show --last 1h | lx`,
  `dmesg | lx` — no flag needed (`-` also accepted). The viewer stays
  fully interactive (keyboard from the controlling terminal), live
  streams keep flowing in (`journalctl -f | lx -F`), and `(EOF)`
  shows when the producer closes the pipe. See platform notes below.
- **Print mode** (`-P`): print filtered lines to stdout for scripts;
  with piped input it consumes the stream to EOF first, like grep.

## Building & installing

```
make            # builds build/lx
make test       # builds and runs the unit tests
make install    # installs lx + man page (PREFIX=/usr/local)
```

One-command install (build + binary + man page, sudo only when
needed, man database refresh on Linux):

```
scripts/install.sh              # Linux / macOS (optional arg: PREFIX)
scripts\install.bat             # Windows: installs to %LOCALAPPDATA%\Programs\lx
```

Tagging a release (`git tag v1.2.3 && git push --tags`) triggers a
GitHub workflow that runs the test suite on Linux, macOS and Windows,
builds all three executables (macOS as a universal arm64+x86_64
binary), and publishes them as release assets.

Requires only a C99 compiler and make. Linux and macOS build out of the
box; other POSIX systems fall back to a generic backend
(`source/posix/fswatch_poll.c`).

On **Windows**, build from an MSYS2/MinGW shell (`pacman -S
mingw-w64-ucrt-x86_64-gcc make`, then `make`); the Makefile detects
MinGW/MSYS (or `OS=Windows_NT` for native `mingw32-make`) and selects
the Win32 console backend in `source/windows`, producing
`build/lx.exe`. Cygwin intentionally builds the POSIX backend instead
(its gcc provides termios/select and does not define `_WIN32`).

## Platform notes (piped input)

Explicitly, per platform:

- **Linux, macOS** — fully supported and tested. Interactive use with
  a pipe re-opens the keyboard from `/dev/tty`, so a controlling
  terminal is required: in environments without one (cron, detached
  daemons, CI) interactive mode cannot start — use `-P` there. New
  pipe data wakes the viewer instantly (`select` on the pipe).
- **Windows** — the keyboard is re-opened from `CONIN$`; console and
  pipe handles cannot be waited on together, so new pipe data is
  polled about every 500 ms (streamed lines may lag up to half a
  second). The Windows backend ships with the source but is **not
  regularly tested** — treat it as best effort.
- **All platforms** — `-P` reads the pipe to EOF before printing, so a
  never-ending producer (`journalctl -f`) keeps it running until
  interrupted. The template wizard (`-g`) also consumes the whole pipe
  before prompting and cannot re-open the stream afterwards; it prints
  the `lx -T` command to run instead.

## Try it

```
make
./build/lx documentation/examples/macos.log            # auto-detects
./build/lx documentation/examples/apache.log -f 'status >= 400'
./build/lx documentation/examples/myapp.log -T documentation/examples/myapp.lxt
./build/lx -e serilog > mine.lxt                       # export a built-in
```

[documentation/examples/](documentation/examples/) has a sample log for
every built-in template with suggested filters to try.

Inside the viewer: `?` shows all key bindings, `Enter` inspects an
entry, `:filter level==ERROR` filters, `:clear` unfilters, `q` quits.

## Repository layout

```
source/              core C sources (C99, standard library only)
source/posix/        termios/ANSI terminal backend, poll fallback
source/linux/        inotify file watching
source/darwin/       kqueue file watching
source/windows/      Win32 console backend
scripts/             one-command installers (install.sh, install.bat)
tests/               unit tests + sample logs (make test)
documentation/       man page, template format reference, examples
```

## Author

Written by **Augustin Jose**.

## License

Copyright (C) 2026 Augustin Jose. Licensed under the GNU General
Public License, version 2 or later — see [LICENSE](LICENSE). Every
source file carries an SPDX `GPL-2.0-or-later` header.

## Documentation

- [documentation/usage.md](documentation/usage.md) — usage guide:
  every flag with runnable examples, pipes, filters, follow mode
- `man ./documentation/lx.1` — full manual
- [documentation/template-format.md](documentation/template-format.md)
  — writing custom templates
- [documentation/examples/](documentation/examples/) — sample logs for
  every built-in template, plus a worked custom-template example
