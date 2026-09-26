# third_party/openbsd/

Source imported from [OpenBSD](https://www.openbsd.org/), ManiOS's first
BSD-derived code ([FOUNDING-PROPOSAL §3.3](../../docs/FOUNDING-PROPOSAL.md#33-process-for-any-bsd-derived-import)).
Every file is OpenBSD's own, from `src` at commit
`3d80b15b1ec664bb0fc72f48e55e5488a147aa50` (imported 2026-09-26), with
its copyright header and license kept verbatim and one note added under
them. Nothing else in the files is changed: ManiOS's C library provides
what they need. The directories mirror OpenBSD's `src/`.

Each file's license and origin are recorded in
[`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## What is here

| Path | What | License |
|---|---|---|
| `usr.bin/{basename,comm,cut,expand,fold,head,paste,rev,uniq,yes}/` | programs, installed in `/bin` | BSD-3-Clause (University of California) |
| `usr.bin/dirname/` | program, installed in `/bin` | ISC |
| `lib/libc/stdlib/strtonum.c`, `reallocarray.c` | functions, built into ManiOS's libc | ISC |
| `lib/libc/gen/basename.c`, `dirname.c` | functions, built into ManiOS's libc | ISC |
| `sys/sys/queue.h` | linked-list macros, used by `paste` | BSD-3-Clause (University of California) |

`tests/openbsd_test.py` runs every program (their output, their error
messages and exit statuses); `ctest`, at every boot, checks the libc
functions they depend on.

## How they build

The `Makefile` compiles the programs listed in `OBSD_TOOLS` from
`usr.bin/NAME/NAME.c`, and adds `lib/libc/*/*.c` to libc. They are
compiled with the userland flags plus `-Isys` (for `<sys/queue.h>`),
`-DDEF_WEAK(x)=` (OpenBSD's libc symbol macro) and three warnings turned
off, because OpenBSD's code does not claim to be free of them:
`-Wno-sign-compare`, `-Wno-unused-parameter`, and
`-Wno-maybe-uninitialized` (checked: a false positive in `cut`).

What ManiOS's libc provides for them (see [`libc/README.md`](../../libc/README.md)):

- headers OpenBSD programs include: `<unistd.h>`, `<err.h>`,
  `<sys/types.h>`, `<sys/cdefs.h>`, `<limits.h>` (`PATH_MAX`,
  `LINE_MAX`), `<libgen.h>`, `<strings.h>`, `<locale.h>`, `<wchar.h>`,
  `<wctype.h>`;
- `getopt()`, `err()`/`warn()` and their relatives, `getline()`,
  `strtoll()`/`strtoull()`, `strsep()`, `strcasecmp()`, `freopen()`,
  `isblank()`, `getprogname()` (and `__progname`, which `crt0` sets from
  `argv[0]`);
- `pledge()` and `unveil()`, which do nothing (ManiOS has neither; the
  programs call them to give up rights they won't use);
- the "C" locale only: characters are bytes (`MB_CUR_MAX` is 1), so
  `cut -c` counts bytes, as it does in OpenBSD's "C" locale.

ManiOS has no signals. OpenBSD's `yes` never stops by itself: on Unix,
SIGPIPE ends it when its reader has gone. In ManiOS, stdio does the
same -- a program whose stdio output reaches a pipe with no reader ends
with status 141, what a Unix shell shows for SIGPIPE -- so `yes | head`
finishes.

## Adding a program, or updating one

1. Fetch the file from OpenBSD's `src` at a recorded commit (for
   example from the GitHub mirror, `raw.githubusercontent.com/openbsd/src/COMMIT/PATH`).
2. Put it here at the same path, and add the note under its license
   header, as the other files have it.
3. Add it to `OBSD_TOOLS` in the `Makefile` (a program) -- or, for a
   libc function, under `lib/libc/`, where the `Makefile` finds it.
4. If it doesn't build, give libc what it lacks rather than changing
   the file. If the file must change, say what changed in the note and
   in the ledger.
5. Add its entry to [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md),
   and cases to `tests/openbsd_test.py`.

To update a file, fetch the new revision the same way, keep the note
(with the new commit), and update the ledger entry.
