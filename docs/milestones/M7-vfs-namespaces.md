# M7 — VFS, Namespaces, FAT

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §2.10](../FOUNDING-PROPOSAL.md#210-vfs) and the
namespace half of
[ADR-0003](../adr/0003-plan9-namespaces-and-resource-protocol.md).

## What M7 delivers

| Piece | Files | Summary |
|---|---|---|
| VFS core | `zkt/fs/vfs.c`, `vfs.h` | Refcounted vnodes with 9P-shaped operations; open files; union-aware directory reads |
| Namespaces | `zkt/fs/namespace.c` | Per-thread mount tables, bind/union, lexical path cleaning, `ns_fork` |
| ramfs | `zkt/fs/ramfs.c` | In-memory directory trees: the kernel root, and private test trees |
| devfs | `zkt/fs/devfs.c` | Every registered device as a file under `/dev` |
| FAT | `zkt/fs/fat.c` | Read-only FAT12/16, auto-mounted at `/n/<device>` |
| Boot wiring | `zkt/fs/fs_init.c` | Builds the kernel namespace |
| Self-test | `zkt/fs/vfs_selftest.c` | Resolution, unions, isolation, leak checks, all at every boot |
| Monitor | `monitor.c` | `ls`, `cat`, `sum`, `bind [-a\|-b]`, `unbind`, `ns` |

The kernel namespace at boot:

```
/            ramfs
/dev         devfs: cons, com1, vga, ata0, ata0p1, ...
/n/ata0p1    FAT volume on that partition (one directory per FAT volume found)
```

## Design decisions

- **The vnode operations are shaped like 9P**: `walk` looks up a single
  name, `read`/`write` take an explicit offset, `readdir` lists
  entries, and `release` corresponds to 9P's clunk. When ZRP (ADR-0003)
  arrives at M10, its messages map one-to-one onto these, so a remote
  filesystem can be just another vnode implementation.
- **Namespaces belong to threads for now.** A new thread shares its
  creator's namespace. `ns_fork()` makes a private copy, the equivalent
  of Plan 9's `rfork(RFNAMEG)`. At M8 the namespace moves to the
  process. Dropping a namespace never blocks, because a thread's last
  reference can go while the thread is being reclaimed, possibly inside
  an interrupt.
- **Bind semantics follow Plan 9.**
  - `bind new old` replaces what `old` resolves to.
  - `bind -b` makes a union with `new` searched first; `bind -a`
    searches it last.
  - Lookups take the first union member that has the name.
  - Listing a union returns every member's entries, duplicates
    included, as Plan 9 does.
  - A directory binds only onto a directory, and unions only form on
    directories.
  - `unbind` removes every bind on a path, including the filesystem that
    was mounted there.
- **Paths are cleaned lexically before resolution**, as in Plan 9's
  `cleanname`: `//` collapses, `.` is dropped, `..` removes the
  preceding name, and `..` at the root stays at the root. Filesystems
  therefore never see `.` or `..`, and `..` can't escape a bind.
- **Deliberate deviation from Plan 9:** the mount table is keyed by
  *cleaned path*, not by the identity (qid) of the file being bound
  over. The same file reached by two different paths would be treated
  as two mount points. Given lexical `..`, only binding over something
  that is itself reachable twice would show a difference, which
  doesn't happen in the M7 tree. Recorded in ADR-0003 as a candidate
  to revisit when ZRP brings qids at M10.
- **FAT reads the FAT through a one-sector cache.** The alternative,
  loading the whole table (up to 128 KiB for FAT16), is too much on an
  old machine with a few MiB of RAM. Every chain walk is bounded by the
  cluster count and every cluster number is validated, so a corrupt or
  looping chain gives `EIO`, not a hang. Each open file remembers its
  last chain position, so sequential reads don't rewalk the chain.
- **FAT boundaries:** read-only; 512-byte sectors; FAT12 and FAT16
  (FAT32 returns `EINVAL`). Names are 8.3, matched case-insensitively.
  Long-name entries are skipped, so a file with a long name appears
  under its short alias (`manios~1.txt`).
- **A volume is only mounted when its boot sector checks out**: 55 AA
  signature, sane BPB, FAT type decided by cluster count as the spec
  requires, and a total size that fits the device.

## Verification

- **At every boot, and in `make test`:** `vfs_selftest` runs in a
  thread with a private namespace over a private ramfs tree. Two
  directories both contain an `x`, so which `x` a union finds shows its
  search order. It checks:
  - lexical path cleaning;
  - `ENOENT`, `ENOTDIR` and `EINVAL` cases;
  - `bind -a` and `bind -b` search order;
  - that a union lists every member's entries;
  - `unbind`, and the bind type rules.

  Afterwards the main thread checks that none of those binds is visible
  in its own namespace, that `ramfs_destroy` finds no leaked vnode
  references (it panics if any remain), and that heap usage is back to
  its starting value.
- **The FAT volumes scenario in `tests/console_test.py`** builds a
  16 MiB disk with mtools: a FAT16 volume (512-byte clusters) and a
  FAT12 volume (4 KiB clusters). It then checks, through the monitor:
  - listings, including a 21-entry subdirectory spanning clusters;
  - `cat` with case-insensitive paths, and a long-name alias;
  - byte-exact FNV-1a checksums against the host of a **fragmented**
    file on each volume (the generator asserts they really are
    fragmented) and of a 50 KB multi-cluster file;
  - devfs byte reads of a whole disk and a partition;
  - error messages;
  - a full union sequence: `bind -a`, then `bind -b` on top (`ns` shows
    `fat:ata0p2 fat:ata0p1 fat:ata0p2`), then `unbind`, then a
    replacing bind.
- **Negative controls** (temporary sabotage, reverted afterward), each
  caught:
  - FAT12 odd/even entries swapped → only the FAT12 checksum fails;
  - clusters assumed contiguous → both fragmented files fail while the
    contiguous one still passes;
  - unions returning the last match → *bind -a searches the original
    first*;
  - `ns_fork` sharing instead of copying → *a private namespace's binds
    leaked into its parent's*;
  - `vfs_close` dropping its references → *vnode reference leak*.
- On an 8 MiB machine the 50 KB checksum still matches, with 3.8 KB of
  heap in use. The kernel also boots under GRUB with both volumes
  mounted.
- The console test's first run failed two checks because its checksum
  cases ran *after* the union tests had rebound `/n/ata0p1`. That was a
  test ordering bug; the generated read-only cases now run first.

## Known limits

- Read-only everywhere: no file creation or writes on FAT, and ramfs
  holds only directories.
- No long filenames, no FAT32, and 32-bit file offsets.
- The monitor has no current directory, so paths are absolute.
- Mounts are keyed by path, not qid (see above).
- Directory listing rescans the directory for each entry (O(n²)). That
  is fine at FAT12/16 directory sizes.
