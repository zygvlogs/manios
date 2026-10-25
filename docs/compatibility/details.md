# DOS compatibility test records

This file preserves the full per-application test history and evidence links.
The red/yellow/green overview is in
[dos-compatibility.md](../dos-compatibility.md); the machine-readable index is
[database.json](database.json).

Last updated: **2026-10-25**. ManiOS version: **0.26.1-10252026**.

The sections below record tests of real DOS applications through `dosrun`.
Results apply only to the identified program version, ManiOS revision and test
setup. ManiDOS's built-in commands and synthetic interpreter tests are tracked
separately.

This is the full record, not the index. Current statuses and one-line results
are in the red/yellow/green [overview](../dos-compatibility.md) and the
machine-readable [database](database.json). The sections are kept in test order
and include earlier builds, so a section can predate the current result; use it
for commands, timings, hashes and exact failure addresses. The 2026-10-23
[dosprograms.info.tt catalog](https://dosprograms.info.tt/apps.htm) batch is
followed by the compat4 Info-ZIP, DOSStart, Spectra and PasswordBox batch and
the FreeDOS-repo Games batch, followed by the text-adventure batch at the end.

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
- **Evidence:** [guest transcript](2026-10-22-warcraft.txt).
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
- **Evidence:** [guest transcripts and tested-source hashes](2026-10-22-checks-and-balances.txt).
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
- **Evidence:** [transcripts](2026-10-22-freedos-bonus.txt),
  [machine-readable results, commands, fixtures and SHA256 hashes](2026-10-22-freedos-bonus.json).
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
line; the [VGA screenshot](2026-10-22-fdbanner.png) also shows no
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

Known limits remain: no protected mode/DOS extender, general hardware port I/O
(the master PIC mask at `21h` and PIT channels 0 and 2 are modeled, but IRQ
delivery is not), or TSR support; FCB zero-count block-write truncation returns
an error; direct video-memory rendering beyond the supported text and mode 13h
paths is incomplete. Keyboard tests use the existing cooked terminal and
require Enter. DOS date/time services still return the existing fixed values.
Wildcard searches are limited to short names.
No compatibility is claimed for other programs on the CD.

Evidence: [guest transcripts and regression results](2026-10-22-dos-compat-runtime.txt),
[commands, fixtures, expectations and source/binary hashes](2026-10-22-dos-compat-runtime.json),
[FreeDOS banner](2026-10-22-fdbanner-working.png),
[Checks & Balances screenshot](2026-10-22-checks-working-ui.png).
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

The editor also writes directly to emulated B800 text memory. The renderer
copies changed spans of the active 80x25 colour page to the terminal, retaining
attributes and cursor position. DOS/BIOS character writes update the shadow to
avoid redrawing ordinary console output. Pending direct writes flush before a
blocking BIOS key read and before program exit. Monochrome/high-row modes and
some text-mode scrolling remain unverified.

Pre-0.26.0 baseline (the 0.26.0 retest follows the table):

| Test | Verified result |
| --- | --- |
| `dosrun C:/TSE/E.EXE` | Welcome and file-selection dialog visible; still active after eight seconds. This is a bounded observation of a prompt, not proof of a hang. |
| `dosrun C:/TSE/E.EXE C:/INPUT.TXT` | Pre-0.26.0: input text becomes visible, then fails at `E4`, `1070:F8A7`. A host CPU probe reads the next operand as `61`, identifying `IN AL,61h`. The 0.26.0 retest below no longer faults. |
| `dosrun C:/TSE/GREP.COM apple C:/INPUT.TXT` | Returns `apple` and `apple pie`, excluding `pear` and `banana`. No regex/recursive-search claim is made. |
| `dosrun C:/TSE/TSORT.COM C:INPUT.TXT C:SORTED.TXT` | Output reads exactly `apple`, `apple pie`, `banana`, `pear`, in that order. The `C:/...` argument form produced `Bad!! /INPUT.TXT`; the successful case uses DOS drive-relative names. |
| `dosrun C:/TSE/SC.EXE C:/HELLO.S` | Compiles the fixture and announces `c:/hello.mac`. A separate run on a disposable persistent image exports and verifies the 87-byte result, signature `FFFF426F62626900`, and embedded `ManiOS macro test` string. |

The source fixture is `proc Main()` / `Message("ManiOS macro test")` / `end`,
with DOS CRLF endings. SC without arguments reaches its interactive choice menu;
no compiler-menu workflow is claimed beyond that. Eight package cases plus a
separate persisted-output check were run. Main-editor editing, save/reopen,
macros, help, spelling and interactive key bindings remain unverified. The editor
stays Partial even though companion utilities complete their tested tasks.

**0.26.0 update (2026-10-23):** with commit `c10d9f0`, the former file-opening
failure is gone. `dosrun C:/TSE/E.EXE C:/INPUT.TXT` displays the welcome box,
`Loading: c:/input.txt`, all four input lines and `<*** End of File ***>` with the
F-key status line; no instruction fault appears during the 10-second observation.
The editor still does not return to the shell. A bounded edit attempt (typed text,
then `Ctrl+K S` save and `Ctrl+K X` save-and-quit, each followed by Enter for the
cooked console) changed nothing and left `INPUT.TXT` byte-identical. Typed input
does not yet reach the editor's keyboard path, so editing, save and reopen remain
unverified. The editor stays Partial.

**Keyboard and save follow-up (2026-10-23):** `dosrun` now polls physical
keyboard input into the BIOS data-area buffer for legacy editors, exposes
8042 status/data reads, and preserves BIOS scan codes for Ctrl-letter keys.
TSE accepted a physical `z` at the start of `INPUT.TXT`; Ctrl+K S saved it and
Alt+X exited. A second launch displayed `zpear`, and mtools read back the same
saved bytes followed by the original remaining lines. This updates the TSE
application status to Working for the tested edit/save/reopen workflow; the
keyboard and display evidence is in
[the interactive app record](2026-10-23-int10-interactive-apps.txt).

The 31 host checks, string/runtime state suites, both QEMU DOS suites and a new
short direct-VGA program regression pass. Five representative FreeDOS tasks were
also rerun successfully. Results apply to source/kernel hashes in the evidence,
not every DOS program. A local Android APK with these changes uses versionCode
25005 / `0.25.0-manios5`; signature, embedded ISO and clean Android 10 x86_64
startup were verified. Physical ARM hardware and TSE inside Android were not
tested. No release was created. User executables, media and generated MAC binaries are
kept outside the repository and APK.

Evidence: [transcripts](2026-10-22-tse.txt),
[commands, exact outputs and hashes](2026-10-22-tse.json),
[editor start screenshot](2026-10-22-tse-editor-start.png),
[file-opening failure screenshot](2026-10-22-tse-editor-file.png).

## VisiCalc

**History.** The earliest retained test is from 2026-10-23 on ManiOS 0.25.0,
commit `8446024`. It returned to the shell at unsupported opcode `EC`
(`IN AL,DX`, port `03DAh`). After video-status input was added, a 0.26.0
12-second retest no longer faulted but still showed no worksheet. Both reports
use the same unchanged 27,520-byte `VC.COM` (SHA-256
`06b80d1c56b69232fd85cf55dee0b3369c2af3197bfbda5c4c584490e0418495`). The
user reports that VisiCalc had worked during earlier development; no successful
earlier test or screenshot was retained in Git or the test evidence. The first
successful recorded workflow is the 2026-10-24 retest below.

**Root cause of the blank screen.** `VC.COM` selects BIOS text mode 2 and writes
its worksheet directly to `B800` text memory. `dosrun` mirrored direct text
writes only in mode 3, and mode 2 did not initialize the BIOS data area or
visible text page. The app had built its worksheet in emulated memory, but the
screen renderer ignored it.

**Fix.** `dosrun` now initializes and renders BIOS mode 2 using the existing
80x25 text-page path. A runtime regression checks the mode-2 BDA fields,
readback mode, page reset and direct VGA text rendering.

**Root cause of the saved-sheet load failure.** After reading the worksheet,
VisiCalc closes the same FCB twice. MS-DOS 4.0's `FCBIO.ASM` explicitly returns
success for successive closes because real applications close FCBs more than
once. ManiDOS released the slot on the first close, so the second `INT 21h`
`AH=10h` returned `FFh`; VisiCalc interpreted that status as `ERROR: File not
found` and discarded the loaded sheet. The diagnostic trace in
[the earlier FCB report](2026-10-24-visicalc-final-fcb-trace.txt) shows the
successful open/read/first close followed by the failing second close.

**Fix.** The DOS FCB close path (`INT 21h` `AH=10h`) now closes the underlying
descriptor only when a valid slot remains and always returns `AL=0`. This
matches MS-DOS's tolerant repeated-close behavior and also covers a close of an
FCB that was never opened. It is a general DOS compatibility fix, not a
VisiCalc-specific path. `tests/dosrun_host_test.py` adds
`an FCB may be closed more than once`, which opens a file through an FCB and
closes it twice; it passes.

**Current workflow (2026-10-24).** QEMU i386, Intel 486, 64 MiB, and a
disposable FAT16 copy of the office test image; launched as
`dosrun C:/VC/VC.COM` from `/n/ata0p1/vc`. The worksheet and headers render.
Emulated keyboard input moves between cells; A1=10, B1=20 and C1=`+A1+B1` were
entered, with C1 displaying 30. `/SS` saved `TEST.VC` (128 bytes, SHA-256
`30a79fedb90b1e9bffd791f88743ebb75aa616bbe9e37d256a6a75aebc46801b`), and `/SQ`
followed by `Y` returned to the ManiOS shell. A second launch loaded the sheet
with `/SL TEST.VC`; A1 displayed 10, B1 displayed 20, C1 displayed 30, and the
formula line showed `+A1+B1`. `/SQ` followed by `Y` returned to the shell again.
The serial stream shows VisiCalc's transient `ERROR` indicator while the sheet
is being parsed (the saved records put the formula before its inputs); it is
cleared by the final redraw, and the restored sheet is correct.

**Status: Working.** Worksheet rendering, navigation, editing, calculation,
saving, clean exit and reopening a saved worksheet are verified. Broader
features such as printing, recalculations beyond the tested formula and other
file operations remain untested.

**Evidence:** [successful save/reopen serial transcript](2026-10-24-visicalc-save-reopen-qemu.txt),
[machine-readable results and hashes](2026-10-24-visicalc-save-reopen-qemu.json),
[entered values and formula result](2026-10-24-visicalc-save-reopen-formula.png),
[reopened worksheet](2026-10-24-visicalc-save-reopen-reopened.png),
[shell after reopening](2026-10-24-visicalc-save-reopen-shell.png),
[earlier failing-load transcript](2026-10-24-visicalc-final-qemu.txt),
[earlier failing-load results](2026-10-24-visicalc-final-qemu.json),
[FCB diagnostic trace](2026-10-24-visicalc-final-fcb-trace.txt),
[earlier opcode and video-status reports](2026-10-23-visicalc-ability.txt),
[video-status QEMU retest](2026-10-23-video-status.txt).

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
- **Evidence:** [guest transcripts and instruction probes](2026-10-23-visicalc-ability.txt),
  [commands and archive/program/kernel/source hashes](2026-10-23-visicalc-ability.json).

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

Evidence for both applications: [transcripts](2026-10-23-catalog.txt),
[commands, exact output, fixture and SHA256 hashes](2026-10-23-catalog.json),
[uHex screenshot](2026-10-23-catalog-uhex.png).

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

Evidence: [before/after transcripts and QEMU regressions](2026-10-23-maven.txt),
[commands, exact output, probes and hashes](2026-10-23-maven.json).

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
Directory creation/removal were unsupported in that build; see the [later directory-service fix](#dos-directory-services-and-windows-setup).

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

Evidence: [guest transcripts and exported-byte comparisons](2026-10-23-search-utf8-basic.txt),
[commands, exact output, fixtures, hashes and regression results](2026-10-23-search-utf8-basic.json).

## Microsoft Windows 1.01

These are pre-fix observations. See the [latest directory-service retest](#dos-directory-services-and-windows-setup) for the current Partial installer result.

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

Evidence: [guest transcripts and instruction probes](2026-10-23-windows101.txt),
[exact outputs, media listings, source and input hashes](2026-10-23-windows101.json).

## Video-status emulation and retest

Fixed source: `4eaaa5c`, following baseline `dea2d01`. A DOS device callback
now handles byte `IN AL,DX` at `03DAh`. Bit 0 reports an inactive display
interval; bit 3 reports vertical retrace. Other status bits are zero. The model
uses deterministic guest-instruction phase (4,096-instruction frame, 64-instruction
line, final 16 instructions blank, final 256 instructions in vertical retrace).
It does not claim a real refresh rate or cycle-accurate CGA/EGA hardware.
Unknown ports, word reads, immediate reads and all writes remain unsupported.
No native machine ports are accessed. See the [runtime documentation](../dosrun.md#minimal-cgaega-compatible-video-status)
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

### VisiCalc after video status (historical 2026-10-23 retest)

The unchanged `VC.COM` no longer reports the previous `EC` error at `1000:6A3E`.
In the 12-second QEMU observation it displays no usable worksheet and does not
return to the shell. A separate bounded host probe ends in a BIOS keyboard-status
polling path at `1000:5FF1`; this identifies a later execution path, not a
successful spreadsheet workflow or proof of an infinite hang. This was the
historical result before the mode-2 renderer fix documented in the current
[VisiCalc recheck](#visicalc) above.

All tests use independent snapshots and original program hashes. Kernel/source
hashes, exact inputs/outputs and regression logs are retained in the evidence.
Existing Android APKs were not rebuilt; no release was created.

Evidence: [transcripts and regression results](2026-10-23-video-status.txt),
[exact outputs, inputs, source/program hashes and test logs](2026-10-23-video-status.json),
[Windows setup welcome](2026-10-23-video-status-windows-start.png),
[Windows directory prompt](2026-10-23-video-status-windows-directory.png).

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

The [PIT/speaker model](../dosrun.md#pit-channel-2-and-pc-speaker) now implements
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
desktop. DOS directory creation was not implemented in that tested build;
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

Evidence: [trace, disassembly and retest transcripts](2026-10-23-pit-speaker.txt),
[exact results, source/kernel hashes and compatibility checks](2026-10-23-pit-speaker.json),
[setup directory-error screen after the PIT fix](2026-10-23-pit-speaker-setup.png).

## DOS directory services and Windows setup

Fixed source: `fbaaaff`, following baseline `eede40b`. `INT 21h/AH=39h` now
creates one directory and `AH=3Ah` removes an empty directory through the real
filesystem. Relative, drive-relative and rooted DOS names resolve within the
selected drive. Existing files/directories, missing parents, nonempty directories,
read-only destinations and invalid/oversized paths return DOS errors with CF set.
Mounted roots cannot be removed; removing a DOS current directory returns error
16. Failed calls preserve drive and current-directory state.

A QEMU COM regression creates `C:\WTEST`, changes into it, creates `SUB`,
returns to root and removes both directories. Host regressions cover error cases,
current/root protection and actual on-disk effects. A separate QEMU guest has
C: on FAT16 and D: on a read-only ISO9660 CD: attempting `D:\ROCHK` creation
correctly returns access denied (CF=1, AX=5). Drive-letter assignment was not
changed; the Windows test's C: is the FAT16 hard disk, not the CD.

Windows 1.01 setup is rerun with the same merged-media disk copy and inputs:
`c`+Enter at Continue, then Enter to accept `C:\WINDOWS`. It passes the former
directory error and advances to the preparation/information screen listing
required Windows disks and hardware information. After guest shutdown, host
mtools confirms that `::/WINDOWS` really exists on the disposable FAT16 image.
A separate snapshot run reaches the same next screen. Status stays **Partial**;
full installation, file-copy/driver configuration, produced `WIN.COM` and the
Windows desktop have not been verified. Display remnants remain visible.

The host, string, runtime, video-status, PIT and QEMU DOS runner suites pass.
All ten previously verified utility tasks also pass on the rebuilt runtime.
Exported TSORT, SAL and converter outputs are checked; FDBanner's screenshot is
identical to the previously visually verified banner. Tests use QEMU 486 /
64 MiB and disposable images. Original media are unchanged; existing APKs were
not rebuilt and no release was created.

Evidence: [guest checks and directory listing](2026-10-23-dos-directories.txt),
[exact outputs, hashes and regression/compatibility results](2026-10-23-dos-directories.json),
[Windows setup after directory creation](2026-10-23-dos-directories-setup.png).

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
images. See [dosrun](../dosrun.md) for the implemented execution environment and
[ManiDOS](../dos.md) for the command shell.


## Windows 1.01 installation completed; startup blocked by EXEC

The latest disposable FAT16 run completes setup, selecting no pointing device,
CGA graphics and no printer. A: explicitly maps to the staged installation
media with `dosrun --mount A=/n/ata0p1 C:/SETUP.EXE`; C: remains the hard disk.
Continue is accepted for the Build, Utilities, Desktop Applications and Write
disks. Setup reports “Microsoft Windows is set up to operate on your computer”
and returns to the ManiOS shell. `WINDOWS/WIN.COM`, `WIN100.BIN`, `WIN100.OVL`,
applications and PIFs persist on the image. Source media are unchanged. The five
supplied disks were merged for this test, so setup also copies extra staged
files; this is a completed setup run on merged media, not a physical disk-swap
test. Staging timestamps are preserved, rather than restoring original archive
dates. No Microsoft binaries are committed to Git or bundled in an APK.

Two file-copy blockers were fixed without suppressing errors:

- `AX=5700h` previously returned CF=1/AX=1 after a successful copy, which setup
  misreported as insufficient space. Handle timestamp get/set now reads/writes
  real FAT modification fields through an additive native syscall. Unsupported
  filesystems, invalid handles/subfunctions and read-only writes fail.
- PIF copying then reached the 15th file in a 512-byte directory cluster and
  failed to create it despite free disk space. Subdirectories now allocate,
  clear and link additional clusters; fixed FAT12/16 root capacity is unchanged.

Startup remains **Partial**. With the installed directory explicitly mapped as
C: (`dosrun --mount C=/n/ata0p1/windows C:/WIN.COM`), WIN.COM prints the 1.01
banner and requests `INT 21h/AX=4B00h` for `C:\WIN100.BIN`. ManiDOS returns
CF=1/AX=1 because child EXEC is unimplemented. WIN.COM displays “Cannot find
Windows startup files”; this error does not establish that files are missing.
The installed Windows desktop has not run. Launching from a shell subdirectory
also does not automatically set the DOS current directory.

Regressions verify timestamps after close/reopen and directly in the disk entry,
invalid/unsupported calls, read-only storage, and 40 persistent files spanning
several directory clusters, independently listed by mtools. The host, runtime,
string, video-status, PIT, QEMU runner and DOS shell suites pass. All ten existing
utility workflows pass; FDBanner's PNG matches its prior verified image exactly.
The APK was not rebuilt and no release was created.

Evidence: [installation and regression record](2026-10-23-windows101-install.txt),
[completion screen](2026-10-23-windows101-installed.png).

## 0.26.0 compatibility retest

Tested source: `c10d9f0`, ManiOS 0.26.0, clean tree; kernel SHA256
`5553a1934d19165f34dcea365ffcb4aaec0d11b81877521da23ec889726fc88c`. No DOS
runtime source changes were made during this batch. Every case boots QEMU i386
(CPU 486, 64 MiB RAM) from a fresh disposable FAT16 copy; most cases observe
8-12 seconds, Windows setup 60 seconds. Generated files are exported with
mtools and compared byte-for-byte. A timeout is a bounded observation, not
proof of a hang.

49 scripted cases covered the ten Working utility workflows and every other
application in the list. All reached their expected 0.26.0 outcome:

| Area | 0.26.0 result |
| --- | --- |
| Ten utilities | The same verified tasks pass: MORE (help, file, stdin, 24-line paging with Space then q), FC (text equal/different and `/B`), FDBanner, B64 (encode/decode/help), BSUM (two text checksums, empty file, 20 KiB binary, usage, missing-file `ERROR`), SemWare GREP, TSORT, SAL SC, Search, utf8tocp (CP437 and CP775, both directions). |
| Checks & Balances | Configuration save/run and the supplied DEMO still work; the menu shows `TRUE $2450.20` and `BANK $279.04` after `SH`. A new ledger entry and save/reopen remain unverified. |
| SemWare editor | An input file now opens and renders instead of failing at `IN AL,61h`. Typed serial input did not reach the editor; editing/save/reopen remain unverified. |
| Windows 1.01 | Setup completes again and writes `WIN.COM`, `WIN100.BIN` and `WIN100.OVL`. `WIN.COM` now loads `WIN100.BIN`, prints the 1.01 banner and `Incorrect DOS version`, then faults at `7504:65D1` (opcode `DB`) with `FatalExit code = 0 stack overflow` text. The runtime reports DOS 4.00; an earlier host probe returning 3.10 reached the unimplemented `AX=5203h` list-of-lists call. Desktop still unavailable. |
| uHex | Usage and the read-only 34-byte view still work; the offset column repeats `00000022` and no shell return was seen. |
| PEDIT, Directory Maven, File Maven, Ability Plus, Bywater BASIC, Warcraft, VisiCalc | Unchanged blockers: operand-size prefix `66h` (PEDIT, Maven scan, Ability installer), `IN AL,DX` on port `0006h` (Maven help/main), x87 `FNINIT` (`DB`), LE/32-bit protected-mode rejection (Bywater, Warcraft), installer `OUT 43h` (`E6`), and VisiCalc's blank screen with no return. |

The host, string, runtime, port/video-status, PIT, EXEC and QEMU DOS runner
suites also pass. Statuses in the application list are unchanged: ten utilities
Working, Checks & Balances/SemWare editor/Windows/uHex Partial, File Maven
Broken, VisiCalc Broken, and PEDIT/Maven-directory/Ability/Bywater/Warcraft
Unsupported. This is task-specific coverage, not a compatibility percentage.

Evidence: [case records, transcripts, hashes and regression log](2026-10-23-retest-026.json),
[readable summary](2026-10-23-retest-026.txt),
[banner](2026-10-23-retest-026-fdbanner.png),
[editor file](2026-10-23-retest-026-tse-file.png),
[editor welcome](2026-10-23-retest-026-tse-welcome.png),
[uHex](2026-10-23-retest-026-uhex.png),
[VisiCalc](2026-10-23-retest-026-visicalc.png),
[Checks & Balances](2026-10-23-retest-026-checks.png),
[Windows setup](2026-10-23-retest-026-windows-setup.png),
[Windows startup](2026-10-23-retest-026-windows-start.png).

## dosprograms.info.tt applications batch (2026-10-23)

Tested source: `c10d9f0`, ManiOS 0.26.0, clean tree; kernel SHA256
`5553a1934d19165f34dcea365ffcb4aaec0d11b81877521da23ec889726fc88c`. No DOS
runtime source changes were made. Ten packages linked from the catalog page
were downloaded on 2026-10-23, placed on 16 MiB FAT16 images and run in QEMU
i386 (CPU 486, 64 MiB RAM) from a fresh disposable copy per case. UI programs
were observed for 6-10 seconds; a timeout is a bounded observation, not proof
of a hang. Downloads and disk images remain outside the repository.

| Application | Version / media | Status | Observed result | Last tested |
| --- | --- | --- | --- | --- |
| Directory Freedom | 4.61B, `df461b.zip` | **Partial** | Directory listing, version 4.61B and the F1-F10 menu render. No navigation, file operation or exit was exercised; shell text remains in uncleared cells. | 2026-10-23 |
| Costa GUI | 1.8.0, `costa180.zip` | **Unsupported** | `COSTA.EXE` prints `DESKTOP.EXE not found.` in root and `C:/COSTA/` layouts. Direct `DESKTOP.EXE` stops during startup on `IN AL,21h` (port `0021h`) at `1D20:70B6`; no GUI. | 2026-10-23 |
| AUMenu | 1.4.3a archive; reports 1.4.3b Unix edition | **Broken** | The linked ZIP contains the Unix edition: it displays `AUMenu for Unix 1.4.3b` and a $79 registration screen, not the DOS menu. The free DOS build was not obtained. | 2026-10-23 |
| VIEW | 17.2, `view.zip` | **Partial** | Opens the 23-page `VIEW.DOC` and renders page 1 with header and status bar; Esc returns. A three-line `INPUT.TXT` shows `Lines 1 to 3 of 3`, but the document overlaps the header and shell remnants remain. Paging/cursor keys were not exercised. | 2026-10-23 |
| DATES | 2002.08, `DATE0208.ZIP` via Wayback | **Unsupported** | Stops during startup on `IN AL,21h` (port `0021h`) at `161C:4006`; no date list or usage text. | 2026-10-23 |
| Cash Register | 7.1j, `POS71J.ZIP` | **Unsupported** | `POS.EXE` stops during startup on `IN AL,21h` (port `0021h`) at `718A:3E98`; no register screen. | 2026-10-23 |
| Figlet | 2.2 DOS port, `figdos22.zip` | **Working** | Standard-font banner for `ManiOS` is correct; `-I 1` prints `20200`; fonts load from `C:/fonts/standard`. | 2026-10-23 |
| EDITV | 4.1 US, `editv41u.zip` | **Partial** | Opens `INPUT.TXT`, displays all three lines and accepts typed text (`Z` appears at 1:1). Save/reopen unverified; F2 cannot be delivered on the serial console. | 2026-10-23 |
| MinEd | 2022.27, `mined-2022.27-dos.zip` | **Unsupported** | DJGPP/go32 extender reports `Load error: no DPMI - Get csdpmi*b.zip`; no editor screen. | 2026-10-23 |
| As-Easy-As | 5.7, DOS files from archive.org item | **Unsupported** | Stops in FPU detection on `OUT 0F0h` at `3704:3004`; following bytes are `FNINIT` (`DB E3`) and `FSTSW` (`D9 3F`). No worksheet. | 2026-10-23 |

### Directory Freedom 4.61B

`dosrun C:/DF.COM` renders the directory list for `C:\`, the
`Directory Freedom / Version 4.61B` box and the F1-F10 function menu. It was
still active at the 7-second limit in this initial display-only run. The later
keyboard retest verified F1 copy, F2 delete and F4 rename on disposable FAT16
images; see the [interactive app retest](2026-10-23-int10-interactive-apps.txt).

### Costa GUI 1.8.0

`dosrun C:/COSTA.EXE` prints `DESKTOP.EXE not found.` and returns, both with the
package in the image root and under `C:/COSTA/`. Running `C:/DESKTOP.EXE`
directly reaches the real blocker:

```text
dosrun: C:/DESKTOP.EXE: can't run opcode EC (at 1D20:70B6)
dosrun: unsupported IN port 0021, AL=24 AX=0024; bytes at 1D20:70AD:
88 26 B3 30 BA 21 00 FA EC 80 FC FC 75 03 24 FB
```

The launcher's file lookup and the runtime's port handling are separate issues;
no Costa screen was reached.

### AUMenu 1.4.3a

`dosrun C:/MENU.EXE` draws a boxed splash identifying itself as
`AUMenu for Unix / Version 1.4.3b` with a `$79.00 Dollars US Per Computer`
registration notice for Solaris, AIX, SCO, UnixWare, FreeBSD and Linux. No menu
is displayed. The archived ZIP appears to be the Unix distribution even though
the catalog entry describes the DOS/Unix package; the free DOS build remains
untested. See the [AUMenu screenshot](2026-10-23-dosprograms-aumenu.png).

### VIEW 17.2

`dosrun C:/VIEW.EXE C:/VIEW.DOC` displays page 1 of a 23-page document
(`1 of 23`, `82%`) with the title/status bars; Esc returns to the shell.
`dosrun C:/VIEW.EXE C:/INPUT.TXT` reports `Lines 1 to 3 of 3`, but the document
text overlaps the header rows and shell text remains visible in both runs.
The keyboard retest delivers PgDn to BIOS INT 16h as scan `51h` and confirms
Esc returns to the shell, but the page and 18% indicator remain unchanged;
multi-page navigation was unverified in that run.

On 2026-10-24 the rebuilt checkout navigated the original 35,362-byte
VIEW.DOC. A separate console bug immediately scrolled when writing the
bottom-right cell, corrupting redraws behind the DOS render cache. Deferred
wrapping fixes that corruption. Three PgDn presses now show lines 24–46,
47–69 and 70–92; PgUp restores 47–69, and Esc exits cleanly. The previous
18% stall was not reproduced and its cause is unresolved. Status remains
Partial because broader VIEW features have not been validated. See the
[investigation report](2026-10-24-view-paging.md),
[native API trace](2026-10-24-view-api-trace.txt), and
[third PgDn screenshot](2026-10-24-view-pgdn-3.png).

Two auxiliary programs of the same package did not add a Working task:
VIEWSEEK 3.1 searched a file containing `alpha` but reported no matches and
exited with `run-time error R6001 - null pointer assignment`; BATCHCNV 4.3
reported `Converting INPUT.TXT to OUT\INPUT.TXT` and `Completed` but left no
file in `OUT`.

Screenshots: [VIEW.DOC](2026-10-23-dosprograms-view-doc.png),
[short file](2026-10-23-dosprograms-view-short.png).

### VIEWSEEK 3.1 (auxiliary)

`dosrun C:/VIEWSEEK.EXE INPUT.TXT alpha` searched a file that contains
`alpha`, reported `No files containing "alpha" were found.`, then exited with
`run-time error R6001 - null pointer assignment`. The program is auxiliary to
VIEW, so this is recorded as a Broken auxiliary result rather than a failure of
VIEW's open/display task. See the [catalog batch summary](2026-10-23-dosprograms-apps.txt).

### DATES 2002.08

Both a fixture date file and `/?` stop before any output:

```text
dosrun: C:/DATES.EXE: can't run opcode EC (at 161C:4006)
dosrun: unsupported IN port 0021, AL=24 AX=0024; bytes at 161C:3FFD:
88 26 CD 0B BA 21 00 FA EC 80 FC FC 75 03 24 FB
```

### Cash Register 7.1j

`dosrun C:/POS.EXE` stops at the same startup pattern:

```text
dosrun: C:/POS.EXE: can't run opcode EC (at 718A:3E98)
dosrun: unsupported IN port 0021, AL=24 AX=0024; bytes at 718A:3E8F:
88 26 D5 A7 BA 21 00 FA EC 80 FC FC 75 03 24 FB
```

No register, configuration or data-file workflow was reached.

### Figlet 2.2

`dosrun C:/FIGLET.EXE -f C:/fonts/standard ManiOS` prints the expected
six-line standard-font banner, and `-I 1` prints `20200`. This is a complete
command-line task with the correct output. See the
[Figlet screenshot](2026-10-23-dosprograms-figlet.png).

### EDITV 4.1

`dosrun C:/EDITV.EXE C:/INPUT.TXT` opens the file in a Turbo Vision editor and
shows all three lines. Sending `Z` plus Enter inserted `Z` before `alpha`
(cursor moved to line 2, column 1). The later retest verified cursor movement,
insertion, Backspace, F2 save and reopening from the same FAT16 image. See the
[EDITV screenshot](2026-10-23-dosprograms-editv.png) and
[multiedit/reopen screenshot](2026-10-23-int10-editv-multiedit.png).

### MinEd 2022.27

The DJGPP/go32 stub reports:

```text
Load error: no DPMI - Get csdpmi*b.zip
```

The editor never starts; DPMI is not implemented.

### As-Easy-As 5.7

`dosrun C:/ASEASY.EXE CHKBOOK.WKS` stops in the math-coprocessor probe:

```text
dosrun: C:/ASEASY.EXE: can't run opcode E6 (at 3704:3004)
dosrun: unsupported OUT port 00F0, AL=00 AX=0000; bytes at 3704:2FFB:
33 C0 F2 AE 74 DE 33 C0 E6 F0 DB E3 89 07 D9 3F
```

`E6 F0` writes port `00F0h`; `DB E3` is `FNINIT` and `D9 3F` is `FSTSW`. This
is the same class of x87 requirement that stops Ability Plus 3.0.

### Runtime gaps exposed

- **PIC mask read (`IN AL,21h`).** Costa's desktop, DATES and Cash Register stop
  on the same startup sequence: `mov [..],ah; mov dx,21h; cli; in al,dx;
  cmp ah,0FCh; jne +3; and al,0FBh`. The current port model accepts only the
  bounded PIT/video ports, so one unimplemented hardware read blocks three
  unrelated applications before their own code runs.
- **x87/FPU detection.** As-Easy-As writes `00F0h`, then executes `FNINIT` and
  `FSTSW`. Ability Plus 3.0 already stops on `FNINIT`; x87 instructions and the
  FPU clear ports remain absent.
- **DPMI/32-bit extender.** MinEd's go32 stub cannot load without a DPMI host.
- **Text-screen clearing and scrolling.** Directory Freedom and VIEW leave
  shell/boot text in cells and rows they do not overwrite; VIEW's short-file
  rendering overlaps the header. This is the incomplete BIOS text scrolling
  already noted in [dosrun](../dosrun.md).
- **AUMenu packaging.** The linked archive is the Unix edition. This is a
  source/package issue, not a ManiOS execution result.

### Evidence and inputs

Input hashes, command lines, cleaned transcripts and per-case checks:
[readable summary](2026-10-23-dosprograms-apps.txt) and
[JSON records](2026-10-23-dosprograms-apps.json). Screenshots:
[Directory Freedom](2026-10-23-dosprograms-directory-freedom.png),
[AUMenu](2026-10-23-dosprograms-aumenu.png),
[VIEW.DOC](2026-10-23-dosprograms-view-doc.png),
[VIEW short file](2026-10-23-dosprograms-view-short.png),
[EDITV](2026-10-23-dosprograms-editv.png),
[EDITV typed](2026-10-23-dosprograms-editv-typed.png),
[Figlet](2026-10-23-dosprograms-figlet.png).

No application binaries, ZIP archives or disk images are committed.

## compat4 batch: Info-ZIP, DOSStart, Spectra, PasswordBox (2026-10-23)

Tested source tree: `dac8108`, ManiOS 0.26.0; kernel SHA256
`5553a1934d19165f34dcea365ffcb4aaec0d11b81877521da23ec889726fc88c`. The
kernel is the same build recorded for `c10d9f0`; `dac8108` is the
compatibility-documentation reorganization. Each case booted a fresh 16 MiB
FAT16 image in QEMU i386 (CPU 486, 64 MiB RAM) with the serial console.
Downloads and disk images remain outside the repository.

| Application | Version / media | Status | Observed result | Last tested |
| --- | --- | --- | --- | --- |
| Info-ZIP UnZip | 6.00 (20 Apr 2009), `unz600x3.exe` | **Working** | `-l`, `-t`, `-p`, `FUNZIP` and extraction of all four members, including nested `SUB/THREE.TXT`, succeed; extracted hashes match the fixtures. | 2026-10-23 |
| Info-ZIP Zip | 2.32, `zip232x.zip` | **Unsupported** | 16-bit `ZIP16.EXE` stops at `0000:0001` (`E5`); `ZIP16-SM.EXE` at `0000:0001` (`EF`); 32-bit `ZIP.EXE` reports no DPMI. No archive is created. | 2026-10-23 |
| DOSStart | 1.9b build 7.08.2002, `dosstart.zip` | **Unsupported** | `DANSHELL.EXE` stops on `IN AL,21h` at `1FBB:6B78`; no launcher screen. | 2026-10-23 |
| Spectra | 1.0, `spec10.zip` | **Unsupported** | `HITECH.EXE` stops on `IN AL,21h` at `1345:2710`; no GUI. | 2026-10-23 |
| PasswordBox | 0.11 DOS build, `pbox011-dos.zip` | **Unsupported** | DJGPP/go32 stub reports `Load error: no DPMI`; no password database. | 2026-10-23 |

### Info-ZIP UnZip 6.00

`dosrun C:/UNZIP.EXE -v` identifies `UnZip 6.00 of 20 April 2009`, compiled
for MS-DOS as a 16-bit large-model executable. The command-line tasks are
fully exercised:

- `dosrun C:/UNZIP.EXE -l C:/KNOWN.ZIP` lists the four members (`ONE.TXT`,
  `TWO.TXT`, `DATA.BIN`, `SUB/THREE.TXT`) with their sizes and dates.
- `dosrun C:/UNZIP.EXE -t C:/KNOWN.ZIP` tests all four CRCs and prints
  `No errors detected in compressed data of C:/KNOWN.ZIP.`
- `dosrun C:/UNZIP.EXE -o C:/KNOWN.ZIP -d C:/OUT` creates `OUT` and inflates
  all four files. The exported `ONE.TXT`, `TWO.TXT`, `DATA.BIN` and
  `SUB/THREE.TXT` match the fixture SHA-256 values in the transcript.
- `dosrun C:/UNZIP.EXE -p C:/KNOWN.ZIP ONE.TXT` prints `alpha one` and
  `second line`; `dosrun C:/FUNZIP.EXE C:/KNOWN.ZIP` prints the first member
  and warns that the remaining entries are ignored.

UnZip prints `Warning: TZ environment variable not found, cannot use UTC
times!!`; listing, CRC checking and extraction are unaffected. Input
`unzip.exe` SHA-256:
`c3d2eb5322ed896f34efde8b1a7e4bb2ce0d9f26c28fc14c6d5ad7db75f6f3fb`.

### Info-ZIP Zip 2.32

The package's 16-bit builds do not reach their own startup code:

```text
dosrun C:/ZIP16.EXE -q C:/TEST.ZIP C:/ONE.TXT C:/TWO.TXT
dosrun: C:/ZIP16.EXE: can't run opcode E5 (at 0000:0001)
dosrun: unsupported IN port 0000, AL=80 AX=0080; bytes at 0000:FFF8:
00 00 00 00 00 00 00 00 E5 00 24 1D 00 00 00 00
```

The small-model build fails the same way with `EF` and `OUT 3FCE` at
`0000:0001`. The `TEST.ZIP` and `TREE.ZIP` commands create no archive, and the
separate small-model check leaves `SM.ZIP` absent. The 32-bit `ZIP.EXE` is a
DJGPP/go32 build and prints:

```text
Load error: no DPMI - Get csdpmi*b.zip
```

The 16-bit binaries are UPX-packed startup stubs that the current MZ loader
does not unpack; the 32-bit build additionally needs a DPMI host. Input `zip16.exe`
SHA-256:
`5abd88ee0d7f184b888a3d6c3a2e32ec7046a0e690744f6995e50766ba909fbd`.

### DOSStart 1.9b

`dosrun C:/DANSHELL.EXE` stops during startup:

```text
dosrun: C:/DANSHELL.EXE: can't run opcode EC (at 1FBB:6B78)
dosrun: unsupported IN port 0021, AL=24 AX=0024; bytes at 1FBB:6B6F:
88 26 5F A8 BA 21 00 FA EC 80 FC FC 75 03 24 FB
```

The README requires a 286, a VGA screen and a mouse driver. The run does not
reach the mouse check or the launcher: the PIC-mask read at port `0021h` stops
it first. Input `DANSHELL.EXE` SHA-256:
`bf1fbf209b663ef336cd8a1a14472f5505a0bfeb71b35cdf82dfc90ec0da3bec`.
See the [startup screenshot](2026-10-23-compat4-dosstart.png).

### Spectra 1.0

`dosrun C:/HITECH.EXE` stops in the same startup pattern:

```text
dosrun: C:/HITECH.EXE: can't run opcode EC (at 1345:2710)
dosrun: unsupported IN port 0021, AL=24 AX=0024; bytes at 1345:2707:
88 26 0B 86 BA 21 00 FA EC 80 FC FC 75 03 24 FB
```

No Spectra window or menu is reached. Input `HITECH.EXE` SHA-256:
`87bed2dd78df75824d9d4f406c3d39b7a89811916a1f4ef79f5494a425c4ac31`.
See the [startup screenshot](2026-10-23-compat4-spectra.png).

### PasswordBox 0.11

`dosrun C:/PBOX.EXE --help` starts the DJGPP/go32 stub and stops before the
program:

```text
Load error: no DPMI - Get csdpmi*b.zip
```

PasswordBox is a 32-bit build; no password database is opened. Input
`PBOX.EXE` SHA-256:
`9e7efc9b8d6fc24a8bcfc7ff7fa04bb0bbc2dd207616b289e41ee346ce00719c`.

### Interactive input limitation (EDITV)

A separate 2026-10-23 probe tried to save and exit EDITV with QEMU monitor
key events. It sent `Z`, `F2` and `Alt+X`; the editor did not return, `Z` was
not inserted (the line was never completed with Enter), and `INPUT.TXT`
remained 37 bytes with SHA-256
`c30a4d57cc62616d8e95765ee079a45b66057f9ce71e4b1de5b7c0cdeee65eab` before
and after. This is consistent with the console's cooked line discipline:
printable text is delivered a line at a time, the probe sent no Enter, and
function and Alt keys have no ASCII representation. An earlier serial test
that sent `Z` followed by Enter did insert text, so this probe confirms the
existing EDITV **Partial** result rather than a new EDITV failure. Details:
[interactive probe JSON](2026-10-23-compat4-editv-interactive.json); screenshots:
[start](2026-10-23-compat4-editv-start.png),
[typed](2026-10-23-compat4-editv-typed.png),
[save attempt](2026-10-23-compat4-editv-saved.png),
[exit attempt](2026-10-23-compat4-editv-exit-failed.png),
[reopen](2026-10-23-compat4-editv-reopened.png).

### Runtime gaps exposed

- **PIC mask read (`IN AL,21h`).** DOSStart and Spectra join Costa's desktop,
  DATES and Cash Register in stopping on the same startup sequence. Five
  unrelated programs now fail before their own code because the port model
  does not answer this hardware read.
- **16-bit UPX startup.** Both Info-ZIP Zip 16-bit builds are UPX-packed and
  stop at `0000:0001` on the first instruction or port access. This is a
  loader gap distinct from missing DOS services.
- **DPMI/32-bit extender.** MinEd, PasswordBox and the 32-bit Info-ZIP Zip
  build all stop in their go32 stubs with `Load error: no DPMI`.
- **Keyboard scan codes.** EDITV, TSE, VIEW and Directory Freedom cannot be
  driven through function, cursor or Alt keys because the console input path
  is line-oriented. Printable text plus Enter still reaches DOS programs.

### Evidence and inputs

Command lines, cleaned transcripts and per-case checks:
[Info-ZIP transcript](2026-10-23-compat4-infozip.txt) and
[JSON records](2026-10-23-compat4-infozip.json); [startup batch transcript](2026-10-23-compat4-extra.txt)
and [JSON records](2026-10-23-compat4-extra.json). Screenshots:
[DOSStart](2026-10-23-compat4-dosstart.png),
[Spectra](2026-10-23-compat4-spectra.png). The transcript lists SHA-256
values for the downloaded archives, the executables and the fixture files.

No application binaries, ZIP archives or disk images are committed.

## FreeDOS-repo Games batch (2026-10-23)

Tested source tree: `44d4751`, ManiOS 0.26.0; kernel SHA256
`5553a1934d19165f34dcea365ffcb4aaec0d11b81877521da23ec889726fc88c`.
Each case booted a fresh 16 MiB FAT16 image in QEMU i386 (CPU 486, 64 MiB
RAM) with the serial console. The package files were copied to the image
root; downloads and disk images remain outside the repository. All eleven
games on the [FreeDOS-repo Games page](https://clasqm.github.io/freedos-repo/Games.html)
were tested unchanged.

| Application | Version / media | Status | Observed result | Last tested |
| --- | --- | --- | --- | --- |
| Champ Galagon | 1.0 (registered, 1997), `galagon.zip` | **Unsupported** | Returns to the shell in under 0.6 s with no console output or title screen. | 2026-10-23 |
| Dr Mind Lite | LITE variant, 2019, `drlite.zip` | **Unsupported** | Stops on `OUT 0x3C8` (VGA DAC palette) at `1010:02CA`; no board. | 2026-10-23 |
| Drobos | 1.3 (DRoboWHOids), `drobos.zip` | **Unsupported** | Stops on x87 opcode `D8` at `1010:0487`; no game screen. | 2026-10-23 |
| Ford Driving Simulator | 1988 models, `ford.zip` | **Unsupported** | Stops on `IN` port `0021h` (PIC) at `1010:10C8`; no simulator screen. | 2026-10-23 |
| Life2 | text-mode Conway Life, `life2.zip` | **Partial** | Instructions and EDIT mode render; RUN mode stops on `OUT` port `0040h` (PIT channel 0) at `111E:0B64`. | 2026-10-23 |
| Mahjong! | 1996/1997 SoftSpot, `mahjong.zip` | **Unsupported** | Stops on operand-size prefix `66h` at `101D:0990`; no screen. Requires SVGA 1024x768 VESA graphics and a mouse. | 2026-10-23 |
| Mine Mayhem | freeware by Jason Hood, `minemay.zip` | **Unsupported** | Stops on operand-size prefix `66h` at `1426:2DC3`; no screen. DJGPP/go32 build. | 2026-10-23 |
| Nero 5 | 08.02.2000, `nero5.zip` | **Unsupported** | Returns to the shell in under 0.6 s with no console output or menu; BGI EGAVGA 640x480 initialization produces no board. | 2026-10-23 |
| ShuffleV | 1.1 (2000), `shufflev.zip` | **Unsupported** | Turbo Vision board renders, then stops on opcode `66h` at `1C36:07FD`; no move is possible. | 2026-10-23 |
| Hanoi | 1.3 (2000), `hanoi.zip` | **Unsupported** | LZEXE startup unpacks, then stops on `IN` port `0006h` at `115B:0603`; no board. | 2026-10-23 |
| TypeFast | typing drill, `typefast.zip` | **Broken** | Starts and remains running, but the console stays blank; no prompts or game screen appear. | 2026-10-23 |

### Champ Galagon

`dosrun C:/GALAGON.EXE` returns to the shell in under 0.6 s with no console
output. The screenshot shows the shell prompt and no title or VGA frame.
The binary contains Borland C++ and DiamondWare sound code and requires VGA
graphics; the exact failing display or sound probe was not isolated. No
gameplay, input or audio was verified. Input `GALAGON.EXE` SHA-256:
`fde1aa1569117d7748800fc21aa12fe5c36655921cf1d12a1b7670a32cba180c`.
See the [return screenshot](2026-10-23-compat4-games-galagon.png).

### Dr Mind Lite

`dosrun C:/DRMIND.EXE` stops during VGA palette programming:

```text
dosrun: C:/DRMIND.EXE: can't run opcode EE (at 1010:02CA)
dosrun: unsupported OUT port 03C8, AL=F0 AX=00F0; bytes at 1010:02C1:
8B C6 02 46 0A BA C8 03 EE 8B C6 03 C6 03 C6 C4
```

The package requires VGA or MCGA; no board or menu is reached. Input
`DRMIND.EXE` SHA-256:
`391645e0dd87b0c66934bc3c3a32f9954864102e3be6c83b1949afb462686055`.

### Drobos

`dosrun C:/DROBOS.EXE` stops on the first x87 instruction:

```text
dosrun: C:/DROBOS.EXE: can't run opcode D8 (at 1010:0487)
```

No game screen is reached. Input `DROBOS.EXE` SHA-256:
`eef6382b4f415716e17e905457b82101aaed7fff4b4974f6cbd472758bb52329`.

### Ford Driving Simulator

`dosrun C:/SIMUL.EXE` stops on the PIC-mask read:

```text
dosrun: C:/SIMUL.EXE: can't run opcode EC (at 1010:10C8)
dosrun: unsupported IN port 0021, AL=00 AX=1C00; bytes at 1010:10BF:
52 57 56 1E 06 BA 21 00 EC 0C 03 EE B8 00 00 8E
```

No simulator screen is reached. Input `SIMUL.EXE` SHA-256:
`0dbe08a5f5eac0a0aeba4ec8a310f8ca7b895c16ffb4e156aa82e36bba8d4a69`.

### Life2

`dosrun C:/LIFE2.EXE` presents the two instruction pages and reaches EDIT
mode. Starting RUN mode stops when BASIC's `SOUND` writes the PIT channel-0
port:

```text
dosrun: C:/LIFE2.EXE: can't run opcode E6 (at 111E:0B64)
dosrun: unsupported OUT port 0040, AL=00 AX=0800; bytes at 111E:0B5B:
8C 0E 22 00 07 B8 00 08 E6 40 8A C4 E6 40 80 3E
```

The instructions and EDIT screen are usable, but no generation runs.
Input `LIFE2.EXE` SHA-256:
`45f45ec0ff09ef9eb7f7a222b255d39ee263f6119467ec28e34a2f042760fd0a`.
See the [RUN fault screenshot](2026-10-23-compat4-games-life2.png).

### Mahjong!

`dosrun C:/MAHJONG.EXE` stops on the operand-size prefix:

```text
dosrun: C:/MAHJONG.EXE: can't run opcode 66 (at 101D:0990)
```

No screen is reached. The package documentation requires a 386 or better,
SVGA 1024x768 256-colour VESA graphics, a mouse and a protected-mode DOS
environment. Input `MAHJONG.EXE` SHA-256:
`ca03bb09254cbea8eed1ce858245fc96d9d04a4c35e56840f7927c5d7851753d`.

### Mine Mayhem

`dosrun C:/MINE.EXE` stops on the operand-size prefix:

```text
dosrun: C:/MINE.EXE: can't run opcode 66 (at 1426:2DC3)
```

No screen is reached. The executable is a DJGPP/go32 build; the package
includes `CWSDPMI.EXE`, but the current runner has no DPMI host. Input
`MINE.EXE` SHA-256:
`bac57d57471e381af874dd88c880b774b1b6de15d20004460c12d52f5420a56d`.

### Nero 5

`dosrun C:/NERO5.EXE` returns to the shell in under 0.6 s with no console
output. The screenshot shows the shell prompt and no menu or chessboard.
The executable contains Borland BGI EGAVGA drivers, including 640x480 VGA;
the current DOS video path provides text and mode 13h only. No chess
position or move was verified. Input `NERO5.EXE` SHA-256:
`db83314435dc6f952b6496f4225d14b02274a171887f6c081cbbc1bc9c557936`.
See the [return screenshot](2026-10-23-compat4-games-nero5.png).

### ShuffleV

`dosrun C:/SHUFFLEV.EXE` draws the Turbo Vision 4x4 puzzle board, then
stops:

```text
dosrun: C:/SHUFFLEV.EXE: can't run opcode 66 (at 1C36:07FD)
```

The screen remains visible, but no move is possible. The executable is
LZEXE-compressed; the loader unpacked it successfully before the fault.
Input `SHUFFLEV.EXE` SHA-256:
`60895319166b91b66e4dbac462963009c351267c4d8ea7c4fd113dbc2262900b`.
See the [board and fault screenshot](2026-10-23-compat4-games-shufflev.png).

### Hanoi

`dosrun C:/HANOIE.EXE` unpacks the LZEXE-compressed executable and then
stops on a hardware port read:

```text
dosrun: C:/HANOIE.EXE: can't run opcode EC (at 115B:0603)
dosrun: unsupported IN port 0006, AL=54 AX=B854; bytes at 115B:05FA:
FC 0A DB 74 16 AC 8A D8 EC A8 01 75 FB FA EC A8
```

No board is reached. Input `HANOIE.EXE` SHA-256:
`a73066b6ad8ad3ef45852988a4b2532b0938306283858ab30bb1ff62a17a37ab`.

### TypeFast

`dosrun C:/TYPEFAST.EXE` starts and remains running, but the console stays
blank. Sending `n1`, `a` and Enter produced no visible prompts or game
screen; the process did not return during the bounded observation. The
README identifies a Turbo C/PCcurses build that uses the PC display
character set. No typing task was completed. Input `TYPEFAST.EXE` SHA-256:
`a995ed918ecba7eb934269cfd3b7e9a9833a19ff21e275f205723ab12d4583e6`.
See the [blank screenshot](2026-10-23-compat4-games-typefast.png).

### Evidence and inputs

The full transcript and per-case observations are in
[2026-10-23-compat4-games.txt](2026-10-23-compat4-games.txt); the
machine-readable package and executable hashes are in
[2026-10-23-compat4-games.json](2026-10-23-compat4-games.json).
Screenshots: [Champ Galagon](2026-10-23-compat4-games-galagon.png),
[Nero 5](2026-10-23-compat4-games-nero5.png),
[Life2](2026-10-23-compat4-games-life2.png),
[ShuffleV](2026-10-23-compat4-games-shufflev.png),
[TypeFast](2026-10-23-compat4-games-typefast.png).

## Zork I: The Great Underground Empire

- **Tested build:** ManiOS 0.26.0, source commit
  `55a788a1f78d53636604f4c4d114401cd34b909b`; kernel SHA-256
  `8599009f47058371966bf01f622ce6991fccf7229de26742bc43c72639cdd7cd`.
- **Story:** [historicalsource/zork1](https://github.com/historicalsource/zork1)
  `COMPILED/zork1.z3`, 86,838 bytes, MIT license. SHA-256
  `37084966477dff679282de42974b2077156b1bd68fad92a65d4ea94d8eb64d79`.
- **Interpreter:** ZIP 2.00 (`ZIP.EXE`), SHA-256
  `2bb2577f47e6900154995c9a8138ddb74e3de5cdf94b31426987fcdf4b8794d7`.
- **Command:** `dosrun C:/ZIP.EXE ZORK1.Z3`
- **Status:** **Working**.

ZIP 2.00 starts the story and renders the title, `West of House` and the
mailbox description. `look`, `open mailbox`, `read leaflet`, `north`,
`inventory` and `score` produce readable output. The game accepts ordinary
printable commands and Enter; no function or cursor keys are needed.

`save` asks for a file name, writes `ZORK1.SAV` (13,330 bytes) and reports
`OK.`. After `quit`, `y` and the final `[Hit any key to exit.]` keypress,
control returns to the `manios%` prompt. A fresh boot and `restore` of the
same file reports `OK.`; `look` returns to West of House. The save file was
kept outside the repository; its SHA-256 is
`db9e1cd6c4ca8d0f7d8276ddb6c99d799531658a011aa8f9525c213021a0e199`.

The status line still clips its right side: `Score: 0` and `Moves` are not
both fully visible in every frame, and a black rectangle remains at the
bottom-right of the text screen. Both are cosmetic for the tested commands.

Screenshots: [start](2026-10-23-text-zork1-start.png),
[look](2026-10-23-text-zork1-look.png),
[save](2026-10-23-text-zork1-save.png),
[restore](2026-10-23-text-zork1-restore.png).

## Adventure (350-point DECUS port)

- **Tested build:** ManiOS 0.26.0, source commit
  `55a788a1f78d53636604f4c4d114401cd34b909b`; kernel SHA-256
  `8599009f47058371966bf01f622ce6991fccf7229de26742bc43c72639cdd7cd`.
- **Program:** Kevin Black's DOS build of the DECUS 350-point version from
  [rickadams.org](https://rickadams.org/adventure/e_downloads.html),
  `adv350kb.zip`. Archive SHA-256
  `15688040a21163a236aa2639022807d4aa380e06fb1056fd25aff12e8146dd8a`;
  `ADVENTUR.EXE` SHA-256
  `26bbe95ee8a3097f022bf87a4c3c41a3eb3b357da5dbe32dc70ed9d2f69425f0`.
- **Command:** `dosrun C:/ADVENTUR.EXE`
- **Status:** **Working**.

The welcome screen appears and the game asks `Would you like
instructions?`. The answer is read as a 20-character FORTRAN record
(`FORMAT(20A1)`), so `N` followed by 19 spaces is accepted; an unpadded
`n`, `N`, `no` or `NO` is rejected with `Please answer the question.`.
This is an input-format detail of the program, not a general DOS input
failure.

After the answer, `look`, `inventory`, `north`, `quit` and `Y` work. The
quit message reports 32 of a possible 350 points in 4 turns and returns to
the `manios%` prompt. Screenshots:
[look](2026-10-23-text-adventure-look.png),
[quit](2026-10-23-text-adventure-quit.png).

## Planetfall (Infocom Sampler demo)

- **Tested build:** ManiOS 0.26.0, source commit
  `55a788a1f78d53636604f4c4d114401cd34b909b`; kernel SHA-256
  `8599009f47058371966bf01f622ce6991fccf7229de26742bc43c72639cdd7cd`.
- **Story:** official 1984 Infocom Sampler
  [`sampler1_R26.z3`](https://www.ifarchive.org/if-archive/infocom/demos/),
  112,610 bytes. SHA-256
  `8a333e2381095439acb66db43385f4f94ba5fe63c478c95079ae764bacd2942b`.
  The sampler contains a Planetfall sample alongside Zork I, Infidel and
  The Witness. The full commercial Planetfall story was not tested or
  committed.
- **Commands:** `dosrun C:/ZIP.EXE SAMPLE.Z3`, then `planet`; repeated
  with `JZIP.EXE`.
- **Status:** **Partial**.

The sampler starts under ZIP 2.00 and JZIP, renders the introduction and
shows `(Hit the RETURN key now to start the tutorial.)>>`. Entering
`planet` selects the Planetfall sample. Under ZIP the text window is
cleared to blue and the sample text is not readable. Under JZIP only the
first line, `Briefly, here are the instructions for Planetfall. You are a
lowly Ensign`, appears at the bottom of the screen and the sample does not
advance. The sampler's own selection prompt and the first sample line are
therefore demonstrated, but Planetfall gameplay is not.

The likely remaining gap is the text-window scroll/presentation path used
after the sample clears the window and prints a page of text; ZIP/JZIP do
not keep the sample readable. Full Planetfall was skipped for licensing
reasons, so no commercial story file is part of this test.

Screenshots: [sampler start](2026-10-23-text-planetfall-sampler-start.png),
[Planetfall selection](2026-10-23-text-planetfall-sampler-select.png).

### Text-adventure interpreter notes

- **ZIP 2.00** is the working interpreter for Zork I and the selected
  interpreter for this batch.
- **JZIP** starts Zork I and the sampler, but its status-line and screen
  formatting are poor, and the sampler stalls after its first sample line.
- **Frotz 2.55** and **Frotz 2.53** stop at `OUT 0043h` (PIT channel 0
  control) before the game.
- **DZIP** stops at `OUT 00F0h`.
- The `dosapi.c` change in this batch implements BIOS `INT 10h`
  `AH=06h/07h` text-window scrolling and bottom-row scrolling for DOS
  console output. It made Zork I's room text and commands readable; the
  status-line clipping and bottom-right unpainted area remain.

The full transcript and hashes are in
[2026-10-23-text-adventures.txt](2026-10-23-text-adventures.txt) and
[2026-10-23-text-adventures.json](2026-10-23-text-adventures.json).

No application binaries, ZIP archives or disk images are committed.

## 0.26.0-10232026 PIC and PIT follow-up

The DOS runner now models the master PIC mask at port `21h` and PIT channel 0
modes 0, 2 and 3 with count latching. These are bounded software models driven
by guest instruction count; they do not deliver hardware interrupts. Costa GUI,
DATES and Cash Register now pass their earlier PIC/timer port faults but still
stop on unrelated runtime or instruction gaps. Life2 enters RUN mode, though a
generation was not verified. See the [retest record](2026-10-23-10232026-retest.txt).

## Life2 keyboard navigation retest

The DOS runner now opens `/dev/kbd` while a DOS program is active and maps
PS/2 arrows and function keys to DOS/BIOS scan codes. It still accepts cooked
serial input; serial ANSI cursor sequences are decoded when the terminal
preserves the ESC byte. A fresh Life2 disk was launched in QEMU (486, 64 MiB).
Using PS/2 input, the test drew a 2x2 pattern and entered RUN mode without a
runtime fault. The board remained visible. This closes the input/navigation
gap, but the current evidence does not establish a deterministic generation
transition, so Life2 remains Partial. See the
[test record](2026-10-23-life2-keyboard.txt).

## BIOS INT 10h text display follow-up

The DOS BIOS text services now use the `BH` fill attribute for `AH=06h/07h`
window scrolling (the previous code incorrectly used `BL`). Setting mode 3
clears all eight 80x25 text pages and resets their cursors. Switching pages
repaints the newly active page, and `AH=01h` sends cursor visibility and an
approximate block/underline/bar shape to the terminal. The VGA text driver
now applies ANSI cursor visibility and style sequences to its hardware
cursor. `tests/dosrun_runtime_test.py` covers rectangular scroll up/down,
clear-with-zero-lines, untouched cells, fill attributes, page switching,
cursor shape readback, and text-mode reset. The runtime suite, DOS QEMU test,
and kernel build passed.

The compatibility test fixtures were present under the audit directories, so
the four target applications were also rerun with a fresh copy of each FAT
image. SemWare `E.EXE` now opens its sample file without the old XLAT fault;
VIEW opens its documentation, and Page Down advances after the CGA port stub
without a DOS fault, though the paged screen still has layout defects;
EDITV displays the file and accepts the typed `Z`; Directory Freedom draws
its file list and function-key menu. No file operation or save/reopen flow was
verified for this batch, so all four remain Partial. The new screenshots and
transcript are in the [BIOS INT 10h test record](2026-10-23-bios-int10-text.txt).

### Interactive application checks after the BIOS INT 10h update (2026-10-23)

Follow-up work on fresh disposable FAT16 copies improved three interactive
applications. EDITV supports insertion, cursor movement, Backspace, F2 save and
reopen. TSE accepts physical keyboard input, Ctrl+K S save and Alt+X exit; the
reopened file contains the inserted `z`. Directory Freedom now completes F1
copy, F2 delete and F4 rename; the F1 copy was independently checked byte for
byte. These applications are marked Working for these tested basic workflows.

VIEW remains Partial. PgDn reaches BIOS INT 16h as scan `51h`, and Esc returns to
the shell, but repeated PgDn leaves the visible page and `18% of file read`
unchanged. The input/key buffering changes and all test results are recorded in
the [interactive app record](2026-10-23-int10-interactive-apps.txt). Evidence:
[EDITV reopened after edits](2026-10-23-int10-editv-multiedit.png),
[TSE reopened file](2026-10-23-int10-semware-reopened.png),
[Directory Freedom F1 copy](2026-10-23-int10-directory-freedom-copy.png),
[Directory Freedom F4 rename](2026-10-23-int10-directory-freedom-rename.png),
[F2 delete prompt](2026-10-23-int10-directory-freedom-delete-prompt.png),
and [VIEW page](2026-10-23-int10-view-page.png).


## VIEW and Life2 focused follow-up — 2026-10-24

VIEW traversed the original document to EOF with 35 PgDn presses and returned
to the initial page with 35 PgUp presses. Home/End and two consecutive F2
searches for `bookmarks` passed, followed by clean Esc. These checks extend
the earlier three-page test; printing and other formats remain untested.

Life2's blinker did not complete an unattended cycle in the bounded native
run. Its sound routine hooks INT 08h, programs PIT channel 0 and waits for a
counter that the handler decrements. ManiDOS models the PIT counter but does
not deliver that IRQ to the installed handler. Synthetic IRQ0 plus valid
PIC EOI handling in an isolated host experiment restores repeated horizontal
and vertical generations. That experiment is diagnostic evidence, not a
production fix. Life2 remains Partial.

See the [follow-up report](2026-10-24-navigation-followup.md) for evidence,
limitations and reproduction commands. No production code changed in this
follow-up; original disk fixtures were preserved.


## PIT0 / IRQ0 implementation and Life2 — 2026-10-24

The earlier Life2 timer limitation is now addressed by PIT0 oscillator
clocks, edge-latched master-PIC IRQ0, mask/IF/EOI handling, execution of
installed guest interrupt vectors, and a chainable BIOS INT 08h / INT 1Ch
path. Native Life2 now completes a horizontal → vertical → horizontal
blinker cycle without extra input and exits through Q followed by Esc.
VIEW's full document traversal/search and existing DOS regression suites
also pass. Life2 remains Partial for untested patterns and broader features.
See the [timer implementation report](2026-10-24-pit-irq0.md), including
scope limits and the separate correction to the old PIT test.


## Current tested-workflow status — 2026-10-24

The following entries are **Working** for the confirmed workflows below.
Earlier dated Partial observations remain as test history; this decision
supersedes those status labels for VIEW and Life2.

| Program | Confirmed workflow | Current status |
| --- | --- | --- |
| EDITV 4.1 | Editing, cursor movement, Backspace, save and reopen | Working |
| Directory Freedom 4.61B | File copy, rename and delete | Working |
| SemWare TSE 2.50e | Editing, save and reopen | Working |
| VIEW 17.2 | PgDn/PgUp through the document, Home/End, repeated F2 search, Esc | Working |
| Life2 | Pattern creation, RUN, horizontal/vertical/horizontal blinker evolution, Q/Esc exit | Working |

Working describes these tested workflows, not every application feature.
Life2's isolated BASIC runtime error during an earlier marking run remains
unresolved and documented in the [timer report](2026-10-24-pit-irq0.md).
The historical VIEW 18% stall remains unreproduced. Printing, other document
formats and broader Life2 patterns are not included in the confirmed scope.

## Compatibility Wave 2 — easy utility candidates

Tested on 2026-10-24 against the local ManiOS 0.26.0-10232026 kernel in QEMU
486 / 64 MiB with a disposable FAT16 image. The six unchanged FreeDOS binaries
were taken from local archives; see the [batch transcript and hashes](2026-10-24-wave2-easy-wins.txt).

### FreeDOS FIND 01Nov2003

**Working.** The initial failure was an invocation mismatch: FIND selects A:
before it opens relative filenames, while the fixture was placed on C:. With
the disposable FAT16 disk mounted as A:, "apple" printed apple red, /I "apple"
printed apple red and Apple green, and /V "banana" printed both nonmatching
lines. MISSING.TXT reported FIND: MISSING.TXT: No such file. The package
identifies this GPL-2 binary as version 01Nov2003.

### FreeDOS ATTRIB

**Working.** The DOS INT 21h/AH=43h service had returned success without reading
or changing attributes. A new filesystem-backed path operation reads and
writes FAT directory-entry attribute bits, and DOS search records now carry
those bits too. On a disposable FAT16 image, ATTRIB listed [----A], changed
+R to [---RA], a fresh dosrun listed [---RA], and -R restored [----A].
mattrib independently showed A R after setting and only A after clearing.
Invalid attribute masks and missing files are covered by the DOS API regression
test. The BonusCD binary's version was not verified.

### FreeDOS DELTREE

**Working for the tested workflow.** On 2026-10-25, six guest cases on
disposable FAT16 images verified the nested tree deletion prompt, cancellation,
`/Y` root-safety confirmation, root protection, and missing-target handling.
The confirmed deletion removed all of `TESTTREE` while preserving `KEEP.TXT`
and the unrelated `OTHER` directory. `N` and the root-safety `Q` left the disk
contents intact. The root target remained guarded. The BonusCD build's version
is v1.02g.

### FreeDOS MEM

**Broken.** ManiOS's initial MCB existed, but INT 21h/AH=52h did not expose a
DOS list-of-lists pointer. The service now points to the MCB at segment 0x0FFF,
and regression checks validate the MCB header and chain termination after
allocation, resize, and free cycles. MEM now passes that failure and stops at
unsupported opcode 66h at 1010:03D5; it still produces no memory summary.
Its version was not verified.

### FreeDOS VCHOICE

**Partial.** On 2026-10-25, a two-item menu drawn by separate VCLS, VFRAME and
VECHO processes was read by VCHOICE from the shared text screen. Arrow plus
Enter and digit selection both returned choice 2; Escape returned 0. Help mode
displayed when `HELPPATH=C:` was passed to `dosrun` and the supplied
`VCHOICE.EN` file was copied into the test image (the BonusCD has no help
file). The `/T 8 /D 1` timeout case returned 0 instead of the default choice,
so timeout selection remains unverified. The BonusCD binary's version was not
verified.

### FreeDOS VCLS

**Working.** VCLS reads BIOS video geometry directly from the BIOS data area.
ManiOS reported mode and cursor geometry through BIOS calls but left the
corresponding BDA bytes zero, so VCLS calculated the wrong screen range. The
BDA now reports mode 3, 80 columns, 25 rows and the active page. VCLS clears the
visible VGA screen; the capture shows the shell prompt redrawn on an otherwise
blank display. The [fixed VCLS screen capture](2026-10-24-wave2-fixup-vcls.png)
records the result. Its version was not verified.

Historical failure evidence remains in the original [Wave 2 transcript](2026-10-24-wave2-easy-wins.txt).
The 2026-10-25 DELTREE and VCHOICE results, including the VCHOICE timeout
limitation, are in the [DOS-runner verification record](2026-10-25-dosrun-vchoice-deltree.txt).
Earlier corrected reruns and remaining limitations are in the
[fix-up transcript](2026-10-24-wave2-fixup.txt).

## Compatibility targets added 2026-10-25

These ten targets were added from the user-supplied list. The tests below used
archived evaluation versions that were available in the workspace. They are
bounded application checks. QEdit, TED and Turbo Pascal completed saved-file
workflows; EDLIN input and terminal hardware workflows remain unverified. The per-program test record is
[here](2026-10-25-vintage-app-tests.md); the original target list is
[here](2026-10-25-vintage-app-targets.md).

### PC-Write

**Partial.** Tested version/media: 3.03 (1989; PC-SIG disk 78). Opened INPUT.TXT and displayed its two lines in the editor; editing, save and reopen were not tested. [Screenshot](2026-10-25-pc-write.png); [full test record](2026-10-25-vintage-app-tests.md).

### PC-Calc

**Unsupported.** Tested version/media: PC-Calc+ 2.0 (PC-SIG disk 199). The worksheet executable stops at unsupported opcode DB (2E03:00CF) before a usable spreadsheet opens. [Full test record](2026-10-25-vintage-app-tests.md).

### PC-File

**Unsupported.** Tested version/media: PC-File+ (PC-SIG disk 5; release unverified). The executable stops at unsupported opcode F0 (0000:0024) before a usable database screen opens. [Full test record](2026-10-25-vintage-app-tests.md).

### QEdit

**Working.** QEdit Advanced 2.15 completed the insert, save, exit, reopen and byte-exact FAT16 verification workflow. [Detailed results and hashes](2026-10-25-text-editors-workflow.md); [reopened-file screenshot](2026-10-25-qedit-reopened.png).

### TED

**Working.** TED 3.0 completed the insert, save, exit, reopen and byte-exact FAT16 verification workflow. [Detailed results and hashes](2026-10-25-text-editors-workflow.md); [reopened-file screenshot](2026-10-25-ted-reopened.png).

### Turbo Pascal 3.0

**Working.** The supplied Turbo Pascal 3.0 distribution reports IDE version 3.02A. Created, saved, compiled and ran `HELLO.PAS` (`TP3 works`), reopened the source, and exited to ManiOS. [Detailed test record and hashes](2026-10-25-turbo-pascal-test.md); [program output](2026-10-25-turbo-pascal-run.png); [reopened source](2026-10-25-turbo-pascal-reopened.png).


### EDLIN

**Partial.** FreeDOS EDLIN 2.24 read the fixture but accepted neither keyboard nor serial line input at its `*` prompt, so editing, saving and reopening remain unverified. [Detailed results and attempts](2026-10-25-text-editors-workflow.md); [initial-read screenshot](2026-10-25-edlin.png).

### Galaxy Lite

**Unsupported.** Tested version/media: 1.5 (PC-SIG disk 2318). Stops at unsupported opcode INTO (0000:0001) before opening the editor. [Full test record](2026-10-25-vintage-app-tests.md).

### DOS Navigator

**Broken.** Tested version/media: 1.51 (RIT Labs freeware). Process remains active, but the display contains only the ManiOS banner and a blank screen; no usable file manager UI appeared. [Screenshot](2026-10-25-dos-navigator.png); [full test record](2026-10-25-vintage-app-tests.md).

### PC-Talk

**Partial.** Tested version/media: PC-Talk III (1983–1985). Displays the license/notice screen. A terminal session and modem or serial connection were not tested. [Screenshot](2026-10-25-pc-talk.png); [full test record](2026-10-25-vintage-app-tests.md).

### GW-BASIC 3.23

**Unsupported.** The supplied `GWBASIC.EXE` displays its `Ok` prompt, then stops with `IN AL,DX` (`EC`) at 12DD:6509 and unsupported I/O port 0201 on the first keyboard input. No BASIC statement was accepted. [Test record](2026-10-25-basic-tests.md); [startup screenshot](2026-10-25-gw-basic-start.png).

### Microsoft BASIC 5.28

**Broken.** The supplied `MSBASIC.COM` returns to the ManiOS shell without a BASIC prompt or usable screen. [Test record](2026-10-25-basic-tests.md); [screenshot](2026-10-25-ms-basic-return.png).


## VDE 1.97 and 1.65a

- **Build and environment:** ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, serial console, disposable FAT16 disk.
- **Versions:** VDE 1.97 (`VDE.EXE`) and VDE 1.65a (`VDE.COM`), both downloaded from Internet Archive.
- **Commands:** `dosrun C:/VDE.EXE` and `dosrun C:/VDE.COM`.
- **Result:** both print `Insufficient memory!` and return to the shell. No editor session or file operation was reached.
- **Evidence and hashes:** [acquisition and run record](2026-10-25-priority-apps.md).

## Vern Buerg LIST 7.1a

- **Build and environment:** ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, serial console, disposable FAT16 disk.
- **Command:** `dosrun C:/LIST.COM C:/INPUT.TXT`.
- **Result:** `dosrun` stops at unsupported instruction AAM at `1000:35F7`; the viewer does not open.
- **Evidence and hash:** [acquisition and run record](2026-10-25-priority-apps.md).

## MicroEMACS 3.9

- **Acquisition:** downloaded the exact DOS executable archive [`em39exe.arc`](https://ftp.math.utah.edu/pub/ibmpc/em39exe.arc) from the Utah IBM PC archive. Its contents and dates include `MEIBM.EXE` (77,008 bytes, dated 17 July 1987), matching the 3.9 release notes. Archive SHA-256: `ecfefb169375b6db3c120d5a8334f9501c3e3f10b130fb2d3debed7dc564ce34`; executable SHA-256: `1252e4232867d408aca1d7baaae21cc9d8373f0e9d777f4689bd6839a1732f31`.
- **Build and environment:** ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, disposable FAT16 hard disk; run through `dosrun`.
- **Result:** `MEIBM.EXE C:/TEST.TXT` opened a new buffer, accepted text, saved it, and returned to ManiOS. Reopened the file, moved to its end, appended `!`, saved, exited cleanly, and reopened it again. The final FAT16 file bytes were exactly `hellomanios!\r\n` (14 bytes).
- **Evidence:** [test record, commands and byte comparison](2026-10-25-microemacs-pc-outline.md).

## PC-Outline 3.34

- **Acquisition:** user-supplied `pco334.zip`; `README.1` identifies PC-Outline 3.34. The archive contains `PCO.EXE` (73,315 bytes), documentation and sample outlines.
- **Build and environment:** ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, serial console, disposable FAT16 disk.
- **Command and input:** `dosrun C:/PCO.EXE`; dismissed the shareware screen, chose “Open a new outline”, and typed `test` into the new-outline filename dialog.
- **Result:** the splash, opening menu and filename dialog render, and ordinary filename characters are accepted. Return, keypad Enter and Ctrl+M did not submit the dialog; the program remained there. The editor, content editing, save, clean close and reopen were not reached. No compatibility claim is made for those operations.
- **Evidence:** [filename dialog after Return](2026-10-25-pc-outline-filename-prompt.png); [follow-up test record](2026-10-25-microemacs-pc-outline.md); [archive and source notes](2026-10-25-priority-apps.md).

## PC-TYPE v1 DS

- **Identification:** Jim Button's PC-TYPE evaluation archive; documentation describes a word processor. It is not a typing tutor.
- **Build and environment:** ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, serial console, disposable FAT16 disk.
- **Command:** `dosrun C:/PCTYPE.EXE`.
- **Result:** enters a blank new document. After Enter and attempted `hello`, the captured display contained `qq`; text editing and persistence were not verified.
- **Evidence and hash:** [acquisition and run record](2026-10-25-priority-apps.md).

## PC-DeskTeam

- **Result:** **Untested** as requested because the required library component is unavailable. No launch or feature test was attempted.
- **Evidence:** [test scope note](2026-10-25-microemacs-pc-outline.md); [catalog/source notes](2026-10-25-priority-apps.md).

## COMHEX

- **Acquisition:** user-listed `comhex.zip` from the [COAST DOS decode archive](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/decode/); SHA-256 `343311b6e0ce7bfc835a3aa6669d2ff57867cbde6aaee232c28f83717b22af77`.
- **Environment and command:** ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, disposable FAT16 disk; `dosrun C:/COMHEX.COM`, then entered `TEST` (also tried `C:/TEST`) at the program prompt.
- **Result:** the prompt accepts input, then the Pascal program aborts with `I/O error 99` at `PC=2FB7`. `TEST.HEX` is left at zero bytes. COM-to-HEX conversion did not complete; status is Partial.
- **Evidence:** [archive hashes and test details](2026-10-25-decode-utilities.md).

## COMPHEX

- **Acquisition:** user-listed `comphex.zip`; SHA-256 `dabb361fc3cb4acc1f2b06594d6b242251f1f7b11e9e43ae12b6c92b7b9af100`.
- **Commands:** `dosrun C:/COMPHEX.EXE C:/A.HEX C:/SAME.HEX` and `dosrun C:/COMPHEX.EXE C:/A.HEX C:/B.HEX`.
- **Result:** the equal pair reports byte count `000E`, mismatch count `0000`; the pair differing only at the final byte reports address `000D`, `0A` versus `21`, and mismatch count `0001`.
- **Evidence:** [archive hashes and test details](2026-10-25-decode-utilities.md).

## HEXBIN

- **Acquisition:** user-listed `hexbin.zip`; SHA-256 `27754202e267a1db8d4e2bda67a7eec5567c6380041c925c99db7330057bb7a7`.
- **Commands:** `dosrun C:/HEXBIN.EXE C:/TEST.BIN C:/OUT.HEX`, then `dosrun C:/HEXBIN.EXE C:/OUT.HEX C:/ROUND.BIN`.
- **Result:** binary-to-HEX output contains the 14-byte data record `:0E0000000001027F80FEFF4D616E694F530AC2` and valid EOF record. The decoded FAT16 file is byte-exact with the fixture.
- **Evidence:** [archive hashes and test details](2026-10-25-decode-utilities.md).

## ASCIFY 1.3

- **Acquisition:** user-listed `ascify13.zip`; SHA-256 `251d45708f57b7b4feb6ca332ed10bda4f251d42a34e40295bae699f620f9164`.
- **Commands:** `dosrun C:/ASCIFY.COM TEST.BIN TEST.ASC`; deleted the original fixture from the disposable FAT16 image, then ran `dosrun C:/ASCIFY.COM TEST.ASC`.
- **Result:** generated a 52-byte ASCIFY file and decoded it back to a 14-byte file matching the source byte-for-byte.
- **Evidence:** [archive hashes and test details](2026-10-25-decode-utilities.md).

## FORMAT83

- **Acquisition:** user-listed `format83.zip`; SHA-256 `abc93f762adabd0b0f62829f53e81b2c0cd887faccba73eba17516aa9da1b8df`.
- **Command:** `dosrun C:/FORMAT83.COM C:/TEST.BIN`.
- **Result:** stdout contains a valid 14-byte Intel HEX record and EOF record. Both checksums sum to zero modulo 256. Output is intentionally standard output; a disk redirection attempt was unavailable on the tested mounted path.
- **Evidence:** [captured output and archive hashes](2026-10-25-decode-utilities.md).

## MAKEUUE 1.1

- **Acquisition:** user-listed `makeuu11.zip`; SHA-256 `dac3b9a12316ed6d2921ff589b2cffdec36aa9fdad2ce3babbbecba5c226dc32`.
- **Command:** `dosrun C:/MAKEUUE.EXE TEST.BIN TESTUU.BAT`.
- **Result:** generated a 439-byte self-decoding batch file. An independent decoder for the format's documented character mapping reconstructed the exact 14-byte fixture (with one zero pad byte). The generated batch's DEBUG-based reconstruction commands were not run because DEBUG was not available in the tested ManiOS environment.
- **Evidence:** [archive hashes and test details](2026-10-25-decode-utilities.md).

## MIME64 Revision D

- **Acquisition:** user-listed `mime64d.zip`; SHA-256 `75c6919f12bd20fe8121acf9e1807a548627efa4a9a5bf56a9247dc5b41d7d14`.
- **Commands:** `dosrun C:/MIME64.EXE C:/INPUT.TXT` and `dosrun C:/MIME64.EXE C:/MIME.64 C:/MIME.ZIP`.
- **Result:** the MIME/Base64 attachment decoded to the named `TEST.BIN` and matched the 14-byte fixture exactly. The package's own sample decoded to `MIME.ZIP`; `unzip -t` verified its `mime.txt` member without CRC errors.
- **Evidence:** [archive hashes and test details](2026-10-25-decode-utilities.md).
