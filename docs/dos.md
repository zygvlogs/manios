# ManiDOS

ManiDOS is a disk operating system for ManiOS: type `dos` and you are
at a `C:\>` prompt, with drive letters, `DIR`, `CD`, `TYPE`, `CHKDSK`,
batch files and an `AUTOEXEC.BAT`. It is ManiOS's own, written for
ManiOS from nothing: **it is not MS-DOS, it contains no MS-DOS code,
and it doesn't run MS-DOS** (nor FreeDOS, whose GPL license ManiOS
doesn't take in). What it takes from DOS is the way of working: the
commands' names, their output, drive letters and batch files.
[ADR-0008](adr/0008-manidos.md) records why.

(The disk in this guide's examples is the one `tests/dos_test.py`
makes; its D: has faults put in it on purpose.)

```
manios% dos

ManiDOS 1.0 -- a disk operating system for ManiOS
Drives:  A: boot disk  C: ata0p1  D: ata0p2  Z: ManiOS
Type HELP for the commands, EXIT to go back to ManiOS.

C:\>DIR
 Volume in drive C is MANIDOS
 Volume Serial Number is 230B-9F38
 Directory of C:\

README   TXT            56
NAMES    TXT            30
LONG     TXT           360
TEST     BAT           498
SUB      BAT            21
GAME     EXE            64
DOCS            <DIR>
        6 file(s)          1,029 bytes
        1 dir(s)       8,301,568 bytes free

C:\>
```

## Starting it

| How | |
|---|---|
| `dos` | at ManiOS's shell, or in a ManiDE terminal |
| F1 → ManiDOS | ManiDE's menu (a terminal running `dos`) |
| `shell=dos` | on the boot command line: ManiOS starts in ManiDOS instead of its shell (EXIT then leaves you at the `ZKT>` monitor) |
| `dos /C COMMAND` | runs one command line and exits with its errorlevel |

At its start ManiDOS runs `A:\AUTOEXEC.BAT` (`/boot/autoexec.bat`), which
sets the prompt and the PATH. `EXIT` goes back to ManiOS.

## Drives

Drive letters are places in ManiOS's namespace:

| Drive | Is | In ManiOS |
|---|---|---|
| A: | the boot disk: ManiOS's programs, `AUTOEXEC.BAT` | `/boot` |
| C:, D:, ... | the FAT disks and partitions ManiOS found, in order | `/n/ata0p1`, `/n/ata0p2`, ... |
| Z: | all of ManiOS: `Z:\BIN`, `Z:\DEV`, `Z:\N` ... | `/` |

