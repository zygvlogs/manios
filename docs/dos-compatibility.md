# DOS software compatibility

Last updated: **2026-10-22**. ManiOS version: **0.25.0**.

This list records tests of real DOS applications through `dosrun`. Results apply
only to the identified program version, ManiOS revision and test setup. ManiDOS's
built-in commands and synthetic interpreter tests are tracked separately.

**Five FreeDOS applications have verified Working results for the workflows
listed below.** This is task-specific coverage, not a general DOS compatibility
percentage. Checks & Balances is Partial; Warcraft remains Unsupported.

## Status definitions

| Status | Meaning |
| --- | --- |
| Working | A meaningful task completed with usable output; tested features and exceptions are recorded. |
| Partial | Some application functions work, but a normal workflow is incomplete. Reaching a menu alone does not qualify as Working. |
| Broken | Tested, but fails or cannot complete a basic workflow; the exact cause may still be unknown. |
| Unsupported | A required execution mode or subsystem is known to be absent. |
| Untested | No executable test has been performed; requirements alone do not establish compatibility. |

## Application list

| Application | Version / media | Status | Observed result | Last tested |
| --- | --- | --- | --- | --- |
| [Warcraft: Orcs & Humans](#warcraft-orcs--humans) | 1994 DOS CD; exact patch version unverified | **Unsupported** | Extracted `war.exe` requires a DOS/4GW protected-mode payload. Installer also reaches unsupported hardware port I/O. | 2026-10-22 |
| [Checks & Balances](#checks--balances) | 4.0g (1986), two 360 KB floppy images | **Partial** | Configuration saved; supplied DEMO data opens with balances and command menu. Ledger-entry save/reopen is unverified. | 2026-10-22 |
| [FreeDOS MORE](#freedos-more) | 4.3, FD14 BonusCD | **Working** | Help, exact short-file text, redirected stdin and interactive paging/quit verified. | 2026-10-22 |
| [FreeDOS FC](#freedos-fc) | FD14 BonusCD binary, dated 2005-01-09; version unverified | **Working** | Correct equal/different results for text files and `/B` binary comparisons. | 2026-10-22 |
| [FDBanner](#fdbanner) | 1.1, FD14 BonusCD | **Working** | BIOS text cells and cursor services draw the coloured FreeDOS banner; screenshot verified. | 2026-10-22 |
| [B64](#b64) | 0.06R (rev A), FD14 BonusCD | **Working** | Help (`-h`), Base64 encoding `ABCD` and decoding `QUJDRA==` produce correct files. | 2026-10-22 |
| [BSUM](#bsum) | 1.1a, FD14 BonusCD; help says 1.1 | **Working** | Correct checksums for two text files, an empty file and a 20 KiB binary file; missing-file error and help also verified. | 2026-10-22 |

## Warcraft: Orcs & Humans

- **Tested build:** ManiOS 0.25.0, source matching commit `5965873`
  (DOS interrupt-result and MZ-loader fixes).
- **Environment:** QEMU i386, CPU 486, 64 MiB RAM, serial console; supplied
  ISO attached as CD-ROM. Extracted executables were also copied to a FAT disk.
- **Commands:** `dosrun /n/cd0/install.exe`, `dosrun /n/cd0/war.exa`,
  `dosrun C:/WAR.EXE`.
- **Installer:** `INSTALL.EXE` stops at opcode `E6` (`OUT`) at `1010:4A4C`.
- **Packaging:** `WAR.EXA` and `DOS4GW.EXA` are LHA archives. The extracted
  `war.exe` is 315971 bytes and has an LE header at file offset `0x2998`.
- **Main executable:** before format detection, the stub did not finish in a
  20-second guest run. With detection, it promptly reports the missing
  32-bit protected-mode requirement.
- **Required work:** protected-mode CPU and DOS-extender support, plus the DOS
  and hardware services the game uses. Gameplay, input and audio are unverified.
- **Retest needed:** the later local arithmetic and PSP fixes used in the
  Checks & Balances test have not been used for a new Warcraft executable run.
  They do not implement its missing protected mode.
- **Evidence:** [guest transcript](compatibility/2026-10-22-warcraft.txt).
- **Input SHA256:**

```text
71744e8a133d99ee40fe78e804c1064ed7ae3adeb7b3da3ff00abd9aca761bcb  war.iso
de7d38ed9b00d5bc849dbf45189fe4674185064dd538d95f156e1b60f70514db  extracted war.exe
```

## Checks & Balances

- **Tested build:** ManiOS 0.25.0, `5965873` plus local, uncommitted corrections
  for full-page MZ headers, arithmetic destinations and the PSP memory-ending
  segment. These results are not results for the unmodified GitHub commit.
- **Environment:** QEMU i386, CPU 486, 64 MiB RAM, serial console; a fresh guest
  per program and a FAT hard-disk image in snapshot mode.
- **Media test:** the original floppies were attached read-only, but did not
  mount. QEMU reported 1.2 MB drives and ManiOS reported no disk. Application
  execution was therefore tested with copies of both disks' files on FAT.
- **Command:** `cd /n/ata0p1`, then `dosrun C:/CHECK.EXE`.
- **Main executable:** reports `can't run opcode FF (at 1050:D865)` before a
  usable UI. The underlying cause of this remaining failure is not isolated.
- **Companion programs:** `BLUE.EXE`, `BLACK.EXE` and `CPMV3TO4.EXE` print
  `Insufficient Memory` and do not return during a five-second wait plus
  Enter/Escape input and a further three-second wait. This is a bounded
  observation, not proof of an infinite hang.
- **Requirements:** the distribution note says `ANSI.SYS` is required. Its
  absence has not been established as the cause of the first failure.
- **Untested features:** accounting entry, save/reopen, overlays, reports,
  printing, and correct operation from the original floppy media.
- **Evidence:** [guest transcripts and tested-source hashes](compatibility/2026-10-22-checks-and-balances.txt).
- **Input SHA256:**

```text
39c1df04d178291b7a00bed0b9bb568d201f05ffc703ee0389bbb8de25a4f8ec  disk01.img
a124e41b20de65cd46bc8abfa08a08476a240ee5a536eb39f626af405f0a4eaf  disk02.img
```

## FreeDOS 1.4 BonusCD sample

The following original observations describe the pre-fix baseline. See the
[latest retest](#compatibility-fixes-and-retest) for current results.

These five programs came from the user-supplied `FD14-BonusCD.zip`, containing
`FD14BNS.iso` (volume `FD14-BONUS0`). They were executed **inside ManiOS**, not by
booting FreeDOS. No claim is made about the remaining programs on the CD.

- **Tested build:** the same local ManiOS 0.25.0 source used for the final
  Checks & Balances test: `5965873` plus uncommitted MZ-header, arithmetic and
  PSP-memory corrections. No DOS source changes were made during this batch.
- **Environment:** QEMU i386 10.0.13, CPU 486, 64 MiB RAM, serial console;
  binaries copied to a FAT16 disk, fresh guest and disk snapshot for each case.
  The working directory was `/n/ata0p1`; each command had a six-second limit.
- **Coverage:** 10 initial cases plus three BSUM argument/stdin follow-ups.
  Every invocation returned to the shell, but none completed its main task.
  Help/usage output and a normal return are not Working results.
- **Evidence:** [transcripts](compatibility/2026-10-22-freedos-bonus.txt),
  [machine-readable results, commands, fixtures and SHA256 hashes](compatibility/2026-10-22-freedos-bonus.json).
- **Formats:** MORE, FC and B64 are MZ executables containing a UPX signature;
  BSUM and FDBanner are small COM programs without that signature. Distributed
  binaries were tested unchanged. Failures in packed startup code do not establish
  how an unpacked build would behave, or that each reported opcode is a genuine
  application hardware requirement.

```text
59ef3c2a3011862f408f2f5dba725c08532674b99f0e16451e06cb6de73defa8  FD14-BonusCD.zip
e737a3b0baa0e93090da29c9c78134b82cb8415cbce29ce933d69ed02ab7712d  FD14BNS.iso
```

### FreeDOS MORE

Source: `/FDOS-x86/FREEDOS/BIN/MORE.EXE` (6942 bytes). Tested `/?` and
`C:/INPUT.TXT`, a short three-line file. Both report
`can't run opcode C1 (at 12A2:1142)`; no help or input text is displayed.
Immediate-count shift instruction support and the packed startup path need
investigation. Paging, keyboard controls and stdin mode remain untested.

### FreeDOS FC

Source: `/FDOS-x86/FREEDOS/BIN/FC.EXE` (14835 bytes). Compared a file with
itself and with a modified copy. Both report
`can't run opcode E5 (at 16E5:01F9)`. Neither equal nor different-file results
were obtained. The startup failure needs an instruction trace before assigning
its underlying cause.

### FDBanner

Source: `/FDOS-x86/FREEDOS/BIN/FDBANNER.COM` (475 bytes). Version 1.1 is from
the CD's `fdbanner.zip` package metadata. The program returns after only a blank
line; the [VGA screenshot](compatibility/2026-10-22-fdbanner.png) also shows no
FreeDOS banner. The executable uses BIOS `INT 10h` character output and cursor
services (AH=09/03/02), which are stubs in the tested runtime. A string-instruction
decoding issue also affects its LODSW startup. Neither completion nor absence of
an opcode fault establishes rendering compatibility.

### B64

Source: `/packages/util/b64.zip`, `UTIL/B64/B64.EXE` (11919 bytes).
Version 0.06R (rev A) is from package metadata. Tested `/?` and an encoding
attempt with `-e C:/RAW.TXT C:/ENC.TXT`. Both stop at
`can't run opcode 64 (at 145C:0003)`, before the intended result is available.
The input was four bytes, `ABCD`; expected Base64 was `QUJDRA==`. A follow-up
file read confirms `ENC.TXT` was not created. Decoding and successful argument
parsing have not been verified.

### BSUM

Source: `/packages/util/bsum.zip`, `bin/bsum.com` (256 bytes). The package
identifies version 1.1a; program usage text says 1.1.

The program can display its help/usage text and return, but repeats that text
for absolute and relative file arguments. Three checksum tasks therefore fail:

| Input bytes | Independently calculated BSD checksum | Observed |
| --- | --- | --- |
| `apple red\r\nbanana yellow\r\nApple green\r\n` | `3C36` | Usage, no checksum |
| `apple red\r\nbanana purple\r\nApple green\r\n` | `2C14` | Usage, no checksum |
| Empty file | `0000` | Usage, no checksum |

The checksum oracle rotates the 16-bit sum right by one bit, adds each byte,
and reduces modulo 65536. Input fixture bytes are recorded in the JSON evidence.
No-argument usage, a relative filename, and a relative filename with stdin
redirected to an empty file were also tried.

Disassembly shows `REPE SCASB` in argument parsing. The tested interpreter maps
its string opcode to an unhandled operation, so it does not scan or compare the
argument bytes. String-operation dispatch and repeat semantics are the next
investigation target; no corrected-runtime BSUM result is claimed here.

## Compatibility fixes and retest

The current local build includes the earlier MZ-header, ALU-destination and PSP
memory fixes, plus the following corrections:

- MOVS/CMPS/STOS/LODS/SCAS byte/word decoding, REP/REPE/REPNE, zero counts,
  direction, and segment overrides, including zero segments and prefix order.
- Correct default DS/SS selection for all 16-bit addressing forms; POPA stack
  handling; LAHF/SAHF; word ROL; immediate shifts; far CALL; and restoring IRET
  frames only once. Accumulator TEST is no longer swallowed as a string opcode.
- DAS and DAA decimal adjustments and INC/DEC auxiliary-carry results.
- PSP layout, environment termination and command lengths; conventional-memory
  MCB allocation, resizing, freeing, adjacent free blocks and failure reporting.
- Duplicate/forced-duplicate file handles, DOS 8.3 find-first/find-next with
  independent DTAs, current-directory queries and explicit LFN fallback.
- Legacy FCB open/create/close, sequential/random/block reads and writes,
  zero padding at partial-record EOF, record pointers and DTA bounds.
- BIOS text output/readback, attributes and cursor queries; nonblocking keyboard
  checks cache a byte until it is consumed. Enter maps to DOS carriage return.
  Console output preserves CR and LF as separate operations.

The same unchanged BonusCD binaries were tested in fresh ManiOS QEMU guests
(CPU 486, 64 MiB RAM, FAT16 snapshots). Sixteen cases cover:

| Program | Verified task and result |
| --- | --- |
| MORE 4.3 | Help, three-line file and redirected stdin reproduce the input lines. A separate 60-line test pauses after line 24, advances with Space+Enter, and quits with q+Enter. |
| FC | Equal text and binary files report `No differences`; modified text identifies `banana yellow` / `banana purple`; `/B` identifies offset `00000003`, bytes `44` / `45`. |
| FDBanner 1.1 | Draws the coloured FreeDOS banner using BIOS services; visually inspected screenshot. |
| B64 0.06R | `-e` writes `QUJDRA==` from `ABCD`; `-d` restores exactly `ABCD`. `-h` shows help. `/?` is a syntax error for this program, not a runtime fault. |
| BSUM 1.1a | Text checksums `3C36` and `2C14`; empty file `0000`; 20,480-byte binary (`bytes(range(256)) * 80`) `A000`. All match an independent BSD-checksum oracle. |

An additional Checks & Balances test uses disposable snapshots of the supplied
FAT copy. Inputs select terminal `2`, printer `1`, configuration save/run `6`,
`C:DEMO`, and Enter for the displayed date. The configuration is written, DEMO
opens, and the main menu reports `TRUE $2450.20 BANK $279.04`. `SH` reads and displays the supplied demonstration transactions after the DAA
correction. The final observation is recorded in the transcript. This is Partial:
a new ledger entry, saving and reopening it, reports and printing are unverified.
BLUE.EXE still reaches unsupported port output (`EE`, `1050:004D`). The original
360 KB floppy mounting issue was not addressed.

Known limits remain: no protected mode/DOS extender, arbitrary hardware port I/O,
child EXEC or TSR support; FCB zero-count block-write truncation returns an error;
text scrolling and direct video-memory rendering are incomplete. Keyboard tests
use the existing cooked terminal and require Enter. DOS date/time services still
return the existing fixed values. Wildcard searches are limited to short names.
No compatibility is claimed for other programs on the CD.

Evidence: [guest transcripts and regression results](compatibility/2026-10-22-dos-compat-runtime.txt),
[commands, fixtures, expectations and source/binary hashes](compatibility/2026-10-22-dos-compat-runtime.json),
[FreeDOS banner](compatibility/2026-10-22-fdbanner-working.png),
[Checks & Balances screenshot](compatibility/2026-10-22-checks-working-ui.png).
The earlier sections and evidence files retain the pre-fix failures as history.

These are source changes following `5965873`. A local Android APK packages the
same DOS runtime (versionCode `25004`, versionName `0.25.0-manios4`). It boots into
ManiDE in the Android 10/API 29 x86_64 emulator; physical ARM hardware is untested.
No GitHub release was created.

## Interpreter regression baseline

The latest local source passes 31 host interpreter/service/loader checks, the additional
`dosrun_string_test.py` instruction regressions, `dosrun_runtime_test.py`
startup/service regressions (including all 10,000 packed-decimal addition pairs),
and the
existing `dosrun_test.py` and `dos_test.py` QEMU suites. These are regression
results, not 28 compatible applications or a compatibility percentage.

The sample tests exposed corrections for interrupt status results, multi-segment
MZ loading, older full-page headers, arithmetic destinations and PSP memory
reporting. Passing those regressions does not change either application's status
without a successful application retest.

## Adding or updating an entry

Record:

1. Application name, exact version if known, media type and input hashes.
2. ManiOS commit and any local changes; emulator/hardware, RAM and attached media.
3. Commands, working directory, input supplied and run/time limits.
4. The furthest verified behaviour and a transcript or screenshot.
5. Status, exact failure, untested features and the next investigation needed.

A Working result should include the application's main task. For an accounting
application, verify sample entry, saving and reopening; record printing separately
if it has not been tested. Use disposable copies for write tests.

Keep historical evidence when updating a result. Test programs are supplied
separately; this list and its evidence contain no application executables or disk
images. See [dosrun](dosrun.md) for the implemented execution environment and
[ManiDOS](dos.md) for the command shell.
