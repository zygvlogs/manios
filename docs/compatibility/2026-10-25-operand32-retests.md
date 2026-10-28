# ManiOS 0.27.0 compatibility development report — 2026-10-25

This is **0.27.0-10252026, a development prerelease**. It implements a bounded
real-mode operand-size subset, not a protected-mode execution environment.
No compatibility status was upgraded on the strength of startup progress.

## Repository safety and versioning

The checkout is based on `8dbc9ec`. Before editing, Git reported 14 modified
tracked files and 10 untracked files; the exact paths are preserved in the
[initial status](2026-10-25-027-baseline-status.txt). The pre-update tracked
diff is also saved at `/tmp/manios-027-evidence/baseline.patch`. Existing AAM,
INTO, INT 21h gateway, BIOS IVT/F3 bridge and their tests were inspected and
retained. Earlier evidence reports were retained. No reset, clean, stash,
commit or push was performed; third-party executables remain outside Git.

`VERSION` is the authoritative version source. Make reads it into kernel,
userspace and boot-loader defines, boot-area metadata and ISO volume names.
It now contains `0.27.0-10252026`; README and this release's notes identify a
development prerelease. Historical release/evidence versions and previously
built distribution/Android artifacts were not relabeled.

## Defects and implementation

The interpreter previously stored only low 16-bit registers and did not
consume `66h` as a prefix. It now stores independent upper halves, preserving
them on byte/word writes and replacing both halves on dword writes. DOS EXEC
initializes child upper registers and restores the parent's halves on return,
including nested children. CPUID now reads full EAX, defines all four full
output registers, returns the complete vendor string and stops advertising
an unimplemented feature bit (the previous EDX=10h advertised TSC).

