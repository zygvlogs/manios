# M16 — The First BSD Imports: OpenBSD's Text Tools

**Status:** Achieved (2026-09-26). Released as **0.16.0**. Asked for
once Manikineko.nl set its own operating system aside to concentrate on
ManiOS: "port some OpenBSD tools". These are ManiOS's first
BSD-*derived* files (FOUNDING-PROPOSAL §3.2): everything before was
written for ManiOS.

```
manios% ls /bin | head -n 3
about
basename
bind
manios% yes | head -n 2
y
y
```

## What M16 delivers

| Piece | Files | Summary |
|---|---|---|
| Eleven programs | `third_party/openbsd/usr.bin/` | `basename`, `comm`, `cut`, `dirname`, `expand`, `fold`, `head`, `paste`, `rev`, `uniq`, `yes`, in `/bin` |
| Four libc functions | `third_party/openbsd/lib/libc/` | `strtonum`, `reallocarray`, `basename`, `dirname`, built into libc |
| `<sys/queue.h>` | `third_party/openbsd/sys/sys/` | OpenBSD's list macros, for `paste` |
| POSIX/BSD layer | `libc/getopt.c`, `err.c`, `getline.c`, `progname.c`, `locale.c`, `compat.c`, `libc/include/` | `getopt`, `err`/`warn`, `getline`, `getprogname`, `strtoll`/`strtoull`, `strsep`, `strcasecmp`, `freopen`, `isblank`; `<unistd.h>`, `<err.h>`, `<libgen.h>`, `<strings.h>`, `<sys/types.h>`, ... |
| Broken pipes | `libc/stdio.c` | A program whose stdio output reaches a pipe with no reader ends, as SIGPIPE ends it on Unix |
| Notices | `tools/mknotices.py`, `/boot/etc/notices`, `NOTICES.txt` | Every imported file's copyright notice and license, in the boot archive and with each release |
| Records | `third_party/THIRD_PARTY_NOTICES.md`, `third_party/openbsd/README.md` | The ledger's first entries (§3.3); how the imports build and how to add one |
| Tests | `tests/openbsd_test.py`, `userland/test/ctest.c` | Every program's output and errors; the libc layer at every boot |
| PCnet fix | `zkt/drivers/pcnet.c` | The rare lost frame of M15, found and fixed (below) |

## Design decisions

- **The files are OpenBSD's, unmodified.** Each one is fetched from
  OpenBSD's `src` at one recorded commit
  (`3d80b15b1ec664bb0fc72f48e55e5488a147aa50`), and the only change is
  ManiOS's note under its license header (§3.3, step 4). When a program
  didn't build, libc got what it lacked; nothing was patched in the
  imported code. So an update is a fresh fetch, and a difference from
  OpenBSD's behaviour is a ManiOS libc bug, not a porting choice.
- **Which tools.** Text filters that need nothing ManiOS doesn't have:
  they read files and standard input and write standard output. Not
  imported yet: `nl`, `grep` and `sed` need regular expressions (a much
  larger import), `tee` needs signals and file creation, which ManiOS
  doesn't have, and `sort` writes temporary files.
- **What OpenBSD programs expect of libc**: POSIX `getopt` (grouped
  flags, joined or separate arguments, `--`, a leading `:`), BSD's
  `err()` family (the program's name, the message, and `strerror` for
  `warn`), `getline`, 64-bit `strtoll` (on a 386, without libgcc's
  64-bit division: the parser multiplies 32 by 32 bits), and
  `__progname`, which `crt0` now sets from `argv[0]` before `main`.
  `pledge()` and `unveil()` exist and do nothing: ManiOS has neither,
  and the programs only use them to give up rights.
- **The "C" locale only.** `setlocale` accepts `"C"`, `"POSIX"` and
  `""`; `MB_CUR_MAX` is 1, so `cut -c` counts bytes (as OpenBSD's does
  in the "C" locale) and the wide-character paths of `fold` and `uniq`
  see one byte per character.
