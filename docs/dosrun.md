# dosrun

`dosrun` runs a DOS program -- a `.COM` or an `.EXE` -- on ManiOS.

See the [DOS software compatibility list](dos-compatibility.md) for tested
applications, build-specific results and known failures.

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

| Interrupt | What it serves |
|---|---|
| `INT 21h` | DOS: the console (1, 2, 6-9, 0Ah, 0Bh), the date and time (2Ah, 2Ch), the version (30h), the interrupt vectors (25h, 35h), the PSP (51h, 62h), and file functions (3Ch-43h), duplication (45h/46h), MCB memory blocks (48h-4Ah), directory searches (4Eh/4Fh) and legacy FCB record I/O |
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
- **No sound, no mouse, no serial.** A program that needs them finds
  the services absent.
- **No TSRs, no child processes.** `INT 21h` AH=4Bh (EXEC) isn't there.
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

FCB zero-count block writes cannot truncate through the current filesystem API
and return an explicit failure. BIOS text scrolling is incomplete. Keyboard
input uses the terminal's line discipline (Enter is needed); direct VGA writes,
extended scan-code sequences and game input are not fully emulated. The fixed
DOS date/time responses remain. FCB layout and record semantics follow the
[MS-DOS Programmer's Reference](https://www.pcjs.org/documents/books/mspl13/msdos/dosref40/).
