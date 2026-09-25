# zkt/kernel/

Architecture-neutral kernel core, and the glue that wires the other
subdirectories together. Syscall dispatch joins it once the syscall ABI
exists (M8).

- `main.c` — `kernel_main()`: arch init → console → memory map → PMM →
  VMM → heap → memory self-test → scheduler (becomes thread "main") →
  timer → interrupts on → scheduler self-tests → prompt → `thread_exit()`
- `kconsole.c` — console output to both serial and VGA
- `panic.c` — `panic()` / `panic_dump()`
- `multiboot.c` — Multiboot memory map → generic `struct mem_region` list
- `timer.c` — system tick (drives the scheduler), uptime and sleep, on
  top of the arch `clock.h`
- `kstring.c` — `memset` / `memcpy` / `memmove` / `memcmp`
