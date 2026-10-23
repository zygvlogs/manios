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
| `INT 10h` | BIOS video: mode 13h and text character/attribute/cursor services |
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
and return an explicit failure. BIOS text scrolling is incomplete. Keyboard
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

The runner advances the BIOS tick counter at `0040:006C` using elapsed host
uptime, approximately 18.2 ticks per second. `INT 1Ah/AH=00h` reads the same
counter and clears its midnight rollover flag. Fractional updates are retained;
the counter starts at zero for each program, rather than the host time of day.
This lets startup routines that poll BIOS memory progress during busy loops.
It does not deliver timer interrupts to guest handlers or implement the PIT's
hardware ports. DOS date/time and BIOS RTC responses still use fixed values.
The host runtime suite checks fractions, word carry and midnight rollover; the
QEMU suite checks a direct-memory polling loop through the actual runner.

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
  `80h` latches its count. Other channels, modes, BCD and read-back commands fault.
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
backend. Channel 0/1 and timer IRQ programming remain unsupported, and the
existing elapsed-uptime BIOS clock is independent of this device.

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
