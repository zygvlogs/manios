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
- `stdio.c`, `format.c` — buffered streams (with `fseek`/`ftell`,
  `setvbuf`, `freopen`, `fgetln`), and the printf family (no floating
  point; 64-bit integers without libgcc). No signals: a program whose
  stdio output reaches a pipe with no reader ends, with status 141, as
  SIGPIPE ends it on Unix (`write()` itself returns `EPIPE`)
- `malloc.c` — `malloc`/`free`/`calloc`/`realloc` over `sbrk`;
  `malloc(0)` gives a pointer of its own, as the BSDs do (0.17)
- `stdlib.c` — `exit`/`atexit`/`abort`, `strtol` family (to
  `strtoull`, 64-bit without division), `qsort`, `bsearch`
- `divdi3.c` — 64-bit division (`__udivdi3` and relatives, which GCC
  calls for `/` and `%` on 64-bit numbers), for the 386: libgcc's is
  built for the i686, so ManiOS programs don't link it
- `string.c`, `strdup.c`, `ctype.c`, `assert.c`, `asprintf.c`
- for programs from BSD (0.16 and 0.17, [third_party/openbsd](../third_party/openbsd/README.md)):
  `err.c` (`err`/`warn` and relatives, `warnc`/`errc`), `getline.c`
  (`getline`/`getdelim`), `progname.c` (`getprogname`, `__progname`),
  `locale.c` (the "C" locale only, and wide characters in it),
  `compat.c` (`pledge` and `unveil`, which do nothing);
  `stat.c` (`stat`/`fstat`/`fstatat`, POSIX `open` flags, `fcntl`,
  `access`, `isatty`, over ManiOS's `open` and `fstat`), `dirent.c`
  (`opendir`/`readdir`), `posix.c` (what ManiOS has no counterpart for,
  failing the way POSIX allows: `mmap`, `ioctl`, `kqueue`, file
  creation and removal (EROFS), signals that are never delivered, no
  environment); `strsep`, `strlcat`, `strcasecmp`, `isblank`, `isgraph`
  in the files above; and, built from OpenBSD's own source in
  `third_party/openbsd/lib/`, the regular expression library, `getopt`
  and `getopt_long` (in place of ManiOS's own `getopt` of 0.16), `fts`,
  `vis`/`unvis`, `strtonum`, `reallocarray`, `recallocarray`,
  `basename`, `dirname`, and libutil's `ohash`
- `zrpsrv.c` — serving files from a program: a tree of nodes, fids,
  walks and directory reads over ZRP on a pipe, with the program's read
  and write callbacks; reads may be deferred and answered later
  (`include/zrpsrv.h`; [M12 notes](../docs/milestones/M12-desktop.md))
- `include/` — `manios.h` (the system calls), the standard headers
  above, and the POSIX and BSD headers programs from BSD include
  (`<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<dirent.h>`, `<err.h>`,
  `<signal.h>`, `<time.h>`, `<libgen.h>`, `<strings.h>`, `<locale.h>`,
  `<wchar.h>`, `<wctype.h>`, `<sys/types.h>`, `<sys/cdefs.h>`,
  `<sys/param.h>`, `<sys/mman.h>`, `<sys/ioctl.h>`, `<sys/event.h>`,
  `<sys/uio.h>`, and a `<zlib.h>` with zlib's types only); the headers
  imported from OpenBSD (`<regex.h>`, `<getopt.h>`, `<fts.h>`, `<vis.h>`)
  are in `third_party/openbsd/include/`. `<stddef.h>`, `<stdint.h>`,
  `<stdarg.h>` and `<stdbool.h>` come from the compiler, and
  `<limits.h>` adds `PATH_MAX`, `LINE_MAX` and others to the compiler's.
  A program includes either `<manios.h>` or the POSIX `<fcntl.h>` and
  `<sys/stat.h>`: their `open()` and `fstat()` differ (the POSIX ones
  have other link names)

Programs link only the parts they use: each function is its own
section, and the linker drops what nothing calls (0.17); `exit` reaches
stdio through a weak reference, so a program without stdio doesn't
carry it.
