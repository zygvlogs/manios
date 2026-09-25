# zkt/kernel/

Architecture-neutral kernel core, and the glue that wires the other
subdirectories together.

- `main.c` — `kernel_main()`: arch init → console → memory map → PMM →
  VMM → heap → memory self-test → scheduler (becomes thread "main") →
  timer → interrupts on → scheduler self-tests → drivers → kernel
  namespace and mounts → VFS self-test → user-program and libc
  self-tests → network (`net_init`, loopback ZRP self-test, `export=`)
  → graphics and userspace file server self-tests → console thread →
  `thread_exit()`
- `cmdline.c` — the kernel command line (`KEY=VALUE` words)
- `process.c` — user processes: spawn, exit, wait, descriptor tables,
  ending a process on a CPU exception
  ([M8 notes](../../docs/milestones/M8-userspace.md))
- `syscall.c` — system call dispatch (the ABI is `zkt/abi/zkt_abi.h`)
- `poll.c` — waiting on several files at once (`SYS_POLL`): one wait
  queue that pipes and input devices notify
- `sha256.c` — SHA-256 and HMAC-SHA-256, for ZRP authentication (M13)
- `usercopy.c` — copying to and from user memory, checked first
- `elf.c` — the ELF32 loader
- `user_selftest.c` — runs the boot archive's test programs at boot
- `kconsole.c` — console output (VGA + COM1) and input (keyboard + COM1),
  and device `cons` with its line discipline (cooked input, as Plan 9's
  `/dev/cons`)
- `monitor.c` — the console thread, which runs the `rc=` script, then
  `/bin/sh`, and the `ZKT>` kernel monitor (debugging console) it falls
  back to
- `kprintf.c` — printf subset (32-bit conversions)
- `kerrno.c` / `kerrno.h` — kernel error codes and their messages
- `ring.h` — byte ring buffer
- `panic.c` — `panic()` / `panic_dump()`
- `multiboot.c` — Multiboot memory map → generic `struct mem_region`
  list; the command line; modules (M14)
- `bootmod.c` — Multiboot modules as read-only devices (`/dev/bootarea`,
  the boot area the ManiOS loader started us from; M14)
- `timer.c` — system tick (drives the scheduler), uptime and sleep, on
  top of the arch `clock.h`
- `kstring.c` — `mem*` and a few `str*` functions
