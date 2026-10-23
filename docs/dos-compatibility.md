# DOS software compatibility

Last updated: **2026-10-23**. ManiOS version: **0.26.0**.

**37 programs:** 🟢 **12 Working** · 🟡 **7 Partial** · 🔴 **18 Broken/unsupported** · ⚪ **0 Untested**

This is task-specific coverage, not a compatibility percentage: a green
entry means the listed task completed, not that every feature of the
program works. The full test history is in
[details.md](compatibility/details.md); the machine-readable index is
[database.json](compatibility/database.json).

| Colour | Status | Meaning |
| --- | --- | --- |
| 🟢 | Working | The recorded task completed with usable output. |
| 🟡 | Partial | Some meaningful functions work; the normal workflow is incomplete or unverified. |
| 🔴 | Broken | The program runs but fails or hangs before completing a basic workflow. |
| 🔴 | Unsupported | A required CPU mode, subsystem or interface is missing. |
| ⚪ | Untested | No current result. |

## 🟢 Working (12)

| Application | Version / media | Verified task | Evidence | Last tested |
| --- | --- | --- | --- | --- |
| [B64](compatibility/details.md#b64) | 0.06R (rev A), FD14 BonusCD | Help, Base64 encoding of `ABCD` and decoding of `QUJDRA==` are byte-exact. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [BSUM](compatibility/details.md#bsum) | 1.1a, FD14 BonusCD | Text, empty-file and 20 KiB binary checksums match an independent oracle; usage and missing-file errors verified. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [FDBanner](compatibility/details.md#fdbanner) | 1.1, FD14 BonusCD | Coloured banner rendered through BIOS text services; screenshot checked. | [screenshot](compatibility/2026-10-23-retest-026-fdbanner.png) | 2026-10-23 |
| [Figlet](compatibility/details.md#figlet-22) | 2.2 DOS port, `figdos22.zip` | Correct `ManiOS` standard-font banner; `-I 1` prints `20200`. | [screenshot](compatibility/2026-10-23-dosprograms-figlet.png) | 2026-10-23 |
| [FreeDOS FC](compatibility/details.md#freedos-fc) | FD14 BonusCD; version unverified | Equal/different text and `/B` binary comparisons produce the correct results. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [FreeDOS MORE](compatibility/details.md#freedos-more) | 4.3, FD14 BonusCD | Help, exact short-file text, redirected stdin and 24-line paging/quit verified. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [Info-ZIP UnZip](compatibility/details.md#info-zip-unzip-600) | 6.00 (20 Apr 2009), DOS 16-bit large, `unz600x3.exe` | Lists and CRC-tests `KNOWN.ZIP`; extracts all four members including `SUB/THREE.TXT` with exact hashes; `-p` and `FUNZIP` print the correct text. | [transcript](compatibility/2026-10-23-compat4-infozip.txt) | 2026-10-23 |
| [SAL compiler SC](compatibility/details.md#semware-tse-250e) | 2.50e, bundled with TSE | Compiles a minimal macro to an 87-byte MAC with the expected signature and message. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [Search](compatibility/details.md#search-10a) | 1.0a, FD14 BonusCD | Finds both root and nested matches once; totals and the missing-file result are correct. | [transcript](compatibility/2026-10-23-search-utf8-basic.txt) | 2026-10-23 |
| [SemWare GREP](compatibility/details.md#semware-tse-250e) | bundled with TSE 2.50e | Finds exactly the two `apple` lines in the fixture. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [SemWare TSORT](compatibility/details.md#semware-tse-250e) | bundled with TSE 2.50e | Sorts four input lines into the expected order. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [utf8tocp](compatibility/details.md#utf8tocp-20240918) | 20240918, FD14 BonusCD | Exact UTF-8 ↔ CP437 and UTF-8 ↔ CP775 conversions, including Lithuanian letters and CRLF. | [transcript](compatibility/2026-10-23-search-utf8-basic.txt) | 2026-10-23 |

## 🟡 Partial (7)

| Application | Version / media | Partial result | Evidence | Last tested |
| --- | --- | --- | --- | --- |
| [Checks & Balances](compatibility/details.md#checks--balances) | 4.0g (1986), two 360 KB floppies | Configuration and the supplied DEMO work (`TRUE $2450.20`, `BANK $279.04`); ledger entry, save and reopen are unverified. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [Directory Freedom](compatibility/details.md#directory-freedom-461b) | 4.61B, `df461b.zip` | Directory listing, version 4.61B and the F1-F10 menu render; no file operation was exercised. | [screenshot](compatibility/2026-10-23-dosprograms-directory-freedom.png) | 2026-10-23 |
| [EDITV](compatibility/details.md#editv-41) | 4.1 US, `editv41u.zip` | Opens and displays a file and accepts typed text; save and reopen are unverified. | [screenshot](compatibility/2026-10-23-dosprograms-editv-typed.png) | 2026-10-23 |
| [Microsoft Windows 1.01](compatibility/details.md#microsoft-windows-101) | 1.01, five 360 KB floppy images | Setup completes and writes `WIN.COM`/`WIN100.BIN`; startup prints `Incorrect DOS version` and faults; desktop unavailable. | [screenshot](compatibility/2026-10-23-retest-026-windows-start.png) | 2026-10-23 |
| [SemWare Editor Professional](compatibility/details.md#semware-tse-250e) | 2.50e, `tse250e.zip` | An input file opens and renders, but typed input does not reach the editor; editing, save and reopen are unverified. | [screenshot](compatibility/2026-10-23-retest-026-tse-file.png) | 2026-10-23 |
| [uHex](compatibility/details.md#uhex-104) | 1.0.4 DOS | Help and the 34-byte read-only view work, with display defects; editing, save and reopen are unverified. | [screenshot](compatibility/2026-10-23-retest-026-uhex.png) | 2026-10-23 |
| [VIEW](compatibility/details.md#view-172) | 17.2, `view.zip` | Opens the 23-page `VIEW.DOC` and a short file; Esc exits. Header overlap and screen remnants remain; paging is untested. | [screenshot](compatibility/2026-10-23-dosprograms-view-doc.png) | 2026-10-23 |

## 🔴 Broken and unsupported (18)

| Application | Version / media | Status | Blocker | Evidence | Last tested |
| --- | --- | --- | --- | --- | --- |
| [Ability Plus 3.0](compatibility/details.md#ability-plus-30) | 3.0, `AB3.ZIP` | 🔴 **Unsupported** | Main program stops on x87 `FNINIT`; the installer needs a 32-bit operand. | [transcript](compatibility/2026-10-23-visicalc-ability.txt) | 2026-10-23 |
| [As-Easy-As](compatibility/details.md#as-easy-as-57) | 5.7, DOS files from archive.org item | 🔴 **Unsupported** | FPU probe writes `OUT 0F0h`, then runs `FNINIT`/`FSTSW`; no worksheet. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [AUMenu](compatibility/details.md#aumenu-143a) | 1.4.3a archive; reports 1.4.3b Unix edition | 🔴 **Broken** | The linked ZIP is the Unix edition: a $79 registration splash appears instead of the DOS menu. | [screenshot](compatibility/2026-10-23-dosprograms-aumenu.png) | 2026-10-23 |
| [Bywater BASIC](compatibility/details.md#bywater-basic-330) | 3.30, FD14 BonusCD | 🔴 **Unsupported** | LE protected-mode executable rejected; the supplied arithmetic program was not executed. | [transcript](compatibility/2026-10-23-search-utf8-basic.txt) | 2026-10-23 |
| [Cash Register](compatibility/details.md#cash-register-71j) | 7.1j, `POS71J.ZIP` | 🔴 **Unsupported** | `POS.EXE` startup stops on `IN AL,21h`; no register screen. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [Costa GUI](compatibility/details.md#costa-gui-180) | 1.8.0, `costa180.zip` | 🔴 **Unsupported** | Launcher reports `DESKTOP.EXE not found.`; direct `DESKTOP.EXE` stops on `IN AL,21h`. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [DATES](compatibility/details.md#dates-200208) | 2002.08, `DATE0208.ZIP` | 🔴 **Unsupported** | Startup stops on `IN AL,21h` (port `0021h`); no date list or usage text. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [Directory Maven](compatibility/details.md#directory-maven-13) | 1.3 | 🔴 **Unsupported** | The scan path stops on `66h`; help stops at `IN AL,DX`. | [transcript](compatibility/2026-10-23-maven.txt) | 2026-10-23 |
| [DOSStart](compatibility/details.md#dosstart-19b) | 1.9b build 7.08.2002, `dosstart.zip` | 🔴 **Unsupported** | `DANSHELL.EXE` stops on `IN AL,21h` at `1FBB:6B78`; no launcher screen. | [transcript](compatibility/2026-10-23-compat4-extra.txt) | 2026-10-23 |
| [File Maven](compatibility/details.md#file-maven-35a) | 3.5a | 🔴 **Broken** | Installer, main executable and launcher pass the clock wait, then fail on `IN AL,DX`; no usable manager. | [transcript](compatibility/2026-10-23-maven.txt) | 2026-10-23 |
| [Info-ZIP Zip](compatibility/details.md#info-zip-zip-232) | 2.32, 16-bit and 32-bit DOS builds, `zip232x.zip` | 🔴 **Unsupported** | 16-bit builds stop at `0000:0001` (`E5`/`EF`); the 32-bit build reports `no DPMI`; no archive is created. | [transcript](compatibility/2026-10-23-compat4-infozip.txt) | 2026-10-23 |
| [MinEd](compatibility/details.md#mined-202227) | 2022.27, `mined-2022.27-dos.zip` | 🔴 **Unsupported** | DJGPP/go32 loader reports `Load error: no DPMI`; no editor screen. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [PasswordBox](compatibility/details.md#passwordbox-011) | 0.11 DOS build, `pbox011-dos.zip` | 🔴 **Unsupported** | DJGPP/go32 stub reports `Load error: no DPMI`; no password database. | [transcript](compatibility/2026-10-23-compat4-extra.txt) | 2026-10-23 |
| [PEDIT](compatibility/details.md#pedit-400) | 4.00 standard and Light | 🔴 **Unsupported** | Both variants stop on the operand-size prefix `66h`; no editable document. | [transcript](compatibility/2026-10-23-catalog.txt) | 2026-10-23 |
| [Spectra](compatibility/details.md#spectra-10) | 1.0, `spec10.zip` | 🔴 **Unsupported** | `HITECH.EXE` stops on `IN AL,21h` at `1345:2710`; no GUI. | [transcript](compatibility/2026-10-23-compat4-extra.txt) | 2026-10-23 |
| [VIEWSEEK](compatibility/details.md#viewseek-31-auxiliary) | 3.1, VIEW package (auxiliary) | 🔴 **Broken** | Search reports no match for text that is present, then exits with `run-time error R6001`. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [VisiCalc](compatibility/details.md#visicalc) | `VC.COM`, 27,520 bytes; version unverified | 🔴 **Broken** | No usable worksheet or shell return during the retest; the blank screen remains. | [screenshot](compatibility/2026-10-23-retest-026-visicalc.png) | 2026-10-23 |
| [Warcraft: Orcs & Humans](compatibility/details.md#warcraft-orcs--humans) | 1994 DOS CD; exact patch unverified | 🔴 **Unsupported** | Extracted `war.exe` needs a DOS/4GW protected-mode payload; the installer stops on `OUT 43h` (`E6`). | [transcript](compatibility/2026-10-22-warcraft.txt) | 2026-10-23 |

## ⚪ Untested (0)

No entries are currently untested.

## Scope and method

Tested against ManiOS 0.26.0 (`c10d9f0`, kernel `5553a1934d19165f34dcea365ffcb4aaec0d11b81877521da23ec889726fc88c`). Environment: QEMU i386, CPU 486, 64 MiB RAM, serial console; disposable FAT16 images and 6-12 s observations unless a longer setup run is recorded.

Applications are started with `dosrun` from a fresh disposable FAT16
image per case. Transcripts and screenshots are stored in
[compatibility/](compatibility/); binaries and test disk images are not
committed. See [dosrun.md](dosrun.md) for the runner and
[details.md](compatibility/details.md) for the per-application records.

## Maintaining this page

`docs/compatibility/database.json` is the index. To add or update a result:

1. Add or edit the entry in `docs/compatibility/database.json`.
2. Add the transcript or screenshot to `docs/compatibility/` and the
   per-application notes to `docs/compatibility/details.md`.
3. Regenerate this page with
   `python3 tools/render_dos_compatibility.py`.
4. Run `python3 tools/render_dos_compatibility.py --check` to verify it is
   current.
