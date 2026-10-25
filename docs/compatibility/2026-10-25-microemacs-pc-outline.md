# MicroEMACS 3.9 and PC-Outline 3.34 follow-up

Build: ManiOS 0.26.1-10252026, QEMU i386 with a 486 CPU and 64 MiB RAM. Each
program was launched using `dosrun` on a disposable FAT16 hard disk.

## MicroEMACS 3.9 — Working

The Utah IBM PC archive provides `em39exe.arc`; the archive listing identifies
`MEIBM.EXE` as a 1987 executable. The [3.9 release notes](https://github.com/torvalds/uemacs/blob/master/readme.39e)
likewise identify `MEIBM.EXE` as the IBM-PC CGA/monochrome/EGA build.

- Download: [`em39exe.arc`](https://ftp.math.utah.edu/pub/ibmpc/em39exe.arc)
- Archive SHA-256: `ecfefb169375b6db3c120d5a8334f9501c3e3f10b130fb2d3debed7dc564ce34`
- Extracted `MEIBM.EXE`: 77,008 bytes; SHA-256 `1252e4232867d408aca1d7baaae21cc9d8373f0e9d777f4689bd6839a1732f31`
- Disk: fresh copy of the ManiOS FAT16 test image, with `MEIBM.EXE` and `TEST.TXT` on C:.

Workflow:

1. Ran `dosrun C:/MEIBM.EXE C:/TEST.TXT` and typed `hellomanios` into the new buffer.
2. Saved with Ctrl+X, Ctrl+S and exited with Ctrl+X, Ctrl+C.
3. Reopened the file, moved to the end of the line with Ctrl+E, appended `!`, saved, and exited.
4. Reopened the file a second time. The display showed `hellomanios!` and the program reported that it read one line.
5. Copied the saved file from the FAT16 image and compared its bytes with the expected value. It matched exactly: `hellomanios!\r\n` (14 bytes; SHA-256 `11493c63ca51b22e74a7c83cefd774c366d6ccc0a95ecd3ec6df661dd4d10277`).

This verifies text entry, editing, saving, returning to ManiOS, reopening, and
byte-exact persistence for this test file.

## PC-Outline 3.34 — Partial

- User-supplied archive: `pco334.zip`; SHA-256 `64f8d50eac6932e75d6a00a374e7c37da313dd6fcba26bc2bb85fb471306d252`.
- `README.1` identifies version 3.34; `PCO.EXE` is 73,315 bytes.
- `dosrun C:/PCO.EXE` displayed the shareware screen and opening menu. After
  selecting “Open a new outline”, the new-file dialog accepted the filename
  `test` character by character.
- Pressing Return, keypad Enter, Ctrl+M, and Ctrl+Enter left the dialog open.
  No `TEST.PCO` outline file appeared on the FAT16 disk. The editor and save/reopen path
  were therefore not reached.

Evidence: [new-file dialog after Return](2026-10-25-pc-outline-filename-prompt.png).

## PC-DeskTeam — Untested

Per the user's direction, this remains untested because the required library
component is unavailable. No launch or feature checks were attempted.
