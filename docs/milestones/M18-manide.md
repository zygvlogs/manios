# M18 — ManiDE: a Tiling Desktop

**Status:** Achieved (2026-09-26). Released as **0.18.0**. Asked for as
"this is ManiDE should be", with a mock-up of a tiling desktop: a status
bar of facts, panes with system information, a welcome, a process
viewer, listings and a terminal, on a dark theme. ManiDE takes that
look and layout; the details of the mock-up that belong to another
system (its kernel's name and version, a music player) are ManiOS's own
facts here, or left out.

![ManiDE](../desktop/screenshot.png)

The same request finished what 0.17.1 started: `cat` and `dd`, like
stdio programs, now end quietly when their reader has gone.

## What M18 delivers

| Piece | Files | Summary |
|---|---|---|
| ManiDE | `desktop/manide/` | The desktop, rewritten from M12's floating window manager into a tiling one: grid, tall and mono layouts, nine workspaces, Alt key bindings, a status bar (host, kernel, uptime, CPU, memory, network, date and time), a run prompt with Tab completion, a menu, mouse focus, a session file (`/boot/etc/manide`). `/bin/manide`; `/bin/desktop` runs it by its old name |
| Windows that are told their size | `desktop/manide/wsys.c`, `desktop/libwin/` | The event `r W H` offers a window its pane's size; the program takes it by writing `size W H` to `ctl`. `libwin` does so for its programs and returns `WIN_RESIZE`. `move` is gone. `ws N` moves a window to a workspace ([ADR-0007](../adr/0007-manide-tiling-desktop.md)) |
| A terminal of any size | `desktop/apps/term.c` | As many cells as the pane holds (up to 160×64); lines that no longer fit go to the scroll-back; SGR colours, cursor position and movement, erasing, cursor hiding, and answers to the size and cursor queries; `term COMMAND` |
| `welcome` | `desktop/apps/welcome.c` | *Welcome to ManiDE* — *Minimalist · Modular · Manual* — and the keys |
| `fetch`, `top` | `userland/bin/` | The system beside the ManiOS logo; the processes, busiest first, with CPU and memory bars, full screen or `-b` for plain text |
| Alt | `zkt/drivers/ps2kbd.c`, `input.c`, `zkt_abi.h` | Alt (left or right) held: `/dev/kbd` has `ZKT_KEY_ALT` (0xA0) before the key; both bytes or neither |
| `/dev/sysstat`, `/dev/ps` | `zkt/kernel/sysstat.c`, `process.c`, `zkt/scheduler/sched.c`, `zkt/arch/i386/cpuinfo.c` | Version, uptime, idle time, memory, processes, the CPU; one line per process: pid, parent, state, CPU time, memory, name. Each timer tick is charged to the process it interrupted, or to idle |
| ANSI on the text console | `zkt/drivers/vga_text.c` | SGR colours as VGA attributes, cursor position and movement, erasing; other sequences are swallowed instead of shown |
| A console that can be polled | `zkt/kernel/kconsole.c` | `/dev/cons` is ready once a whole line has been typed, so `top` can wait for a key and a refresh at once |
| Bigger transfers | `zkt/kernel/syscall.c` | Reads and writes over 512 bytes go through a 16 KiB buffer: one ZRP message per 16 KiB to a file server (a window's image) instead of one per 512 bytes |
| Quiet broken pipes | `userland/bin/cat.c`, `dd.c` | A write that finds no reader ends the program with status 141, as stdio does (0.17.1), not an error message |

## Design decisions

**Tiling, and who decides a window's size.** Tiling means ManiDE, not
the program, picks each window's size — a reversal of M12, recorded in
[ADR-0007](../adr/0007-manide-tiling-desktop.md). ManiDE doesn't resize
a window's image by itself: writes already on their way were made for
the old width and would land at the wrong offsets. It offers the size
(`r W H`) and the program takes it (`size W H`), which puts the change in
order among the program's own writes on its one connection. Until then,
and for a program that never takes it, the pane shows the image it has
at its top-left, with the pane's colour around it.

**The layouts.** *grid* is the default because it is the mock-up's:
columns side by side (⌈√n⌉ of them), the windows shared out among them,
the later columns taking one more when they don't divide evenly — so
three windows are one tall pane and two stacked, four a 2×2 grid, five
1+2+2. *tall* (a master pane and a stack) and *mono* (one window) are
the other two classic tiling layouts. Windows keep one order across
workspaces; each workspace remembers its layout, its master's share and
its focus.

**Alt, as a prefix byte.** `/dev/kbd` is a byte stream, and Alt+key is
two bytes: `ZKT_KEY_ALT`, then the key as it would be without Alt
(Alt+Shift+1 is `ZKT_KEY_ALT`, `!`). Programs that don't know Alt see
the key after a byte they ignore; the console, which takes only ASCII,
sees just the key. The driver queues both bytes or neither, so a full
queue can't leave a reader with half of Alt+q. Alt with a key ManiDE
doesn't keep reaches the window as `k CODE a`; `term` ignores those.

**The status bar's facts are files.** `/dev/sysstat` and `/dev/ps` are
text, one fact a line, like `/dev/net` and `/dev/sysname`; ManiDE's bar,
`fetch` and `top` read them, and so does `cat`. CPU time is sampled: the
timer (100 Hz) charges each tick to the process it interrupted, or to
the idle thread, as Unix kernels did. The CPU's busy share is one minus
idle time over elapsed time between two reads. A process's memory is
what it has mapped — image, heap and stack — which is what it costs in
frames, since ManiOS maps nothing lazily. A reader that takes a device's
text in pieces gets one snapshot: it is made at offset 0 and kept for
the reads that follow, with interrupts off so two readers can't mix.

A process that exits leaves its thread running a little longer
(`thread_exit` may wait for a namespace's last clunk), and its parent
may free the process meanwhile. The thread now leaves its process
(`thread_leave_process`) before that, so a late tick can't be charged
to freed memory.

**The terminal.** `term` is now as large as its pane: when ManiDE
shrinks it, the lines above the cursor that no longer fit go into the
scroll-back, and when it grows, the text stays where it was. It
understands the escape sequences a full-screen program needs and no
more: SGR (16 colours, bold as bright), CUP/CUU/CUD/CUF/CUB/CHA, ED, EL,
DECTCEM (cursor on and off), and two queries it answers on the
program's standard input — the cursor's position (`ESC [ 6 n`) and the
text area's size (`ESC [ 18 t`, answered `ESC [ 8 ; ROWS ; COLS t`, as
xterm does). ManiOS has no terminal ioctls, so `top` asks the size that
way, every frame, since the pane may have changed; with no answer (a
serial line, the console) it assumes 80×24. `term COMMAND` types
COMMAND into the shell once the shell has prompted, so a pane opened by
the session shows `manios% fetch` above fetch's output, as a user would
have typed it — and stays a shell afterwards.

**The text console speaks some ANSI.** Once `fetch` and `top` colour
their output, the VGA console must not show the escapes as text. It now
maps SGR colours to VGA attributes (bold to the bright half of the
palette; bright backgrounds come out plain, because attribute bit 7
blinks) and does cursor positioning and erasing, so `top` runs full
screen there too. The serial line passes the escapes through to the
terminal at the other end.

**`top` waits for a key and the clock at once.** It polls its standard
input with the refresh interval as the timeout. A pipe (in `term`) could
be polled already; `/dev/cons` could not — it had no poll operation, so
poll said "ready" and `top` would have blocked in `read`. The console
is now ready when a whole line waits: the rest of one already read, or
the end of one (or ^D) in the input not yet edited, skipping a `\n` that
follows a `\r` as the line editor does.

**Performance: 512-byte messages.** The first ManiDE session took more
than 20 seconds to draw its four panes, with `cpu 100%` in the bar;
`/dev/ps` showed ManiDE itself had used 21.9 s of CPU in the first 34 s.
Logging the image writes it received showed why: 512 bytes each. The
kernel copied every `write` through a 512-byte buffer and passed each
piece on as its own ZRP message; ManiDE answered each and composed
after each (about 10 ms), so a pane of 420 KB cost 800 compositions.
Now transfers over 512 bytes go through a 16 KiB kernel buffer (one ZRP
message over a channel), ManiDE waits up to 5 ms for the next piece
before composing, and `gfx_present` writes whole rows to `/dev/fb` in
one call. Measured the same way: all four panes drawn within 6 s of
starting, ManiDE at 0.5 s of CPU by 17 s of uptime. `clock` also sends
only the rows it changed each second (it would otherwise resend its
whole pane, which matters when it runs on a CPU server, M13).

**The session file.** ManiDE starts the programs it lists one at a
time, each once the one before has its window (or has exited, or had
three seconds), so the panes come in the file's order — the order
decides where they tile. `manide -n` starts none; `-s FILE` another
file.

## Verification

- **`tests/desktop_test.py`**, rewritten for ManiDE: 20 steps, checked
  on the screen (glyphs matched against the font; pane positions worked
  out as `tile.c` does) and on the serial log — the bar's text and
  colours, the session's four panes and their contents (`fetch`'s OS
  line, `top`'s header and ManiDE among its processes, the welcome's
  colours, the shell's prompt), `/dev/wsys` read from the terminal
  (`4 403 326 392 269 1 Terminal`: the window has its pane's size),
  the tall and mono layouts and Alt+l, focus by Alt+j/k and by clicks,
  workspaces (Alt+2, Alt+Enter, Alt+Shift+1, a click on the bar,
  Alt+q), the run prompt (its completion hint, Tab, Enter, Escape), the
  menu, a pane's close mark, `wintest`, the scroll-back, the terminal in
  a smaller and a larger pane, a nested `manide` refused, Alt+Shift+Q,
  `desktop -n 640 480` and the menu's Exit, and a machine without
  Bochs VBE.
- **`wintest`**, 45 checks, run inside ManiDE: `size` (and its errors),
  `move` refused, `ws` (and its errors), the first events `f 1` and
  `r W H`, taking the offered size (the image grows: its last pixel
  shows on the screen), a second window shrinking the first's pane,
  `top`, a window sent to workspace 2 losing the focus and brought back.
- **`utest`**, 134 checks (122 before): `/dev/sysstat` (memory, idle time
  within uptime, processes, the version first, a CPU line), `/dev/ps`
  (the reader running; a CPU-bound child's CPU time and state; the child
  `exited` with no memory until waited for, then gone), and the idle
  time with the CPU busy and with everyone asleep; read in 3- and 5-byte
  pieces, so the snapshot is checked too.
- **`tests/console_test.py`**: the boot's device list; `cat /dev/sysstat`,
  `cat /dev/ps`, `fetch -p`, `fetch` (its escapes), `top -b`,
  `top -b -n 2`, `top -n 1` (the size query, full screen), `top`'s
  usage; `yes | cat | head -n 2` and `yes | dd | head -n 2` with no
  "broken pipe"; and `fetch`'s colours on the text console, read from
  VGA text memory: the label's attribute bright cyan, the value's light
  grey, the logo's bright yellow, and no escape characters on the
  screen.
- **`tests/cluster_test.py`**: a clock started on the CPU server with
  `cpu` opens in ManiDE's right-hand pane, takes the size it is offered
  over the network, draws its time at the scale that pane gives it, and
  ticks; `q` reaches it, and Alt+Shift+Q ends ManiDE.
- `make test`: 370 checks (355 at 0.17), none failing.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | `cat` reports a broken pipe again | `console_test`: `yes \| cat \| head -n 2` |
  | `dd` reports a broken pipe again | `console_test`: `yes \| dd \| head -n 2` |
  | No CPU time charged to processes | `utest` (at boot, too): a busy child's CPU time |
  | No idle time counted | `utest`: idle time while all sleep |
  | An exited process shown as `ready` | `utest`: an exited child not yet waited for |
  | `/dev/ps` newest first | `console_test`: `head -n 1 /dev/ps` is the shell |
  | The keyboard driver ignores Alt | `desktop_test`: Alt+Space changes no layout |
  | The text console shows escapes as text | `console_test`: `fetch`'s colours on the screen |
  | The console is always "ready" to poll | `console_test`: `top -n 1` blocks reading a line |
  | ManiDE never offers a size (`r`) | `desktop_test`: the window doesn't have its pane's size |
  | `libwin` doesn't take the size offered | `desktop_test`: the panes stay empty |
  | ManiDE ignores `size` | `desktop_test`: the panes stay empty |
  | `term` ignores escape sequences | `desktop_test`: `top`'s pane |
  | `term` doesn't answer the size query | `desktop_test`: `top` fits its pane (added when this control first went uncaught: `top` then assumes 80 columns and its header wraps, but the first row alone still looked right) |
  | `top` doesn't cut lines to the width | `desktop_test`: `top` fits its pane |

  Not guarded by a test: the 16 KiB transfer buffer and ManiDE's
  waiting for the rest of an image make ManiDE fast, not correct; the
  measurements above are their check.

## Known limits

- The terminal has no raw keyboard mode: `top` quits on q and Enter.
- Terminal lines are cut, not reflowed, when a pane narrows.
- The bar shows the first network interface only; the time is UTC.
- Programs that draw their whole window each frame are slow over a
  network (every pixel travels); a drawing protocol is the fix
  ([DESIGN §8](../desktop/DESIGN.md#8-later)).
- Every window's image is held twice, and a window that was once large
  leaves its program's heap large: ManiDE at 1024×768 with its session
  uses about three quarters of 32 MiB.
