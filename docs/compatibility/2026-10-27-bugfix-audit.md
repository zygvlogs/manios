# ManiOS 0.27.0 correctness audit — October 26–28, 2026

This session fixes shared CPU and DOS/BIOS defects while preserving the existing
0.27.0 development work. It does **not** establish that every bug is fixed or
that all 200 applications were freshly executed. All 200 catalogue summaries
were reviewed and assigned follow-up work; 22 catalogue entries received fresh
application runs, including both PEDIT variants. The other 178 retain explicitly
identified historical evidence in the accompanying catalogue audit.

Version remains **0.27.0-10252026**. No commits, pushes, Git resets, cleanup of
existing work, or modifications to original compatibility images were performed.

## Baseline and preservation

The incoming tree already contained substantial modified and untracked work,
including the operand-size subset, AAM/INTO fixes and GUI synchronization tests.
Before edits, its status, tracked diff and existing work were saved under
`../bugfix-20261026/` as `baseline-status.txt`, `existing-work.patch` and
`existing-work.tar.gz`. A separate `baseline-tree` preserves the original source,
tests and build; its toolchain is shared read-only with the working checkout.
The frozen baseline full suite passed. An earlier baseline test was interrupted
to move testing to that frozen copy; that interrupted run is not a pass.

Baseline catalogue: **80 Working, 25 Partial, 14 Broken, 66 Unsupported,
15 Untested**, exactly 200 unique IDs. Old reports and evidence are retained.
New classifications depend on observable tasks, not on passing startup code.

## Defect ledger

Counts below use **grouped root causes**, not individual failing test vectors:
**26 focused investigations: 19 corrected groups and 7 unresolved groups**.
The 200 catalogue records are not counted as 200 bugs. Architecture limitations
and untested workflows outside this ledger remain outstanding.

| ID | Symptom and root cause | General correction | Regression coverage |
|---|---|---|---|
| F01 | Narrow shifts/rotates mishandled counts/one-bit overflow; dword forms were absent | Shared 8/16/32-bit engine, five-bit counts, carry-rotate modulo, defined flags, invalid group rejection | `dosrun_shift_test.py`: 344,742 checks |
| F02 | Double-shift flags/counts were incomplete; word counts could invoke undefined C shifts | Shared SHLD/SHRD 16/32-bit implementation; defined one-bit OF, bounded C operations | `dosrun_double_shift_test.py`: 50,176 native defined cases |
| F03 | Memory bit operations masked indexes into one word; BTR16/dword forms missing | Signed bit-string address calculation; BT/BTS/BTR/BTC16/32, immediate/register forms, segment checks | `dosrun_bit_test.py`: 4,192 cases |
| F04 | PUSHFD/POPFD unsupported and upper flag state absent | Four-byte flags stack operations, reserved masks, AC/ID state, load/reset and nested EXEC restoration | `dosrun_flags32_test.py`, `dosrun_exec_test.py` |
| F05 | PEDIT advances from shifts to unsupported IMUL32 | One-, two- and three-operand multiply with full products and CF/OF | `dosrun_imul32_test.py`: 640 cases |
| F06 | PEDIT then reaches IDIV32; narrow division faults terminated execution and signed overflow risked C UB | Bounded 64/32 divide, signed magnitude handling, INT 0 fault frames for zero/overflow at 8/16/32 bits | `dosrun_divide_test.py`: 8,192 results, nine fault frames |
| F07 | CMPXCHG word form used byte data and updated only ZF | Correct byte/word destinations and subtraction flags | `dosrun_cpu_audit_test.py`: 400 native flag/width cases |
| F08 | FS/GS stack opcodes accessed SS instead | Explicit unsupported fault without SS/SP mutation | CPU audit test |
| F09 | CLTS cleared carry despite having no control-register model | Explicit unsupported fault | CPU audit test |
| F10 | WRMSR silently succeeded without MSR state | Explicit unsupported fault | CPU audit test |
| F11 | RDTSC wrote incomplete/wrong register values | Synthetic counter returned in full EDX:EAX, EBX/ECX preserved | CPU audit test; counter still wraps at 32 bits |
| F12 | XGREP closes stdin and expects open to reuse handle 0; close ignored standard handles and allocation started at 5 | Close invalidates every DOS slot; open/dup use lowest free slot; correct invalid-handle errors and descriptor ownership | `dosrun_handle_test.py`; guest XGREP searches |
| F13 | sleep repeatedly polls a constant noon from AH=2Ch | DOS time follows the BIOS tick counter with centisecond conversion | `dosrun_time_test.py`; repeated guest 0/1/2-second delays |
| F14 | GNU bc tries to reopen `C:\DOSRUN.EXE` and emits SYS2090 | Actual mounted executable pathname in environment trailer; longest-root reverse mapping and bounds | `dosrun_environment_test.py`; guest ENVNAME.COM and bc |
| F15 | bc then rejects an opened input file because IOCTL retained incoming CF; handle classification depended on slot number | Validated 4400h, explicit success CF, full DX, actual terminal/file classification and preserved drive identity | Handle tests include PTYs, aliases, invalid calls and duplicates; bc calculations |
| F16 | MEM reports 35,840 KiB conventional memory from leftover AH | INT 12h returns the BDA memory word, initialized to 640 KiB | `dosrun_bios_memory_test.py`; guest MEM |
| F17 | INT 15h silently succeeds with leftover registers, producing invented extended-memory/configuration values | Unsupported services fail with AH=86h/CF=1; XMS/EMS are not advertised as a linear memory map | BIOS memory test; guest MEM |
| F18 | INT 29h produces no output | Shared virtual-console output with direct/chained IVT support and preserved registers/flags | `dosrun_fast_console_test.py`; guest FASTCON.COM |
| F19 | EXEMENU EXEC returns DOS error 8 even though its MZ requests a small bounded block | Initial load honors MZ maximum allocation; PSP/MCB ownership agrees and the remainder is free | `dosrun_initial_memory_test.py`; real EXEMENU child marker |

