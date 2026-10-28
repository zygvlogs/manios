# EXEMENU 2.2 investigation (2026-10-25)

## Diagnosis

The zero target at `11A2:0049` was not a corrupted normal return address, a bad EXE relocation, or broken `RETF` behavior. It was the target EXEMENU deliberately fetched from the real-mode interrupt vector table.

The focused trace showed:

- `1010:0839` far-calls the Turbo Pascal runtime helper at `11A2:0010`.
- The helper reads the interrupt number `10h` from its argument, multiplies it by four, and executes `LDS BX,[BX]` with `DS=0000`. That reads IVT entry `0000:0040`, the BIOS video (`INT 10h`) vector.
- The old IVT entry was `0000:0000`. At `11A2:0027` / `0028`, `PUSH DS` and `PUSH BX` put that vector's segment and offset on the stack. After its register-copy sequence, `POP DS`, `POP SI`, and `POP AX` at `0045`–`0047` restore caller state and leave the vector target at `SS:SP=177D:0998`; `CLI` runs at `0048`, then `RETF` at `0049` pops the fetched target, as expected for Turbo Pascal's far-call `Intr` implementation.
- The following stack words `004A, 11A2, 0202` are the helper's prepared return IP, CS and FLAGS for the BIOS handler's `IRET`. They confirm that the ordinary helper return frame was intact.

Real-mode `RETF` pops IP then CS. It was correct here: it transferred control to the IVT's zero `INT 10h` target. The CPU then decoded IVT bytes as instructions; `F0` at `0000:0023` is the high byte of the existing `INT 08h` segment `F000`, not a legitimate LOCK prefix in EXEMENU code. The later fault at `0000:0024` was therefore a consequence of the missing BIOS vector, not a reason to add LOCK handling.

## Fix

`dos_new()` now installs guest ROM-style stubs for BIOS vectors already implemented by ManiDOS (`10h`, `11h`, `12h`, `14h`, `15h`, `16h`, `17h`, and `1Ah`). The stubs bridge through private `INT F3h` to the existing BIOS service handlers and end in `IRET`, preserving the far-call-to-IVT frame used by Turbo Pascal. BIOS status flags are copied through the nested interrupt frame to the caller's saved FLAGS. Text-screen contents are presented before blocking BIOS keyboard reads, so applications that draw directly to B800 memory become visible before waiting for input.

No CPU opcode semantics, EXEMENU address, or synthetic return address was added. Existing AAM, INTO, and INT 21h code was preserved.

## Tests

- `make -j4`: passed.
- `python3 tests/dosrun_host_test.py`: passed, including existing BIOS keyboard ZF regression.
- `python3 tests/dosrun_test.py build/manios-zkt.elf`: passed all DOS runner checks, including a new FAT16 COM regression that calls BIOS `INT 10h` through its IVT far-call entry and verifies the returned mode/column registers.
- Fresh disposable FAT16 image: EXEMENU v2.2 opened its menu, displayed the `ACTION.COM` and `ZHELLO.COM` entries, and arrow keys moved the highlight. ESC returned to the ManiOS shell and EXEMENU displayed its normal exit message.
- A representative Enter action returned to the menu but the child program's output was not observed. Program launch is therefore not confirmed and the database status remains **Partial**.

## Full regression suite and unrelated intermittent checks

Two `make test -j4` runs stopped at the same `gui_test.py` assertion: the paint palette swatches. An isolated GUI rerun passed all checks, including the palette. This points to an intermittent GUI test failure, but the full suite did not complete as a single clean run.

After the full-suite stop, the remaining DOS and CPU regression scripts were run directly: `dos_test.py`, `dosrun_host_test.py`, AAM, INTO, string, EXEC, runtime, port, PIT, `dosrun_test.py`, and the cluster and driver tests all passed. The new BIOS IVT regression passed in the QEMU FAT16 DOS-runner test.

A previous full run had reported the PC speaker as 439 Hz / 0.7 s instead of 440 Hz / 0.6 s. The driver test passed on isolated follow-up at 440 Hz / 600 ms. The current driver test also passed that measurement; no audio regression attributable to this BIOS change was observed.

## Remaining limitation

EXEMENU's display, directory listing, arrow navigation, and ESC exit were demonstrated. Starting a selected DOS program remains unverified, so EXEMENU is recorded as **Partial**, not Working.
