# zkt/fs/

The virtual filesystem and namespaces. Design and verification:
[M7 notes](../../docs/milestones/M7-vfs-namespaces.md).

- `vfs.c` / `vfs.h` — refcounted vnodes with 9P-shaped operations (walk
  one name, read/write at an offset, readdir, release), open files, and
  `vfs_bind` / `vfs_mount` / `vfs_unbind`
- `namespace.c` — per-thread mount tables (ADR-0003): Plan 9 style
  replace / before / after binds, union directories, lexical path
  cleaning, `ns_fork`
- `ramfs.c` — in-memory directory trees (the kernel root; test trees)
- `devfs.c` — every registered device as a file under `/dev`
- `fat.c` — read-only FAT12/16, mounted at `/n/<device>`
- `fs_init.c` — builds the kernel namespace at boot
- `vfs_selftest.c` — boot-time checks of resolution, unions, namespace
  isolation and reference/heap leaks

The vnode interface draws on the published 4.4BSD vnode design and on
Plan 9's 9P; it is original ManiOS code (see the BSD-inspired vs.
BSD-derived distinction in
[`docs/FOUNDING-PROPOSAL.md` §3.2](../../docs/FOUNDING-PROPOSAL.md#32-two-different-things-bsd-inspired-vs-bsd-derived)).
