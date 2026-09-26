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

The second imports, 0.17 (2026-09-26): more programs, and parts of
OpenBSD's libc, from the same commit.

## OpenBSD expr(1)

- **Origin:** OpenBSD, `src/bin/expr/expr.c` 1.28 (2022/01/28), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** Public domain (J.T. Conklin); no conditions
- **Location in this repo:** `third_party/openbsd/bin/expr/expr.c`;
  built into `/bin/expr`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD test(1)

- **Origin:** OpenBSD, `src/bin/test/test.c` 1.23 (2025/03/24), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** Public domain (Erik Baalbergen, Eric Gisin, Arnold
  Robbins, J.T. Conklin); no conditions
- **Location in this repo:** `third_party/openbsd/bin/test/test.c`;
  built into `/bin/test`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD cmp(1)

- **Origin:** OpenBSD, `src/usr.bin/cmp/cmp.c` 1.19 (2021/10/24),
  `src/usr.bin/cmp/extern.h` 1.6 (2018/03/05), `src/usr.bin/cmp/misc.c`
  1.7 (2018/03/05), `src/usr.bin/cmp/regular.c` 1.13 (2021/01/09),
  `src/usr.bin/cmp/special.c` 1.8 (2018/03/05), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/cmp/`; built
  into `/bin/cmp`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD col(1)

- **Origin:** OpenBSD, `src/usr.bin/col/col.c` 1.21 (2026/05/20), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/col/col.c`;
  built into `/bin/col`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD colrm(1)

- **Origin:** OpenBSD, `src/usr.bin/colrm/colrm.c` 1.14 (2022/12/04), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:**
  `third_party/openbsd/usr.bin/colrm/colrm.c`; built into `/bin/colrm`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD column(1)

- **Origin:** OpenBSD, `src/usr.bin/column/column.c` 1.27 (2022/12/26),
  at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:**
  `third_party/openbsd/usr.bin/column/column.c`; built into
  `/bin/column`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD fmt(1)

- **Origin:** OpenBSD, `src/usr.bin/fmt/fmt.c` 1.39 (2018/10/18), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC for OpenBSD's changes (Ingo Schwarze, Paul Janzen);
  Gareth McCaughan's permissive license for the original (source
  redistributions keep the notice, and modified source says what
  changed: ManiOS's note does)
- **Location in this repo:** `third_party/openbsd/usr.bin/fmt/fmt.c`;
  built into `/bin/fmt`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD grep(1)

- **Origin:** OpenBSD, `src/usr.bin/grep/binary.c` 1.20 (2021/12/15),
  `src/usr.bin/grep/file.c` 1.17 (2021/12/15), `src/usr.bin/grep/grep.c`
  1.68 (2025/05/09), `src/usr.bin/grep/grep.h` 1.29 (2022/06/26),
  `src/usr.bin/grep/mmfile.c` 1.19 (2019/01/27),
  `src/usr.bin/grep/queue.c` 1.7 (2015/01/16), `src/usr.bin/grep/util.c`
  1.68 (2023/11/15), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-2-Clause (James Howard and Dag-Erling Smørgrav)
- **Location in this repo:** `third_party/openbsd/usr.bin/grep/`; built
  into `/bin/grep`, built with OpenBSD's `NOZ` (no zlib, no `-Z`)
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD join(1)

- **Origin:** OpenBSD, `src/usr.bin/join/join.c` 1.34 (2022/12/04), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/join/join.c`;
  built into `/bin/join`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD lam(1)

- **Origin:** OpenBSD, `src/usr.bin/lam/lam.c` 1.24 (2021/12/03),
  `src/usr.bin/lam/utf8.c` 1.1 (2018/07/29), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license) for lam.c; ISC (Ingo Schwarze) for utf8.c
- **Location in this repo:** `third_party/openbsd/usr.bin/lam/lam.c`,
  `third_party/openbsd/usr.bin/lam/utf8.c`; built into `/bin/lam`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD nl(1)

- **Origin:** OpenBSD, `src/usr.bin/nl/nl.c` 1.8 (2022/12/04), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-2-Clause (The NetBSD Foundation)
- **Location in this repo:** `third_party/openbsd/usr.bin/nl/nl.c`;
  built into `/bin/nl`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD sed(1)

- **Origin:** OpenBSD, `src/usr.bin/sed/compile.c` 1.54 (2026/05/13),
  `src/usr.bin/sed/defs.h` 1.11 (2024/07/17), `src/usr.bin/sed/extern.h`
  1.16 (2024/07/17), `src/usr.bin/sed/main.c` 1.47 (2024/07/17),
  `src/usr.bin/sed/misc.c` 1.13 (2024/07/17),
  `src/usr.bin/sed/process.c` 1.39 (2024/12/10), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (Diomidis Spinellis's and the University of
  California's)
- **Location in this repo:** `third_party/openbsd/usr.bin/sed/`; built
  into `/bin/sed`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD tail(1)

- **Origin:** OpenBSD, `src/usr.bin/tail/extern.h` 1.13 (2019/01/04),
  `src/usr.bin/tail/forward.c` 1.33 (2019/06/28),
  `src/usr.bin/tail/misc.c` 1.9 (2015/11/19), `src/usr.bin/tail/read.c`
  1.20 (2017/03/26), `src/usr.bin/tail/reverse.c` 1.21 (2015/11/19),
  `src/usr.bin/tail/tail.c` 1.24 (2022/12/04), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/tail/`; built
  into `/bin/tail`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD tee(1)

