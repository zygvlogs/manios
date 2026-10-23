# DOS software compatibility

Last updated: **2026-10-23**. ManiOS version: **0.25.0**.

This list records tests of real DOS applications through `dosrun`. Results apply
only to the identified program version, ManiOS revision and test setup. ManiDOS's
built-in commands and synthetic interpreter tests are tracked separately.

**Ten DOS utilities have verified Working results for the workflows
listed below:** seven FreeDOS tools and three utilities from the SemWare package. This is task-specific coverage, not a general DOS compatibility
percentage. Checks & Balances, the SemWare editor and Windows 1.01 setup are Partial. Warcraft and Ability Plus encounter unsupported runtime features; VisiCalc still has no usable worksheet.

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
| [SemWare Editor Professional](#semware-tse-250e) | 2.50e, `tse250e.zip` | **Partial** | Welcome dialog and input-file text render; file-opening run then stops on `IN AL,61h`. Editing/save/reopen unverified. | 2026-10-22 |
| [SemWare GREP](#semware-tse-250e) | bundled in `tse250e.zip`; utility version unverified | **Working** | Finds exactly the two `apple` lines in the fixture. | 2026-10-22 |
| [SemWare TSORT](#semware-tse-250e) | bundled in `tse250e.zip`; utility version unverified | **Working** | Sorts four input lines into a correctly ordered output file. | 2026-10-22 |
| [SAL compiler SC](#semware-tse-250e) | 2.50e | **Working** | Compiles a minimal macro to an 87-byte MAC file with the expected signature and message. Macro execution untested. | 2026-10-22 |
| [VisiCalc](#visicalc) | `VC.COM`, 27,520 bytes; archive timestamp 1981-12-17, exact version unverified | **Broken** | Initial `03DAh` fault resolved; no worksheet or shell return during the 12-second retest. | 2026-10-23 |
| [Ability Plus](#ability-plus-30) | 3.0, `AB3.ZIP` | **Unsupported** | Main program stops on x87 `FNINIT`; installer stops on a 32-bit operand instruction. No application workflow completed. | 2026-10-23 |
| [PEDIT](#pedit-400) | 4.00, standard and Light | **Unsupported** | Both stop on operand-size prefix `66h`; no editable document reached. | 2026-10-23 |
| [uHex](#uhex-104) | 1.0.4 DOS | **Partial** | Help returns; opens 34-byte fixture and shows correct hex bytes, with display defects. Editing/save/reopen unverified. | 2026-10-23 |
| [Directory Maven](#directory-maven-13) | 1.3 | **Unsupported** | BIOS clock wait fixed; scan now stops on `66h`. Help stops at `IN AL,DX`; no index verified. | 2026-10-23 |
| [File Maven](#file-maven-35a) | 3.5a | **Broken** | Installer, main executable and launcher pass the clock wait, then fail on `IN AL,DX`; no usable file manager reached. | 2026-10-23 |
| [Search](#search-10a) | 1.0a, FD14 BonusCD | **Working** | Finds both root/nested matches exactly once; correct total and missing-file result after directory fix. | 2026-10-23 |
| [utf8tocp](#utf8tocp-20240918) | 20240918, FD14 BonusCD | **Working** | Exact UTF-8 ↔ CP437 and UTF-8 ↔ CP775 conversions, including Lithuanian letters and CRLF. | 2026-10-23 |
| [Bywater BASIC](#bywater-basic-330) | 3.30, FD14 BonusCD binary | **Unsupported** | LE protected-mode executable rejected; the supplied arithmetic program was not executed. | 2026-10-23 |
| [Microsoft Windows](#microsoft-windows-101) | 1.01, five 360 KB floppy images | **Partial** | Setup passes the PIT/speaker routine and displays its own directory-creation error. Installation/desktop still incomplete. | 2026-10-23 |

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

## SemWare TSE 2.50e

The supplied `tse250e.zip` identifies The SemWare Editor Professional 2.5;
SC's banner reports SAL Compiler v2.50e. The main `E.EXE` is 143,608 bytes.
The extracted package is copied unchanged to `C:/TSE` on a disposable FAT16
image. Tests run inside ManiOS on QEMU 486 / 64 MiB, with the guest working
directory `/n/ata0p1/tse`. Each normal case gets a fresh disk snapshot.

The previous runtime (`647f504`) stops on missing XLAT: editor at `1070:F64B`,
TSORT at `1000:0484` / `1000:0D4E`, and SC at `1010:A7DB`. Adding XLAT enables
SC compilation and advances TSORT to missing AAD. XLAT now preserves AH and flags,
supports source-segment overrides and wraps BX+AL to 16 bits; AAD folds the two
bytes using its immediate base and updates SF/ZF/PF.

The editor also writes directly to emulated B800 text memory. The new renderer
copies changed spans of the active 80x25 colour page to the terminal, retaining
attributes and cursor position. DOS/BIOS character writes update the shadow to
avoid redrawing ordinary console output. Pending direct writes flush before a
blocking BIOS key read and before program exit. Monochrome/high-row modes,
text-mode scrolling and hardware keyboard integration are not completed here.

| Test | Verified result |
| --- | --- |
| `dosrun C:/TSE/E.EXE` | Welcome and file-selection dialog visible; still active after eight seconds. This is a bounded observation of a prompt, not proof of a hang. |
| `dosrun C:/TSE/E.EXE C:/INPUT.TXT` | Input text becomes visible, then fails at `E4`, `1070:F8A7`. A host CPU probe reads the next operand as `61`, identifying `IN AL,61h`. Port I/O remains unsupported. |
| `dosrun C:/TSE/GREP.COM apple C:/INPUT.TXT` | Returns `apple` and `apple pie`, excluding `pear` and `banana`. No regex/recursive-search claim is made. |
| `dosrun C:/TSE/TSORT.COM C:INPUT.TXT C:SORTED.TXT` | Output reads exactly `apple`, `apple pie`, `banana`, `pear`, in that order. The `C:/...` argument form produced `Bad!! /INPUT.TXT`; the successful case uses DOS drive-relative names. |
| `dosrun C:/TSE/SC.EXE C:/HELLO.S` | Compiles the fixture and announces `c:/hello.mac`. A separate run on a disposable persistent image exports and verifies the 87-byte result, signature `FFFF426F62626900`, and embedded `ManiOS macro test` string. |

The source fixture is `proc Main()` / `Message("ManiOS macro test")` / `end`,
with DOS CRLF endings. SC without arguments reaches its interactive choice menu;
no compiler-menu workflow is claimed beyond that. Eight package cases plus a
separate persisted-output check were run. Main-editor editing, save/reopen,
macros, help, spelling and interactive key bindings remain unverified. The editor
stays Partial even though companion utilities complete their tested tasks.

The 31 host checks, string/runtime state suites, both QEMU DOS suites and a new
short direct-VGA program regression pass. Five representative FreeDOS tasks were
also rerun successfully. Results apply to source/kernel hashes in the evidence,
not every DOS program. A local Android APK with these changes uses versionCode
25005 / `0.25.0-manios5`; signature, embedded ISO and clean Android 10 x86_64
startup were verified. Physical ARM hardware and TSE inside Android were not
tested. No release was created. User executables, media and generated MAC binaries are
kept outside the repository and APK.

Evidence: [transcripts](compatibility/2026-10-22-tse.txt),
[commands, exact outputs and hashes](compatibility/2026-10-22-tse.json),
[editor start screenshot](compatibility/2026-10-22-tse-editor-start.png),
[file-opening failure screenshot](compatibility/2026-10-22-tse-editor-file.png).

## VisiCalc

These are the pre-fix observations. See the [video-status retest](#video-status-emulation-and-retest) for the current Broken result.

- **Input:** user-supplied `visicalc-executable.zip`, containing an unchanged
  27,520-byte `VC.COM`. The archive records 1981-12-17; exact version is unverified.
- **Tested build:** ManiOS 0.25.0, commit `8446024`; DOS sources match the local
  build tree. Kernel and source SHA256 hashes are recorded in the evidence.
- **Environment:** QEMU i386, CPU 486, 64 MiB RAM, FAT16 disk; fresh snapshot
  and working directory `/n/ata0p1/vc`. Program observation limit: 12 seconds.
- **Command:** `dosrun C:/VC/VC.COM`.
- **Observed result:** returns to the shell with unsupported opcode `EC` at
  `1000:6A3E`. A separate host CPU probe confirms `IN AL,DX`, with `DX=03DAh`,
  followed by `TEST AL,01h` and a conditional branch back to the input instruction.
  This is video status-port polling, which the runtime does not emulate.
- **Status before video-status fix:** Unsupported. No usable worksheet was reached; data entry,
  formulas, calculation, saving and reopening remain unverified.
- **Next work:** implement and test the relevant video-status port semantics,
  then retest the application. Passing this first instruction would not establish
  spreadsheet compatibility.
- **Evidence:** [guest transcripts and instruction probes](compatibility/2026-10-23-visicalc-ability.txt),
  [commands and archive/program/kernel/source hashes](compatibility/2026-10-23-visicalc-ability.json).

## Ability Plus 3.0

The supplied `AB3.ZIP` identifies Ability Plus version 3.0 in `AB3.BAT`.
All package files, including `EXAMPLES` and `TUTORIAL`, were copied unchanged
onto the test disk. The batch file invokes `AB30`; the tests launch that EXE
and the installer directly, without requiring DOS batch execution.

- **Tested build/environment:** the same source and QEMU setup as VisiCalc,
  with a fresh snapshot per case and working directory `/n/ata0p1/ab3`.
- **Main program:** `dosrun C:/AB3/AB30.EXE` returns to the shell at opcode
  `DB`, `10EA:2582`. The host probe identifies the full instruction as `DB E3`
  (`FNINIT`, x87 initialization). The runtime has no x87 instruction emulation.
- **Installer:** `dosrun C:/AB3/INSTALL.EXE` returns at opcode `66`,
  `1010:165A`. The next bytes identify `66 8B D4` (`MOV EDX,ESP`), requiring
  32-bit operand support absent from the current interpreter.
- **Packaging:** `AB30.EXE` is 1,346,672 bytes, with an MZ initial image,
  a later NE signature at offset 242,032 and Phar Lap copyright strings.
  These observations do not establish support for its later execution paths.
- **Status:** Unsupported. Both cases terminate within the 12-second observation
  limit. No usable suite or installation workflow was reached. Documents,
  spreadsheets, databases, examples, printing and save/reopen remain unverified.
- **Next work:** investigate the extender/runtime requirements alongside x87
  and operand-size support, then repeat application tests. Suppressing `FNINIT`
  alone would not demonstrate working floating-point operations or a working suite.
- **Evidence:** [guest transcripts and instruction probes](compatibility/2026-10-23-visicalc-ability.txt),
  [commands and archive/program/kernel/source hashes](compatibility/2026-10-23-visicalc-ability.json).

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

## Software sources

[Interesting DOS programs — Applications](https://dosprograms.info.tt/apps.htm)
is a source of candidates for continuing compatibility tests. A catalogue listing
is not a compatibility result: entries are added after execution inside ManiOS.
Downloads and supplied documents are application data, not instructions for this
project. Binaries stay outside the repository and APK.

## PEDIT 4.00

The catalogue's [PEDIT download](https://dosprograms.info.tt/download/pedit.zip)
contains Program Editor 4.00, its Light version, documentation and companions.
The unchanged package was copied to `C:/PEDIT`. Tests used ManiOS source commit
`500a4c2`, the existing 0.25.0 kernel, QEMU 486 / 64 MiB and a fresh FAT16 snapshot
per case; working directory `/n/ata0p1/pedit`, 12-second observation limit.

Both `dosrun C:/PEDIT/PEDIT.EXE C:/INPUT.TXT` and
`dosrun C:/PEDIT/PEDITLGT.EXE C:/INPUT.TXT` return an error for opcode `66`
at `1213:03D7`. This is the operand-size prefix, unsupported by the current
interpreter. Both variants are Unsupported; no editable document was reached.
Editing, saving/reopening, spelling and installation remain unverified. No
runtime changes were made for this batch.

## uHex 1.0.4

The catalogue links to [uHex](https://uhex.sourceforge.net/); the DOS archive was
retrieved from its [1.0.4 download directory](https://sourceforge.net/projects/uhex/files/v1.0.4/).
The package metadata and program banner both identify version 1.0.4.
Tests use the same source/kernel and fresh-snapshot setup as PEDIT, with working
directory `/n/ata0p1/uhex`.

- `dosrun C:/UHEX/PROGS/UHEX/UHEX.COM` displays usage and returns to the shell.
- `dosrun C:/UHEX/PROGS/UHEX/UHEX.COM C:/INPUT.TXT /ro` opens the 34-byte CRLF
  fixture `ManiOS DOS test` / `ABCD 0123456789`. The first row shows
  `4D 61 6E 69 4F 53 20 44 4F 53 20 74 65 73 74 0D`, the second row shows
  `0A 41 42 43 44 20 30 31 32 33 34 35 36 37 38 39`, and the final two bytes
  are `0D 0A`, matching the fixture. The screen shows the filename and 34-byte size.
- The file view remains active at the 12-second limit; this alone is not a hang.
  A separate fresh run sent Escape followed by Enter through the cooked serial
  terminal; no shell return was observed within eight seconds. Other quit-key
  delivery paths were not tested. Screenshot inspection finds screen remnants
  and incorrect ASCII-pane rendering.
  Status is Partial; editing, save/reopen, search and larger-file navigation remain
  unverified. The read-only view does not establish working editor compatibility.

Evidence for both applications: [transcripts](compatibility/2026-10-23-catalog.txt),
[commands, exact output, fixture and SHA256 hashes](compatibility/2026-10-23-catalog.json),
[uHex screenshot](compatibility/2026-10-23-catalog-uhex.png).

## Directory Maven 1.3

Discovered through the File Maven author's site linked from the catalogue;
[Directory Maven](https://www.briggsoft.com/dmaven.htm) package `dmav13.zip`
identifies version 1.3. Package files were copied unchanged to `C:/DMAVEN`.
The guest working directory is `/n/ata0p1/dmaven`.

| Command | Before clock fix | After clock fix |
| --- | --- | --- |
| `dosrun C:/DMAVEN/DM.EXE ?` | No application output or shell return within 12 seconds | Returns with opcode `EC`, `15F3:0603` |
| `dosrun C:/DMAVEN/DM.EXE /sC` | No application output or shell return within 12 seconds | Returns with opcode `66`, `1656:0C91` |

Status is Unsupported because the scanning path reaches the unsupported
operand-size prefix. A successful directory index, quick directory change,
configuration and tree navigation remain unverified. The help failure's host
probe identifies `IN AL,DX` with `DX=0006h`; this may reflect missing BIOS display
state and must not be treated as proof that the application actually requires
hardware port 0006h. General port I/O is also unimplemented.

## File Maven 3.5a

The catalogue links to the [File Maven author](https://www.briggsoft.com/fmdos.htm).
The original `fmav35a.zip` contains `INSTALL.EXE` and `INSTALL.DAT`, rather than
an already installed main program. `INSTALL.DAT` is a self-extracting LHA archive
(offset 1702); it was separately extracted with 7-Zip to obtain unchanged
`FMAVEN.EXE`, launcher `FM3.EXE` and all five other payload files. Both original
package files and the extracted payload were copied to `C:/FMAVEN`.
This tests direct execution separately from successful installation.

| Command | Before clock fix | After clock fix |
| --- | --- | --- |
| `dosrun C:/FMAVEN/INSTALL.EXE` | No application output or shell return within 12 seconds | Returns with opcode `EC`, `1099:0603` |
| `dosrun C:/FMAVEN/FMAVEN.EXE` | No application output or shell return within 12 seconds | Returns with opcode `EC`, `36C9:0603` |
| `dosrun C:/FMAVEN/FM3.EXE` | No application output or shell return within 12 seconds | Returns with opcode `EC`, `1063:0603` |

Status is Broken: no usable manager, completed installation, copy/move/delete
operation or viewer workflow was reached. The installer's diagnostic host probe
also reports `IN AL,DX` with `DX=0006h`; the underlying display initialization
needs investigation before assigning a genuine hardware requirement. Archive
handling, editor, serial/parallel transfers and other features remain unverified.

### Maven test setup and BIOS clock fix

Baseline source is `c800e4a`; fixed source is `d1face7`. ManiOS 0.25.0 runs on
QEMU i386, CPU 486, 64 MiB RAM, with independent disposable FAT16 snapshots and
a 12-second limit per command. The File Maven working directory is
`/n/ata0p1/fmaven`. Original archives and application binaries remain outside
the repository and APK. The kernel and individual DOS sources are hashed in
the evidence; existing Android APKs have not been rebuilt with this fix.

Diagnostic instruction probes locate the pre-fix startup wait in a direct BIOS
tick-memory polling loop. The runner now advances `0040:006C` from elapsed host
uptime; `INT 1Ah/AH=00h` reads that same counter. Fractions survive small updates,
word carry works and midnight wraps with a read-and-clear rollover flag. The
counter starts at zero per execution. Timer interrupt delivery, PIT ports, DOS
wall-clock time and BIOS RTC updates are not implemented by this change.

All five real-program retests leave the former wait and return an explicit
instruction error. This is runtime progress, not a successful application task.
The 31 host checks, string suite, runtime suite (including new clock edge cases)
and QEMU DOS runner suite pass. A new QEMU regression polls the BIOS memory tick
without any guest interrupt calls and reaches `CLOCK OK` through the real runner.

Evidence: [before/after transcripts and QEMU regressions](compatibility/2026-10-23-maven.txt),
[commands, exact output, probes and hashes](compatibility/2026-10-23-maven.json).

## Search 1.0a

The FreeDOS BonusCD package `/packages/util/search.zip` identifies Search 1.0a;
its unchanged `BIN/SEARCH.EXE` is 14,461 bytes. This is a recursive **filename**
search tool, rather than a text-content search tool.

- `dosrun C:/SEARCH.EXE /?` prints usage and returns.
- The fixture contains `C:\TARGET.TXT` and `C:\NESTED\TARGET.TXT`, with
  `OTHER.TXT` as a nonmatching file. Before the fix (`565e8ee`), searching for
  `TARGET.TXT` incorrectly prints the root result eight times and says
  `8 files found`. Returning to the shell did not make that result Working.
- With directory support (`ee88879`), `dosrun C:/SEARCH.EXE TARGET.TXT`
  prints both matching paths once and reports `2 files found`. The program
  prints an extra separator in the root path; the nested path and total are correct.
- `dosrun C:/SEARCH.EXE ABSENT.ZZZ` correctly reports `No files found`.

Status is Working for these searches. Deeper recursion, wildcard combinations,
other drives and larger trees remain unverified. DOS directory changes now
validate the destination, retain independent per-drive current directories and
resolve relative paths there. Current-directory queries return the actual path;
failed changes preserve it. Host regressions cover these cases and root clamping.
Directory creation and removal are still unsupported.

## utf8tocp 20240918

The BonusCD `/packages/util/utf8tocp.zip` contains the unchanged 9,788-byte
`UTF8TOCP.COM`; the supplied documentation identifies version 20240918.
The codepage list (`-l`) displays successfully. Four conversion workflows were
run independently, each with a fresh disposable disk copy:

| Task | Input | Verified output |
| --- | --- | --- |
| UTF-8 → CP437 | `Cafe: café ü ö £` plus CRLF | Exact bytes from Python's independent `cp437` encoder |
| CP437 → UTF-8 | Independently encoded fixture | Exact original UTF-8 bytes and CRLF |
| UTF-8 → CP775 | `ĄČĘĖĮŠŲŪŽąčęėįšųūž` plus CRLF | Exact bytes from Python's independent `cp775` encoder |
| CP775 → UTF-8 | Independently encoded Lithuanian fixture | Exact original UTF-8 bytes and CRLF |

Commands use `dosrun C:/UTF8TOCP.COM 437 C:/UTF8.TXT C:/ENC.TXT` and
`dosrun C:/UTF8TOCP.COM -r 437 C:/CP437.TXT C:/DEC.TXT`; CP775 cases use
encoding `775` and separate Lithuanian fixture/output files. Output files are
exported from the disposable FAT16 images after guest shutdown, then compared
byte-for-byte. Status is Working for these conversions. Other encodings,
invalid UTF-8 handling, unmappable characters, map dumps and large files are
unverified. File-byte correctness does not establish correct terminal rendering
of every Lithuanian glyph. The two CP437 tasks pass before and after the runtime
fix; Lithuanian CP775 tasks were tested on the fixed build.

## Bywater BASIC 3.30

The BonusCD `/packages/devel/bwbasic.zip` identifies Bywater BASIC 3.30. Its
`BWBASIC.EXE` is 142,318 bytes and contains an LE header at offset `0x3700`.
The package describes a DOS extender; the tested loader reports the actual
32-bit protected-mode requirement. Neither the interpreter nor its stub runs.

`dosrun C:/BWBASIC.EXE C:/SUM.BAS` is attempted with the CRLF fixture
`10 PRINT 7 * 6` / `20 END`, whose expected result is `42`. The loader rejects
execution in both the baseline and fixed build. Status is Unsupported for this
binary. Interactive BASIC, program execution, file I/O and arithmetic remain
untested; this result does not apply to other real-mode BASIC interpreters.

### Search / converter / BASIC test setup

Baseline is `565e8ee`; fixed sources are `ee88879`. Tests run inside ManiOS
0.25.0, QEMU i386, CPU 486, 64 MiB RAM, working directory `/n/ata0p1`.
Each case uses a fresh disposable copy of the FAT16 disk, with a ten-second
program observation limit. Seven baseline cases and nine fixed-build cases
were run. The original BonusCD and archives are unchanged; no application
binaries, media or generated conversion files are added to the repository/APK.
Existing Android APKs were not rebuilt for this source change.

The 31 host checks, string suite, runtime suite (including new directory-service
regressions) and QEMU DOS runner suite pass. These two new Working entries bring
the list to ten utilities with verified tasks; this is not a general DOS
compatibility percentage.

Evidence: [guest transcripts and exported-byte comparisons](compatibility/2026-10-23-search-utf8-basic.txt),
[commands, exact output, fixtures, hashes and regression results](compatibility/2026-10-23-search-utf8-basic.json).

## Microsoft Windows 1.01

These are pre-fix observations. See the [latest PIT/speaker retest](#windows-101-pit-and-speaker-retest) for the current Partial installer result.

The user-supplied `Microsoft Windows 1.01 (5.25).7z` contains five 368,640-byte
FAT12 floppy images: Setup, Build, Utility, Apps and Write. File timestamps in
the media are mostly 1985-11-15. The package identifies Windows 1.01; that
identification was not independently checked against a running Windows banner.

- **Tested source:** `4685cf9`, ManiOS 0.25.0; kernel and DOS source hashes are
  recorded in the evidence. No runtime changes were made for this batch.
- **Environment:** QEMU i386, CPU 486, 64 MiB RAM; independent FAT16 snapshots,
  guest working directory `/n/ata0p1`, DOS drive C: root, 12-second limit per case.
- **Preparation:** floppy contents were read with host mtools and copied into
  a disposable hard disk. Top-level files from all five disks were merged;
  duplicate files were verified identical and the `PIF` subdirectory retained.
  Original archive and images are unchanged. Original floppy mounting and
  in-guest disk swapping were not tested.

| Diagnostic command | Observed result |
| --- | --- |
| `dosrun C:/SETUP.EXE` | Returns with unsupported opcode `EC` at `1010:38A3`. Host instruction probe confirms `IN AL,DX`, `DX=03DAh`, followed by a status-bit test and polling loop. |
| `dosrun C:/KERNEL.EXE` | Prints `Incorrect DOS version`, then NUL/binary-like output, and stops at `DB`, `10A6:65D1`. Probe identifies `DB E3` (`FNINIT`); this direct invocation is not an installed Windows startup and does not establish an application x87 requirement. |
| `dosrun C:/NOTEPAD.EXE` | Its DOS stub says `This program requires Microsoft Windows.` and exits. The Windows editor itself did not run. |

**Status before video-status fix: Unsupported.** The first confirmed installer blocker is video-status
port input, which the current interpreter does not emulate. No usable installer
workflow, completed installation, desktop or Windows application was reached.
Removing this first blocker alone would not establish Windows compatibility.

`KERNEL.EXE` and `NOTEPAD.EXE` have NE headers at `0x400`; this is not the
LE/LX protected-mode diagnosis used for Warcraft or the tested Bywater BASIC.
No 32-bit-mode requirement is inferred here. The media contain `WIN.CNF`, a
1,936-byte bootstrap/template, but no installed `WIN.COM` was produced. The
kernel and Notepad direct runs are diagnostics, not a substitute for the proper
installation/bootstrapping process. Subsequent loader, DOS services, video,
input and driver requirements remain untested.

Next investigation: implement and test the relevant video-status port semantics,
then repeat setup and determine its next concrete requirement. Runtime graphics
support is currently limited and Windows GUI compatibility is not claimed.
User binaries, floppy images and merged test disks remain outside the repository
and APK; existing APKs were not rebuilt and no release was created.

Evidence: [guest transcripts and instruction probes](compatibility/2026-10-23-windows101.txt),
[exact outputs, media listings, source and input hashes](compatibility/2026-10-23-windows101.json).

## Video-status emulation and retest

Fixed source: `4eaaa5c`, following baseline `dea2d01`. A DOS device callback
now handles byte `IN AL,DX` at `03DAh`. Bit 0 reports an inactive display
interval; bit 3 reports vertical retrace. Other status bits are zero. The model
uses deterministic guest-instruction phase (4,096-instruction frame, 64-instruction
line, final 16 instructions blank, final 256 instructions in vertical retrace).
It does not claim a real refresh rate or cycle-accurate CGA/EGA hardware.
Unknown ports, word reads, immediate reads and all writes remain unsupported.
No native machine ports are accessed. See the [runtime documentation](dosrun.md#minimal-cgaega-compatible-video-status)
for the register reference and exact scope.

The dedicated `dosrun_port_test.py` verifies active/inactive/retrace states,
repeated transitions, bit 0 during vertical retrace, preserved AH/flags/registers,
both-polarity bit 0/3 wait loops with interrupts disabled, instruction-counter
wrap and rejection of unsupported I/O. Same-phase reads are stable; the model
does not simply flip a bit per read. It is wired into `make test`. A new QEMU COM
regression completes the same edge waits and prints `VIDEO STATUS OK` through
the actual runner. The host, string, runtime, port and QEMU DOS runner suites pass.

### Windows 1.01 after video status

Using the same merged-file FAT16 snapshot and unmodified `SETUP.EXE`, QEMU
486 / 64 MiB now renders the welcome screen and Continue/Quit menu. A fresh
interactive run sends `c` followed by Enter through the cooked serial terminal;
it reaches directory selection with default `C:\WINDOWS`. A further fresh run
accepts that default with Enter and returns a new instruction error:

```text
dosrun: C:/SETUP.EXE: can't run opcode E6 (at 1010:167B)
```

Unmodified executable bytes at `1010:167A` are `E6 43`, identifying
`OUT 43h,AL` (timer control). This is outside the read-only video-status model.
No timer command was ignored or forwarded. The new result is **Partial** for the
installer interface. Installation, driver configuration, produced `WIN.COM`,
Windows desktop, application execution, mouse and GUI graphics remain unverified.
The directory screen has display remnants; full screen rendering is not claimed.

### VisiCalc after video status

The unchanged `VC.COM` no longer reports the previous `EC` error at `1000:6A3E`.
In the 12-second QEMU observation it displays no usable worksheet and does not
return to the shell. A separate bounded host probe ends in a BIOS keyboard-status
polling path at `1000:5FF1`; this identifies a later execution path, not a
successful spreadsheet workflow or proof of an infinite hang. Current status
is **Broken**. Input, worksheet rendering, calculations, save/reopen and the
cause of the remaining blank screen need further investigation.

All tests use independent snapshots and original program hashes. Kernel/source
hashes, exact inputs/outputs and regression logs are retained in the evidence.
Existing Android APKs were not rebuilt; no release was created.

Evidence: [transcripts and regression results](compatibility/2026-10-23-video-status.txt),
[exact outputs, inputs, source/program hashes and test logs](compatibility/2026-10-23-video-status.json),
[Windows setup welcome](compatibility/2026-10-23-video-status-windows-start.png),
[Windows directory prompt](compatibility/2026-10-23-video-status-windows-directory.png).

## Windows 1.01 PIT and speaker retest

Fixed source: `ff4602d`, following baseline `b3b7dc3`. The failing instruction
starts at `1010:167A`; reported post-opcode CS:IP `1010:167B` points to its
immediate port byte. A baseline interpreter trace captures:

```text
1010:1678  B0 B6       MOV AL,B6h
1010:167A  E6 43       OUT 43h,AL
FAULT CS:IP=1010:167B opcode=E6 next_port_byte=43 AL=B6 AX=00B6 DX=0C32
1010:167C  B8 33 05    MOV AX,0533h
1010:167F  E6 42       OUT 42h,AL
1010:1681  8A C4       MOV AL,AH
1010:1683  E6 42       OUT 42h,AL
1010:1687  E4 61       IN AL,61h
```

The complete surrounding disassembly is in the evidence. `B6h` programs PIT
channel 2 for binary mode-3 square-wave generation, with low/high-byte count
loading. The routine then sets speaker gate/enable bits at `61h`, waits in CPU
loops, loads divisor `0633h` and restores the old speaker control bits. It is a
PC-speaker two-tone routine. The post-fix device trace reports logical integer
frequencies 896 Hz, 751 Hz, then zero after restoration.

The [PIT/speaker model](dosrun.md#pit-channel-2-and-pc-speaker) now implements
this sequence, count latching/readback, zero divisor = 65,536, gate state and
OUT2 status. Byte `IN`/`OUT` support both immediate and DX addressing and preserve
flags/unaffected registers. Unsupported ports, modes, BCD commands and word I/O
still fault; the runtime now prints port, AL/AX and surrounding guest bytes.
The model does not produce audible sound, deliver timer interrupts or emulate
all PIT channels. Timing is instruction-driven; completed reloads restart the
period immediately. No native I/O is issued and no unsupported command is
silently treated as success.

The same QEMU 486 / 64 MiB FAT16 snapshot setup run now passes the entire
previously failing beep routine. Inputs are `c`+Enter at Continue, then Enter
at `C:\WINDOWS`. Instead of an opcode error, the installer displays its own
message that it cannot create the specified directory and requests another one.
This remains **Partial**: no completed installation, generated `WIN.COM` or Windows
desktop. DOS directory creation is not implemented in the current runtime;
this batch did not isolate the exact failing directory-service call. That work
is separate from the confirmed PIT fault and is not masked by this change.

The dedicated PIT suite tests partial/complete divisor loading, both actual
Windows divisors, gate/enable independence, even/odd mode-3 counts, OUT2 status,
count freezing/restart, stable latches, zero divisors, instruction-counter wrap,
IN/OUT operand consumption, register/flag preservation, cleanup and refusal of
unsupported requests without timer-state corruption. It is wired into `make test`.
QEMU regressions run the programming sequence and poll both OUT2 edges; an
unsupported BCD control command still produces a port/value diagnostic.

The host, string, runtime, video-status and PIT suites and QEMU DOS runner suite
pass. All ten earlier Working utilities were retested for their representative
tasks on the rebuilt kernel: MORE, FC, FDBanner, B64, BSUM, SemWare GREP, TSORT,
SAL compiler, Search and utf8tocp. Sorted and converted outputs were exported and
compared; SAL's 87-byte macro matches the earlier SHA256. FDBanner's rendered
banner was visually checked. This preserves the tested workflows, not a claim
that all program features work. Original media/executables are unchanged.
Existing APKs were not rebuilt; no release was created.

Evidence: [trace, disassembly and retest transcripts](compatibility/2026-10-23-pit-speaker.txt),
[exact results, source/kernel hashes and compatibility checks](compatibility/2026-10-23-pit-speaker.json),
[setup directory-error screen after the PIT fix](compatibility/2026-10-23-pit-speaker-setup.png).

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
