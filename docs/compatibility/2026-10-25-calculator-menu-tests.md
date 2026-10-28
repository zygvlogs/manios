# Calculator and menu programs (2026-10-25)

## Scope and environment

Tested the twelve archives supplied in the user’s list. Archives were downloaded from the COAST DOS calculator and menu catalogs ([calculat](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/calculat/), [menu](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/menu/)); archive filenames, byte sizes and SHA-256 hashes are recorded below. Archive timestamps and bundled manuals were used to identify versions where possible.

Each application was staged on a fresh disposable FAT16 image and launched with `dosrun` in ManiOS 0.26.1-10252026 under QEMU i386/486 with 64 MiB RAM. Tests were bounded to startup and one representative calculation or menu action. Unsupported CPU instructions, I/O ports, or DOS-version checks are reported as observed; no compatibility claim is made beyond the actions listed.

## Results

### `numconv.zip`

- **Archive:** 7157 bytes; SHA-256 `cb9b54912855e551a764502f001fe37c32f3d091f0d41e39bcde7a6a846d6612`.
- **Command:** `dosrun C:/NUMCONV.EXE`

- **Result:** DOS runner reported `unsupported OUT port 03D4` while the program attempted to program the VGA CRTC; conversion could not be tested.

### `hexdec.zip`

- **Archive:** 5476 bytes; SHA-256 `6e60c9e7d21a79313becd770c3507add055185045b2a14a3c60227dd28d84fbd`.
- **Command:** `dosrun C:/HEX.EXE 27 78 255`; `dosrun C:/DEC.EXE 1B 4E FF`

- **Result:** stdout returned `1B 4E FF`; reverse conversion returned `27 78 255`. Both passed exactly.

### `eval.zip`

- **Archive:** 18415 bytes; SHA-256 `2ca11bbaf59046d42063da80bb5557282c4bab55ab160b31915b716ac50b72df`.
- **Command:** `dosrun C:/EVAL.EXE 2+3`

- **Result:** stopped at unsupported x87 opcode `DB` (`12E7:0168`); no value was produced.

### `calculat.zip`

- **Archive:** 18416 bytes; SHA-256 `761865d3dd6e95dc3fa6a50a02b9582ef1b313ee5677e7a245ce6de95474dac2`.
- **Command:** `dosrun C:/CALCULAT.COM`; entered `2+3`, then an empty Return

- **Result:** the screen output was `Value: 5.000000`; empty Return returned to the shell.

### `balicalc.zip`

- **Archive:** 18613 bytes; SHA-256 `81291bb302e75170f8b135abbcb36b73f1cc0686448cc39d251db10a2f4a793c`.
- **Command:** `dosrun C:/BALICALC.EXE`

- **Result:** stopped at unsupported `OUT` to port `03D4` (`127A:0598`); no calculator interaction was reached.

### `calc100.zip`

- **Archive:** 29834 bytes; SHA-256 `4ff9898dbbc0a75ce16962734d6db229225a5e2a121d270764959805dcd2a71e`.
- **Command:** `dosrun C:/CALC.EXE 2+3`

- **Result:** stopped at unsupported opcode `E6` / I/O port `00F0` (`1323:3004`); no result was produced.

### `kycal207.zip`

- **Archive:** 39366 bytes; SHA-256 `c8788ce7e90ebddca75688ca524a9455bd3849d8d489b99433cf347f1962d5d6`.
- **Command:** `dosrun C:/KYCALC.EXE /n`

- **Result:** stopped at unsupported x87 opcode `DB` (`172C:0168`) before the calculator opened.

### `vecalc12.zip`

- **Archive:** 41758 bytes; SHA-256 `c036f2efa334ca7dbfd872e570ed0e790a4c47d767a467b622937eb02d56d7f8`.
- **Command:** `dosrun C:/VECALC.EXE`

- **Result:** returned immediately to the shell without opening the documented vector-selection screen or creating `VEC.DAT`; calculation was not reached.

### `dosmenu.zip`

- **Archive:** 12681 bytes; SHA-256 `5246dd26e4562add26ea4cee2b4e7d20dd1bc577adc43fa4494eaaa34b8d1745`.
- **Command:** `dosrun C:/MENU.EXE C:/M1.MEN`; selected `F` and Return

- **Result:** the menu title and all six choices were readable; selecting `F. EXIT FROM THIS MENU` returned to the shell.
- **Evidence:** [menu](2026-10-25-dosmenu-menu.png), [exit](2026-10-25-dosmenu-exit.png).

### `dskm10a.zip`

- **Archive:** 17942 bytes; SHA-256 `fd965ec873f350b518c0723395451fb284c0105525dedf0e4773e5645d97e441`.
- **Command:** `dosrun C:/DMC.EXE C:/SAMPLE.SRC`; then `dosrun C:/DMENU.EXE C:/SAMPLE.MNU`

- **Result:** DMC reported `Compiling: C:/SAMPLE.SRC . . . Success.` and `Writing: C:/SAMPLE.MNU . . . Done.` The generated 1,063-byte `SAMPLE.MNU` was present on FAT16. DMENU launched, but its text-mode screen was garbled/overlaid in the captured display; functional menu selection and execution were not confirmed.

### `exemnu22.zip`

- **Archive:** 21129 bytes; SHA-256 `5f1a25d84e2c13e311406f6303fca10e94c59506d3471cba87368e2abb5c070e`.
- **Command:** `dosrun C:/EXEMENU.EXE C:/`

- **Result:** the initial run stopped at INTO (`0000:0001`). Follow-up passed INTO but stopped at unsupported LOCK prefix `F0` (`0000:0024`); no usable menu opened. [Retest](2026-10-25-into-retests.md#exemenu-22).

### `menumt21.zip`

- **Archive:** 8226 bytes; SHA-256 `234b76d488d1d67aa69d970425cd187e15e5cc431d0d4e8018cd9283baac32eb`.
- **Command:** `dosrun C:/MENU.EXE C:/`

- **Result:** printed `Sorry, MS-DOS version 5.0 or higher is required to run`, then stopped at unsupported `OUT` to VGA port `03D4`; no menu opened.