- **Origin:** OpenBSD, `src/usr.bin/tee/tee.c` 1.15 (2023/03/04), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/tee/tee.c`;
  built into `/bin/tee`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD tr(1)

- **Origin:** OpenBSD, `src/usr.bin/tr/extern.h` 1.6 (2003/06/03),
  `src/usr.bin/tr/str.c` 1.15 (2023/05/04), `src/usr.bin/tr/tr.c` 1.22
  (2022/12/04), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/tr/extern.h`,
  `third_party/openbsd/usr.bin/tr/str.c`,
  `third_party/openbsd/usr.bin/tr/tr.c`; built into `/bin/tr`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD tsort(1)

- **Origin:** OpenBSD, `src/usr.bin/tsort/tsort.c` 1.41 (2026/06/26), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC (Marc Espie)
- **Location in this repo:**
  `third_party/openbsd/usr.bin/tsort/tsort.c`; built into `/bin/tsort`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD unexpand(1)

- **Origin:** OpenBSD, `src/usr.bin/unexpand/unexpand.c` 1.13
  (2016/10/11), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:**
  `third_party/openbsd/usr.bin/unexpand/unexpand.c`; built into
  `/bin/unexpand`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD unvis(1)

- **Origin:** OpenBSD, `src/usr.bin/unvis/unvis.c` 1.15 (2022/12/04), at
  commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:**
  `third_party/openbsd/usr.bin/unvis/unvis.c`; built into `/bin/unvis`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD vis(1)

- **Origin:** OpenBSD, `src/usr.bin/vis/foldit.c` 1.8 (2020/08/17),
  `src/usr.bin/vis/vis.c` 1.22 (2022/12/04), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/usr.bin/vis/foldit.c`,
  `third_party/openbsd/usr.bin/vis/vis.c`; built into `/bin/vis`
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD regex(3), the regular expression library

- **Origin:** OpenBSD, `src/include/regex.h` 1.7 (2012/12/05),
  `src/lib/libc/regex/cclass.h` 1.7 (2020/12/30),
  `src/lib/libc/regex/cname.h` 1.6 (2020/12/30),
  `src/lib/libc/regex/engine.c` 1.26 (2020/12/28),
  `src/lib/libc/regex/regcomp.c` 1.44 (2022/12/27),
  `src/lib/libc/regex/regerror.c` 1.15 (2020/12/30),
  `src/lib/libc/regex/regex2.h` 1.12 (2021/01/03),
  `src/lib/libc/regex/regexec.c` 1.14 (2018/07/11),
  `src/lib/libc/regex/regfree.c` 1.11 (2015/12/28),
  `src/lib/libc/regex/utils.h` 1.4 (2003/06/02), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (Henry Spencer's and the University of
  California's)
- **Location in this repo:** `third_party/openbsd/lib/libc/regex/` and
  `third_party/openbsd/include/regex.h`; built into ManiOS's libc
  (`regexec.c` includes `engine.c`)
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD getopt(3) and getopt_long(3)

- **Origin:** OpenBSD, `src/include/getopt.h` 1.3 (2013/11/22),
  `src/lib/libc/stdlib/getopt_long.c` 1.32 (2020/05/27), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC (Todd C. Miller) and BSD-2-Clause (The NetBSD
  Foundation)
- **Location in this repo:** `third_party/openbsd/include/getopt.h`,
  `third_party/openbsd/lib/libc/stdlib/getopt_long.c`; built into
  ManiOS's libc, in place of its own getopt of 0.16
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD recallocarray(3)

- **Origin:** OpenBSD, `src/lib/libc/stdlib/recallocarray.c` 1.2
  (2021/03/18), at commit `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC (Otto Moerbeek)
- **Location in this repo:**
  `third_party/openbsd/lib/libc/stdlib/recallocarray.c`; built into
  ManiOS's libc
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD fts(3), walking directory trees

- **Origin:** OpenBSD, `src/include/fts.h` 1.14 (2012/12/05),
  `src/lib/libc/gen/fts.c` 1.61 (2021/11/29), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/include/fts.h`,
  `third_party/openbsd/lib/libc/gen/fts.c`; built into ManiOS's libc
  (`grep -r`)
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD vis(3) and unvis(3)

- **Origin:** OpenBSD, `src/include/vis.h` 1.15 (2015/07/20),
  `src/lib/libc/gen/unvis.c` 1.17 (2015/09/13), `src/lib/libc/gen/vis.c`
  1.26 (2022/05/04), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** BSD-3-Clause (the University of California's three-clause
  license)
- **Location in this repo:** `third_party/openbsd/include/vis.h`,
  `third_party/openbsd/lib/libc/gen/unvis.c`,
  `third_party/openbsd/lib/libc/gen/vis.c`; built into ManiOS's libc
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

## OpenBSD ohash(3), from libutil

- **Origin:** OpenBSD, `src/lib/libutil/ohash.c` 1.1 (2014/06/02),
  `src/lib/libutil/ohash.h` 1.2 (2014/06/02), at commit
  `3d80b15b1ec664bb0fc72f48e55e5488a147aa50`
- **License:** ISC (Marc Espie)
- **Location in this repo:** `third_party/openbsd/lib/libutil/ohash.c`,
  `third_party/openbsd/lib/libutil/ohash.h`; built into ManiOS's libc
  (`tsort`)
- **Modifications:** none to the files; a ManiOS note added under each
  license header. ManiOS's libc provides what they need
  ([third_party/openbsd/README.md](openbsd/README.md))
- **Notice preserved at:** the top of each file, and in
  `/boot/etc/notices` in every build (`tools/mknotices.py`)

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
