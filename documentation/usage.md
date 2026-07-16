# lx usage guide

Every flag, with examples. All file paths below refer to the sample
logs shipped in [`examples/`](examples/) so each command can be run
as-is from the repository root (after `make`, use `./build/lx`, or just
`lx` once installed).

```
lx [options] [logfile]
<producer> | lx [options] [-]
```

## Opening log files

```sh
lx documentation/examples/serilog.log        # template auto-detected
lx documentation/examples/dmesg.log -t dmesg # force a built-in template
lx app.log -T mytemplate.lxt                 # use a custom template file
lx -l                                        # list built-in templates
```

Without `-t`/`-T` the template is auto-detected by matching the built-in
templates against the file's first lines. `-t` names a built-in
(`plain`, `syslog`, `syslog-iso`, `dmesg`, `serilog`, `python`,
`macos`, `apache`); `-T` loads a `.lxt` file (see
[template-format.md](template-format.md)).

## Reading from a pipe

No flag needed — when stdin is not a terminal, lx reads the log from
the pipe and the keyboard from your terminal. `-` makes it explicit.

```sh
journalctl -b | lx                     # systemd journal (Linux)
journalctl -u nginx --since today | lx
log show --last 1h | lx                # macOS unified log
log show --predicate 'processImagePath contains "Safari"' --last 30m | lx
dmesg | lx                             # kernel ring buffer
docker logs mycontainer 2>&1 | lx      # container logs
kubectl logs -f deploy/api | lx -F     # kubernetes, live
ssh web01 'journalctl -u nginx -n 5000' | lx    # remote logs
zcat /var/log/app.log.3.gz | lx        # rotated/compressed logs
lx < saved.log                         # plain redirection works too
```

Live producers keep streaming into the viewer:

```sh
journalctl -f | lx -F                  # follow the stream (tail -f style)
log stream --level info | lx -F       # macOS, live
dmesg -w | lx -F
tail -f /var/log/app.log | lx          # works, but 'lx -F app.log' is better
```

`(EOF)` appears in the status bar when the producer closes the pipe.
Platform notes (POSIX `/dev/tty`, Windows polling) are in the man page
under *PIPED INPUT* and in the README.

## Follow mode (`-F`, key `F`)

Auto-scrolls to the newest entry, exactly like `tail -f` — and only in
follow mode; otherwise the view never moves on its own.

```sh
lx /var/log/app.log -F                 # start following a growing file
journalctl -f | lx -F                  # follow a live stream
```

Inside the viewer, `F` toggles following at any time. Navigating away
from the newest entry (`k`, `PageUp`, a search jump, ...) pauses
follow so you can read scrollback while data keeps arriving; press `F`
again to resume. `G` jumps to the newest entry without breaking follow.

## Filtering (`-f`, command `:filter`)

Filters use the named fields of the active template. Quote the
expression with single quotes so the shell leaves it alone.

```sh
# equality on an enum field
lx documentation/examples/serilog.log -f 'level==ERR'

# several levels
lx documentation/examples/serilog.log -f 'level==ERR || level==FTL'

# substring (~ is contains, case-insensitive); != and !~ negate
lx documentation/examples/syslog.log -f 'proc ~ sshd'
lx documentation/examples/python.log -f 'logger ~ app.db && message !~ pool'

# numeric comparisons on int/float fields
lx documentation/examples/apache.log -f 'status >= 400'
lx documentation/examples/apache.log -f 'status >= 500 || (status == 403 && request ~ admin)'
lx documentation/examples/dmesg.log -f 'time > 1 && time < 20'

# timestamp ranges (chronological, wall-clock as printed in the file)
lx documentation/examples/serilog.log -f 'timestamp >= "2026-07-13 09:18:00"'
lx documentation/examples/python.log -f 'timestamp >= "2026-07-13 09:15:00" and timestamp < "2026-07-13 09:20:00"'

# timestamp equality names a granule: a whole day, minute, or second
lx documentation/examples/serilog.log -f 'timestamp == "2026-07-13"'        # that day
lx documentation/examples/serilog.log -f 'timestamp == "2026-07-13 09:15"'  # that minute
lx documentation/examples/apache.log  -f 'timestamp == "13/Jul/2026:09:16:41 +0200"'

# a time without a date compares by time of day, on any date
lx documentation/examples/serilog.log -f 'timestamp >= "09:18:00"'
lx documentation/examples/syslog.log  -f 'timestamp < "09:16"'

# pseudo-fields: raw = the whole unparsed line, line = line number
lx documentation/examples/macos.log -f 'raw ~ bluetooth'
lx big.log -f 'line > 100000'

# boolean words work too: and, or, not
lx documentation/examples/macos.log -f 'not type==Default'
```

