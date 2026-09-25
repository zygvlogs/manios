# zkt/fs/

The virtual filesystem and namespaces. Design and verification:
[M7 notes](../../docs/milestones/M7-vfs-namespaces.md).

- `vfs.c` / `vfs.h` — refcounted vnodes with 9P-shaped operations (walk
  one name, read/write at an offset, readdir, release), open files, and
  `vfs_bind` / `vfs_mount` / `vfs_unbind`
- `namespace.c` — per-thread mount tables (ADR-0003): Plan 9 style
  replace / before / after binds, union directories, lexical path
  cleaning, `ns_fork`
- `ramfs.c` — in-memory trees (the kernel root; test trees; the boot
  archive's files)
- `bootfs.c` — the boot archive (ustar, linked into the kernel) as a
  ramfs tree at `/boot`
- `devfs.c` — every registered device as a file under `/dev`; block
  devices can be written at any offset (partial blocks are read,
  changed and written back; M14)
- `fat.c` — read-only FAT12/16, mounted at `/n/<device>`
- `pipe.c` — pipes: two connected ends, a message per write, both
  directions; also the channel a userspace ZRP server is mounted over
  ([M12 notes](../../docs/milestones/M12-desktop.md))
- `fs_init.c` — builds the kernel namespace at boot
- `vfs_selftest.c` — boot-time checks of resolution, unions, namespace
  isolation and reference/heap leaks

The vnode interface draws on the published 4.4BSD vnode design and on
Plan 9's 9P; it is original ManiOS code (see the BSD-inspired vs.
BSD-derived distinction in
[`docs/FOUNDING-PROPOSAL.md` §3.2](../../docs/FOUNDING-PROPOSAL.md#32-two-different-things-bsd-inspired-vs-bsd-derived)).
