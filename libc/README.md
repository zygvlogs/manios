# libc/

ManiOS's own C library, built against the ZKT system call ABI
([`zkt/abi/zkt_abi.h`](../zkt/abi/zkt_abi.h),
[M8 notes](../docs/milestones/M8-userspace.md)). Compiled like the
kernel: `-march=i386`, freestanding, no libgcc.

- `crt0.S` — process entry: `main(argc, argv)`, then `exit`
- `syscalls.c` — one wrapper per system call (`-1` and `errno` on
  failure), and `zkt_syscall()` for raw access
- `string.c` — `mem*` and `str*` functions
- `include/` — `manios.h` (the system calls), `errno.h`, `string.h`

Standard I/O, a heap and the rest of a small C library come with M9.
