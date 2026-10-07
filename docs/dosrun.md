# dosrun

`dosrun` runs a DOS program -- a `.COM` or an `.EXE` -- on ManiOS.

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
  table's entries added to the load segment.

The program's memory is a 1 MiB 8086 address space. The interpreter
runs the instructions DOS programs use: the full ALU, the shifts and
rotates, the string operations with their `REP` prefixes, the
ModR/M addressing modes, the segment overrides, and the control flow.
An instruction it doesn't have stops with a message naming it, rather
than doing the wrong thing.

## The services

| Interrupt | What it serves |
|---|---|
| `INT 21h` | DOS: the console (1, 2, 6-9, 0Ah, 0Bh), the date and time (2Ah, 2Ch), the version (30h), the interrupt vectors (25h, 35h), the PSP (51h, 62h), and the file functions (3Ch-43h) |
| `INT 10h` | BIOS video: setting the mode, including mode 13h |
| `INT 16h` | BIOS keyboard: reading a key |
| `INT 1Ah` | BIOS clock: the tick count and the RTC time |

A function `dosrun` doesn't have is refused the way DOS refuses it --
carry set, `AX` an error code -- so a program that probes a service
gets a sensible answer.

## The files and the screen

A program's files are ManiOS's. A DOS path is mapped onto a ManiOS
path by the same scheme ManiDOS uses: a drive letter names a ManiOS
directory (`A:` the boot disk, `Z:` ManiOS itself, `C:` onwards the
disks under `/n`), and the path's backslashes become slashes. So a
DOS program and ManiDOS see the same files.

The console is the terminal `dosrun` runs on. Mode 13h (320x200, 256
colours) is ManiOS's VGA mode 13h: the program's framebuffer at
physical `0xA0000` is copied to ManiOS's screen as it runs.

## What it doesn't do

- **No protected mode, no 386 instructions.** It is an 8086: a program
  that needs a 386 (or a DOS extender) won't run.
- **No sound, no mouse, no serial.** A program that needs them finds
  the services absent.
- **No TSRs, no child processes.** `INT 21h` AH=4Bh (EXEC) isn't there.
- **No EMS or XMS.** A program that asks for expanded or extended
  memory gets a refusal.
- **A single program at a time.** `dosrun` runs one program and returns
  to the shell when it ends.

## The source

In [`userland/dosrun/`](../userland/dosrun/):

- `cpu.c`, `cpu.h` -- the 8086 interpreter.
- `dosapi.c`, `dosapi.h` -- the DOS and BIOS services.
- `loader.c`, `loader.h` -- the `.COM` and `.EXE` loader.
- `drives.c`, `drives.h` -- DOS paths onto ManiOS paths.
- `main.c` -- the program: load, run, serve interrupts.