- **No signals, so stdio stands in for SIGPIPE.** OpenBSD's `yes` loops
  forever; on Unix, SIGPIPE ends it when `head` has read enough.
  ManiOS's pipes already made such a write fail with `EPIPE`; now,
  when a stdio stream's write fails that way, the program ends at once
  with status 141 (what a Unix shell shows for SIGPIPE; `abort` already
  uses 134, SIGABRT's). Plan 9 does the same with its "write on closed
  pipe" note. `write()` itself still returns `EPIPE`, so a program that
  must outlive its reader -- the desktop's terminal, which feeds its
  shell with `write()` -- is unaffected.
- **Notices travel with the binaries.** The BSD and ISC licenses ask
  for the copyright notice to go with copies in binary form too, and
  the ISO is one. `tools/mknotices.py` copies everything above each
  file's ManiOS note, verbatim, into `/boot/etc/notices`, which `make
  release` also publishes as `NOTICES.txt`; a file without the note
  stops the build, so no import can skip §3.3's step 4.
- **Built with OpenBSD's warnings, not ManiOS's.** ManiOS builds with
  `-Wall -Wextra`; OpenBSD's code isn't written for `-Wextra`'s style
  warnings, so the imports are built with `-Wno-sign-compare
  -Wno-unused-parameter -Wno-maybe-uninitialized` (the one warning of
  the last kind, in `cut`, was checked: `ch` is always set before it is
  read). `-DDEF_WEAK(x)=` removes OpenBSD's libc symbol macro.

## Also fixed: the PCnet's lost frame

The rare failure the [M15 notes](M15-network-cards-dhcp.md#verification)
couldn't explain -- one of 300 paced pings over the PCnet unanswered --
came back in this milestone's first full `make test`. A stress script
(20 rounds of the 300 paced pings per boot) caught it twice in 24,000
pings, and the counters said what the test didn't: one frame had never
reached the network stack, and the driver had counted it as dropped.
Printing each dropped descriptor showed it: handed back to the driver
(OWN clear) with only STP set -- no ENP, length 0 -- and a moment later
the length was there (58 bytes, the lost ping's frame). QEMU's PCnet
writes a receive descriptor twice, first handing it back marked as a
frame's start, then adding the end and the length; the driver, reading
in between, took it for a malformed frame and dropped it.

- **The fix** (`pcnet_poll`): a descriptor without ENP (or without a
  length) is still being written while the card owns the next one;
  the driver stops there, and the interrupt that ends the frame brings
  it back. If the next descriptor is the driver's too, the card has
  moved on: that is a frame too big for one buffer, spread over
  several descriptors, and it is dropped as before.
- **After:** 36,000 paced pings without a loss; in 12,000 more, with
  the new path reporting itself, it waited at two half-written
  descriptors -- two frames the old driver would have dropped.
- **A new check:** `net_test.py`'s malformed packets now include frames
  of 2,000 and 3,000 bytes, which the PCnet spreads over two and three
  descriptors; the guest must go on answering, over all three cards.
  Without the next-descriptor condition the PCnet's ring stalls there,
  and the test catches it.

## Verification

- **`tests/openbsd_test.py`**, 42 checks: the 35 shell cases (every
  program's whole output -- files, standard input, `-`, a pipe from
  `cat`, `yes | head` -- and error messages from `getopt`, `err`/`warn`
  and `strtonum`), four exit statuses through the monitor's `run`, and
  `/boot/etc/notices`: every imported file's header in it (checked on
  the host), and the file in the running system whole (`sum`). The
  expected outputs were checked against GNU's tools where they agree
  (all but `uniq -c`'s column width and the messages).
- **`ctest`** at every boot: 74 checks (56 before) -- `strtoll`/`strtoull`
  limits and overflow, `strtonum`, `strsep`, `strcasecmp`, `basename`,
  `dirname`, `reallocarray` overflow, `getopt` (grouped flags, joined and
  separate arguments, `--`, a missing argument, the first operand),
  `getprogname`, `getline`, `freopen`, and `basename` writing into a
  pipe with no reader ending with 141.
- Each imported file differs from OpenBSD's only by the added note
  (`diff` against the fetched originals: 7 lines added, none removed).
- `make test`: 283 checks (241 at 0.15), none failing.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | stdio doesn't end the program on `EPIPE` | `ctest`: "writing to a pipe with no reader ends the program" |
  | `getopt` ignores joined arguments (`-dtwo`) | `ctest` and `openbsd_test` (`cut -d: -f2-`) |
  | `__progname` keeps the directory | `ctest`: `getprogname` |
  | `crt0` doesn't call `__libc_init` | `ctest`: `getprogname` |
  | `strtoull` misses overflow | `ctest`: "strtoull overflow" |
  | `getline` doesn't stop at the newline | `openbsd_test`: `rev`, `uniq`, `cut`, ... |
  | `strsep` skips empty fields | `ctest`: "strsep keeps empty fields" |
  | `mknotices.py` leaves a file out | `openbsd_test`: "yes.c: its license header is not in ..." |
  | `mknotices.py` cuts headers short | `openbsd_test` |
  | An imported file without ManiOS's note | the build stops (`mknotices.py`) |
  | The PCnet waits even when the card has moved on | `net_test`: the guest stops answering after the big frames |

## Known limits

- The "C" locale only; no wide characters.
- No signals: only stdio output ends a program on a broken pipe.
- `getopt` only: no `getopt_long` (OpenBSD's tools don't need it).
- The shell has no `$?`; exit statuses are seen through the monitor's
  `run`.
