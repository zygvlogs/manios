# dosrun

`dosrun` runs a DOS program -- a `.COM` or an `.EXE` -- on ManiOS.

See the [DOS software compatibility list](dos-compatibility.md) for
red/yellow/green results, the [database](compatibility/database.json) for the
index, and the [full records](compatibility/details.md) for build-specific
results and known failures.

```
manios% dosrun HELLO.COM
Hello from DOS!
```

ManiDOS (`docs/dos.md`) is a DOS-like *shell*: drive letters, batch
files, `DIR`, and ManiOS programs. It does not run DOS programs, and
refuses a `.COM` or `.EXE` by name. `dosrun` is the other half: an
8086 interpreter that runs the program itself, with the DOS and BIOS
services it asks for served by ManiOS.

It is ManiOS's own, written for ManiOS from nothing. It contains no
MS-DOS code. The MS-DOS 4.0 source, which Microsoft published under
the MIT License, is a *reference* for what each DOS function must do
(its dispatch table and its PSP layout), not code copied from it.

## What it runs

- **`.COM`**: the file's bytes at offset `0x100` of the program's
  segment, with the stack at the top of the segment.
- **`.EXE`**: the MZ header, the image after it, and the relocation
  table's entries added to the load segment. Real-mode images can span
  several segments; they must fit below the conventional-memory limit.
  Invalid or truncated headers and out-of-image relocations are rejected.

### Warcraft: Orcs & Humans (1994)

The tested installation disc contains `WAR.EXA` and `DOS4GW.EXA` as LHA
archives. They need extraction; renaming `.EXA` to `.EXE` does not unpack
them. The extracted `war.exe` contains a 32-bit LE executable for the
DOS/4GW extender. This version of `dosrun` cannot run that protected-mode
payload and reports the unsupported requirement before executing its stub.

The disc's `INSTALL.EXE` also performs hardware port I/O (`OUT`, opcode
`E6`), which the interpreter does not emulate. Extracting the archives
therefore avoids the installer but does not make the game compatible.
Running this game needs protected-mode CPU and extender support, further
DOS services, and the game's graphics/input devices. The status-flag and
MZ-loading fixes do not provide those capabilities.

The program's memory is a 1 MiB 8086 address space. The interpreter
runs the instructions DOS programs use: the full ALU, the shifts and
rotates, the string operations with their `REP` prefixes, the
ModR/M addressing modes, the segment overrides, and the control flow.
An instruction it doesn't have stops with a message naming it, rather
than doing the wrong thing.

## Real-mode operand-size override (0.27.0 development)

Real mode defaults to 16-bit operands and addresses. `66h` selects 32-bit
operands for the supported forms below; it does not enter protected mode.
Addressing still uses the existing 16-bit ModR/M decoder, DS/SS defaults,
segment overrides and the 20-bit physical address wrap. Stack operands use
SP in SS. Repeated `66h` prefixes select 32 bits once, rather than toggling.

Supported 32-bit forms:

- MOV register/immediate (`B8–BF`), register/memory (`89`, `8B`),
  accumulator/moffs16 (`A1`, `A3`), and memory/register immediate (`C7 /0`).
- ADD, OR, ADC, SBB, AND, SUB, XOR and CMP: word-sized opcode forms become
  dword operations, including accumulator imm32 and groups `81`/`83`.
  `83` sign-extends imm8. TEST (`85`, `A9`, `F7 /0`), NOT and NEG.
- INC/DEC registers and `FF /0,/1`, preserving CF; XCHG (`87`, `90–97`).
- PUSH/POP general registers, PUSH imm32/sign-extended imm8, PUSH `FF /6`,
  POP `8F /0`; dword stack data with a 16-bit stack pointer.
- LEA with zero-extended 16-bit effective address, CWDE, CDQ,
  MOVZX/MOVSX from byte/word to dword, and BSWAP of the full register.
- MOVSD/STOSD/LODSD, with optional REP/REPNE and segment overrides;
  CX/SI/DI remain 16-bit, their upper halves are preserved, DF selects ±4,
  and a zero repeat count makes no memory access. Completed REP iterations
  remain committed on a later segment-boundary fault.
- SHL/SAL, SHR, SAR, ROL, ROR, RCL and RCR (`C1`, `D1`, `D3`);
  counts are masked to five bits and zero counts preserve flags. One-bit
  overflow follows x86 semantics; undefined flags are not an API guarantee.
- SHLD/SHRD with immediate or CL counts, and BT/BTS/BTR/BTC including
  signed register indexes into memory bit strings and immediate bit indexes.