Lines that do not match the template (stack traces, wrapped output)
are continuations: they follow their parent entry through every
filter, so a filtered Python error keeps its traceback.

Interactively, `:filter level==ERR` (or `:f ...`) applies a filter,
`:clear` removes it.

## Print mode (`-P`)

Non-interactive: write the (filtered) lines to stdout and exit —
lx as a structured grep.

```sh
lx app.log -P -f 'level==ERR'                        # extract errors
lx documentation/examples/apache.log -P -f 'status >= 500' | wc -l
journalctl -b | lx -P -f 'message ~ "oom"' > oom.txt
log show --last 24h | lx -P -f 'type==Fault' | less
lx app.log -P > full-copy.log                        # no filter: all lines
```

With piped input, `-P` consumes the stream to end-of-file first (like
grep), so don't combine it with never-ending producers such as
`journalctl -f`.

Exit codes for scripting: `0` success, `1` I/O error, `2` usage error
(unknown option/template, invalid filter).

## Huge files (`-H`)

High-performance mode for logs in the gigabyte range (1-15 GB):

```sh
lx /var/log/huge-app.log -H
lx huge.log -H -f 'level==ERR'         # filter still works (full scan)
lx huge.log -H -P -f 'status >= 500'   # structured grep over gigabytes
lx huge.log -H -F                      # follow a huge growing file
```

Instead of loading and parsing the whole file up front, `-H`
memory-maps it and parses lines on demand: only 8 bytes + 1 bit of
memory are kept per line, opening a 6 GiB log takes seconds (one
sequential newline scan), and jumping to the last of tens of millions
of entries is instant. Applying a filter scans the file once, like
grep. Everything works as in standard mode: navigation, search,
filters, the inspector, colours, `:template`, follow mode.

Two visible differences: the status bar shows `HP` and the total entry
count instead of the parsed-lines counter (counting parses would
defeat the lazy design), and `-H` is ignored for piped input (a pipe
has no file to map). High-performance mode is planned to become the
default implementation.

## The entry inspector (`Enter`, `-d`)

`Enter` on any entry shows each parsed field with its type, unit and
value. Long values wrap and are capped at 500 characters by default;
`-d <n>` allows up to `n` wrapped lines per value instead.

```sh
lx app.log -d 20        # inspector may use 20 lines per field value
lx app.log -d 1         # terse inspector
```

## Templates: list, export, custom, wizard

```sh
lx -l                                  # list built-ins with descriptions
lx -e serilog                          # print a built-in as .lxt text
lx -e macos > macos.lxt                # ... export to a file and edit it
lx app.log -T macos.lxt                # use the edited template

lx app.log -g myapp.lxt                # interactive template wizard
log show --last 5m | lx -g macos2.lxt  # wizard on piped data
```

The wizard (`-g`) splits a sample line, suggests field names, types,
timestamp formats and colours (with live swatches), validates the
result against the log, reports the match rate, writes the `.lxt`, and
offers to open the log with it. With piped input it consumes the pipe
before prompting and prints the matching `lx -T` command instead.

Inside the viewer, `:template <name-or-file.lxt>` (or `:t`) re-parses
the open log with another template on the fly:

```
:template dmesg
:template ./mine.lxt
```

## Everything else

```sh
lx -h                # usage summary
lx -V                # version
man lx               # full manual (installed by scripts/install.sh)
```

Interactive keys (also on `?` inside the viewer):

| keys                   | action                                    |
|------------------------|-------------------------------------------|
| `j` / `k`, arrows      | move down / up                             |
| `d` / `u`              | half page down / up                        |
| `space` / `b`, PgDn/Up | full page down / up                        |
| `g` / `G`, Home/End    | first / last entry                         |
| `h` / `l`, `0`         | scroll left / right, reset                 |
| `Enter`                | inspect the parsed fields of the entry     |
| `/`, `n` / `N`         | search (case-insensitive), next / previous |
| `F`                    | toggle follow mode                         |
| `:filter EXPR`         | filter; `:clear` removes it                |
| `:N`                   | go to line N                               |
| `:template NAME`       | switch template                            |
| `?`                    | help                                       |
| `q`                    | quit                                       |
