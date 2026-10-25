# DOS software compatibility

Last updated: **2026-10-25**. ManiOS version: **0.26.1-10252026**.

**75 programs:** 🟢 **28 Working** · 🟡 **10 Partial** · 🔴 **36 Broken/unsupported** · ⚪ **1 Untested**

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

## 🟢 Working (28)

| Application | Version / media | Verified task | Evidence | Last tested |
| --- | --- | --- | --- | --- |
| [Adventure (350-point DECUS port)](compatibility/details.md#adventure-350-point-decus-port) | Kevin Black DOS build, `adv350kb.zip` | Starts, accepts a 20-character instructions answer, and completes look, inventory, movement and quit; the quit score is 32/350. | [transcript](compatibility/2026-10-23-text-adventures.json) | 2026-10-23 |
| [B64](compatibility/details.md#b64) | 0.06R (rev A), FD14 BonusCD | Help, Base64 encoding of `ABCD` and decoding of `QUJDRA==` are byte-exact. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [BSUM](compatibility/details.md#bsum) | 1.1a, FD14 BonusCD | Text, empty-file and 20 KiB binary checksums match an independent oracle; usage and missing-file errors verified. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [Directory Freedom](compatibility/details.md#directory-freedom-461b) | 4.61B, `df461b.zip` | On disposable FAT16 copies, F1 copied AR.COM to COPYTEST.COM byte-for-byte, F2 deleted AR.COM, and F4 renamed AR.COM to AR.BAK. The directory listing and function menu render; originals were unchanged. | [screenshot](compatibility/2026-10-23-int10-directory-freedom-copy.png) | 2026-10-23 |
| [EDITV](compatibility/details.md#editv-41) | 4.1 US, `editv41u.zip` | Inserted `Z`, moved right, inserted then removed `Y` with Backspace, saved with F2, and reopened. The disposable FAT16 file independently contains `Zalpha one` followed by the original remaining lines. | [screenshot](compatibility/2026-10-23-int10-editv-multiedit.png) | 2026-10-23 |
| [FDBanner](compatibility/details.md#fdbanner) | 1.1, FD14 BonusCD | Coloured banner rendered through BIOS text services; screenshot checked. | [screenshot](compatibility/2026-10-23-retest-026-fdbanner.png) | 2026-10-23 |
| [Figlet](compatibility/details.md#figlet-22) | 2.2 DOS port, `figdos22.zip` | Correct `ManiOS` standard-font banner; `-I 1` prints `20200`. | [screenshot](compatibility/2026-10-23-dosprograms-figlet.png) | 2026-10-23 |
| [FreeDOS ATTRIB](compatibility/details.md#freedos-attrib) | FD14 BonusCD; version unverified | Lists FAT attributes and persists `+R`/`-R`; a fresh DOS execution reads the changed state and `mattrib` independently confirms it on disk. | [transcript](compatibility/2026-10-24-wave2-fixup.txt) | 2026-10-24 |
| [FreeDOS DELTREE](compatibility/details.md#freedos-deltree) | BonusCD build v1.02g | Six guest cases passed on disposable FAT16 images: confirmed nested deletion, cancellation, /Y root-safety confirmation, root guard, and missing-target handling. The unrelated KEEP.TXT and OTHER survived. | [transcript](compatibility/2026-10-25-dosrun-vchoice-deltree.txt) | 2026-10-25 |
| [FreeDOS FC](compatibility/details.md#freedos-fc) | FD14 BonusCD; version unverified | Equal/different text and `/B` binary comparisons produce the correct results. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [FreeDOS FIND](compatibility/details.md#freedos-find-01nov2003) | 01Nov2003, FreeDOS package (GPL-2) | With the test disk mounted as A: (FIND selects A: before opening relative paths), exact, case-insensitive and inverted searches return the expected lines; a missing file reports `No such file`. | [transcript](compatibility/2026-10-24-wave2-fixup.txt) | 2026-10-24 |
| [FreeDOS MORE](compatibility/details.md#freedos-more) | 4.3, FD14 BonusCD | Help, exact short-file text, redirected stdin and 24-line paging/quit verified. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [FreeDOS VCLS](compatibility/details.md#freedos-vcls) | FD14 BonusCD; version unverified | Reads the BIOS video geometry from the initialized BDA, clears the visible VGA text screen, and returns to the redrawn ManiOS prompt. | [transcript](compatibility/2026-10-24-wave2-fixup.txt) | 2026-10-24 |
| [Info-ZIP UnZip](compatibility/details.md#info-zip-unzip-600) | 6.00 (20 Apr 2009), DOS 16-bit large, `unz600x3.exe` | Lists and CRC-tests `KNOWN.ZIP`; extracts all four members including `SUB/THREE.TXT` with exact hashes; `-p` and `FUNZIP` print the correct text. | [transcript](compatibility/2026-10-23-compat4-infozip.txt) | 2026-10-23 |
| [Life2](compatibility/details.md#life2) | text-mode Conway Life, `life2.zip` | Native timer IRQ0 delivery now executes the installed SOUND handler. Blinker horizontal/vertical/horizontal evolution and Q/Esc exit verified on a disposable FAT16 image. Working for pattern creation, RUN, verified blinker evolution and Q/Esc exit. One earlier marking run reported a BASIC runtime error; its cause remains unresolved. Other patterns/features remain untested. | [evidence](compatibility/2026-10-24-pit-irq0.md) | 2026-10-24 |
| [MicroEMACS](compatibility/details.md#microemacs-39) | 3.9 (1987 DOS release) | Typed and edited `hellomanios!`, saved, exited and reopened it; FAT16 contents matched exactly (`hellomanios!` + CRLF). | [evidence](compatibility/2026-10-25-microemacs-pc-outline.md) | 2026-10-25 |
| [QEdit](compatibility/details.md#qedit) | Advanced 2.15 (1991 shareware) | Inserted `EDITED ` at the start of INPUT.TXT, saved with Ctrl+K S, exited with Alt+X, reopened it, and verified the exact 32 bytes on FAT16. | [evidence](compatibility/2026-10-25-text-editors-workflow.md) | 2026-10-25 |
| [SAL compiler SC](compatibility/details.md#semware-tse-250e) | 2.50e, bundled with TSE | Compiles a minimal macro to an 87-byte MAC with the expected signature and message. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [Search](compatibility/details.md#search-10a) | 1.0a, FD14 BonusCD | Finds both root and nested matches once; totals and the missing-file result are correct. | [transcript](compatibility/2026-10-23-search-utf8-basic.txt) | 2026-10-23 |
| [SemWare Editor Professional](compatibility/details.md#semware-tse-250e) | 2.50e, `tse250e.zip` | Physical keyboard editing works: inserted `z` at the start of INPUT.TXT, saved with Ctrl+K S, exited with Alt+X, and reopened. mtools confirmed persisted `zpear` followed by the original remaining lines. | [screenshot](compatibility/2026-10-23-int10-semware-reopened.png) | 2026-10-23 |
| [SemWare GREP](compatibility/details.md#semware-tse-250e) | bundled with TSE 2.50e | Finds exactly the two `apple` lines in the fixture. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [SemWare TSORT](compatibility/details.md#semware-tse-250e) | bundled with TSE 2.50e | Sorts four input lines into the expected order. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [TED](compatibility/details.md#ted) | 3.0 (1991) | Inserted `EDITED ` at the start of INPUT.TXT, saved with F7, exited, reopened it, and verified the exact 32 bytes on FAT16. | [evidence](compatibility/2026-10-25-text-editors-workflow.md) | 2026-10-25 |
| [Turbo Pascal 3.0](compatibility/details.md#turbo-pascal-30) | 3.02A DOS IDE (Turbo Pascal 3.0 distribution) | Created HELLO.PAS in the IDE, saved it on FAT16, compiled and ran it (`TP3 works`), reopened the source, and exited to the ManiOS shell. | [evidence](compatibility/2026-10-25-turbo-pascal-test.md) | 2026-10-25 |
| [utf8tocp](compatibility/details.md#utf8tocp-20240918) | 20240918, FD14 BonusCD | Exact UTF-8 ↔ CP437 and UTF-8 ↔ CP775 conversions, including Lithuanian letters and CRLF. | [transcript](compatibility/2026-10-23-search-utf8-basic.txt) | 2026-10-23 |
| [VIEW](compatibility/details.md#view-172) | 17.2, `view.zip` | Original VIEW.DOC verified from start to EOF and back with PgDn/PgUp, Home/End, repeated F2 search and clean Esc. Console redraw corruption fixed. Historical 18% stall remains unreproduced; printing and other formats untested. | [screenshot](compatibility/2026-10-24-view-pgdn-3.png) | 2026-10-24 |
| [VisiCalc](compatibility/details.md#visicalc) | `VC.COM`, 27,520 bytes; version unverified | Mode-2 worksheet renders; A1=10, B1=20 and C1=+A1+B1 (30) were saved as TEST.VC, the app exited cleanly, and a fresh launch reopened the sheet with the values and formula intact. | [transcript](compatibility/2026-10-24-visicalc-save-reopen-qemu.json) | 2026-10-24 |
| [Zork I: The Great Underground Empire](compatibility/details.md#zork-i-the-great-underground-empire) | MIT historicalsource release, `zork1.z3` (86,838 bytes), ZIP 2.00 | ZIP 2.00 starts the story; room text, commands, inventory, score, save/restore and quit work. The status line still clips its right edge. | [transcript](compatibility/2026-10-23-text-adventures.json) | 2026-10-23 |

## 🟡 Partial (10)

| Application | Version / media | Partial result | Evidence | Last tested |
| --- | --- | --- | --- | --- |
| [Checks & Balances](compatibility/details.md#checks--balances) | 4.0g (1986), two 360 KB floppies | Configuration and the supplied DEMO work (`TRUE $2450.20`, `BANK $279.04`); ledger entry, save and reopen are unverified. | [transcript](compatibility/2026-10-23-retest-026.txt) | 2026-10-23 |
| [EDLIN](compatibility/details.md#edlin) | FreeDOS EDLIN 2.24 | Opened the two-line fixture, but ignored both keyboard and serial input at the `*` prompt; edit, save, close and reopen could not be verified. | [evidence](compatibility/2026-10-25-text-editors-workflow.md) | 2026-10-25 |
| [FreeDOS VCHOICE](compatibility/details.md#freedos-vchoice) | FD14 BonusCD; version unverified | Help, arrow+Enter, digit jump, and Escape passed using a shared B800 text screen; the /T 8 /D 1 timeout returned 0 instead of the default choice. | [transcript](compatibility/2026-10-25-dosrun-vchoice-deltree.txt) | 2026-10-25 |
| [Microsoft Windows 1.01](compatibility/details.md#microsoft-windows-101) | 1.01, five 360 KB floppy images | Setup completes and writes `WIN.COM`/`WIN100.BIN`; startup prints `Incorrect DOS version` and faults; desktop unavailable. | [screenshot](compatibility/2026-10-23-retest-026-windows-start.png) | 2026-10-23 |
| [PC-Outline](compatibility/details.md#pc-outline-334) | 3.34 (`pco334.zip`; README dated 1987) | Shareware screen, menu and filename dialog work; typed `test`, but Enter, keypad Enter and Ctrl+M did not submit it, so editing and save/reopen remain unverified. | [evidence](compatibility/2026-10-25-microemacs-pc-outline.md) | 2026-10-25 |
| [PC-Talk](compatibility/details.md#pc-talk) | PC-Talk III (1983–1985) | Displays the license/notice screen. A terminal session and modem or serial connection were not tested. | [screenshot](compatibility/2026-10-25-pc-talk.png) | 2026-10-25 |
| [PC-TYPE](compatibility/details.md#pc-type-v1-ds) | v1 DS evaluation (Jim Button) | Starts a new blank document, but attempted `hello` input renders incorrectly as `qq`; editing and save/reopen are unverified. This is a word processor, not a typing tutor. | [evidence](compatibility/2026-10-25-priority-apps.md) | 2026-10-25 |
| [PC-Write](compatibility/details.md#pc-write) | 3.03 (1989; PC-SIG disk 78) | Opened INPUT.TXT and displayed its two lines in the editor; editing, save and reopen were not tested. | [screenshot](compatibility/2026-10-25-pc-write.png) | 2026-10-25 |
| [Planetfall (Infocom Sampler demo)](compatibility/details.md#planetfall-infocom-sampler-demo) | official 1984 Infocom Sampler, `sampler1_R26.z3` (112,610 bytes) | The sampler starts and accepts `planet`, but the Planetfall sample text does not render past its first line under ZIP/JZIP; full-game testing was skipped for licensing. | [transcript](compatibility/2026-10-23-text-adventures.json) | 2026-10-23 |
| [uHex](compatibility/details.md#uhex-104) | 1.0.4 DOS | Help and the 34-byte read-only view work, with display defects; editing, save and reopen are unverified. | [screenshot](compatibility/2026-10-23-retest-026-uhex.png) | 2026-10-23 |

## 🔴 Broken and unsupported (36)

| Application | Version / media | Status | Blocker | Evidence | Last tested |
| --- | --- | --- | --- | --- | --- |
| [Ability Plus 3.0](compatibility/details.md#ability-plus-30) | 3.0, `AB3.ZIP` | 🔴 **Unsupported** | Main program stops on x87 `FNINIT`; the installer needs a 32-bit operand. | [transcript](compatibility/2026-10-23-visicalc-ability.txt) | 2026-10-23 |
| [As-Easy-As](compatibility/details.md#as-easy-as-57) | 5.7, DOS files from archive.org item | 🔴 **Unsupported** | FPU probe writes `OUT 0F0h`, then runs `FNINIT`/`FSTSW`; no worksheet. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [AUMenu](compatibility/details.md#aumenu-143a) | 1.4.3a archive; reports 1.4.3b Unix edition | 🔴 **Broken** | The linked ZIP is the Unix edition: a $79 registration splash appears instead of the DOS menu. | [screenshot](compatibility/2026-10-23-dosprograms-aumenu.png) | 2026-10-23 |
| [Bywater BASIC](compatibility/details.md#bywater-basic-330) | 3.30, FD14 BonusCD | 🔴 **Unsupported** | LE protected-mode executable rejected; the supplied arithmetic program was not executed. | [transcript](compatibility/2026-10-23-search-utf8-basic.txt) | 2026-10-23 |
| [Cash Register](compatibility/details.md#cash-register-71j) | 7.1j, `POS71J.ZIP` | 🔴 **Unsupported** | After PIC and PIT0 support, some text renders before unsupported `ARPL` at `6DCC:0293`; no register operation. | [transcript](compatibility/2026-10-23-10232026-retest.txt) | 2026-10-23 |
| [Champ Galagon](compatibility/details.md#champ-galagon) | 1.0 (registered, 1997), `galagon.zip` | 🔴 **Unsupported** | Returns to the shell in under 0.6 s with no console output or title screen; no VGA/game frame is reached. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [Costa GUI](compatibility/details.md#costa-gui-180) | 1.8.0, `costa180.zip` | 🔴 **Unsupported** | After PIC support, `DESKTOP.EXE` reaches COSTALIB but stops with `Illegal function call` at `15CA:2E00`; no usable desktop. | [transcript](compatibility/2026-10-23-10232026-retest.txt) | 2026-10-23 |
| [DATES](compatibility/details.md#dates-200208) | 2002.08, `DATE0208.ZIP` | 🔴 **Unsupported** | After PIC support, startup faults on opcode `FF` at `0A34:5CD8`; no date list or usage screen. | [transcript](compatibility/2026-10-23-10232026-retest.txt) | 2026-10-23 |
| [Directory Maven](compatibility/details.md#directory-maven-13) | 1.3 | 🔴 **Unsupported** | The scan path stops on `66h`; help stops at `IN AL,DX`. | [transcript](compatibility/2026-10-23-maven.txt) | 2026-10-23 |
| [DOS Navigator](compatibility/details.md#dos-navigator) | 1.51 (RIT Labs freeware) | 🔴 **Broken** | Process remains active, but the display contains only the ManiOS banner and a blank screen; no usable file manager UI appeared. | [screenshot](compatibility/2026-10-25-dos-navigator.png) | 2026-10-25 |
| [DOSStart](compatibility/details.md#dosstart-19b) | 1.9b build 7.08.2002, `dosstart.zip` | 🔴 **Unsupported** | `DANSHELL.EXE` stops on `IN AL,21h` at `1FBB:6B78`; no launcher screen. | [transcript](compatibility/2026-10-23-compat4-extra.txt) | 2026-10-23 |
| [Dr Mind Lite](compatibility/details.md#dr-mind-lite) | LITE variant, 2019, `drlite.zip` | 🔴 **Unsupported** | Stops on `OUT 0x3C8` (VGA DAC palette) at `1010:02CA`; no game board. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [Drobos](compatibility/details.md#drobos) | 1.3 (DRoboWHOids), `drobos.zip` | 🔴 **Unsupported** | Stops on x87 opcode `D8` at `1010:0487`; no game screen. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [File Maven](compatibility/details.md#file-maven-35a) | 3.5a | 🔴 **Broken** | Installer, main executable and launcher pass the clock wait, then fail on `IN AL,DX`; no usable manager. | [transcript](compatibility/2026-10-23-maven.txt) | 2026-10-23 |
| [Ford Driving Simulator](compatibility/details.md#ford-driving-simulator) | 1988 models, `ford.zip` | 🔴 **Unsupported** | Stops on `IN` port `0021h` (PIC) at `1010:10C8`; no simulator screen. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [FreeDOS MEM](compatibility/details.md#freedos-mem) | FD14 BonusCD; version unverified | 🔴 **Broken** | The DOS list-of-lists now points to a valid terminated MCB chain, but MEM stops at unsupported opcode 66h before producing a memory summary. | [transcript](compatibility/2026-10-24-wave2-fixup.txt) | 2026-10-24 |
| [Galaxy Lite](compatibility/details.md#galaxy-lite) | 1.5 (PC-SIG disk 2318) | 🔴 **Unsupported** | Stops at unsupported opcode INTO (0000:0001) before opening the editor. | [evidence](compatibility/2026-10-25-vintage-app-tests.md) | 2026-10-25 |
| [GW-BASIC](compatibility/details.md#gw-basic-323) | 3.23 (1988; GWBASIC.EXE) | 🔴 **Unsupported** | Shows the `Ok` prompt, then stops on opcode EC / unsupported IN port 0201 when keyboard input begins; no statement is accepted. | [evidence](compatibility/2026-10-25-basic-tests.md) | 2026-10-25 |
| [Hanoi](compatibility/details.md#hanoi) | 1.3 (2000), `hanoi.zip` | 🔴 **Unsupported** | LZEXE startup unpacks, then stops on `IN` port `0006h` at `115B:0603`; no board. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [Info-ZIP Zip](compatibility/details.md#info-zip-zip-232) | 2.32, 16-bit and 32-bit DOS builds, `zip232x.zip` | 🔴 **Unsupported** | 16-bit builds stop at `0000:0001` (`E5`/`EF`); the 32-bit build reports `no DPMI`; no archive is created. | [transcript](compatibility/2026-10-23-compat4-infozip.txt) | 2026-10-23 |
| [Mahjong!](compatibility/details.md#mahjong) | 1996/1997 SoftSpot, `mahjong.zip` | 🔴 **Unsupported** | Stops on operand-size prefix `66h` at `101D:0990`; no screen. The package requires SVGA 1024x768 VESA graphics and a mouse. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [Microsoft BASIC](compatibility/details.md#microsoft-basic-528) | 5.28 (MSBASIC.COM, 1983-05-24) | 🔴 **Broken** | The tested COM binary returns to the ManiOS shell without a BASIC prompt or usable screen. | [evidence](compatibility/2026-10-25-basic-tests.md) | 2026-10-25 |
| [Mine Mayhem](compatibility/details.md#mine-mayhem) | freeware by Jason Hood, `minemay.zip` | 🔴 **Unsupported** | Stops on operand-size prefix `66h` at `1426:2DC3`; no screen. The executable is a DJGPP/go32 build. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [MinEd](compatibility/details.md#mined-202227) | 2022.27, `mined-2022.27-dos.zip` | 🔴 **Unsupported** | DJGPP/go32 loader reports `Load error: no DPMI`; no editor screen. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [Nero 5](compatibility/details.md#nero-5) | 08.02.2000, `nero5.zip` | 🔴 **Unsupported** | Returns to the shell in under 0.6 s with no console output or menu; BGI EGAVGA 640x480 initialization produces no board. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [PasswordBox](compatibility/details.md#passwordbox-011) | 0.11 DOS build, `pbox011-dos.zip` | 🔴 **Unsupported** | DJGPP/go32 stub reports `Load error: no DPMI`; no password database. | [transcript](compatibility/2026-10-23-compat4-extra.txt) | 2026-10-23 |
| [PC-Calc](compatibility/details.md#pc-calc) | PC-Calc+ 2.0 (PC-SIG disk 199) | 🔴 **Unsupported** | The worksheet executable stops at unsupported opcode DB (2E03:00CF) before a usable spreadsheet opens. | [evidence](compatibility/2026-10-25-vintage-app-tests.md) | 2026-10-25 |
| [PC-File](compatibility/details.md#pc-file) | PC-File+ (PC-SIG disk 5; release unverified) | 🔴 **Unsupported** | The executable stops at unsupported opcode F0 (0000:0024) before a usable database screen opens. | [evidence](compatibility/2026-10-25-vintage-app-tests.md) | 2026-10-25 |
| [PEDIT](compatibility/details.md#pedit-400) | 4.00 standard and Light | 🔴 **Unsupported** | Both variants stop on the operand-size prefix `66h`; no editable document. | [transcript](compatibility/2026-10-23-catalog.txt) | 2026-10-23 |
| [ShuffleV](compatibility/details.md#shufflev) | 1.1 (2000), `shufflev.zip` | 🔴 **Unsupported** | Turbo Vision board renders, then stops on opcode `66h` at `1C36:07FD`; no move is possible. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [Spectra](compatibility/details.md#spectra-10) | 1.0, `spec10.zip` | 🔴 **Unsupported** | `HITECH.EXE` stops on `IN AL,21h` at `1345:2710`; no GUI. | [transcript](compatibility/2026-10-23-compat4-extra.txt) | 2026-10-23 |
| [TypeFast](compatibility/details.md#typefast) | typing drill, `typefast.zip` | 🔴 **Broken** | Starts and remains running, but the console stays blank; no prompts or game screen appear, so no task is completed. | [transcript](compatibility/2026-10-23-compat4-games.txt) | 2026-10-23 |
| [VDE](compatibility/details.md#vde-197-and-165a) | 1.97 and 1.65a DOS releases | 🔴 **Unsupported** | Both versions stop at startup with `Insufficient memory!`; no document editing or FAT16 save/reopen was reached. | [evidence](compatibility/2026-10-25-priority-apps.md) | 2026-10-25 |
| [Vern Buerg LIST](compatibility/details.md#vern-buerg-list-71a) | 7.1a (`LIST.COM`) | 🔴 **Unsupported** | Stops on unsupported 8086 instruction AAM at `1000:35F7` before the file viewer opens. | [evidence](compatibility/2026-10-25-priority-apps.md) | 2026-10-25 |
| [VIEWSEEK](compatibility/details.md#viewseek-31-auxiliary) | 3.1, VIEW package (auxiliary) | 🔴 **Broken** | Search reports no match for text that is present, then exits with `run-time error R6001`. | [transcript](compatibility/2026-10-23-dosprograms-apps.txt) | 2026-10-23 |
| [Warcraft: Orcs & Humans](compatibility/details.md#warcraft-orcs--humans) | 1994 DOS CD; exact patch unverified | 🔴 **Unsupported** | Extracted `war.exe` needs a DOS/4GW protected-mode payload; the installer stops on `OUT 43h` (`E6`). | [transcript](compatibility/2026-10-22-warcraft.txt) | 2026-10-23 |

## ⚪ Untested (1)

| Application | Version / media | Evidence | Last tested |
| --- | --- | --- | --- |
| [PC-DeskTeam](compatibility/details.md#pc-deskteam) | PC-SIG disk 405 (DESKTEAM.COM; version unverified) | [evidence](compatibility/2026-10-25-microemacs-pc-outline.md) | 2026-10-25 |

## Scope and method

Tested against ManiOS 0.26.1-10252026 (`release tag v0.26.1-10252026`, kernel `cc0a429f6278b2d399e81bbefd45d1185165940e07708d5789e10bafee7275c3`). Environment: QEMU i386, CPU 486, 64 MiB RAM, serial console; fresh disposable FAT16 disk per program; applications launched inside ManiOS via dosrun.

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
