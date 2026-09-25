# M9 — libc, a Versioned ABI, Coreutils, Shell

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §6](../FOUNDING-PROPOSAL.md#6-development-roadmap)
milestone M9 and the ABI versioning promised in
[§2.9](../FOUNDING-PROPOSAL.md#29-system-call-interface).

ManiOS now boots into a userspace shell:

```
Milestone M9: libc, ABI v1 and shell online (self-test passed).
manios% cd /boot; ls
bin/
etc/
test/
manios% wc etc/motd
      1       3      19 etc/motd
manios% newns; bind -a /n/ata0p1/bin /bin
manios% exit
console: the shell has exited; this is the kernel monitor (run /bin/sh to go back)
ZKT>
```

## What M9 delivers

| Piece | Files | Summary |
|---|---|---|
| ABI version | `zkt/abi/zkt_abi.h`, `libc/crt0.S`, `userland/user.ld`, `zkt/kernel/elf.c` | Every program carries an ELF note with its ABI version; the kernel refuses programs without one |
| New system calls | `zkt/kernel/syscall.c`, `process.c` | `sbrk`, `chdir`, `getcwd`; relative paths everywhere |
| Cooked console | `zkt/kernel/kconsole.c` | Plan 9-style line discipline on `/dev/cons`, shared by the monitor and programs |
| libc | `libc/` | stdio, printf family, malloc, stdlib, ctype, more string functions, assert |
| Coreutils | `userland/bin/` | `cat`, `echo`, `ls`, `wc`, `sum`, `sleep`, `pwd`, `bind`, `unbind`, `uptime`, `true`, `false` |
| Shell | `userland/bin/sh.c` | `/bin/sh`: quoting, `;`, comments, builtins, scripts, `-c` |
| Console thread | `zkt/kernel/monitor.c` | Boot runs the shell; when it exits, the kernel monitor takes over |
| Self-test | `userland/test/ctest.c`, `user_selftest.c` | 56 libc checks at every boot; `utest` grew to 83 |

## ABI version 1

The M8 system calls, plus:

| # | Call | Notes |
|---|---|---|
| 14 | `sbrk(increment)` | Returns the previous end of the heap. The heap starts on the page after the program and may grow up to the stack's guard page. New memory is zeroed; memory given back is unmapped |
| 15 | `chdir(path)` | The directory must exist; the stored path is cleaned |
| 16 | `getcwd(buf, len)` | `ERANGE` if `len` has no room for the NUL |

Every path argument (`open`, `spawn`, `bind`, `unbind`, `chdir`) may
now be relative to the process's current directory. A program the
kernel starts begins at `/`, and a child inherits its parent's
directory. Relative paths are joined to the directory, then cleaned
lexically like any other path, so `..` never escapes a bind.

**Error returns.** A result from -1 to -4095 (`ZKT_ERRNO_MAX`) is a
negated error number; any other value is a result. On i386, `sbrk` can
return addresses above 2 GiB, which are negative as a signed number, so
"negative means error" would have been ambiguous.

## Design decisions

- **The ABI version is an ELF note, and the kernel checks it.** `crt0.S`
  emits a note named `ZKT`, of type 1, whose descriptor is the version
  number. `user.ld` places it in a `PT_NOTE` segment. The loader refuses
  (`ENOEXEC`) a program with no note, or with a version newer than the
  kernel knows. The rule, written in `zkt_abi.h`, is:
  - new calls may be added under the same version;
  - changing the meaning of an existing call requires a new version.

  So a future kernel can keep running version-1 programs, or refuse
  them explicitly, never misinterpret them. A note, rather than
  `EI_OSABI`, leaves room for more fields later. The checks walk every
  note in the segment, with lengths validated against the file, like
  the rest of the loader.
- **`/dev/cons` is cooked, as in Plan 9.** Line editing now lives in the
  kernel's console device, not in each reader:
  - input is echoed;
  - Backspace/DEL erases a character, and ^U erases the line;
  - a read returns at most one line, ending in `\n`;
  - ^D on an empty line reads as end of file; ^D after some text
    ends the line without a newline.

  The monitor's own editor became a single `device_read` call, so the
  shell and the monitor behave identically. Typed-ahead input stays
  queued for whoever reads next: the console test types
  `cat⏎two words⏎^D` in one go. The shell reads the first line, `cat`
  the second, then gets end of file. A raw mode, as Plan 9's
  `/dev/consctl` provides, can come when a full-screen program needs it.
- **The shell is a program, and the monitor is its fallback.** Boot
  hands the console to `/bin/sh`. If the shell exits or can't start,
  the kernel monitor runs on the same console, and `run /bin/sh` starts
  a new shell. The monitor remains the kernel's own debugging console,
  independent of userspace.
- **Shell language, kept small:**
  - `;` and newlines separate commands; blanks separate words;
  - `'…'` quotes literally, `"…"` quotes with `\"` and `\\` escapes,
    and `\` escapes one character; `#` at the start of a word begins a
    comment;
  - a name without `/` runs `/bin/NAME`;
  - the builtins are those that must run inside the shell process:
    `cd`, `exit`, `newns` (`nsfork`), and `help`.

  `bind` and `unbind` are ordinary programs, as in Plan 9, because a
  child shares its parent's namespace: running them from the shell
  changes the shell's view. After `newns`, those changes stay private
  to this shell and its children.
- **libc is written for ManiOS and kept honest:**
  - **No libgcc.** 64-bit `printf` conversions (`%lld`, `%llu`, `%llx`)
    divide 16 bits at a time using 32-bit operations, so the libgcc
    division helpers are never needed.
  - **`malloc`** keeps a first-fit free list in address order, so
    `free` coalesces with both neighbours. Blocks are 16-byte aligned,
    and each header carries a magic value, so a double free or a stray
    pointer aborts instead of corrupting the heap. The heap grows by at
    least 16 KiB at a time.
  - **`qsort` is a heapsort**: O(n log n) in the worst case, with no
    recursion and no extra memory.
  - **stdio**: stdout is line-buffered and stderr unbuffered. Reading
    stdin flushes stdout first, so prompts appear. There is no seeking,
    because the ABI has none yet.
  - **Programs link only what they use.** `exit` reaches stdio's flush
    through a weak reference, and `strdup` has its own file, away from
    `strlen`. So `true` is 3 KB and `hello` 5 KB; a program that uses
    `printf` is about 12 KB. Programs are linked with `-n`: the loader
    copies segments rather than mapping file pages, so page padding
    inside the file would be wasted space. Together these shrank the
    boot archive from 368 KB to 225 KB.

## Verification

- **At every boot, and in `make test`:** `utest` (now 83 checks) adds:
  - **directories**: `chdir` path cleaning; relative paths and `..`;
    `..` stopping at the root; `ENOTDIR` and `ENOENT`; `getcwd`'s exact
    buffer boundary (`ERANGE`) and `EFAULT`; a child starting in its
    parent's directory; `spawn` with a relative path;
  - **heap**: `sbrk` alignment and return values; zeroed, writable new
    memory; the heap ending at the break's page; shrinking; `EINVAL`
    below the start and for `INT32_MIN`; a child that touches memory
    it gave back being killed by a page fault.

  Then `ctest` runs 56 libc checks:
  - `printf`: every flag, width and precision form; `INT_MIN`;
    `ULLONG_MAX` and `LLONG_MIN` through the 16-bit division path;
    `%p`; truncation and measuring with `snprintf`;
  - `strtol`/`strtoul`: bases, prefixes, end pointers, overflow and
    underflow with `ERANGE`;
  - the string functions, including overlapping `memmove` in both
    directions;
  - `qsort` on 300 values (sorted, and every element kept); `bsearch`;
  - `malloc`: alignment; `realloc` keeping the contents; `calloc`
    zeroing and overflow; a 1 MiB block; and, after freeing everything,
    one block spanning the *whole* heap, which requires full
    coalescing;
  - stdio: `fopen`/`fgets`/`fread`/`ungetc`/end of file; `fopen`
    errors, including `EROFS`; writing to a device;
  - a double free and a failed `assert` both aborting with status 134.

  The kernel checks for leaked frames and heap after each program.
- **The console test** (`make test`: 100 checks in all) now boots to the
  shell. At the shell it covers:
  - quoting and `;`;
  - typing over the PS/2 keyboard;
  - relative paths after `cd`;
  - typed-ahead input read by `cat`, then ^D;
  - errors, and a killed program;
  - `newns` followed by `bind`, with `sh -c` in a child seeing the
    shared namespace;
  - `ctest`;
  - on the FAT disk: `cd` into a volume, a relative `sum` matching the
    host's checksum, and `../` paths.

  It then leaves with `exit` and checks that the monitor's namespace
  never saw the shell's private bind. Two more malformed ELF files
  cover the ABI note: one with the wrong note type, one with version 99.
- **Negative controls** (temporary sabotage, reverted afterward), each
  caught:

  | Sabotage | Caught by |
  |---|---|
  | Relative paths joined to `/` instead of the current directory | *relative paths start at the current directory*, and the child/relative spawn check |
  | `sbrk` shrink leaving pages mapped | *memory given back by sbrk faults* |
  | 64-bit `printf` dropping the middle 16-bit quotient | Both `%ll` checks |
  | `free` not merging with the following block | *freed blocks coalesce and are reused*. The first version of this check, which only reallocated 1 MiB, missed this sabotage and was strengthened |
  | `free` not merging with the preceding block | Same |
  | Children not inheriting the directory | The child/relative spawn check |
  | New heap pages not zeroed | *new heap memory is zeroed and writable* |
  | ABI note check disabled | The `noabi` and `abi99` ELF cases |
  | ^D ignored by the line discipline | The `cat` case hangs, so the console test fails |
- All tests pass on the 8 MiB 486 machine, and the kernel boots under
  GRUB. Boot reaches the shell prompt 0.9 s after the kernel starts,
  with every self-test included.

## Known limits

- No pipes or redirection, no environment variables, and no job
  control. The shell waits for each command.
- No writable filesystem, so `>` would have nowhere to write.
- No `lseek`: stdio can't seek, and a stream opened `r+` shares one
  file offset between reading and writing.
- No raw console mode, and no signals: ^C does nothing.
- No floating point in `printf`.