- IMUL (`0F AF`, `69`, `6B`), one-operand MUL/IMUL and DIV/IDIV (`F7`).
  Division by zero or an unrepresentable quotient delivers real-mode INT 0
  at the faulting instruction, preserving the dividend and destination.
- PUSHFD/POPFD: four-byte stack values, reserved-bit canonicalization and
  stored AC/ID bits. VM/VIF/VIP/RF are not writable in this real-mode model.
  AC/ID survive ordinary word flags/interrupt operations and nested EXEC;
  a freshly loaded child starts with them clear.
- CPUID reads EAX and defines full output registers, with or without `66h`;
  the vendor leaf is complete and no x87/TSC features are advertised.

Upper register halves are stored independently of the existing low-word
service ABI. Byte and word writes preserve them; dword writes replace both
halves. Program loading initializes upper halves, and EXEC restores them
across nested child termination. Arithmetic defines CF/OF/AF/SF/ZF/PF;
logical operations clear CF/OF, define SF/ZF/PF and clear undefined AF.
Parity always uses the low result byte. Flags outside each instruction's
specified effects are preserved.

Other `66h` forms stop explicitly, including byte operations,
CMPSD/SCASD, control transfers, interrupt frames, segment-register operations
and system instructions.
`67h`, FS/GS overrides, LOCK, x87 and protected mode remain unsupported.
The fault message prints the instruction bytes and points to the first
prefix; unsupported forms do not change operands or flags. Supported
encodings are checked against the 15-byte instruction limit before execution.
Dword data/stack accesses crossing the real-mode 64 KiB segment limit are
refused; this interpreter reports CPU_FAULT rather than synthesizing #GP.
There is no DPMI host or 32-bit instruction pointer.

The focused `tests/dosrun_operand32_test.py` uses a native x86 host as an
independent ALU/flags oracle (32,768 cases), plus ModR/M, mixed widths,
segments, stack, instruction-length and fault-state regressions. This oracle
requires an x86-64 host with GCC. `tests/dosrun_exec_test.py` verifies upper
register lifetime across nested EXEC. `tests/gui_frame_test.py` reproduces a
partial first paint frame and verifies that a permanently bad palette fails.
All are included in `make test`.

Reference: [Intel instruction format and operand-size prefixes](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf).

## The services

Each mounted drive starts at its root and keeps an independent DOS current
directory. `INT 21h/AH=3Bh` changes it after checking the destination exists;
`AH=47h` returns it without a drive or leading slash. Relative paths, including
`C:FILE.TXT`, use that drive's directory. Rooted paths such as `C:\FILE.TXT`
start at the drive root, and `..` is clamped there. `AH=39h` creates one
directory and `AH=3Ah` removes an empty directory. Existing/nonempty or
read-only destinations return access denied (5), missing paths return 3, and
removing a DOS current directory returns 16. Mounted roots are protected.
Relative and rooted DOS paths stay within the selected drive. Failed calls
preserve drive/CWD state; these changes do not change the shell's directory.

| Interrupt | What it serves |
|---|---|
| `INT 21h` | DOS: the console (1, 2, 6-9, 0Ah, 0Bh), the date and time (2Ah, 2Ch), the version (30h), the interrupt vectors (25h, 35h), the PSP (51h, 62h), directory functions (39h-3Bh, 47h), file functions (3Ch-43h), duplication (45h/46h), MCB memory blocks (48h-4Ah), directory searches (4Eh/4Fh), FAT modification timestamps (57h) and legacy FCB record I/O |
| `INT 10h` | BIOS video: mode 13h, text character/attribute/cursor services and `AH=06h/07h` text-window scrolling |
| `INT 16h` | BIOS keyboard: reading a key and non-consuming readiness queries |
| `INT 1Ah` | BIOS clock: the tick count and the RTC time |

A function `dosrun` doesn't have is refused the way DOS refuses it --
carry set, `AX` an error code for handle services; FCB calls use `AL` status.
The Windows LFN API reports `AX=7100h` so DOS callers can fall back to 8.3 names.

## The files and the screen

A program's files are ManiOS's. A DOS path is mapped onto a ManiOS
path by the same scheme ManiDOS uses: a drive letter names a ManiOS
directory (`A:` the boot disk, `Z:` ManiOS itself, `C:` onwards the
disks under `/n`), and the path's backslashes become slashes. So a
DOS program and ManiDOS see the same files.

For installation media, override an individual drive for one invocation:

