# Third-Party Notices

This file is the aggregate ledger of every BSD-derived (not merely
BSD-*inspired*) import in the ManiOS tree, per
[`docs/FOUNDING-PROPOSAL.md` §3.3](../docs/FOUNDING-PROPOSAL.md#33-process-for-any-bsd-derived-import).
Every entry here must correspond to a file (or set of files) under
`third_party/` (or, once imported/adapted, wherever it lands in the
tree) whose original copyright header and license text have been
preserved verbatim.

The licenses also require the notices to go with the binaries: the
build collects every imported file's header into `/boot/etc/notices`
(`tools/mknotices.py`), which is in the boot archive of every ManiOS
build and published with each release as `NOTICES.txt`.

## Entries

The first imports, 0.16 (2026-09-26): programs and C library functions
from OpenBSD, all from OpenBSD's `src` at commit
`3d80b15b1ec664bb0fc72f48e55e5488a147aa50`, fetched file by file.

## OpenBSD basename(1)

- **Origin:** OpenBSD, `src/usr.bin/basename/basename.c`, revision 1.14
  (2016/10/28), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1991, 1993, 1994
- **Location in this repo:**
  `third_party/openbsd/usr.bin/basename/basename.c`; built into
  `/bin/basename`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:**
  `third_party/openbsd/usr.bin/basename/basename.c` (top of the file),
  and in `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD comm(1)

- **Origin:** OpenBSD, `src/usr.bin/comm/comm.c`, revision 1.11
  (2022/12/04), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1989, 1993, 1994
- **Location in this repo:** `third_party/openbsd/usr.bin/comm/comm.c`;
  built into `/bin/comm`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/comm/comm.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD cut(1)

- **Origin:** OpenBSD, `src/usr.bin/cut/cut.c`, revision 1.28
  (2023/03/08), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1989, 1993
- **Location in this repo:** `third_party/openbsd/usr.bin/cut/cut.c`;
  built into `/bin/cut`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/cut/cut.c` (top
  of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD dirname(1)

- **Origin:** OpenBSD, `src/usr.bin/dirname/dirname.c`, revision 1.17
  (2019/01/25), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC; Copyright (c) 1997 Todd C. Miller
  <millert@openbsd.org>
- **Location in this repo:**
  `third_party/openbsd/usr.bin/dirname/dirname.c`; built into
  `/bin/dirname`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:**
  `third_party/openbsd/usr.bin/dirname/dirname.c` (top of the file), and
  in `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD expand(1)

- **Origin:** OpenBSD, `src/usr.bin/expand/expand.c`, revision 1.15
  (2022/12/04), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1980, 1993
- **Location in this repo:**
  `third_party/openbsd/usr.bin/expand/expand.c`; built into
  `/bin/expand`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/expand/expand.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD fold(1)

- **Origin:** OpenBSD, `src/usr.bin/fold/fold.c`, revision 1.18
  (2016/05/23), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1990, 1993
- **Location in this repo:** `third_party/openbsd/usr.bin/fold/fold.c`;
  built into `/bin/fold`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/fold/fold.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD head(1)

- **Origin:** OpenBSD, `src/usr.bin/head/head.c`, revision 1.24
  (2022/02/07), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1980, 1987 Regents of the University of
  California.
- **Location in this repo:** `third_party/openbsd/usr.bin/head/head.c`;
  built into `/bin/head`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/head/head.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD paste(1)

- **Origin:** OpenBSD, `src/usr.bin/paste/paste.c`, revision 1.27
  (2022/12/04), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1989 The Regents of the University of
  California.
- **Location in this repo:**
  `third_party/openbsd/usr.bin/paste/paste.c`; built into `/bin/paste`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/paste/paste.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD rev(1)

- **Origin:** OpenBSD, `src/usr.bin/rev/rev.c`, revision 1.16
  (2022/02/08), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1987, 1992, 1993
- **Location in this repo:** `third_party/openbsd/usr.bin/rev/rev.c`;
  built into `/bin/rev`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/rev/rev.c` (top
  of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD uniq(1)

- **Origin:** OpenBSD, `src/usr.bin/uniq/uniq.c`, revision 1.33
  (2022/01/01), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1989, 1993
- **Location in this repo:** `third_party/openbsd/usr.bin/uniq/uniq.c`;
  built into `/bin/uniq`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/uniq/uniq.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD yes(1)

- **Origin:** OpenBSD, `src/usr.bin/yes/yes.c`, revision 1.9
  (2015/10/13), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1987, 1993
- **Location in this repo:** `third_party/openbsd/usr.bin/yes/yes.c`;
  built into `/bin/yes`
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/usr.bin/yes/yes.c` (top
  of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD strtonum(3)

- **Origin:** OpenBSD, `src/lib/libc/stdlib/strtonum.c`, revision 1.8
  (2015/09/13), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC; Copyright (c) 2004 Ted Unangst and Todd Miller
- **Location in this repo:**
  `third_party/openbsd/lib/libc/stdlib/strtonum.c`; built into ManiOS's
  libc
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:**
  `third_party/openbsd/lib/libc/stdlib/strtonum.c` (top of the file),
  and in `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD reallocarray(3)

- **Origin:** OpenBSD, `src/lib/libc/stdlib/reallocarray.c`, revision
  1.3 (2015/09/13), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC; Copyright (c) 2008 Otto Moerbeek <otto@drijf.net>
- **Location in this repo:**
  `third_party/openbsd/lib/libc/stdlib/reallocarray.c`; built into
  ManiOS's libc
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:**
  `third_party/openbsd/lib/libc/stdlib/reallocarray.c` (top of the
  file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD basename(3)

- **Origin:** OpenBSD, `src/lib/libc/gen/basename.c`, revision 1.17
  (2020/10/20), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC; Copyright (c) 1997, 2004 Todd C. Miller
  <millert@openbsd.org>
- **Location in this repo:**
  `third_party/openbsd/lib/libc/gen/basename.c`; built into ManiOS's
  libc
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/lib/libc/gen/basename.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD dirname(3)

- **Origin:** OpenBSD, `src/lib/libc/gen/dirname.c`, revision 1.17
  (2020/10/20), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC; Copyright (c) 1997, 2004 Todd C. Miller
  <millert@openbsd.org>
- **Location in this repo:**
  `third_party/openbsd/lib/libc/gen/dirname.c`; built into ManiOS's libc
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/lib/libc/gen/dirname.c`
  (top of the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## OpenBSD queue(3) macros, `<sys/queue.h>`

- **Origin:** OpenBSD, `src/sys/sys/queue.h`, revision 1.47
  (2026/06/12), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license); Copyright (c) 1991, 1993
- **Location in this repo:** `third_party/openbsd/sys/sys/queue.h`;
  built into `paste`, which includes it
- **Modifications:** none to the file; a ManiOS note added under its
  license header. ManiOS's libc provides what it needs
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** `third_party/openbsd/sys/sys/queue.h` (top of
  the file), and in `/boot/etc/notices` in every build
  (`tools/mknotices.py`)

## Entry format

```
## <Component name>

- **Origin:** <project name>, <file path>, <commit/revision or release>
- **License:** <exact license, e.g. BSD-2-Clause, BSD-3-Clause, ISC>
- **Location in this repo:** <path(s)>
- **Modifications:** <summary of what ManiOS changed>
- **Notice preserved at:** <path to the file whose header carries the
  original copyright/license text>
```
