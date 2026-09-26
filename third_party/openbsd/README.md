# third_party/openbsd/

Source imported from [OpenBSD](https://www.openbsd.org/), ManiOS's
BSD-derived code ([FOUNDING-PROPOSAL §3.3](../../docs/FOUNDING-PROPOSAL.md#33-process-for-any-bsd-derived-import)).
Every file is OpenBSD's own, from `src` at commit
`3d80b15b1ec664bb0fc72f48e55e5488a147aa50` (imported 2026-09-26: the
first files in 0.16, the rest in 0.17), with its copyright header and
license kept verbatim, byte for byte, and one note added under them.
Nothing else in the files is changed: ManiOS's C library provides what
they need. The directories mirror OpenBSD's `src/`.

Each file's license and origin are recorded in
[`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## What is here

Programs, installed in `/bin` (each directory under `usr.bin/` and
`bin/` is one):

| Programs | License |
|---|---|
| `basename`, `cmp`, `col`, `colrm`, `column`, `comm`, `cut`, `expand`, `fold`, `head`, `join`, `paste`, `rev`, `tail`, `tee`, `tr`, `unexpand`, `uniq`, `unvis`, `vis`, `yes` | BSD-3-Clause (University of California) |
| `sed` | BSD-3-Clause (Diomidis Spinellis; University of California) |
| `lam` | BSD-3-Clause (University of California), and ISC for its `utf8.c` |
| `grep` | BSD-2-Clause (James Howard and Dag-Erling Smørgrav) |
| `nl` | BSD-2-Clause (The NetBSD Foundation) |
| `dirname`, `tsort` | ISC |
| `fmt` | ISC (OpenBSD's changes) and Gareth McCaughan's permissive license |
| `expr`, `test` | public domain |

Parts of ManiOS's C library:

| Files | What | License |
|---|---|---|
| `lib/libc/regex/`, `include/regex.h` | regular expressions: `regcomp`, `regexec`, `regerror`, `regfree` | BSD-3-Clause (Henry Spencer; University of California) |
| `lib/libc/stdlib/getopt_long.c`, `include/getopt.h` | `getopt` and `getopt_long` | ISC and BSD-2-Clause (The NetBSD Foundation) |
| `lib/libc/gen/fts.c`, `include/fts.h` | walking directory trees (`grep -r`) | BSD-3-Clause (University of California) |
| `lib/libc/gen/vis.c`, `unvis.c`, `include/vis.h` | `vis` and `unvis` | BSD-3-Clause (University of California) |
| `lib/libc/stdlib/strtonum.c`, `reallocarray.c`, `recallocarray.c`; `lib/libc/gen/basename.c`, `dirname.c` | functions | ISC |
| `lib/libutil/ohash.c`, `ohash.h` | open hashing, from OpenBSD's libutil (`tsort`) | ISC |
| `sys/sys/queue.h` | list macros (`paste`, `tee`) | BSD-3-Clause (University of California) |

`tests/openbsd_test.py` runs every program (their output, their error
messages and exit statuses); `ctest`, at every boot, checks the libc
functions they depend on.

## How they build

The `Makefile` builds each program from all the `.c` files in its
directory, and adds `lib/libc/*/*.c` and `lib/libutil/*.c` to libc
(except `regex/engine.c`, which `regexec.c` includes). The headers under
`include/` are ManiOS's system headers too. The imports are compiled
with the userland flags plus `-Isys` and `-Ilib/libutil`,
`-DDEF_WEAK(x)=` (OpenBSD's libc symbol macro), grep's `-DNOZ` (no
zlib, OpenBSD's own switch for small builds), and without some of
`-Wall -Wextra`'s warnings, which OpenBSD's code isn't written for:
`sign-compare`, `unused-parameter`, `maybe-uninitialized` (checked: a
false positive in `cut`), `unused-but-set-variable`, `type-limits`,
`pointer-sign`, `implicit-fallthrough`, `missing-field-initializers`.

What ManiOS's libc provides for them (see [`libc/README.md`](../../libc/README.md)):

- the POSIX and BSD headers these programs include, and the functions
  behind them: `getopt`, `err`/`warn`/`warnc`, `getline`/`fgetln`,
  `asprintf`, `strtoll`, `strsep`, `strlcat`, `setvbuf`, wide characters
  in the "C" locale, and 64-bit division (the 386 has none, and ManiOS
  programs don't link libgcc);
- files: `stat`/`fstat`/`fstatat`, `opendir`/`readdir`, and POSIX
  `open` flags, over ManiOS's `open` and `fstat`. ManiOS has no owners,
  permissions or times, so files read as mode 0644 and directories 0755,
  with no times; `st_nlink` is 1, as on BSD file systems that keep no
  link counts, and `st_ino` comes from the cleaned path (two paths to one
  file through a bind count as different files);
- what ManiOS doesn't have, reported the way POSIX allows: `mmap` fails
  (ENODEV: `grep` and `cmp` read instead), `ioctl` fails (ENOTTY:
  `column` and `sed` use 80 columns), `kqueue` fails (ENOSYS: `tail -f`
  says so and stops), creating, renaming and removing files fail
  (EROFS: in ManiOS only devices and pipes take writes, so `sed -i` and
  `tee FILE` say so), signals are recorded but never delivered, and
  there is no environment;
- `pledge()` and `unveil()`, which do nothing (ManiOS has neither; the
  programs call them to give up rights they won't use);
- the "C" locale only: characters are bytes (`MB_CUR_MAX` is 1).

ManiOS has no signals. OpenBSD's `yes` never stops by itself: on Unix,
SIGPIPE ends it when its reader has gone. In ManiOS, stdio does the
same -- a program whose stdio output reaches a pipe with no reader ends
with status 141, what a Unix shell shows for SIGPIPE -- so `yes | head`
finishes.

Not imported: `printf`, `rs` and `awk` use floating point, which ManiOS
programs don't have; `look` works only on memory-mapped files; `diff`,
`sort` and `split` write temporary or new files; `wc` and `cat` are
ManiOS's own.

## Adding a program, or updating one

1. Fetch the file from OpenBSD's `src` at a recorded commit (for
   example from the GitHub mirror, `raw.githubusercontent.com/openbsd/src/COMMIT/PATH`),
   byte for byte (some headers are ISO 8859-1).
2. Put it here at the same path, and add the note under its license
   header, as the other files have it (after the last license block
   when there are two).
3. A program: a new directory under `usr.bin/` or `bin/` is picked up by
   the `Makefile`. A libc function: under `lib/libc/`, where the
   `Makefile` finds it.
4. If it doesn't build, give libc what it lacks rather than changing
   the file. If the file must change, say what changed in the note and
   in the ledger.
5. Add its entry to [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md),
   and cases to `tests/openbsd_test.py`.

To update a file, fetch the new revision the same way, keep the note
(with the new commit), and update the ledger entry.