Native CPU comparisons use independent host x86 operations where their flags
are defined, plus ASan/UBSan. Undefined architecture flags are not used as
oracle expectations. The new native instruction suites cover over 400,000
checks in addition to the incoming 32,768 operand-size ALU cases. Dword divide
uses no unavailable freestanding 64-bit division runtime helper.

The fast-output investigation initially misread a short HEAD output excerpt as
a regression. The full serial logs contain all fixture lines before a long
screen redraw, including on the pre-INT29 build. HEAD never required the INT29
fix in these runs. INT29 was independently absent from the implementation and is now covered
by direct/chained synthetic output tests. The initial test harness incorrectly
rejected the interpreter’s IRET result; that harness defect was fixed before
the final passing gate. No HEAD regression is claimed.

## Application verification

Every guest run uses a freshly formatted disposable FAT16 image. Original
images are used only as read-only extraction sources. DOS binaries run through
the interpreter or QEMU, never as native host programs. Input and kernel hashes,
commands, full serial transcripts and saved-file bytes are retained in the
session evidence directory; the committed evidence export contains text and
metadata rather than third-party executables or disk images.

| Application | Verified observation and limit | Classification decision |
|---|---|---|
| PEDIT 4.00 | Both variants enter the editor. Standard version inserts CHECK, saves, exits to the shell and reopens; mtools confirms exact `CHECKalpha\r\nbeta target\r\ngamma\r\n` bytes | Unsupported → Partial; advanced features and Light save cycle untested |
| ShuffleV | Board reacts to directional moves; move counter changes; Alt+X returns to shell | Unsupported → Partial; complete game/win state untested |
| GNU bc | Reopens itself and evaluates 2+2=4, 12*13=156 and scale=4;22/7=3.1428 from relative and drive-qualified files | Broken → Working for tested calculations |
| XGREP | Relative/qualified matching search and anchored regex print exactly beta target; absent pattern returns without a match | Broken → Working |
| sleep | 0/1/2-second requests and repeated 1-second requests return; wall times include startup overhead | Broken → Working |
| FreeDOS MEM | Conventional 640K total/64K used/576K free; reserved 384K; XMS 8192K; largest executable 576K agree with this model | Broken → Working for default report; other options untested |
| EXEMENU 2.2 | Menu selection launches ACTION.COM, returns to menu and exits; disk contains exact `EXEMENU CHILD OK\r\n` marker | Partial → Working for COM launch/navigation/exit; BAT/BAS dispatch untested |
| Directory Maven | Advances from SHL32 to OUT 03D4h, cursor-high index 0Eh | Unsupported unchanged |
| FoxType, LHa, lzop, UnRAR | Advance past PUSHFD to `0F 01 E0` (SMSW AX); no viewing/archive workflow completes | Unsupported unchanged |
| Mine Mayhem | Advances past PUSHFD to group-7 system opcode 0F 01 | Unsupported unchanged |
| DOS/32A | Advances past PUSHFD to x87 DB at 1016:030C | Unsupported unchanged |
| Mahjong | Still reports DPMI initialization failure | Unsupported unchanged |
| ZDir | VGA screenshot has readable CP437 borders and correct fixture names; historical garbling was not reproduced on the actual screen | Partial unchanged; output metadata/options remain unverified |
| CPUID 5.93 | Advances beyond the old 66h stop, reaches x87 DB at 1000:028C | Unsupported unchanged |
| SPACE 3 | Now prints a report, but claims identical free 8-MiB disks for C: through Z:, including absent drives | Unsupported → Broken; numerical output is not meaningful compatibility |
| GNU grep and HEAD | Expected matching line / all three fixture lines are present in complete serial logs | Working retained |
| MD5SUM | No digest on current or frozen baseline; startup reaches unimplemented AH=37h. Scratch-only support then reaches missing AH=60h | Working → Broken; previous success is not reproducible with the hashed binary |
| STATUS 2.01 | Conventional total now 640K, but free remains 1024K and device claims are not trustworthy | Working → Partial; report accuracy not verified |

