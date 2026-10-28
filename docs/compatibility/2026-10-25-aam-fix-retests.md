# ManiOS 0.26.1 — AAM compatibility retests

- **Build:** ManiOS 0.26.1-10252026, working tree based on commit `8dbc9ec`.
- **Kernel SHA-256:** `539c0af8e76dd471680649a7967276720f659c6f6656ff0cdf14dc9c2964752a`.
- **Environment:** QEMU i386 with a 486 CPU; each application was run on its own fresh disposable clone of the 8 MiB FAT16 test disk (partition offset 1 MiB).
- **CPU implementation:** `AAM imm8` now divides AL by its immediate base, stores quotient in AH and remainder in AL, updates SF/ZF/PF from AL and preserves undefined flags. An immediate base of zero enters interrupt 0.
- **Focused regression:** `tests/dosrun_aam_test.py` checks multiple bases, quotient/remainder, SF/ZF/PF, preserved flags and the INT 0 frame for base zero.

<a id="carve"></a>
## CARVE 2.0a

- **Executable:** `CARVE.EXE` from `carve20a.zip` (archive SHA-256 `9377472d77016009e13269715a5514ab0f938d2d4c3503e3645536227f8e4a6c`).
- **Input:** deterministic 2,500-byte binary file on a fresh FAT16 image.
- **Commands:** `dosrun C:/CARVE.EXE /s IN.TXT 1024`; deleted the original on-disk `IN.TXT`; then `dosrun C:/CARVE.EXE /j IN.TXT`.
- **Observed:** Split completed as three chunks, then successfully rebuilt `IN.TXT` after the source had been removed. The restored file was 2,500 bytes and matched the original exactly, SHA-256 `ce6c543d6426fcbb94d19209ac6691ffca371826a0641fe8a45d870843762de0`.
- **Result:** **Working** for the documented split and join workflow.

<a id="vern-buerg-list"></a>
## Vern Buerg LIST 7.1a

- **Executable:** `LIST71.COM` (renamed to `LIST.COM` on the guest FAT16 disk), SHA-256 `aa954f7c51386ed66b88f6beb2a135330a68a0a5b2e104ea489e48288474e60b`.
- **Fixture:** `LISTTEST.TXT` containing `LIST_EXIT_FIXTURE` and `second known content line`.
- **Command:** `dosrun C:/LIST.COM C:/LISTTEST.TXT`.
- **Observed:** LIST opened its interactive viewer and rendered both known lines; pressing F10 returned to the ManiOS shell.
- **Result:** **Working** for opening, viewing and exiting a text file. Editing or file modification is not a feature of this viewer and was not tested.

## Regression suite

`make -j4` and `make test -j4` passed. The full regression includes the focused AAM test and the existing DOS interpreter, filesystem, device, graphics, networking and cluster checks.
