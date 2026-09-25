# M8 — Userspace: Processes, Ring 3, System Calls

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §6](../FOUNDING-PROPOSAL.md#6-development-roadmap)
milestone M8. It also moves the namespace operations of
[ADR-0003](../adr/0003-plan9-namespaces-and-resource-protocol.md)
(`bind`, `unbind`, private namespaces) into userspace.

## What M8 delivers

| Piece | Files | Summary |
|---|---|---|
| Address spaces | `zkt/arch/i386/paging.c`, `zkt/mm/kpage.c` | One page directory per process; the kernel half is shared |
| Ring 3 | `gdt.c`, `tss.c`, `user_entry.S` | User segments, per-thread kernel stack in the TSS, `iret` into user mode |
| System calls | `isr.S`, `idt.c`, `zkt/kernel/syscall.c` | `int $0x80`, 14 calls, argument checking |
| User memory access | `zkt/kernel/usercopy.c` | Every user pointer is checked against the page tables first |
| ELF loader | `zkt/kernel/elf.c` | Static ELF32 i386 executables; every field is validated |
| Processes | `zkt/kernel/process.c` | spawn / exit / wait, descriptor tables, orphans, fault handling |
| Boot archive | `zkt/fs/bootfs.c`, `Makefile` | A ustar archive of programs linked into the kernel, mounted at `/boot` |
| ABI | `zkt/abi/zkt_abi.h` | The single header shared by the kernel and userspace |
| libc (minimal) | `libc/` | `crt0`, system call wrappers, `errno`, string functions |
| Programs | `userland/bin/hello.c`, `userland/test/*.c` | The first program, and the test programs |
| Self-test | `zkt/kernel/user_selftest.c`, `userland/test/utest.c` | 64 system call checks at every boot, plus a leak check |
| Monitor | `monitor.c` | `run PATH [ARGS...]` |

The kernel namespace now also contains the boot archive:

```
/boot        bootfs: bin/hello, test/{utest,fault,isotest,nstest}, etc/motd
/bin         bound from /boot/bin
```

A process's address space:

```
0x00000000             page 0, never mapped: NULL dereferences fault
0x08048000             program: code and constants (read-only), then data and bss
     ...               free (programs may be linked anywhere from 0x1000)
0xBFFEE000             guard page, never mapped: stack overflow faults
0xBFFEF000-0xBFFFF000  stack, 64 KiB
0xBFFFF000             never mapped
0xC0000000-0xFFFFFFFF  the kernel, shared by every process, supervisor-only
```

## The system call ABI

`int $0x80`, with the call number in EAX and up to five arguments in
EBX, ECX, EDX, ESI and EDI. The result comes back in EAX: zero or more
on success, or a negated error number. The libc wrappers turn that into
the usual `-1` plus `errno`. All numbers, flags, structures and limits
live in `zkt/abi/zkt_abi.h`.

| # | Call | Notes |
|---|---|---|
| 0 | `exit(code)` | Code 0-255 |
| 1 | `read(fd, buf, len)` | A directory reads as whole `struct zkt_dirent` records |
| 2 | `write(fd, buf, len)` | |
| 3 | `open(path, mode)` | `OREAD`, `OWRITE`, `ORDWR`; absolute paths only |
| 4 | `close(fd)` | |
| 5 | `spawn(path, argv)` | Returns the child's pid (see below) |
| 6 | `wait(pid, *status)` | `status` may be NULL |
| 7 | `getpid()` | |
| 8 | `sleep(ms)` | |
| 9 | `bind(new, old, flag)` | `BIND_FLAG_REPLACE`, `_BEFORE` (-b), `_AFTER` (-a) |
| 10 | `unbind(old)` | |
| 11 | `nsfork()` | Gives the calling process a private copy of its namespace |
| 12 | `fstat(fd, *dirent)` | Name, type and size |
| 13 | `uptime()` | Milliseconds since boot, modulo 2^31 |

A wait status is either an exit code, or `ZKT_WAIT_KILLED` combined with
the CPU exception vector that killed the process. At entry, ESP points
at `argc`, followed by `argv`.

## Design decisions

- **`int $0x80`, not `sysenter`.** `sysenter` needs a Pentium II, and
  ManiOS targets the 386. The gate is a *trap* gate with DPL 3: user code
  may raise it, and interrupts stay enabled during system calls. A call
  that blocks, such as `wait`, `sleep` or a console `read`, just sleeps
  like any kernel thread, and a long call can be preempted. The
  exception vectors remain DPL 0 gates. A program that tries
  `int $14` to fake a page fault gets a general protection fault instead,
  and the test suite checks this.
- **One page directory per process; the kernel half is shared.**
  Directory entries 768-1022 point at the same kernel page tables in
  every address space. The page directories themselves live at kernel
  addresses, in the `kpage` region at `0xE2000000`. That lets the
  kernel reach all of them from anywhere. When a kernel page table is
  created, for example because the heap grew past a 4 MiB boundary
  during a system call, its entry is written into every directory at
  once. A new boot-time self-test checks this: it creates a kernel page
  table while one process address space is active, then reads through
  it from a second address space and from the kernel's own.
- **The loader works from inside the child's address space.** `spawn`
  moves the calling thread into the new address space, loads the ELF
  file and builds the stack there, then moves back. The move lasts
  across preemption, because the scheduler restores each thread's
  address space. So there are no temporary mappings, and no window
  where the loader writes into the wrong process.
- **User pointers are checked against the page tables, not trusted to
  fault.** Every buffer, path and `argv` entry is checked with
  `vmm_user_range_ok()` before the kernel touches it: it must lie in the
  user half, be mapped user-accessible, and be writable when the
  kernel will write to it. This check is required on a 386, not just
  defensive. The 80386 has no `CR0.WP` bit, so ring-0 writes ignore
  read-only pages. Without a software check, the kernel would silently
  write into a program's read-only code. Processes have one thread and
  there is no `munmap`, so nothing can change a checked range before
  the copy, and no fault-recovery tables are needed. `read` checks the
  whole buffer before reading anything, so a bad buffer never loses
  input.
- **`spawn` instead of `fork` + `exec`.** A cheap `fork` needs
  copy-on-write. On a 386, copy-on-write can't catch the kernel's own
  writes (again, no `CR0.WP`), so every copy-out would need a manual
  copy-on-write check. `spawn` also matches how programs are really
  started: a shell doesn't need a copy of itself to run `ls`.
  `rfork`-style sharing options can come later if a real need appears.
- **A child shares its parent's namespace**, as with Plan 9's `rfork`
  without `RFNAMEG`. `nsfork()` is that flag: after it, a process's
  binds are its own. Children also inherit descriptors 0-2. A program
  started by the kernel (the monitor's `run`, the boot self-test) gets
  `/dev/cons` on all three. The namespace is still stored on the
  thread, and since a process has exactly one thread, that is the same
  as storing it on the process.
- **A fault in user mode ends the process, not the kernel.** The kernel
  prints `name[pid]: killed: <exception> [at <address>]`, frees
  everything the process held, and reports `ZKT_WAIT_KILLED | vector`
  to the waiter. When a parent exits first, its children become orphans
  and are reclaimed as soon as they exit.
- **Segments without `PF_W` are mapped read-only once loaded.** A write
  into program code faults, and `read()` into code returns `EFAULT`.
- **The ELF loader treats every field as untrusted.** It checks:
  - class, byte order, type, machine and program header size;
  - that the program header table lies within the file;
  - for each loadable segment, that `memsz >= filesz`, that the file
    data lies within the file, and that the whole segment lies between
    `0x1000` and the stack's guard page, with no wraparound;
  - that the entry point lies inside a loaded segment.

  Any failure gives `ENOEXEC`, and the half-built address space is
  discarded.
- **The boot archive is a ustar file linked into the kernel.** ManiOS
  has userspace even with no disk. The build packs it with sorted
  names, fixed owners and a fixed timestamp, so the same sources give
  the same archive. ustar was chosen because it's a public POSIX
  format, simple to parse defensively (the kernel checks every header
  checksum), and every host can create it. It shows up as a ramfs tree
  at `/boot`. `/bin` is a *bind* of `/boot/bin`, so a disk's `bin/` can
  be unioned in front of it or behind it (the console test does this).
- **Programs are built like the kernel:** `-march=i386`, and no libgcc.
  The libc is ManiOS's own and, for M8, minimal: `crt0`, the system call
  wrappers, `errno`, and string functions. M9 grows it.

## Verification

- **At every boot, and in `make test`:** `user_selftest` runs
  `/bin/hello`, then `/boot/test/utest`, a 64-check conformance test
  of the ABI. `utest` first calls `nsfork`, so its binds stay private.
  It checks:
  - **files**: open and read; `fstat`; end of file; directory records;
    `EBADF`, `EROFS`, `EISDIR`, `ENOENT` and `EINVAL`; running out of
    descriptors (`EMFILE`);
  - **pointers**: kernel, NULL, and range-overflowing buffers for every
    pointer-taking call (`EFAULT`); reading into program code; a failed
    read consuming no input; `ENOSYS`;
  - **time**: `sleep` lasts about as long as asked;
  - **processes**: exit codes; ten kinds of fault, each with the right
    vector (NULL, kernel read and write, writing to code, jumping to
    unmapped memory, stack overflow into the guard page, `cli`, port
    I/O, `int $14`, divide by zero); `ENOENT`, `EISDIR` and `ENOEXEC`
    spawns; `E2BIG` for too many or too long arguments; `ECHILD`; a
    CPU-bound child not starving its parent (user-mode preemption); two
    concurrent copies of `isotest` seeing only their own memory at the
    same addresses, plus initialised data and zeroed bss;
  - **namespaces**: a child's bind is visible to its parent; a child
    that forks its namespace keeps its changes private; bind errors.

  The kernel then checks, with a warm-up run first, that no frames
  (excluding permanent heap growth) and no heap bytes leaked. It also
  checks that none of `utest`'s binds reached the kernel's namespace.
- **The console test** (`tests/console_test.py`) adds 25 cases:
  - listing `/boot` and `/bin`;
  - `run` with arguments, over serial and over the PS/2 keyboard;
  - faulting programs, and the monitor carrying on afterwards;
  - exit codes and spawn errors;
  - `utest` run again from the monitor;
  - `threads` showing that no process thread outlives its program;
  - ten malformed ELF files, each derived from `hello` with one field
    broken, all giving "not an executable", with the unmodified control
    still running;
  - a program on a FAT disk, run through `bind -a /n/ata0p1/bin /bin`.
- **Negative controls** (temporary sabotage, reverted afterward), each
  caught:

  | Sabotage | Caught by |
  |---|---|
  | `copy_to_user` without the range check | *fstat into the kernel is EFAULT*, then a kernel page fault |
  | Range check looking at the first page only | *vmm_user_range_ok misjudged a range* |
  | New kernel page tables not propagated | *a new kernel page table did not reach every address space* |
  | Entering user mode with interrupts off | Boot hangs in the CPU-bound-child check: no M8 marker, so the smoke test fails. The check was added for this: without it, the sabotage went unnoticed |
  | `read` without the up-front buffer check | *a failed read consumes nothing* |
  | TSS `esp0` not switched per thread | Double fault |
  | `exit` not freeing user pages | *processes leaked physical memory* |
  | User faults handled as kernel faults | Kernel panic on the first faulting test program |
  | Loader without the segment-end bound | Kernel panic on the `wrap` ELF (console test) |
  | Code segments left writable | *reading into program code is EFAULT*, *wtext* |
- All tests pass on the 8 MiB 486 machine. Boot reaches the monitor
  prompt about 0.8 s after the kernel starts, with every self-test
  included.
- The first boot failed one check: *divide by zero* came back as a
  normal exit. GCC may compile `1 / x` as `x == 1`, because dividing by
  zero is undefined in C, so no `div` instruction was ever executed.
  The test now divides in inline assembly.

## Known limits

- Programs have no heap (`sbrk` arrives in M9) and no current directory,
  so paths are absolute.
- There are no signals, no `kill`, one thread per process, and static
  executables only.
- An executable is read whole into the kernel heap to load it, which
  caps it at 1 MiB. There is no demand paging, and the stack is a fixed
  64 KiB.
- The ABI has no version marker in executables yet (M9).
- Nothing reclaims a process that the kernel spawned and never waited
  for. The monitor and the self-test always wait.
