Microsoft BASIC and GW-BASIC compatibility tests — 2026-10-25

Environment: ManiOS 0.26.1-10252026 in QEMU i386, CPU 486, 64 MiB RAM,
serial console, fresh disposable FAT16 hard disk per program. The supplied 7z
archives passed listing/extraction checks. Only their DOS program binaries were
copied into the guests; archive text files were not treated as instructions.

| Program and tested file | Result |
| --- | --- |
| Microsoft BASIC 5.28, `MSBASIC.COM` (31,744 bytes) | `dosrun C:/MSBASIC.COM` returned to the ManiOS shell without a BASIC prompt, banner or usable screen. No statement could be entered. Classified Broken for this tested binary. [Screenshot](2026-10-25-ms-basic-return.png). |
| GW-BASIC 3.23, `GWBASIC.EXE` (80,608 bytes; disk image dates the file 1988-10-05) | `dosrun C:/GWBASIC.EXE` displayed the `Ok` prompt. On the first keyboard input, `10 PRINT "GW BASIC 3.23 WORKS"`, execution stopped with `can't run opcode EC (at 12DD:6509)` and `unsupported IN port 0201`. No BASIC statement was accepted; save/reload was not reached. [Startup screenshot](2026-10-25-gw-basic-start.png). |

Input archive SHA-256:

```text
Microsoft BASIC 5.28: d60984b987ac98867619c5f8a6a42c1d91df53276a1403f0c0bb3360af36f840
GW-BASIC 3.23:        98709f74b019d1b86e30df4a7ad26c727385c4f95db2e8024648553f6eca5890
```

Tested binary SHA-256:

```text
MSBASIC.COM: 8b443bb86e0a1d14cd927eb0f964451612b15e6acee3cdb0e95260af3468ee9a
GWBASIC.EXE: e1e9fe300fe1d7fbb3a4f3a542d841abdfaf2a5f8f90c11eddd5b3d6b43aeaf3
```

These results apply to the supplied binaries and the tested DOS runtime. No
BASIC-language execution or file persistence compatibility is claimed for
these versions because the tested session did not get past interactive input.
