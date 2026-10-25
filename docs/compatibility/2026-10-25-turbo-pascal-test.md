Turbo Pascal 3.0 compatibility test — 2026-10-25

Input: user-provided `BorlandTurboPascal3.zip`, CRC verified. The archive
identifies the distribution as Turbo Pascal 3.0; the DOS IDE banner reports
Version 3.02A, PC-DOS. Only the standard compiler `TURBO.COM` and its companion
`TURBO.MSG` were staged in the guest. No installer or configuration program
was run.

- Archive SHA-256: `a383b78614585bf7cbb070f974b421ab359d1ddb79c7cd5ee3c4b8e5e0667aef`
- `TURBO.COM` SHA-256: `480dcdd39f49e4f36c022fc18965ab35ef4c5d2f5273f44155b8f9296b1a0f85`
- ManiOS 0.26.1-10252026; QEMU i386, CPU 486, 64 MiB RAM; disposable FAT16 disk.
- Launch: `dosrun C:/TURBO.COM`; accepted the “Include error messages” prompt and reached the IDE.
- Created `C:/HELLO.PAS` in the integrated editor, left the editor, saved through the IDE menu, compiled with the Compile command, and ran it with the Run command.
- Program output was `TP3 works`; control returned to the IDE.
- Reopened `HELLO.PAS` in a fresh IDE launch and confirmed its source remained visible, then quit to the ManiOS shell.
- The saved FAT16 source is 56 bytes, including CRLF line endings and DOS EOF byte `1Ah`; SHA-256: `de37b627ecb3736871efa1da681093c045beb6cbbbbb70a8f008e5b13e4f982c`.

Test source:

```pascal
program Hello;
begin
  writeln('TP3 works');
  end.
```

Screenshots: [program output](2026-10-25-turbo-pascal-run.png),
[reopened source](2026-10-25-turbo-pascal-reopened.png),
[exit to ManiOS shell](2026-10-25-turbo-pascal-exit.png).

This verifies one small console program and the edit/save/compile/run/reopen
path. Other Turbo Pascal features, compiler options and generated standalone
executables were not tested.
