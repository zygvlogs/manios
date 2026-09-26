# M17 — grep, sed and More from OpenBSD; a POSIX File Layer

**Status:** Achieved (2026-09-26). Released as **0.17.0**. The second
round of imports from OpenBSD ([M16](M16-openbsd-tools.md) was the
first), asked for as "continue till end": the text tools M16 left out
because they need regular expressions, and every other OpenBSD text
tool whose needs ManiOS's C library can meet without new kernel
features.

```
manios% ls /bin | grep -c .
54
manios% grep -rn Welcome /boot/etc
/boot/etc/motd:1:Welcome to ManiOS.
manios% echo hello | sed 's/l*o/0/'
he0
manios% expr 4294967296 / 3
1431655765
```

## What M17 delivers

| Piece | Files | Summary |
|---|---|---|
| 19 programs | `third_party/openbsd/usr.bin/`, `bin/` | `grep`, `sed`, `expr`, `test`, `nl`, `tr`, `tail`, `tee`, `cmp`, `join`, `lam`, `fmt`, `column`, `colrm`, `col`, `unexpand`, `tsort`, `vis`, `unvis`: 30 from OpenBSD in all |
| OpenBSD's libc parts | `third_party/openbsd/lib/`, `include/` | regular expressions (Henry Spencer's library), `getopt`/`getopt_long` (replacing ManiOS's `getopt` of 0.16), `fts`, `vis`/`unvis`, `recallocarray`, libutil's `ohash` |
| POSIX files | `libc/stat.c`, `dirent.c` | `stat`/`fstat`/`fstatat`, `opendir`/`readdir`, POSIX `open` flags, `fcntl`, `access`, `isatty`, over ManiOS's `open` and `fstat` |
| What ManiOS lacks | `libc/posix.c` | `mmap`, `ioctl`, `kqueue`, file creation, signals, the environment: failing the way POSIX allows |
| 64-bit division | `libc/divdi3.c` | `__udivdi3` and relatives, for the 386 |
| More libc | `asprintf.c`, `err.c`, `locale.c`, `stdio.c`, `string.c`, headers | `asprintf`, `warnc`/`errc`, wide characters, `setvbuf`, `fgetln`, `fpurge`, `strlcat`, `strnlen`, `explicit_bzero`, `isgraph`, `<sys/stat.h>`, `<fcntl.h>`, `<dirent.h>`, `<signal.h>`, `<time.h>` and others |
| Kernel | `zkt/fs/vfs.c`, `zkt_abi.h` | seeking a pipe fails with ESPIPE |
| Smaller programs | `Makefile` | each function its own section; the linker drops the unused ones |
| Tests | `tests/openbsd_test.py`, `ctest.c`, `utest.c` | 114 checks of the programs (42 before); 104 of libc at every boot (74 before) |

## Design decisions

- **Still unmodified.** As in M16, every imported file is OpenBSD's own
  byte for byte, apart from ManiOS's note; libc grew to fit them. (Some
  headers are ISO 8859-1 -- grep's authors' names -- so the import and
  the notices are copied as bytes, not as text.) Files with two license
  blocks (`getopt_long.c`) have the note after both, so `/boot/etc/notices`
  carries both.
- **A POSIX layer over ManiOS's own calls**, not new system calls.
  ManiOS's `open` and `fstat` already say what a file is (its type and
  size, `struct zkt_dirent`); `stat`, `opendir` and the POSIX `open`
  are built on them. ManiOS keeps its own `open(path, OREAD)` and
  `fstat(fd, &dirent)` for its own programs; the POSIX functions of the
  same names are declared with other link names (`__posix_open`,
  `__posix_fstat`), so a program includes either `<manios.h>` or
  `<fcntl.h>`/`<sys/stat.h>`.
- **What a file's status says.** ManiOS has no owners, permissions or
  times: files read as mode 0644, directories 0755, devices 0666, all
  owned by 0, with no times. `st_nlink` is 1: with the Unix 2 for a
  directory, `fts` would take it to have no subdirectories (the link
  count trick); 1 is what BSD file systems without link counts report,
  which turns the trick off. `st_ino` is a hash of the cleaned absolute
  path, so `test a -ef dir/../a` holds, and a directory named like its
  parent isn't taken for a loop. Two paths to one file through a bind
  count as different files -- the safe way to be wrong: a program that
  thinks two files differ only compares them.
- **Saying no the way POSIX allows.** `mmap` fails with ENODEV, and
  `grep` and `cmp` read the file instead, as they do on OpenBSD when
  mapping fails. `ioctl` fails with ENOTTY, so `column` and `sed` use 80
  columns. `kqueue` fails with ENOSYS, so `tail -f` prints the file, says
  it can't follow, and stops. In ManiOS only devices and pipes take
  writes, so creating, truncating, renaming and removing files fail with
  EROFS, and `sed -i` and `tee FILE` say "read-only file system".
  Signals are recorded and never delivered; `getenv` finds nothing.
- **`malloc(0)` gives a pointer of its own** (and `realloc(p, 0)` keeps
  one), as the BSDs and glibc do. ManiOS's `malloc(0)` returned NULL,
  which POSIX allows; OpenBSD's programs take NULL for "out of memory":
  `sed` asked for zero elements and stopped.
- **64-bit division in libc.** `expr` works in 64 bits, and GCC turns a
  64-bit `/` into a call to `__divdi3`, which libgcc provides -- built
  for the i686, which is why ManiOS programs never link it (M8, M9).
  `divdi3.c` does it for the 386: two `DIV` instructions when the divisor
  fits in 32 bits (the first remainder carries into the second), shifts
  and subtractions when it doesn't.
