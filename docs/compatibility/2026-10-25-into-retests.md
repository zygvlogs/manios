# ManiOS 0.26.1 — INTO compatibility retests

- **Build:** ManiOS 0.26.1-10252026, working tree based on `8dbc9ec`, with the AAM and INTO CPU fixes.
- **Kernel SHA-256:** `cad480630e3fffab85ca82592c3db4ad023a871e8300dde1ce8767d1aece15ee`.
- **Environment:** QEMU i386/486, 64 MiB RAM; separate fresh disposable FAT16 image per application, each cloned from the test image; applications launched through `dosrun`.
- **Focused INTO regression:** OF=0 continues at the following byte without touching the interrupt frame; OF=1 enters IVT vector 4, saves the original FLAGS/CS and post-INTO IP, and clears IF/TF in live FLAGS.
- **DOS INT 21h chain regression:** a test program called the DOS version service using `PUSHF; CALL FAR [0000:0084]`; it returned AX=4 and printed its success marker.

## Programs previously stopped at INTO

| Program | Command | Retest result | Compatibility status |
| --- | --- | --- | --- |
| MemMap 2 | `dosrun C:/MEMMAP.EXE` | After fixing the missing DOS INT 21h IVT gateway, it rendered a complete memory-block row (`0FFF`, `90000` bytes, owner segment `1000`, type `Program`) and returned to the shell. | Working, updated after functional output verified |
| EXEMENU 2.2 | `dosrun C:/EXEMENU.EXE C:/` | Still faults on `F0` at `0000:0024`; the IVT INT 21h entry is `F000:0130` at the fault. The earlier missing-vector explanation does not yet explain this application's remaining failure. | Unsupported, unchanged |
| Galaxy Lite 1.5 | `dosrun C:/GALAXY/GLITE.EXE C:/GALAXY/INPUT.TXT` | Advances beyond the previous BOUND stop, then faults on opcode `00` at `0000:E003`; the editor did not open. | Unsupported, unchanged |

No LOCK instruction semantics were added. The original MemMap trace showed bytes that decode syntactically as `LOCK ADD [BX+SI],AL`, but they were fetched as IVT data after a far return to `0000:0000`: its INT 21h chain call had read a zero vector. `dosrun` now installs a guest-visible DOS INT 21h ROM stub at `F000:0130`, which invokes the existing DOS service through a private trap and returns through the original interrupt/far-call frame. The regression test exercises the vector far-call convention. This fixes MemMap's wrong-address execution without app-specific behavior.

### MemMap 2

MemMap now finishes the report and returns to the shell. The displayed row identifies the allocated DOS program block at segment `1000`, with 589,824 bytes available in the block. This is functional memory-map output, so its status is **Working**.

### EXEMENU 2.2

The diagnostic trace shows this is still a wrong-address execution, not evidence that EXEMENU reached a LOCK-prefixed instruction in its own code:

```text
1010:0839  CALL FAR 11A2:0010
11A2:0048  CLI
11A2:0049  RETF       SS:SP=177D:0998, stack top=0000:0000
0000:0000  CE         INTO; stack after RETF: return CS:IP=11A2:004A
0000:0001  00 11      ADD [BX+DI],DL
0000:0003  11 ??      ADC [BX+SI],AX (the IVT byte at 0004 supplies ModR/M)
0000:0005..001F  00 00 repeated: ADD [BX+SI],AL from zeroed IVT entries
0000:0021  01 00      ADD [BX+SI],AX
0000:0023  F0 00 00  bytes spanning IVT entries, not application code
0000:0024  fault is reported after fetching F0
```

At `0000:0023`, `F0` is the high byte of the INT 08h vector's segment (`F000`); the following `00 00` bytes belong to the next IVT entry. Although these bytes decode syntactically as `LOCK ADD [BX+SI],AL`, the CPU reached them as IVT data after EXEMENU's own `RETF` at `11A2:0049` popped a zero far target. The INT 21h IVT entry remains `F000:0130` at the fault. The trace establishes the bad destination, but not yet which EXEMENU state or absent service produced that zero pointer. No LOCK behavior or application-specific workaround was added. EXEMENU remains **Unsupported**.

### Galaxy Lite 1.5

Execution advances past the prior BOUND stop but then faults on opcode `00` at `0000:E003`, before the editor opens. Its **Unsupported** status is unchanged.