ManiDOS starts on C: if there is a FAT disk, otherwise on A:. `DRIVES`
lists them; `TRUENAME PATH` shows where a DOS path is in ManiOS
(`C:\DOCS = /n/ata0p1/docs`). Each drive has its own current directory,
as in DOS (`CD D:\X` changes D:'s without leaving C:).

Names are what DOS's are: case doesn't matter, `\` separates, and
`*` and `?` are wildcards (`*.*` is everything). `..` goes up and never
above a drive's root. ManiOS's FAT support reads long file names as
their 8.3 aliases (`MANIOS~1.TXT`).

## Commands

| Command | |
|---|---|
| `DIR [PATH] [/W] [/B] [/P] [/S]` | lists a directory: wide, bare, a screen at a time, subdirectories too |
| `CD [D:][PATH]`, `CHDIR` | shows or changes the current directory (`CD..`, `CD\`) |
| `D:` | makes drive D current |
| `TYPE FILES` | shows files |
| `COPY FILES CON\|NUL\|DEVICE` | copies files to the screen, to nothing, or to a device (`Z:\DEV\...`) |
| `VOL [D:]` | a disk's label and serial number |
| `CHKDSK [D:] [/V]` | checks a FAT disk (below) |
| `TREE [PATH] [/F]` | the directory tree, with the files (/F) |
| `FIND [/V] [/C] [/N] [/I] "TEXT" [FILES]` | lines with (/V: without) TEXT; count, line numbers, any case; errorlevel 1 when nothing is found |
| `SORT [/R] [/+N] [FILE]` | sorts lines (any case), in reverse, from column N |
| `MORE [FILES]` | a screen at a time; also `COMMAND \| MORE` |
| `ECHO [ON\|OFF\|TEXT]`, `ECHO.` | text, a blank line, command echoing |
| `SET [NAME=[VALUE]]` | the environment; `%NAME%` in a command line is its value |
| `PATH [DIRS]`, `PATH ;` | where programs are looked for (`Z:\BIN`) |
| `PROMPT [TEXT]` | the prompt: `$P` path, `$G` >, `$N` drive, `$D` date, `$T` time, `$V` version, `$_` new line, `$$` $ |
| `CLS`, `VER`, `MEM`, `DATE`, `TIME` | the screen, the version, memory, the date and time (UTC) |
| `DRIVES`, `TRUENAME` | ManiDOS's own: the drives, and a path in ManiOS |
| `REM`, `PAUSE`, `CALL`, `GOTO`, `IF`, `FOR`, `SHIFT` | batch files (below), `IF` and `FOR` at the prompt too |
| `HELP [COMMAND]`, `EXIT` | |

A word that isn't a command is a batch file (`NAME.BAT`) or a ManiOS
program, looked for in the current directory and then on the PATH.

## Running ManiOS programs

Every ManiOS program is on the PATH (`Z:\BIN`): `grep`, `sed`, `wc`,
`fetch`, `top`, `manide`... A program starts in the current directory,
and its arguments that are DOS paths are given to it as ManiOS paths: a
word with a drive letter (`C:\LONG.TXT` becomes `/n/ata0p1/long.txt`),
or one that names a file or directory that exists (`DOCS\NOTES.TXT`).
Its exit status is the errorlevel.

```
C:\>grep -c line C:\LONG.TXT
40

C:\>wc README.TXT
      2       9      56 /n/ata0p1/readme.txt
```

ManiDOS runs batch files and ManiOS programs; it doesn't run DOS
programs. A `.COM` or `.EXE` is found, and refused by name.

## Pipes and redirection

`|` joins commands; `< FILE` reads a file; `> FILE` and `>> FILE` write
one. ManiOS's drives are read-only for now, so output goes to `CON`,
`NUL` or a device (`> Z:\DEV\NULL`); to a file it is refused. An
internal command in the middle of a pipeline runs in a second ManiDOS
(`dos /C`), which starts in the same directory.

```
C:\>TYPE NAMES.TXT | SORT /R | MORE
C:\>DIR /B | FIND "BAT"
```

## Batch files

A `.BAT` file's lines run one by one. `%0` is its name as typed, `%1`
to `%9` its parameters (`SHIFT` moves them down), `%NAME%` a variable.
`@` at a line's start, or `ECHO OFF`, keeps lines from being shown.
`:LABEL` marks a line for `GOTO LABEL` (`GOTO :EOF` ends the file).

```
@ECHO OFF
IF "%1"=="" GOTO USAGE
IF EXIST %1 TYPE %1
IF NOT EXIST %1 ECHO no %1
FOR %%F IN (DOCS\*.TXT) DO ECHO found %%F
CALL OTHER.BAT
FIND "x" %1 > NUL
IF ERRORLEVEL 1 ECHO no x in %1
GOTO :EOF
:USAGE
ECHO usage: SHOW FILE
```

`IF [NOT] ERRORLEVEL N` is true when the last command's errorlevel is
N or more; `IF [NOT] EXIST NAME` (wildcards allowed); `IF [NOT] A==B`
compares two words as they are. `FOR %%V IN (SET) DO COMMAND` runs the
command for each word of the set, wildcards made file names (at the
prompt it is `%V`). A batch file run from another without `CALL` takes
its place; with `CALL`, the first carries on after it. CR LF line ends,
as DOS writes them, and ManiOS's LF both work.

## The disk tools

`VOL`, `DIR` and `CHKDSK` read a FAT disk's boot sector and FAT from the
disk itself (`/dev/ata0p1`...), for what ManiOS's file system doesn't
say: the volume label (the root directory's, as DOS has it, or the boot
sector's), the serial number, and free space.

`CHKDSK` follows every file's and directory's cluster chain and checks
it against the FAT. It reports:

- a file cross-linked with another (two chains through one cluster);
- a file whose size doesn't match its chain ("Allocation error");
- a chain that runs into a free or bad cluster, or out of the disk;
- lost allocation units: clusters the FAT says are in use that no file
  or directory has, counted in chains;
- FAT copies that differ.

and then the figures: total disk space, bytes in directories, in user
files, in lost chains, available; the allocation unit's size, their
number, how many are free. ManiDOS doesn't correct what it finds:
ManiOS's drives are read-only (`CHKDSK /F` says so, and checks).

```
C:\>CHKDSK D:
Volume Serial Number is 230B-9F38
D:\CROSS2.TXT  Is cross-linked on allocation unit 3
D:\CROSS2.TXT  Allocation error: its size needs 1 allocation units, its chain has 0
D:\SHORT.TXT  Allocation error: its size needs 3 allocation units, its chain has 1
3 lost allocation units found in 2 chains.
The copies of the file allocation table differ.

5 problems found. ManiDOS doesn't correct them: ManiOS's drives are read-only.

    4,173,824 bytes total disk space
            0 bytes in 0 directories
       12,288 bytes in 4 user files
       12,288 bytes in lost chains
    4,149,248 bytes available on disk

        4,096 bytes in each allocation unit
        1,019 total allocation units on disk
        1,013 available allocation units on disk
```

## Differences from DOS

- The drives are read-only: `DEL`, `REN`, `MD`, `RD`, `COPY` to a file
  and `> FILE` say "Access denied"; `FORMAT`, `FDISK`, `LABEL` and
  `SYS` aren't there. ManiOS can't write files yet.
- No DOS programs (`.COM`, `.EXE`): ManiDOS runs ManiOS programs.
- `DIR` shows no dates: ManiOS's files have no times yet.
- `DATE` and `TIME` show the clock (UTC) but don't set it.
- No Ctrl+C: ManiOS has no signals yet. A program runs until it ends.
- `PAUSE` and `MORE` wait for Enter, not any key: ManiOS's terminals
  send whole lines.
- `DRIVES` and `TRUENAME`'s ManiOS path are ManiDOS's own.

The source is in [`userland/dos/`](../userland/dos/): `main.c` (the
command line), `drives.c` (drive letters and paths), `commands.c`,
`batch.c`, and `fat.c` (the disk tools).