Supported 32-bit forms include MOV, the eight basic ALU operations including
ADC/SBB/CMP, TEST/NOT/NEG, INC/DEC, XCHG, LEA, PUSH/POP, CWDE/CDQ,
MOVZX/MOVSX and BSWAP. MOVSD/STOSD/LODSD support REP/REPNE with 16-bit
CX/SI/DI and segment overrides. The [CPU contract](../dosrun.md#real-mode-operand-size-override-0270-development)
lists exact encodings, flag effects and limitations. Duplicate `66h` selects
32-bit operands once. Addresses and SP remain 16-bit; existing segment
selection and physical-memory wrapping remain in use. Unsupported forms,
invalid opcode extensions, oversized supported encodings and dword accesses
crossing a segment limit explicitly fault. Prefixed fault messages include
instruction bytes and point to the first prefix, rather than the byte after
it; a one-byte diagnostic-address difference alone is not execution progress.

### Paint palette timing

The GUI test waited for a white pixel near the top of the canvas, then
immediately asserted later palette/label pixels. Window flushes pass through
multiple wsys writes, each capable of triggering composition, so a partial
first frame can satisfy the old predicate before those controls arrive.
The test now waits for page, palette, selection border and brush label;
the original exact assertions remain. A permanently incorrect palette still
fails with a bounded timeout.

The intermittent QEMU assertion was **not observed** in the initial full GUI
run, six isolated original paint runs, or the original-predicate GUI stage
of the initial full-suite run. `gui_frame_test.py` deterministically
reproduces the partial-frame ordering that makes the old predicate fail and
checks the repaired wait, a delayed label and a permanent palette error.
This establishes a synchronization defect and a covered fix; it does not
claim that every historical intermittent failure had this cause. No shared
QEMU guest state or parallel GUI recipe was found: `make test -j4` executes
its test commands sequentially. The initial PC speaker check also passed.

## Fresh-image application retests

Final kernel SHA-256:
`1fd35069cfadeb2bb630e5ea9748f2b3ae4970e609d1973cd7331f36c6bf2c32`.

Each launch used QEMU i386, `-cpu 486`, 64 MiB RAM and a freshly formatted,
disposable 8 MiB MBR/FAT16 image, with cwd `/n/ata0p1`. Each command was
observed until shell return or a 12-second bound. A shell return after a CPU
fault is **not** a successful application result. The [machine-readable
results](2026-10-25-operand32-results.json) contain exact commands, executable
and asset hashes, raw/normalized output, observations and screenshot paths.
All twelve final probe commands returned after a fault or runtime error;
therefore input/edit/archive/game workflows could not be completed.

| Program | Observed result | Status retained |
|---|---|---|
| PEDIT 4.00, full and light | Passes REP MOVSD. Full stops at `2251:00AD`, light at `20FA:00B0`, both on `66 C1 E0 04` (SHL EAX,4). No document editing verified. | Unsupported |
| Directory Maven | Scan stops at `1656:0C90`, `66 C1 E0 10` (SHL EAX,16). No usable directory manager. | Unsupported |
| ShuffleV | Board appears; stops at `1C36:07FC`, `66 C1 E0 10`. No move verified. | Unsupported |
| Mine Mayhem | MOVZX ESI,AL executes; then `66 9C` (PUSHFD) stops at `1426:2DC6`. No gameplay. | Unsupported |
| FoxType | MOVZX ESI,AL executes; PUSHFD stops at `1010:30FB`. Fixture not viewed. | Unsupported |
| LHa | Archive-create attempt passes MOVZX ESI,AL; PUSHFD stops at `1010:30FB`. No archive round trip verified. | Unsupported |
| lzop | MOVZX ESI,AL executes; PUSHFD stops at `1010:30FB`. No compression verified. | Unsupported |
| UnRAR | MOVZX ESI,AL executes; PUSHFD stops at `1010:3130`. No extraction verified. | Unsupported |
| DOS/32A | Prints 9.1.2, then PUSHFD stops at `1016:02A1`. No protected-mode payload. | Unsupported |
| Mahjong! | Advances beyond the previous prefix stop and prints `DPMI error (0001): insufficient memory for initialization`. This is the application's message, not proof that adding RAM would solve it. | Unsupported |
| FreeDOS MEM | Stops at `1010:03D4`, `66 C1 E3 10` (SHL EBX,16). No summary. | Broken |

The first narrower implementation still stopped PEDIT at `66 F3 A5`;
MOVSD/STOSD/LODSD were subsequently implemented as general CPU operations
and retested on new images. All final observations above use the final
kernel hash, not that intermediate build. Priority applications whose first
blocker was already a shift still stop there; their execution progress is
not overstated.

Reproduction with the existing local audit assets:

```sh
python3 tools/retest_operand32_apps.py --output ../compat-027-repeat-audit
```

Choose a new output directory outside the repository on each run. The tool
uses the previous PEDIT/Maven audit extracts, FreeDOS marathon assets under
`/tmp/manios-marathon-200`, the previous game disks as read-only asset sources,
and MEM from `freedos-bonus-audit/FD14BNS.iso`; it formats new disks rather
than reusing their file systems. It never downloads or adds binaries to Git.
The old evidence links remain in each database entry's `previous_evidence`.

### EXEMENU 2.2 launch check

A hand-assembled ACTION.COM creates an 18-byte `LAUNCH.OK` file containing
`EXEMENU CHILD OK\r\n`. Direct `dosrun C:/ACTION.COM` on an independent fresh
FAT16 image produced the exact bytes, checked after QEMU stopped with mtools.
On a second fresh image, EXEMENU was placed under RUN and ACTION.COM at the
root. The menu listed both, Down selected ACTION.COM, Enter returned to the
menu and Escape was sent to exit. Recursive mtools inspection after shutdown
found no LAUNCH.OK anywhere. Thus child execution remains unverified; no
EXEMENU-specific changes were introduced and its status stays **Partial**.
The [menu screenshot](2026-10-25-027-exemenu-initial.png) and
[selected entry](2026-10-25-027-exemenu-after-down.png) preserve the UI evidence.
The generated probe and full logs remain in `/tmp/manios-027-evidence` and
`../compat-027-exemenu-verified`. The missing marker does not identify which
DOS service or application state prevented the launch.

## Database and remaining limitations

There are still exactly **200 unique IDs**: **80 Working, 25 Partial,
14 Broken, 66 Unsupported, 15 Untested**. Working + Partial remains
**105/200 (52.5%)**. Twelve database entries received current evidence
(including EXEMENU and the additional Mahjong/MEM investigations); no status
changed. Historical evidence remains linked, with its original build context.
The compatibility overview is regenerated from the JSON source.

The next CPU blockers include 32-bit shifts/double shifts, EFLAGS stack and
bit operations, PUSHA/POPA, comparison strings, multiply/divide and control
transfers. `67h`, protected-mode state, DPMI and x87/FWAIT remain absent.
PIC port 0021h already has virtual hardware backing. CRTC 03D4h, CMOS 0070h
and FPU-related 00F0h need corresponding device state; no arbitrary port
success values were added. [Architecture notes](0.27.0-future-architecture.md)
describe these boundaries and future DPMI work.

## Regression and build evidence

- Initial full-suite invocation: exit 0, including its original GUI predicate
  and a successful speaker measurement. Its kernel was the pre-update build;
  host tests executed current source as development proceeded, so this is
  not described as a frozen-source baseline certification.
- Final `make -j4`: exit 0. An inherited `CPU_GUEST` switch warning in
  dosrun/main.c was observed during earlier compilation; no new CPU-core
  warnings or link failures were reported.
- Focused CPU test: 32,768 native x86 ALU/flag comparisons, plus addressing,
  widths, memory writes, stack, sign extension, strings, CPUID, length and
  failure-state checks; address/undefined-behavior sanitizers passed.
- Nested EXEC upper-register regression, AAM, INTO and deterministic GUI
  first-frame regressions passed. [Focused test transcripts](2026-10-25-027-focused-tests.txt).
- Focused GUI paint test on the final build: exit 0; strokes, palette,
  eraser, brush sizing and Clear passed.
- Focused QEMU DOS runner: exit 0, including OP32.COM, DOS INT 21h IVT far
  call, BIOS INT 10h IVT far call, keyboard, PIT, directory and file tests.
  [Complete focused transcript](2026-10-25-027-dos-guest.txt).
- An intermediate final-suite invocation was intentionally stopped while
  MOVSD/STOSD/LODSD were added. It is not counted as a passing suite.
- Final `make test -j4`: **exit 0**, including boot, installation, console,
  OpenBSD userspace, network, graphics/VBE, desktop, GUI, DOS/BIOS, CPU,
  ports/PIT, driver and cluster stages. PC speaker measured **440 Hz / 600 ms**.
  [Complete suite transcript](2026-10-25-027-full-test.txt) and
  [final incremental build transcript](2026-10-25-027-build.txt).
- Final documentation generation check and Git whitespace check passed.
- **Unresolved test failures: none.** Application compatibility blockers listed
  above remain explicit runtime failures, not passing compatibility tests.

No test assertions were deleted or weakened. The new CPU and deterministic
GUI tests are part of `make test`. Existing app reports, tests and earlier
uncommitted compatibility work were retained.

Final ISO SHA-256: `45feafc756eb935df582b7c6e493e7d2d129ec45af6c55851c1baa41a40562f6`.
