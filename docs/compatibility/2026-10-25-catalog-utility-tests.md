# Disk catalog, directory, diary, text-conversion and cursor tests (2026-10-25)

## Scope and environment

Tested the six archives listed by the user. Downloads came from the COAST DOS catalogs for [cataloging](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/catalog/), [system utilities](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/sysutil/), [editors](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/editor/), [text utilities](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/textutil/) and [screen utilities](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/screen/). Archive dates and sizes below are from those indexes; versions are taken from bundled manuals or startup screens where available.

Each runnable program was tested on a disposable FAT16 image under ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM. Each was launched with `dosrun`; functions were tested only to the extent recorded below.

## Results

### `catdisk.zip`

- **Archive:** 24809 bytes; SHA-256 `9b929870d5ebd615019b4119ea2d9c0b9d172f0fbb6437555ccbc2f6bd8a4968`.
- **Test:** `dosrun C:/CATDISK.EXE`; entered `A` then Return. A second disposable run selected `B`.

- **Result:** Startup reported version 3.0. On A:, it listed `AUTOEXEC.BAT`, `BIN`, `ETC` and `TEST`, then wrote `CATDISK.CAT` (152 bytes, four records) and `CATDISK.VOL` (12 bytes, `8192K free`) to the FAT16 test volume. Selecting B: produced an empty CAT file and a volume record. CATDISK asks only for A or B, so it could not enumerate the test partition mounted as C:.

### `doscat.zip`

- **Archive:** 2199 bytes; SHA-256 `7131215197f3777ae6c2879d7bbfce20801b971587cb970c9e36943d8167d003`.
- **Test:** Read the full archive text; tried its documented `DIR B:` operation in the ManiOS shell.

- **Result:** The archive contains only the `DOSCAT` text instructions. It has no executable. The described workflow depends on DOS `DIR`, two floppy letters and output redirection; a direct `dir B:` attempt returned `sh: dir: not found` in ManiOS.

### `dircmdr1.zip`

- **Archive:** 17695 bytes; SHA-256 `e8e688a5df9fceeb0d432a4d03b60e50779d124971988cc049cc661e70883406`.
- **Test:** `dosrun C:/DIRCMDR.EXE`; pressed `W`, then Esc.

- **Result:** The options screen rendered. Pressing W selected wide format; Esc exited and created `SETFILE.BAT` on FAT16. Its bytes decode to `SET DIRCMD=/w` plus CR/LF. [Options screen](2026-10-25-directory-commander.png).

### `diary.zip`

- **Archive:** 2617 bytes; SHA-256 `f5049ee4cfb1f0aba351a0c81a236ea00f7d533c8cf5f0468734f8ef76129a13`.
- **Test:** `dosrun C:/DIARY.COM C:/NEWFILE.TXT TEST ENTRY`. Also tried existing `C:/DIARY.TXT` with additional text arguments.

- **Result:** `NEWFILE.TXT` was created but had a size of zero bytes. An existing 11-byte fixture remained unchanged. The documented date/time and argument text were not appended.

### `asc2ebc.zip`

- **Archive:** 2017 bytes; SHA-256 `6fcad3a45532327f3e89dc03af6591e387e9fe46165f10a9b36646b829f2d113`.
- **Test:** `dosrun C:/ASC2EBC.COM`; sent `ABC 123` on standard input and captured raw standard output.

- **Result:** Raw stdout contained `C1 C2 C3 40 F1 F2 F3` for the ASCII input `ABC 123`; this is the expected EBCDIC conversion. Redirecting to a FAT16 pathname through the ManiOS shell was unavailable, so validation used the raw terminal stream.

### `cylon4.zip`

- **Archive:** 1503 bytes; SHA-256 `816a89590a70268ee362e467c1b7e20425f439d83eb26cdd5fd0735715704135`.
- **Test:** `dosrun C:/CYLONEGA.COM`; observed the shell cursor after the TSR launch.

- **Result:** The EGA binary returned to ManiOS and the cursor appearance changed from a flat underline to the moving wave described in the manual. The monochrome and CGA variants were not tested. [Cursor capture](2026-10-25-cylon4-cursor.png).
