# zkt/kernel/

Architecture-neutral kernel core, and the glue that wires the other
subdirectories together. Syscall dispatch joins it once the syscall ABI
exists (M8).

- `main.c` — `kernel_main()`: arch init → console → memory map → PMM →
  VMM → heap → memory self-test → scheduler (becomes thread "main") →
  timer → interrupts on → scheduler self-tests → drivers → monitor thread →
  `thread_exit()`
- `kconsole.c` — console output (VGA + COM1) and input (keyboard + COM1),
  and device `cons`
- `monitor.c` — the `ZKT>` kernel monitor (debugging console)
- `kprintf.c` — printf subset (32-bit conversions)
- `kerrno.c` / `kerrno.h` — kernel error codes and their messages
- `ring.h` — byte ring buffer
- `panic.c` — `panic()` / `panic_dump()`
- `multiboot.c` — Multiboot memory map → generic `struct mem_region` list
- `timer.c` — system tick (drives the scheduler), uptime and sleep, on
  top of the arch `clock.h`
- `kstring.c` — `mem*` and a few `str*` functions