- **Seeking a pipe fails (ESPIPE).** The kernel let a seek on a pipe
  succeed, though pipe reads ignore offsets; `tail` and `grep` decide how
  to read by trying to seek, so `cat file | tail -n 2` went wrong. Now
  SYS_SEEK on a pipe returns ESPIPE (a new error number, the traditional
  29), as POSIX requires. It changes what an existing call returns,
  which `zkt_abi.h` says needs a new ABI version; it is kept at version
  1 because the old result meant nothing -- no program could have used
  it. `fseek` now keeps its buffered input when the seek fails, which it
  used to drop first.
- **`<zlib.h>` with types only.** OpenBSD's grep, built with its own
  `NOZ` switch (as for OpenBSD's install media), has no `-Z` but still
  names zlib's types.
- **Smaller programs.** Programs linked whole object files: one call to
  `stdio.o` or `getopt_long.o` brought all of it. Now each function is
  its own section (`-ffunction-sections -fdata-sections`) and the linker
  drops what nothing calls (`--gc-sections`; the ABI note is `KEEP`-ed).
  `cut` went from 18.7 KiB in 0.16 to 15.3, `cat` from 14.5 to 6.3; the
  boot archive holds 54 programs in 1.08 MB (1.67 MB without it). The
  boot area is now 1222 KiB, so `install` makes a 2 MiB partition.
- **Not imported:** `printf`, `rs` and `awk` use floating point, which
  ManiOS programs don't have (`-mno-80387`, no soft-float library);
  `look` works only on memory-mapped files; `diff`, `sort` and `split`
  write temporary or new files; `wc` and `cat` are ManiOS's own.

## Verification

- **`tests/openbsd_test.py`**, 114 checks (42 at 0.16): 96 shell cases
  -- `grep` (regular and extended expressions, `-c -v -n -i -w -l -F -r
  -m`, a pipe, errors), `sed` (substitutions with groups, `-n`, `q`,
  `d`, `y`, `=`, `-E`, `-i` refused), `nl`, `expr` (64-bit division,
  overflow, division by zero, `:`), `tr`, `colrm`, `fmt`, `join`, `lam`,
  `unexpand`, `tee`, `tail` (from a pipe too, `-r`, `-c`, `+n`, `-f`
  refused), `cmp`, `column`, `tsort`, `vis`/`unvis`, `col`, and a
  directory named like its parent for `grep -r` -- 15 exit statuses
  through the monitor's `run` (`test` with files, directories, strings,
  numbers and `-ef`; `cmp -s`; `grep -q`), and the notices. Where GNU's
  tools behave the same, the expected outputs were checked against them
  (all of `grep`, `sed`, `nl`, `expr`, `tr`, `join`, `unexpand`, `tail`,
  `tsort` cases); the rest were checked by hand against OpenBSD's
  documented behaviour.
- **`ctest`**, 104 checks at every boot (74 before): 64-bit division
  (with remainders that carry), `regcomp`/`regexec`/`regerror`, `stat`
  (types, sizes, `st_ino` by path), `opendir`/`readdir`, `fstatat`
  relative to an open directory, POSIX `open` flags refused as they
  should be, `access`, `mmap`/`ioctl`/`rename`/`unlink` failing,
  `unlink` of a missing file (ENOENT), `signal`, `getopt_long`,
  `strlcat`, `asprintf`, `fgetln`, `setvbuf`, `malloc(0)`, and a pipe
  that can't be seeked keeping its input.
- **`utest`**: seeking a pipe is ESPIPE (120 checks: one new, and five
  more directory reads of the larger `/bin`).
- Each imported file differs from OpenBSD's only by the note (8 lines
  added, none removed).
- `make test`: 355 checks (283 at 0.16), none failing. On the way,
  the desktop test's scroll-back check timed out: it listed `/bin`
  twice, 108 lines now, and the terminal draws about five lines a second
  in QEMU (23 s, over the test's 20). It now lists the first 30
  programs twice -- still over two screens -- whatever `/bin` holds.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | 64-bit division drops the high word's remainder | `ctest` (after strengthening: its first values all divided evenly) and `openbsd_test` (`expr 4294967296 / 3`) |
  | Long division skips equal values | `ctest`: 64-bit division |
  | Signed division loses its sign | `openbsd_test`: `expr -9 / 2` |
  | Seeking a pipe succeeds again | `utest` and `openbsd_test` (`cat lines.txt \| tail -n 2`) |
  | `fseek` drops buffered input before seeking | `ctest`: "a pipe can't be seeked, and keeps its buffered input" |
  | Directories have `st_nlink` 2 | `openbsd_test`: `grep -r` |
  | `fstatat` can't work relative to a directory | `openbsd_test`: `grep -r` |
  | `st_ino` from the name only | `openbsd_test`: `grep -r` into a directory named like its parent |
  | `malloc(0)` returns NULL | `openbsd_test`: `sed` |
  | `fgetln` loses the newline | `ctest` |
  | `strlcat` returns the wrong length | `ctest` |
  | `access` grants execution to files | `ctest` |

## Known limits

- The "C" locale only; no floating point, so no `printf(1)`, `awk`.
- ManiOS can't create, rename or remove files, so `sed -i`, `tee FILE`,
  `sort`, `split` and `diff` (which needs temporary files) can't work.
- No signals, no `kqueue`: `tail -f` doesn't follow.
- `st_ino` follows paths, not files: two paths to one file through a
  bind count as different files.