```
dosrun --mount A=/n/ata0p1 C:/SETUP.EXE
```

`--mount LETTER=/absolute/directory` can be repeated before the program name.
The directory must exist; ordinary arguments after the program remain its DOS
command tail. Defaults still apply to all other drives. This lets an installer
read A: without treating the installation target C: as a CD or boot archive.
Each invocation starts the DOS drive directories at their roots.

`INT 21h/AX=5700h` reads and `5701h` persists the open file's FAT modification
time (CX) and date (DX). These are the actual packed local-time fields in its
directory entry, shared across handles and preserved across close/reopen and
reboot. Unsupported filesystems return an error, as do invalid handles and
read-only writes. This uses the additive native `SYS_FILETIME` interface;
POSIX `stat` and DOS directory-search timestamps retain their earlier epoch
placeholder. New files do not yet acquire the current clock time automatically.
FAT subdirectories now extend their cluster chains when full; the FAT12/16
root directory retains its fixed on-disk capacity.

The console is the terminal `dosrun` runs on. Mode 13h (320x200, 256
colours) is ManiOS's VGA mode 13h: the program's framebuffer at
physical `0xA0000` is copied to ManiOS's screen as it runs.

## What it does and doesn't do

- **8086 with common 386 instructions.** `dosrun` is a 16-bit
  interpreter, but it implements the 386 instructions DOS programs and
  their libraries commonly use: `MOVZX`, `MOVSX`, `SETcc`, the bit
  operations, `CPUID`, the two-byte `Jcc`, `CMOVcc`, `XADD`,
  `CMPXCHG`, `BSWAP`, and the double shifts. A program that needs a
  full 386 protected-mode environment or a DOS extender still won't
  run.
- **XMS and EMS.** Extended memory (`INT 2Fh` AH=43h) and expanded
  memory (`INT 67h`) are present, with modest pools (8 MiB and 4 MiB
  by default). A program can allocate handles, move data between
  conventional and extended memory, and map EMS pages into the page
  frame.
- **No audio playback, mouse or serial.** The limited PIT channel-2/PC-speaker
  register model below supplies timer state, not an audio output backend.
- **No TSRs or load-only/overlay EXEC.** `INT 21h` AX=4B00h now supports
  synchronous child COM/MZ programs; other EXEC subfunctions are refused.
- **A single program at a time.** `dosrun` runs one program and returns
  to the shell when it ends.

## The source

In [`userland/dosrun/`](../userland/dosrun/):

- `cpu.c`, `cpu.h` -- the 8086 interpreter.
- `dosapi.c`, `dosapi.h` -- the DOS and BIOS services.
- `loader.c`, `loader.h` -- the `.COM` and `.EXE` loader.
- `drives.c`, `drives.h` -- DOS paths onto ManiOS paths.
- `main.c` -- the program: load, run, serve interrupts.

### String-instruction compatibility checks

Run `python3 tests/dosrun_string_test.py` to check byte/word string operations,
repeat conditions, zero counts, direction, segment overrides, accumulator TEST,
immediate shifts and BSUM's DAS-based hexadecimal conversion. The host suite
also checks PSP argument lengths. The [compatibility list](dos-compatibility.md)
records real-program results; BSUM from the FD14 BonusCD completes the tested
checksum tasks with this local build.

### Runtime and service compatibility checks

`python3 tests/dosrun_runtime_test.py` checks startup addressing/stack/flags,
packed-decimal DAA additions, PSP/environment layout, DOS memory blocks and
failure paths, file-handle duplication, independent directory searches, legacy
FCB reads/writes and save/reopen, keyboard peeking, and BIOS text state.
`tests/dosrun_test.py` also runs a guest IRET regression through the actual runner.
The three host suites and the QEMU DOS suites are included in `make test`.

`python3 tests/dosrun_port_test.py` checks emulated input status, register/flag
preservation, both polling polarities with interrupts disabled, counter wrap
and rejection of unsupported ports. It is also included in `make test`.

