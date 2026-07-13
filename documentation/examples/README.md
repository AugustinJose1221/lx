# Example logs

One sample log per built-in template, plus a worked custom-template
example. Each file auto-detects to its template — `lx <file>` is enough,
no `-t` needed.

| file             | template     | things to try                                          |
|------------------|--------------|---------------------------------------------------------|
| `plain.log`      | `plain`      | `lx plain.log` — unstructured text, whole line = message |
| `syslog.log`     | `syslog`     | `lx syslog.log -f 'proc ~ sshd'`                        |
| `syslog-iso.log` | `syslog-iso` | `lx syslog-iso.log -f 'timestamp >= "2026-07-13 09:18:00"'` |
| `dmesg.log`      | `dmesg`      | `lx dmesg.log -f 'time > 1 && time < 20'`               |
| `serilog.log`    | `serilog`    | `lx serilog.log -f 'level==ERR || level==FTL'` (both timestamp shapes, plus a stack-trace continuation) |
| `python.log`     | `python`     | `lx python.log -f 'logger ~ app.db'` (traceback lines follow their entry) |
| `macos.log`      | `macos`      | `lx macos.log -f 'type==Error || type==Fault'`          |
| `apache.log`     | `apache`     | `lx apache.log -f 'status >= 400'`                      |
| `myapp.log`      | *(custom)*   | `lx myapp.log -T myapp.lxt -f 'level==ERROR'`           |

Inside the viewer: `Enter` inspects the parsed fields of an entry,
`/` searches, `:filter` filters, `?` shows all keys.

`myapp.lxt` is a hand-written custom template — the format reference is
[../template-format.md](../template-format.md). Two other ways to get a
starting point for your own template:

```
lx -e serilog > mine.lxt      # export a built-in and edit it
lx yourapp.log -g mine.lxt    # let the interactive wizard build one
```