A live editor or menu intentionally remains active; that is not an execution
hang. Conversely a clean return without a calculation/hash/file operation is
not a pass. Complete raw transcripts, not just their last lines, determine
whether console text was actually printed.

## Unresolved investigation ledger

| ID | Confirmed remaining boundary | Next required work |
|---|---|---|
| U01 | SMSW/group-7 control-state probe | Coherent CR0 state and reset/exception contracts; no chosen-to-pass constant |
| U02 | x87/FWAIT | Register stack, tags, rounding, environment and pending exception model with numerical/state oracles |
| U03 | DPMI/protected mode/LE-LX and 67h | Descriptor caches, EIP, address decoder/SIB, memory translation, transitions and DPMI bridge; see architecture notes |
| U04 | VGA CRTC 03D4h/03D5h | Register bank plus cursor/start-address behavior integrated with text rendering |
| U05 | DOS free-space AH=36h | Mounted-drive validation and native FAT free-cluster/geometry information |
| U06 | MD5SUM startup | DOS AH=37h and canonical-path AH=60h with correct errors, bounds and per-drive paths; scratch prototype is not a fix |
| U07 | STATUS free-memory/device report | Trace its numerical source and reconcile DOS allocations, BDA equipment data and reported totals |

Other existing limits include CMOS 70h/71h, external FPU port F0h, synchronized
RTC/date, serial/mouse/audio backends, incomplete BIOS peripheral and palette
services, non-4400h IOCTL operations and incomplete DOS metadata semantics.
PIC 21h and PIT models remain real stateful implementations, covered by their
existing tests. No success stub was added for unimplemented hardware.

## GUI, filesystem and broader system checks

No GUI or native FAT implementation was changed in this session. Existing
frame-synchronization fixes were preserved. Full-suite runs cover framebuffer
modes and restoration, GUI drawing/palette/input, install/boot paths, filesystem
read/write/metadata/directory growth, networking, drivers and cluster execution.
Additional GUI runs repeat the paint swatches and interaction checks; test
assertions were not relaxed. PEDIT's saved bytes and EXEMENU's marker provide
independent mtools checks of actual FAT writes.

Previously separate seek, BIOS timer, keyboard-queue and VGA-wrap tests are now
in the default gate. A test fixture's environment-message address was corrected
during development and then checked in the host and guest. Explicitly invalid
opcode cases replaced tests of instructions that became supported; rejection
coverage was retained rather than removed.

## Verification record

Final verification and catalogue totals follow. Intermediate suites whose source changed during execution
are checkpoints only, even if all their stages passed.