FCB zero-count block writes cannot truncate through the current filesystem API
and return an explicit failure. Repeated FCB closes (`INT 21h` `AH=10h`) return
success, matching MS-DOS's deliberate tolerance for applications that close an
FCB more than once; VisiCalc's saved-worksheet reopen depends on this. BIOS
`INT 10h AH=06h/07h` scrolling and
bottom-row DOS console scrolling are implemented for the active 80x25 text
page; mode-specific and arbitrary window behavior remains limited. Keyboard
input uses the terminal's line discipline (Enter is needed); direct VGA writes,
extended scan-code sequences and game input are not fully emulated. UPX-packed
MZ startup stubs are not supported. The fixed DOS date/time responses remain.
FCB layout and record semantics follow the
[MS-DOS Programmer's Reference](https://www.pcjs.org/documents/books/mspl13/msdos/dosref40/).

### SemWare-driven compatibility fixes

XLAT and AAD are covered by the string/instruction test suite. Direct writes to
the active 80x25 colour text page at B800 are now rendered as changed spans.
DOS/BIOS output updates the shadow to keep ordinary console text from being
redrawn, and a final frame is flushed even for short programs. The runtime suite
checks unchanged cells are not emitted twice; the QEMU runner suite checks a
short direct-VGA COM actually appears before exit. This does not emulate arbitrary
ports, monochrome/high-row text modes or full hardware keyboard handling.

### BIOS clock polling

Elapsed host uptime feeds PIT channel 0 at 1,193,182 oscillator clocks per
second. Ports 40h/43h support binary modes 0, 2 and 3, low/high divisor
transfers, and counter latching; zero reload means 65,536. The default is
mode 3 with that divisor. Unlike the instruction-clock channel-2 model,
PIT0 uses wall time (deterministic tests can feed oscillator clocks directly).

PIT0 rising edges latch IRQ0 in the master PIC. Port 21h masks delivery;
20h supports IRR/ISR selection and IRQ0 specific/non-specific EOI. Masking
and IF=0 retain an edge, while further edges coalesce. IRQ0 remains in
service until EOI. STI, MOV SS and POP SS inhibit delivery for the following
instruction. Guest IVT handlers execute with their interrupt frame intact.

The default BIOS INT 08h stub updates `0040:006C`, handles midnight rollover,
calls the hookable INT 1Ch vector and acknowledges IRQ0. `INT 1Ah/AH=00h`
reads that same counter and clears the rollover flag. A replaced INT 08h
handler must chain to the saved BIOS vector for BIOS ticks and INT 1Ch to
continue. Life2 reprograms PIT0 and chains its custom SOUND handler to BIOS
every 32 pulses; native blinker evolution is now verified.

This is a bounded timer/PIC implementation: PIC initialization/remapping,
other IRQ sources, PIT1, modes 1/4/5, BCD and read-back remain unsupported.
Clock checks occur every 1000 interpreted instructions, with millisecond
host granularity; reload-at-cycle-boundary timing is not modeled. Blocking
host BIOS input calls suspend guest execution and timer servicing, and HLT
still ends the runner. It is not cycle-accurate PC emulation. Channel 2
remains instruction-clocked and has no audio playback backend. Internal
BIOS ROM stubs use a private INT F1 trap. DOS date/time and BIOS RTC values
remain fixed. See `tests/dosrun_timer_test.py` and the
[Life2 timer report](compatibility/2026-10-24-pit-irq0.md).

### Minimal CGA/EGA-compatible video status

`IN AL,DX` can read the colour Input Status Register 1 at `03DAh`. Bit 0
indicates an inactive horizontal or vertical display interval; bit 3 indicates
vertical retrace. Their meanings follow the
[FreeVGA register reference](https://web.stanford.edu/~ouster/cgi-bin/cs140-spring18/pintos/specs/freevga/vga/extreg.htm).
Other status bits return zero. The CPU delegates byte input to the DOS device
model; it never reads host hardware. Other devices must explicitly handle each
port access; unsupported ports and word I/O still fault. The PIT/speaker byte
ports implemented below also support immediate-address and DX-address I/O.

The synthetic scan has a 4,096-instruction frame, 64-instruction lines, the
last 16 instructions of each line inactive, and the last 256 instructions of
the frame in vertical retrace. Phase follows executed guest instructions,
rather than toggling on every port read. Bit 0 is always high during vertical
retrace. This deterministic approximation lets programs wait for either edge,
including with interrupts disabled; it is not a real refresh frequency or
cycle-accurate CGA/EGA emulation. CRTC programming, graphics modes, attribute
controller side effects, monochrome ports and other hardware registers are
outside this model. The QEMU runner suite also executes a COM program that
waits for both edges of both bits before printing `VIDEO STATUS OK`.

### PIT channel 2 and PC speaker

Windows 1.01 setup's error at `1010:167B` points to the immediate port byte
following opcode `E6` at `1010:167A`. The captured state is port `43h`, `AL=B6h`:
channel 2, low-byte/high-byte count loading, binary mode 3 (square wave).
The routine loads `0533h` then `0633h` at port `42h`, enables gate/speaker bits
0/1 at `61h`, and restores the previous control bits. This is a two-tone PC
speaker routine, nominally 896 Hz and 751 Hz. Register meanings follow
[Intel's 8254 timer description, section 11](https://cdrdv2-public.intel.com/332995/332995-skl-io-platform-datasheet-vol1_rev004.pdf#page=86).

The DOS device model accepts these byte ports through `IN`/`OUT` immediate and
DX forms, preserving CPU flags and unaffected registers:

- `43h`: `B6h` programs channel 2, binary mode 3, low/high-byte transfers;
  `80h` latches its count. PIT0 commands are described above; other modes, BCD and read-back fault.
- `42h`: loads a divisor after both bytes have arrived; zero means 65,536.
  Reads return the mode-3 down-counter in low/high order. A count latch remains
  stable until both bytes are read; repeated latch requests preserve an unread latch.
- `61h`: retains the writable low four bits, with bit 0 gating the counter and
  bit 1 connecting the logical speaker. Bit 5 reports OUT2, including even/odd
  square-wave duty. Falling gate freezes counting and forces OUT2 high; rising
  gate reloads/restarts it. Bits 4/6/7 return zero; refresh, parity and NMI events
  are not modeled. Read-only bits in writes have no effect, as for hardware.

One interpreted instruction is a synthetic PIT input clock. Completed divisor
writes restart the modeled period immediately; cycle-boundary reload delays
are not modeled. This supports deterministic polling and tone/gate state,
not cycle-accurate timing or audible sound. `dos_speaker_frequency()` reports
the nominal logical tone or zero when gated/disconnected; there is no playback
backend. Channel 0 and BIOS timing use the separate wall-time implementation
described above; channel 1 remains unsupported.

Unsupported I/O reports the port, AL/AX and a guest-byte context window, retaining
the existing opcode/CS:IP error. `tests/dosrun_pit_test.py` covers partial/full
loads, both tone divisors, gate/enable behavior, odd/even counts, count latching,
zero divisors, wrap, CPU IN/OUT semantics and rejection without state corruption.
It is included in `make test`. QEMU regressions execute a channel-2 polling COM
and check that an unsupported BCD command faults with port/value diagnostics.

## Child processes (0.26.0)

`INT 21h AX=4B00h` loads a COM or MZ child from `DS:DX`, using the
parameter block at `ES:BX`. The parent must release enough conventional
memory first (usually with `AH=4Ah`). Each child receives its own PSP,
copied environment, command tail, default FCBs, and duplicated inherited
handles (excluding those opened with the private/no-inherit bit). Nested EXEC calls are supported. On `INT 20h`, `AH=00h`, or
`AH=4Ch`, child allocations and handles are released and the parent's
registers, stack, DTA, handles, and search state are restored. `AH=4Dh`
returns the child's status once. Zero-minimum/maximum MZ images load high.

The installed Windows 1.01 disk now gets past the missing-EXEC failure and
executes `WIN100.BIN`. It still does not reach the desktop: it rejects DOS
4.00, and a diagnostic version override reaches the unsupported `AH=52h`
DOS-internal list-of-lists request. No version override is shipped. The
Windows installation image is unchanged and is not a release asset.

Regression command: `python3 tests/dosrun_exec_test.py`.

### Games batch gaps (2026-10-23)

The FreeDOS-repo Games batch exercised eleven small DOS games. It exposed
several limits that are broader than any one title:

- **Video.** Only text mode and mode 13h are provided. Nero 5's BGI EGAVGA
  640x480 initialization produces no menu, and Dr Mind Lite stops on VGA DAC
  palette programming at `OUT 03C8h`.
- **Instructions and extenders.** Drobos stops on x87 opcode `D8`; Mahjong!,
  Mine Mayhem and ShuffleV stop on the operand-size prefix `66h`. Mahjong!
  and Mine Mayhem also require SVGA/protected-mode facilities.
- **Hardware ports.** Ford Driving Simulator stops on the PIC read
  `IN AL,21h`; Hanoi stops on `IN` port `0006h`. These join DOSStart, Spectra,
  DATES and Cash Register in the same startup gap.
- **PIT channel 0 (resolved 2026-10-24).** Life2's BASIC `SOUND` also needs
  its installed IRQ0 handler to run. The timer/PIC path above now allows
  native horizontal/vertical blinker evolution.
- **Loader.** LZEXE-compressed ShuffleV and Hanoi both unpack and begin
  execution; the earlier UPX limitation remains specific to those packed
  startup stubs.
- **Unresolved screens.** Champ Galagon and Nero 5 return before a visible
  screen; TypeFast remains running with a blank console. The exact failing
  services were not isolated, so these are recorded as observed results rather
  than assigned causes.

### Text-adventure batch (2026-10-23)

Zork I, Adventure and the official Infocom Sampler were tested with ZIP 2.00;
JZIP, Frotz 2.55/2.53 and DZIP were also tried. The detailed records are in
[compatibility/details.md](compatibility/details.md).

- **Scrolling.** `INT 10h AH=06h/07h` now scrolls a rectangular text window
  on the active 80x25 page, and DOS console output scrolls the page when a
  newline reaches the bottom row. This made Zork I's room text and command
  echo readable. The bottom-right unpainted rectangle and Zork I's clipped
  status line remain.
- **Zork I.** ZIP 2.00 starts the MIT `zork1.z3`, accepts commands, renders
  room text, and completes inventory, score, save, restore and quit. The save
  file is written and read back.
- **Adventure.** The 350-point DECUS DOS build starts and completes look,
  inventory, movement and quit. Its instruction prompt is a FORTRAN
  `FORMAT(20A1)` read, so the answer must be padded to 20 characters.
- **Planetfall.** Only the official 1984 Infocom Sampler was used. It starts
  and accepts `planet`, but ZIP clears the sample text and JZIP shows only the
  first sample line before stalling. Full Planetfall was skipped for licensing.
- **Interpreter gaps.** Frotz 2.55 and 2.53 stop at `OUT 0043h` (PIT channel 0
  control), and DZIP stops at `OUT 00F0h`. JZIP's status-line and screen
  formatting are poor.
- **Build note.** The kernel for this batch was linked with a host
  `gcc -m32` fallback because the i686-elf cross toolchain was unavailable.

### October 27 correctness audit

Byte/word shifts and double shifts share the corrected count/flag rules with
32-bit forms. Word BTR and signed memory bit-string indexing are supported.
CMPXCHG byte/word forms now use the correct width and subtraction flags.
Unmodeled FS/GS PUSH/POP, CLTS and WRMSR explicitly fault. RDTSC returns the
synthetic instruction counter in full EDX:EAX, preserving EBX/ECX; the counter
still wraps at 32 bits and CPUID does not advertise a hardware TSC.

DOS close invalidates standard handles as well as allocated handles, and open
and duplicate choose the lowest available slot. Borrowed host console file
descriptors remain owned by the runner. IOCTL 4400h validates the handle,
clears CF on success, replaces DX and recognizes a duplicated terminal or a
file opened in a formerly standard slot. File drive identity survives duplicate
and EXEC operations. Fast console output (INT 29h) writes through the same virtual console as DOS
AH=40h and is callable through its installed IVT stub. Registers and flags
are preserved. Other IOCTL functions remain explicitly unsupported;
this is not a complete DOS device-driver or raw-console-mode implementation.

The PSP environment's executable trailer names the actual mapped executable,
allowing runtimes to reopen their own file. Initial MZ allocation now honors
the header maximum (including the zero-minimum/maximum high-load convention),
and PSP/MCB boundaries agree. A bounded parent leaves a free MCB for child
EXEC without first shrinking itself. Reverse mapping chooses the longest
mount root with a component boundary; it rejects unrepresentable names.
DOS AH=2Ch derives elapsed time from the BIOS tick counter. It no longer returns
a constant noon, but is not a synchronized civil clock. INT 12h reads the BIOS
memory word initialized to 640 KiB. Unsupported INT 15h services return
CF=1/AH=86h; separately allocated XMS/EMS pools are not a linear physical
extended-memory map. BIOS RTC/date and several equipment/peripheral services
remain incomplete.

See the [October 27 engineering audit](compatibility/2026-10-27-bugfix-audit.md)
for application evidence, regression results and unresolved limits. Instruction
semantics were checked against the [Intel instruction reference](https://cdrdv2-public.intel.com/774492/325383-sdm-vol-2abcd.pdf);
IOCTL against [Microsoft's DOS implementation](https://github.com/microsoft/MS-DOS/blob/main/v4.0/src/DOS/IOCTL.ASM);
and BIOS contracts against the [IBM BIOS interface reference](https://bitsavers.org/pdf/ibm/pc/ps2/PS2_and_PC_BIOS_Interface_Technical_Reference_Apr87.pdf).
