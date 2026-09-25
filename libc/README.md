# libc/

ManiOS's own C library, built against the ZKT system call ABI
([`zkt/abi/zkt_abi.h`](../zkt/abi/zkt_abi.h)). Compiled like the kernel:
`-march=i386`, freestanding, no libgcc. Design and verification:
[M8](../docs/milestones/M8-userspace.md) and
[M9](../docs/milestones/M9-libc-shell.md) notes.

- `crt0.S` — process entry (`main(argc, argv)`, then `exit`), and the
  ELF note naming the ABI version, which the kernel requires
- `syscalls.c` — one wrapper per system call (`-1` and `errno` on
  failure), and `zkt_syscall()` for raw access
- `stdio.c`, `format.c` — buffered streams (with `fseek`/`ftell`), and
  the printf family
  (no floating point; 64-bit integers without libgcc)
- `malloc.c` — `malloc`/`free`/`calloc`/`realloc` over `sbrk`
- `stdlib.c` — `exit`/`atexit`/`abort`, `strtol` family, `qsort`,
  `bsearch`
- `string.c`, `strdup.c`, `ctype.c`, `assert.c`
- `zrpsrv.c` — serving files from a program: a tree of nodes, fids,
  walks and directory reads over ZRP on a pipe, with the program's read
  and write callbacks; reads may be deferred and answered later
  (`include/zrpsrv.h`; [M12 notes](../docs/milestones/M12-desktop.md))
- `include/` — `manios.h` (the system calls), and the standard headers
  above; `<stddef.h>`, `<stdint.h>`, `<stdarg.h>`, `<stdbool.h>` and
  `<limits.h>` come from the compiler

Programs link only the parts they use: `exit` reaches stdio through a
weak reference, so a program without stdio doesn't carry it.