- Final `make -j4` and `make test -j4`: **PASS**, exit status **0**. Boot and guest execution pass within the full suite.
- Verified kernel SHA-256: `80758868c2fc3ad2f5ff04eba3e1fb03bc36b08e040e81cd24a9c012b9e3328e`.
- [Build transcript](2026-10-28-bugfix-build.txt), [full final suite](2026-10-28-bugfix-full-test.txt), [frozen baseline suite](2026-10-28-bugfix-baseline-test.txt).
- The development fast-console harness initially failed because it rejected `CPU_IRET`; the corrected test passes in the final suite. Intermediate failures and missing-asset launch attempts are retained, not counted as successful executions.
- [Application results and complete raw logs](2026-10-28-bugfix-results.json) retain per-run kernel/input hashes. CPUID, SPACE and STATUS checkpoints precede the final INT29/MZ changes; they are identified by their own hashes.
- [All-200 catalogue review](2026-10-28-bugfix-catalogue.json): exactly **200 unique IDs**, **22 fresh entries**, **178 with historical evidence**. Reviewing a summary is not executing an application.
- Final catalogue: **83 Working, 27 Partial, 12 Broken, 63 Unsupported, 15 Untested** (110 Working or Partial). Baseline: 80/25/14/66/15. HEAD retains Working; its complete transcript was already correct.
- [Session source manifest](2026-10-28-bugfix-source-manifest.json) compares against the frozen incoming tree, preserving attribution of pre-existing work.

Visual evidence: [PEDIT reopened file](2026-10-28-bugfix-pedit-reopened.png), [EXEMENU return/exit](2026-10-28-bugfix-exemenu-esc.png), [ShuffleV move](2026-10-28-bugfix-shufflev-left.png), [ZDir screen](2026-10-28-bugfix-zdir-start.png).

Additional GUI runs preserve both passes and failures. [Repeat 1](2026-10-28-bugfix-gui-1.txt) passes on the final kernel. [Repeat 2](2026-10-28-bugfix-gui-2.txt) reaches paint but cannot create a screenshot; [repeat 3](2026-10-28-bugfix-gui-3.txt) encounters the same capture failure at desktop startup. Host inspection shows `/tmp` at 100% (7.7 GiB used, only 768 KiB available), while the workspace disk has 248 GiB free. Tests retain temporary screenshot directories. No screenshots or user files were deleted. An unchanged test rerun uses `TMPDIR` on the workspace disk.

The first attempted standalone invocation used a nonexistent kernel filename and was rerun with the correct path; that was a launch error, not a guest failure. Final documentation checks pass: the generated overview matches the database, all current evidence links exist, `git diff --check` is clean, and the concluding build reports everything up to date.

The [workspace-disk repeat](2026-10-28-bugfix-gui-4.txt) **passes every check**, including paint and return to the text console (exit 0), without changing source or assertions. Thus two standalone GUI runs and the full-suite GUI gate pass on the final kernel; the two resource-limited capture failures remain recorded. The host `/tmp` remains full and should not be used for another large screenshot run until space is made available.

## Changed files and reproducibility

Production edits are limited to `userland/dosrun/cpu.c`, `cpu.h`, `dosapi.c`,
`dosapi.h`, `drives.c`, `drives.h`, `loader.c` and `main.c`. `Makefile` adds focused
gates; existing EXEC/operand-size/guest fixtures are extended. Thirteen focused
test files were introduced during this audit: shift, double-shift, flags32, bit,
imul32, divide, CPU audit, handles, time, environment, BIOS memory, fast console,
and initial memory. The pre-existing
untracked AAM, INTO, operand-size and GUI frame tests are retained, not attributed
to this session. The compatibility retest tool now retains full diagnostic logs
and uses a plain filename for PEDIT's document argument.

Documentation updates include the DOS runtime contract, test README, release
notes, architecture boundaries, database, per-application details, generated
overview, this report and the all-200 catalogue triage/evidence exports. The
session diff manifest distinguishes these changes from incoming uncommitted work.
All raw before/after tests, traces and disposable artifacts remain in the sibling
`bugfix-20261026` directory; no third-party executable is copied into Git.
