# M19 — ManiDOS: a Disk Operating System of ManiOS's Own

**Status:** Achieved (2026-09-26). Released as **0.19.0**. Asked for as
"if we use a command DOS, we getting in some sort of dos or disk
operating system -- do not, and please do not use MS DOS". `dos` starts
ManiDOS: a DOS-style disk operating system written for ManiOS, with no
MS-DOS code and no FreeDOS code ([ADR-0008](../adr/0008-manidos.md)).
The guide is [docs/dos.md](../dos.md).

## What M19 delivers

| Piece | Files | Summary |
|---|---|---|
| The command line | `userland/dos/main.c` | The prompt (`PROMPT`'s `$` codes), `%VARIABLES%`, pipelines with `\|`, `<`, `>` and `>>`, the environment, running batch files and ManiOS programs (looked for in the current directory, then the PATH) with DOS paths in their arguments made ManiOS paths, errorlevels; `dos /C` |
| Drives and paths | `userland/dos/drives.c` | A: the boot disk (`/boot`), C: onwards the FAT volumes under `/n`, Z: all of ManiOS; a current directory per drive; paths made absolute and never above a drive's root; DOS wildcards |
| The commands | `userland/dos/commands.c` | `DIR` (/W /B /P /S), `CD`, `TYPE`, `COPY` (to CON, NUL, devices), `VOL`, `CHKDSK`, `TREE`, `FIND`, `SORT`, `MORE`, `ECHO`, `SET`, `PATH`, `PROMPT`, `CLS`, `VER`, `MEM`, `DATE`, `TIME`, `HELP`, `DRIVES`, `TRUENAME`; the writing ones refuse, the drives being read-only |
| Batch files | `userland/dos/batch.c` | `%0`–`%9`, `SHIFT`, `@`, `ECHO OFF`, `:LABEL` and `GOTO` (and `GOTO :EOF`), `IF [NOT] ERRORLEVEL / EXIST / ==`, `FOR ... IN (...) DO` with wildcards, `CALL`, chaining; `A:\AUTOEXEC.BAT` at start |
| The disk tools | `userland/dos/fat.c` | FAT12/16 read from the disk itself: label (root directory, then boot sector), serial number, free space; CHKDSK: every chain followed and checked against the FAT |
| Booting into it | `zkt/kernel/monitor.c` | `shell=PROGRAM` on the command line: what the console runs instead of `sh` (`shell=dos`) |
| In ManiDE | `desktop/manide/main.c` | F1's menu has ManiDOS (`term dos`) |

## Design decisions

**Ours, not MS-DOS.** MS-DOS was ruled out by the request itself, and
FreeDOS by ManiOS's rule against GPL code, so ManiDOS is written from
nothing, taking from DOS only the way of working: command names, their
output's shape, drive letters, batch files. It is a program (`/bin/dos`)
on ManiOS, not a second kernel; it sees the same files as ManiOS's own
shell, so a FAT volume is at once `C:\` and `/n/ata0p1`.

**Drive letters are places in the namespace.** A: is the boot disk
(where `AUTOEXEC.BAT` is), C: and on are the FAT volumes ManiOS mounted
(in the order of their names under `/n`), and Z: is `/`, the whole of
ManiOS -- where the PATH (`Z:\BIN`) finds its programs and `Z:\DEV` its
devices. A DOS path is made absolute lexically (`..` never leaves the
drive's root) and lower-cased for ManiOS: FAT names are found whatever
their case, and ManiOS's own names are lower case.

**ManiOS programs, with DOS paths.** A program started from ManiDOS
starts in the current DOS directory, and an argument that is a DOS path
becomes a ManiOS one -- if it has a drive letter, or names something
that exists. The second rule is a guess about intent (a `grep` pattern
that happens to name a file would be turned into a path); DOS users
type relative names, and without it `head DOCS\NOTES.TXT` couldn't
work. `.COM` and `.EXE` files are looked for too, so that a DOS program
is refused by name rather than "Bad command or file name".

**Pipelines with real pipes.** DOS joined commands through temporary
files; ManiOS has pipes. The last command of a pipeline runs in ManiDOS
itself (so `| MORE` can read the keyboard while its input is the pipe);
an internal command earlier in the pipeline runs in a second ManiDOS
(`dos /C`), which finds its drive and directory from the ManiOS
directory it is started in.

**CHKDSK reads the disk.** ManiOS's file system shows files, not
clusters, so `fat.c` reads the boot sector, the FATs and the directories
from the partition's device (`/dev/ata0p1`) and walks every chain:
cross-links (a cluster reached twice), files whose sizes don't match
their chains, chains that run into free or bad clusters or out of the
disk, lost clusters (used in the FAT, reached by nothing) counted in
chains, and FAT copies that differ. It reports; it can't correct, the
drives being read-only.

**Read-only, said plainly.** ManiOS can't write files yet, so `DEL`,
`REN`, `MD`, `RD`, `COPY` to a file and `> FILE` answer "Access denied -
ManiOS's drives are read-only", and `FORMAT`, `FDISK`, `LABEL` and `SYS`
say they aren't in ManiDOS. Output can go to `CON`, `NUL` or a device.

**The terminal's size, only where asked.** `DIR /W`, `DIR /P` and
`MORE` need the screen's size. In ManiDE's terminal ManiDOS asks it
(`ESC [ 18 t`, answered by `term`); on the console and a serial line it
doesn't -- a serial terminal's answer would arrive as typing -- and
takes 80x24.

## Bugs found on the way

- `DEL` crashed ManiDOS: the refusal message upper-cased the command's
  name in place, and that name is a string constant (read-only memory).
- `NOPE` (an unknown command) hung: the PATH search and the path
  resolver it calls both used `strtok`, whose one hidden position the
  inner call moved to its own, since-gone buffer. All of ManiDOS now
  uses `strtok_r`.
- `ECHO hi > CON` printed "hi " -- the blank before `>` stayed; a
  command's text is now trimmed after its redirections are taken out.
- `GAME` didn't find `GAME.EXE` to refuse it: .COM and .EXE are now
  looked for.

## ManiOS needs 7 MiB now

The boot image grew by 52 KiB, mostly ManiDOS, and ManiOS no longer fits a
6 MiB machine: it needs 6,022 KiB (its images up to where the boot area
ends, and 2 MiB to run in), and QEMU's 6 MiB machine has 6,016 KiB free
-- its BIOS keeps 128 KiB at the top. The loader's refusal, "ManiOS
needs 6 MiB", was true (6,022 KiB rounded up) but read as nonsense on
a 6 MiB machine; it now adds what the machine has: "ManiOS needs 6 MiB;
this machine has 6016 KiB". The install test boots a 7 MiB machine, and
checks that message on a 6 MiB one.

## Verification

- **`tests/dos_test.py`** (new, in `make test`): 27 steps at ManiDOS's
  prompt over the serial line, on a disk built for it with mtools (see
  `tests/README.md`), including CHKDSK on a FAT12 volume with a lost
  chain, a cross-link, a file longer than its chain, and FAT copies that
  differ -- each reported, with the figures checked against the image
  and against mtools -- a batch file using everything batch files have,
  and a machine booted with `shell=dos`.
- **`tests/desktop_test.py`**: ManiDE's menu with ManiDOS in it.
- **`tests/install_test.py`**: a 7 MiB machine boots the CD; a 6 MiB
  machine is refused with what ManiOS needs and what the machine has.
- `make test`: 397 checks (370 at 0.18), none failing.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | The PATH search and the path resolver both on `strtok` again | "NOPE" hangs (a two-file sabotage: either alone is harmless, which a first, one-file try showed) |
  | CHKDSK doesn't count lost clusters | CHKDSK D: |
  | CHKDSK doesn't notice cross-links | CHKDSK D: |
  | CHKDSK doesn't compare the FAT copies | CHKDSK D: |
  | CHKDSK doesn't compare sizes with chains | CHKDSK D: |
  | Free space counts chains' last clusters as free | DIR's bytes free, against mtools (a first try, counting entries 1 and 2 as free, changed nothing on this disk and wasn't caught) |
  | The root directory's label is ignored | DIR's label (the boot sector's is "NO NAME") |
  | The serial number's halves swapped | DIR's serial number, against the boot sector |
  | `?` matches only itself | `DIR /B ????.TXT` |
  | `..` goes above a drive's root | `CD DOCS\..\..\..\DOCS` |
  | GOTO doesn't jump | the batch file |
  | SHIFT does nothing | the batch file |
  | CALL doesn't come back | the batch file |
  | ECHO OFF outlives AUTOEXEC.BAT | the prompt never shows |
  | Arguments aren't made ManiOS paths | `wc README.TXT` |
  | MORE doesn't stop at a screenful | MORE |
  | A program's exit status is lost | `IF ERRORLEVEL 1` after `grep` |
  | The kernel ignores `shell=` | the machine booted with `shell=dos` |
  | The loader doesn't say what the machine has | `install_test`: the 6 MiB machine's message |

## Known limits

See [docs/dos.md](../dos.md#differences-from-dos): read-only drives, no
DOS programs, no dates in `DIR`, no Ctrl+C, and `PAUSE` and `MORE` wait
for Enter.
