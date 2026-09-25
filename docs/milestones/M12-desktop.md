# M12 — The ManiOS Desktop Environment MVP

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §6](../FOUNDING-PROPOSAL.md#6-development-roadmap)
milestone M12: compositor, shell, launcher. The design is in
[docs/desktop/DESIGN.md](../desktop/DESIGN.md), the founding-proposal-style
document §1.4 of the proposal asked for, and the central decision is
[ADR-0004](../adr/0004-window-system-as-a-file-server.md): **the window
system is a file server**.

```
manios% desktop                 # 800x600; or: desktop 640 480
  F1            the ManiOS menu: Terminal, Clock, About ManiOS, Exit desktop
  F2            bring the bottom window to the top
  mouse         click to focus and raise; drag title bars; close boxes
```

Inside a terminal window the window system is just files:

```
manios% ls /dev/wsys
1/
new
manios% cat /dev/wsys/1/ctl
1 60 50 488 272 1 Terminal
```

## What M12 delivers

Kernel groundwork (committed first, as "M12 part 1"), then the desktop.

| Piece | Files | Summary |
|---|---|---|
| Pipes | `zkt/fs/pipe.c`, `SYS_PIPE` | Two connected ends, both directions; a write is one message; an empty write reads as end of file; `EPIPE` when the other end is gone |
| Descriptors | `SYS_DUP`, `SYS_DUP2` | Redirection, and giving a child a pipe as its standard input or output |
| Waiting | `SYS_POLL`, `zkt/kernel/poll.c` | Wait for any of several descriptors, with a timeout |
| Children | `SYS_REAP` | An exited child's status, without waiting |
| Userspace file servers | `SYS_MOUNTFD`, `zkt/net/zrp_client.c` | Mount a ZRP server running as a program at the other end of a pipe; any number of requests in flight ([docs/zrp.md](../zrp.md#local-channels-m12)) |
| Server library | `libc/zrpsrv.c` | A tree of files, fids, walks, directory reads; programs supply read/write; reads may be deferred |
| Devices | `zkt/drivers/input.c`, `ps2mouse.c`, `rtc.c`, `null.c` | `/dev/kbd` (raw keys, taking the keyboard from the console while open), `/dev/mouse` (PS/2 mouse, text records), `/dev/time` (CMOS clock), `/dev/null` |
| Shell | `userland/bin/sh.c` | Pipelines (`\|`) and redirection (`<`, `>`) |
| Compositor | `desktop/wm/` → `/bin/desktop` | Mode set, `/dev/wsys` server, composition, window management, panel, launcher menu, pointer |
| Window library | `desktop/libwin/` | `win_open`, `win_flush`, `win_next`, `win_close` |
| Applications | `desktop/apps/` → `/bin/term`, `/bin/clock`, `/bin/about` | A terminal running `sh`; a clock; what the system is |
| Tests | `userland/test/ztest.c`, `wintest.c`, `tests/desktop_test.py` | Server library at boot; window system protocol; the desktop through the screen |

## Design decisions

- **The window system is a file server** (ADR-0004), served by the
  desktop over a pipe and mounted *after* `/dev`, a union, so
  `/dev/wsys` appears beside the devices. Each window is a directory:
  `ctl`, `image` (pixels at byte offsets), and `event` (text lines, read
  blocking). A window is made by writing its size to the clone file
  `new` and lasts as long as that file is open — so a program that
  crashes loses its window, because the kernel clunks every fid a dying
  process held. Details: DESIGN.md §4.
- **The desktop's namespace is its own.** It calls `nsfork()` first, so
  `/dev/wsys` exists for the desktop and the programs it starts, and
  for no one else; after it exits, the console's shell doesn't see it
  (tested). A desktop started inside the desktop finds `/dev/wsys`
  already there and refuses.
- **A helper mounts the window system.** `mountfd` waits for the server
  to answer `Tversion` and `Tattach`, so the server can't mount itself:
  the desktop starts `desktop -m` with the pipe's client end as its
  standard input, and answers its requests while it mounts.
- **Requests in flight on a channel are unlimited.** An application
  blocked reading its events must not stop its own image writes, or
  anyone else's. The kernel's ZRP client lists every request waiting
  for a reply; whichever waiting thread is receiving reads the next
  reply and hands it to the request with that tag (the others sleep).
  A tag is chosen and the request listed in one step, so two threads
  can't pick the same tag.
- **Reads can be deferred.** `zsrv` keeps a read request the program
  didn't answer yet; the program answers it later with `zsrv_respond`.
  A window's `event` reads work this way.
- **Processes as event sources.** `poll` knows when a *local* file
  would block, but not a remote one (it treats those as always ready).
  So `libwin` starts `cat /dev/wsys/N/event` with its output on a pipe,
  and an application polls that pipe along with anything else — the
  terminal polls its events and its shell's output. This is how Plan
  9's libevent works.
- **Pipes keep message boundaries** (as in Plan 9): the ZRP channel
  needs whole messages, and byte streams lose nothing because a short
  read leaves the rest of the message for the next one.
- **The keyboard belongs to whoever opened `/dev/kbd`.** While it is
  open the PS/2 keyboard feeds it; the serial line always feeds the
  console, so a serial terminal is never locked out. `/dev/kbd` gives
  arrows, Home/End/PgUp/PgDn/Insert/Delete and F1–F10 as single bytes
  (`ZKT_KEY_*` in `zkt_abi.h`).
- **The mouse is text.** `/dev/mouse` gives `m DX DY BUTTONS` lines —
  relative motion, Y growing downwards — like the rest of the system's
  devices. The desktop keeps the pointer position.
- **The terminal edits lines itself.** Its shell's standard input is a
  pipe, not `/dev/cons`, so the kernel's line discipline isn't in the
  way: `term` echoes, handles Backspace and ^U, and sends whole lines;
  ^D on an empty line sends an empty message, which `sh` reads as end
  of file.
- **Composition is damage-driven.** Everything that changes adds its
  rectangle to one damaged rectangle, and each pass of the main loop
  redraws only that, bottom layer first, then copies it to `/dev/fb`. A
  burst of requests (an image arriving in 16 KB pieces) is handled
  before drawing.
- **Integer-only, as everywhere**: the clock's date conversion is the
  well-known days-to-civil algorithm, in integers.

## Verification

- **At every boot:** `ztest` (30 checks) — a program serves files to
  its parent through `mountfd`: listings in order, reads at offsets,
  `fstat`, writes, the server's own error numbers, `ENOENT`/`ENOTDIR`/
  `EROFS`, a 40000-byte file across several messages, a read deferred
  while other requests complete (a second process waits on it), removal
  of an open file (`EIO`), and — after unmounting — that the kernel
  clunked every fid (the server exits 0 only then). `run_checked` also
  requires no frames or heap leaked. The M12 marker reports it; the
  8 MiB machine still boots.
- `utest` gained pipes, `dup`/`dup2`, `poll` (timeouts, readiness,
  `EBADF`, `EINVAL`), `reap`, and `/dev/null`, `/dev/time`; 110 checks
  (the count grows with the number of files in `/bin`, one check per
  directory read).
- **`wintest`** (35 checks), run inside the desktop by the desktop test:
  clone-file rules and bad sizes; `ctl` state, `title`, `move` (limited
  so the title bar stays reachable), bad commands; image writes read
  back from the server *and from the screen* (`/dev/fb`); partial writes
  and bounds; focus events and their order; `EINVAL` for a buffer too
  small for an event; a reader of a closed window's events getting end
  of file; a killed program's window disappearing; the 16-window limit
  across two processes.
- **`tests/desktop_test.py`** (in `make test`, 17 checks) drives the
  desktop with QEMU's `sendkey`, `mouse_move` and `mouse_button`, and
  reads text back off `screendump` images by matching every character
  cell against `font.txt`: the panel, its clock, the pointer and the
  background; the menu and its items; a terminal where `echo`, a
  pipeline and `cat /dev/wsys/1/ctl` print the right rows; `wintest`;
  dragging a title bar (to an exact position); the clock showing and
  advancing the time and date; F2 moving the focus, and keys reaching
  only the focused window; the About window's close box; a nested
  desktop refused; the terminal's close box ending it and its shell;
  exit restoring text mode and giving the keyboard back to the console;
  no `/dev/wsys` left in the console's namespace; a second run at
  640x480; and, on a machine with a Cirrus card instead of Bochs VBE,
  a clear message and a working console.
- `console_test` gained the shell's pipelines and redirections (typed
  on both the serial line and the keyboard) and the new device list.
  `net_test` passes unchanged after the ZRP client's rework.
- `make test`: 164 checks (138 at M11), none failing.
- **Negative controls** (temporary sabotage, reverted afterward), each
  caught:

  | Sabotage | Caught by |
  |---|---|
  | Closing `new` doesn't close the window | `wintest` (desktop test) |
  | A closed window's event readers never get end of file | `wintest` (desktop test) |
  | `win_next` with a zero timeout never reads | The terminal never shows typed text |
  | Keys always go to the console | The menu never opens |
  | A pipe write wakes no `poll` | The terminal never shows the prompt |
  | The mouse's Y axis inverted | The drag lands in the wrong place |
  | The kernel never clunks fids of released vnodes | The boot self-test (M10's, and `ztest`) |
  | Each waiting thread takes only its own reply | Boot hangs in `ztest` |

## Numbers

- `desktop` is 36 KB stripped; `term` 22 KB, `clock` and `about` 20 KB.
  The boot archive is 460 KB; the kernel image 1.0 MB.
- The desktop's back buffer at 800x600 is 1.9 MB; a terminal window's
  image 0.5 MB in the desktop and again in `term`. The test machines
  have 32 MB.

## Known limits

- Pixels travel through the kernel: a full terminal repaint is about
  500 KB of writes. Fine in QEMU; a real 486 will want the drawing
  protocol DESIGN.md §8 plans.
- No resizing, no minimising, no clipboard, no drag and drop; windows
  can't be larger than the screen, and there are at most 16.
- The terminal has no scrollback and no escape sequences, and there is
  no way to interrupt a program (no signals yet).
- There is no Alt key, so desktop shortcuts are F1 and F2.
- The clock is UTC: there are no time zones yet.
- The desktop needs Bochs VBE (QEMU's default display, Bochs,
  VirtualBox); on VGA alone it says so and exits.
