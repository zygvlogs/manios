# libc/

ManiOS's own C library, built against the ZKT system call ABI
([`zkt/abi/zkt_abi.h`](../zkt/abi/zkt_abi.h)). Compiled like the kernel:
`-march=i386`, freestanding, no libgcc. Design and verification:
[M8](../docs/milestones/M8-userspace.md) and
[M9](../docs/milestones/M9-libc-shell.md) notes.

- `crt0.S` — process entry (`__libc_init(argc, argv)`, `main(argc, argv)`,
  then `exit`), and the ELF note naming the ABI version, which the
  kernel requires
- `syscalls.c` — one wrapper per system call (`-1` and `errno` on
  failure), and `zkt_syscall()` for raw access; `export()` and
  `unexport()` (M13) serve a directory of the caller's namespace over ZRP
- `stdio.c`, `format.c` — buffered streams (with `fseek`/`ftell` and
  `freopen`), and the printf family (no floating point; 64-bit integers
  without libgcc). No signals: a program whose stdio output reaches a
  pipe with no reader ends, with status 141, as SIGPIPE ends it on Unix
  (`write()` itself returns `EPIPE`)
- `malloc.c` — `malloc`/`free`/`calloc`/`realloc` over `sbrk`
- `stdlib.c` — `exit`/`atexit`/`abort`, `strtol` family (to
  `strtoull`, 64-bit without division), `qsort`, `bsearch`
- `string.c`, `strdup.c`, `ctype.c`, `assert.c`
- for programs from BSD (0.16, [third_party/openbsd](../third_party/openbsd/README.md)):
  `getopt.c` (POSIX `getopt`), `err.c` (`err`/`warn` and relatives),
  `getline.c` (`getline`/`getdelim`), `progname.c` (`getprogname`,
  `__progname`), `locale.c` (the "C" locale only), `compat.c` (`pledge`
  and `unveil`, which do nothing); `strsep`, `strcasecmp`, `isblank` in
  the files above; and, built from OpenBSD's own source in
  `third_party/openbsd/lib/libc/`, `strtonum`, `reallocarray`,
  `basename` and `dirname`
- `zrpsrv.c` — serving files from a program: a tree of nodes, fids,
  walks and directory reads over ZRP on a pipe, with the program's read
  and write callbacks; reads may be deferred and answered later
  (`include/zrpsrv.h`; [M12 notes](../docs/milestones/M12-desktop.md))
- `include/` — `manios.h` (the system calls), the standard headers
  above, and the POSIX and BSD headers programs from BSD include
  (`<unistd.h>`, `<err.h>`, `<libgen.h>`, `<strings.h>`, `<locale.h>`,
  `<wchar.h>`, `<wctype.h>`, `<sys/types.h>`, `<sys/cdefs.h>`);
  `<stddef.h>`, `<stdint.h>`, `<stdarg.h>` and `<stdbool.h>` come from
  the compiler, and `<limits.h>` adds `PATH_MAX` and `LINE_MAX` to the
  compiler's

Programs link only the parts they use: `exit` reaches stdio through a
weak reference, so a program without stdio doesn't carry it.
