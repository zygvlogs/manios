ManiOS vintage application compatibility tests — 2026-10-25

Build: ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, serial console,
disposable 8 MiB FAT16 hard disk per program. Programs were invoked inside
ManiOS with `dosrun`; no application-level code or packaged executable was
modified. The PC-SIG floppy contents were used as shareware evaluation copies.

| Target / tested media | Command and result |
| --- | --- |
| PC-Write 3.03 (PC-SIG disk 78; released 1989-09-30) | `dosrun C:/ED.EXE C:/INPUT.TXT`; reached the text editor and displayed the two-line fixture. Save/edit/reopen not tested. Screenshot: `2026-10-25-pc-write.png`. |
| PC-Calc+ 2.0 (PC-SIG disk 199) | The included PKUNZIP 2.04g successfully expanded the Reduce/Shrink archive in the guest. `dosrun C:/PCC.EXE` stopped at `can't run opcode DB (at 2E03:00CF)` before a usable worksheet. |
| PC-File+ (PC-SIG disk 5) | `dosrun C:/PCF.EXE` stopped at `can't run opcode F0 (at 0000:0024)` before a usable database screen. |
| QEdit Advanced 2.15 (1991 shareware) | `dosrun C:/Q.EXE C:/INPUT.TXT`; displayed both fixture lines and end-of-file marker. Edit/save/reopen not tested. Screenshot: `2026-10-25-qedit.png`. |
| TED 3.0 (1991) | `dosrun C:/TED3.COM C:/INPUT.TXT`; displayed both fixture lines and the function-key bar. Edit/save/reopen not tested. Screenshot: `2026-10-25-ted.png`. |
| Turbo Pascal 3.0 (1985) | Not run: no original program media was available in the workspace. |
| FreeDOS EDLIN 2.24 | `dosrun C:/EDLIN.EXE C:/INPUT.TXT`; reported `2 lines read` and displayed its `*` command prompt. No edit/save/reopen command was issued. Screenshot: `2026-10-25-edlin.png`. This is FreeDOS EDLIN, not the original Microsoft EDLIN build. |
| Galaxy Lite 1.5 (PC-SIG disk 2318) | `dosrun C:/GLITE.EXE C:/INPUT.TXT` stopped at `can't run INTO (at 0000:0001)`. |
| DOS Navigator 1.51 (RIT Labs freeware package) | `dosrun C:/DN.COM` remained active during the observation, but produced only the ManiOS banner and a blank text screen; no file manager controls rendered. Screenshot: `2026-10-25-dos-navigator.png`. |
| PC-Talk III (copyright notice 1983–1985) | `dosrun C:/PC-TALK.EXE` reached the license/notice screen. No terminal session or modem/serial connection was attempted. Screenshot: `2026-10-25-pc-talk.png`. |

Initial PC-Write and PC-File launch attempts used an incorrect subdirectory layout and
are excluded from these results. The listed retry commands used applications staged
at the DOS root and, where relevant, the same `INPUT.TXT` fixture. The exact tested
build hash is recorded in `database.json`.
