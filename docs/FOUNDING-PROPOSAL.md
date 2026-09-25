# ManiOS Founding Proposal

**Status:** D1–D4 accepted (§11, 2026-09-25). **M1 achieved**: ManiOS
boots on i386 and reaches a ZKT kernel console, verified in QEMU
(`make test`) — see §7 and §12. **M2 achieved**: memory management —
see [docs/milestones/M2-memory-management.md](milestones/M2-memory-management.md).
**M3 achieved**: hardware IRQs and the PIT timer — see
[docs/milestones/M3-interrupts-and-timer.md](milestones/M3-interrupts-and-timer.md).

**Scope of this document:** Originally the response to the ManiOS
founding prompt's "First Task" — architecture, strategy, and planning.
Sections 1–11 remain that proposal (now accepted); §12 records M1's
implementation status as it was built.

---

## 0. Elevator pitch

> ManiOS is an independent operating system with **ZKT (ZygKernel
> Technology)** at its core, BSD technology adopted deliberately and
> legally where it earns its place, a Plan 9-inspired namespace and
> resource model that makes it cluster-transparent, and a desktop
> environment that is genuinely ours. It is not a Linux distribution and
> not a BSD or AmigaOS clone. First target: a bootable i386 system
> reaching a ZKT kernel console, built incrementally with a strong
> documentation and licensing discipline from day one.

---

## 1. ManiOS Architecture

### 1.1 Layering

ManiOS is organized as a stack of layers with narrow, explicit interfaces
between them, so that a layer can be replaced or ported without rewriting
its neighbors:

```
 14  ManiOS Desktop Environment   (compositor, shell, apps)
 13  Package / application system (install, sandboxing, updates)
 12  Core userspace               (init, shell, coreutils, libc)
 11  System-call interface        (stable ABI boundary)
 10  Networking stack             (link/IP/TCP/UDP, socket API)
  9  Device drivers                (block, char, net, input, display)
  8  Virtual filesystem (VFS)     (vnode-style abstraction)
  7  IPC                          (ports/messages, later shared memory)
  6  Interrupt & exception handling
  5  Process / thread scheduling
  4  Memory management            (PMM, VMM, kernel heap)
  3  Hardware abstraction (HAL)   (arch/<cpu> boundary)
  2  ZKT kernel core              (init, panic, logging, boot handoff)
  1  Bootloader                   (BIOS/Multiboot -> protected mode -> ZKT)
```

Layers 1–9 constitute **ZKT** proper (see §2). Layers 10–14 are
**userspace-adjacent or fully userspace** once the syscall ABI (layer 11)
exists — networking and drivers straddle the boundary depending on
whether a given driver is judged safe/valuable to run in-kernel vs. as a
userspace server (a decision made per-driver, not globally, in the spirit
of "modular enough that individual subsystems can evolve").

### 1.2 Kernel design philosophy: pragmatic hybrid, not pure microkernel

This is the single biggest architectural fork in the road, so it is
flagged explicitly as **Decision D1** (see ADR-0002, §11).

Recommendation: **a hybrid kernel**, not a strict microkernel and not a
traditional monolithic-everything-in-ring-0 design:

- Performance-critical, latency-sensitive subsystems (scheduler, virtual
  memory, VFS dispatch, core IPC) live in kernel space, because i386-era
  hardware (25–100 MHz-class CPUs at the low end, no modern TLB/cache
  sizes) cannot comfortably absorb microkernel-style message-passing
  overhead for every page fault or read() call the way a modern seL4
  deployment can.
- Drivers are written against a **driver framework with a defined
  interface** (see §2.7) from day one, even while running in-kernel
  initially. This keeps the door open to moving individual drivers to
  userspace servers later (as NetBSD's rump kernels and XNU's IOKit both
  demonstrate is retrofittable when the interface is disciplined from the
  start) without a rewrite.
- IPC (layer 7) exists as a real kernel primitive from the start — not
  bolted on later — because it is the mechanism userspace servers will
  eventually use to talk to the kernel and each other. Early consumers are
  in-kernel-only (e.g., driver framework callbacks); later consumers are
  real userspace processes.
- Security boundaries are enforced by ring 0 / ring 3 separation
  (hardware-provided, i386-native, free) rather than by software
  capability systems in the first milestones. Capability- or
  sandboxing-style boundaries are a post-M8 (userspace) concern.

This gives ManiOS a monolithic-kernel *performance profile* on i386 with a
microkernel-*shaped* internal architecture, so the project is not
permanently boxed into one extreme.

### 1.3 Portability discipline

Every file that touches CPU-specific behavior lives under `zkt/arch/<cpu>/`
and is reached only through a HAL interface (`zkt/include/zkt/hal.h`
concept — headers TBD at implementation time) exposed to
architecture-neutral code. Generic code under `zkt/mm`, `zkt/scheduler`,
`zkt/ipc`, `zkt/fs`, `zkt/kernel` must never contain `#ifdef __i386__` or
inline assembly. This is the one rule most responsible for whether
x86-64 (and eventually other platforms) is a future port or a future
rewrite.

### 1.4 Desktop environment (forward reference)

The ManiOS Desktop Environment (compositor/WM, shell, panel, launcher,
file manager, settings app, notifications, terminal, theme system) is
layer 14 and depends on a working framebuffer driver, userspace, and a
window/application API — i.e., it is a post-M10 concern (see §6). It gets
its own founding-proposal-style design document when the project reaches
that milestone; scaffolding only (`desktop/README.md`) is created now so
the repository shape is visible today.

### 1.5 Cluster / distributed vision (Plan 9-inspired)

ManiOS targets a **Plan 9-style cluster model**: multiple ManiOS nodes
sharing storage, compute, and devices transparently over the network,
with no special-cased "remote" API. See
[ADR-0003](adr/0003-plan9-namespaces-and-resource-protocol.md) for the
full rationale; in outline:

- **ZRP (ZygKernel Resource Protocol)** — ManiOS's own, original,
  9P-inspired transport-agnostic protocol for reaching any resource
  (local driver, local server process, or a resource on a remote ManiOS
  node) through the same walk/open/read/write-style operations. Local
  IPC (§2.8) and the future network stack (§6, M10) become two
  *transports* for the same protocol, which is what makes local and
  remote resources look identical to client code.
- **Per-process namespaces** — each process has its own mount/bind
  table (inherited at fork/spawn, independently mutable afterward), not
  one global filesystem tree; union directories are part of this from
  the start.
- **Cluster roles** — a ManiOS node can act as a file server, a CPU
  server, or a thin terminal that imports both, once userspace (M8) and
  networking (M10) exist (tracked as milestone M13, §6).

This reshapes the target design of VFS (§2.10), IPC (§2.8), and
networking (§6/M10) but does not change the M1–M6 plan (boot, memory,
interrupts, multitasking, drivers, storage) already detailed in this
document. Wire format and namespace-manipulation syscalls are deferred
to when M7/M8/M10 actually begin, per ADR-0003.

---

## 2. ZKT (ZygKernel Technology) Architecture

ZKT is scoped to layers 2–9 above. Initial responsibilities, in
implementation order:

### 2.1 CPU initialization
Real-mode → protected-mode transition (if ManiOS's own bootloader is used)
or Multiboot handoff already in protected mode (if GRUB/Multiboot is used,
see §4). Either way, ZKT's entry point is responsible for: enabling A20,
loading a ZKT-owned GDT (flat 4 GB code/data segments, ring 0 and ring 3
descriptors), and jumping into 32-bit C code.

### 2.2 GDT / IDT and interrupt handling
- GDT: flat segmentation model (paging does the real memory protection
  work; segmentation is kept minimal, matching how every modern-ish x86
  OS uses it). Descriptors: null, kernel code, kernel data, user code,
  user data, and a TSS descriptor for ring 3→0 transitions.
- IDT: 256-entry table. First milestone wires up the 32 CPU exception
  vectors (divide error, page fault, GPF, double fault, etc.) to a common
  handler that dumps registers to the serial console and halts. IRQ
  vectors (remapped PIC ranges 0x20–0x2F) follow once drivers need them
  (PIT timer, keyboard).
- PIC remapping is done immediately (default PIC vectors collide with CPU
  exception vectors 0x08–0x0F, a classic and avoidable i386 bug).

### 2.3 Physical memory management (PMM)
A bitmap allocator over the memory map ZKT receives from the bootloader
(Multiboot memory map, or a hand-rolled BIOS `int 0x15, eax=0xE820` call
if ManiOS's own bootloader is used). One bit per 4 KiB frame. Simple,
correct, and fast enough; a buddy allocator is a later optimization if
fragmentation becomes a real problem, not a day-one requirement.

### 2.4 Virtual memory management (VMM)
Standard i386 two-level paging (page directory → page table, 4 KiB pages).
Kernel is mapped at a fixed higher-half virtual address (e.g.
`0xC0000000`) from the first paging-enabled boot, even though it is not
strictly required for a single-address-space milestone — doing this from
the start avoids a disruptive later migration once user address spaces
exist. PAE / 4 MiB pages are explicitly deferred (not needed for
correctness, adds complexity).

### 2.5 Kernel heap
A simple free-list allocator (`kmalloc`/`kfree`) over a fixed virtual
region backed by the PMM/VMM. No slab allocator in the first milestones —
that is a later optimization once real allocation-pattern data exists.

### 2.6 Scheduler & processes/threads
- M4 milestone target: cooperative round-robin over kernel threads only
  (proves context switching, TSS usage, and the timer IRQ path).
- Post-M4: preemptive round-robin driven by the PIT (or later APIC/HPET on
  newer targets) timer IRQ, then priority levels.
- Process = address space + ≥1 threads. Thread = kernel stack + saved
  register context + scheduling state. This split is chosen from the
  start (rather than a process-only model retrofitted later) since
  process/thread scheduling is explicitly a top-level architecture layer.

### 2.7 Driver framework
A small vtable-style interface per device class (block, character,
network, input, display) that a driver implements and registers with a
device manager at init time. This is intentionally similar in spirit to
BSD's `cdevsw`/`bdevsw` tables — a well-understood, well-documented
pattern worth learning from even though ManiOS's implementation is
original (see §3 on how "BSD-inspired" and "BSD-derived" are kept
distinct).

### 2.8 IPC
Synchronous message-passing "ports" (send/receive/reply, bounded message
size, kernel-mediated) as the first primitive — simplest to reason about
and to secure. Shared-memory regions and async queues are later additions
once real workloads (e.g., a windowing system) demand the throughput.
Per [ADR-0003](adr/0003-plan9-namespaces-and-resource-protocol.md)
(§1.5), this port mechanism is designed to double as a local transport
for **ZRP** later — its message format should not assume it will only
ever carry ad hoc, per-subsystem payloads.

### 2.9 System-call interface
i386 target uses the classic `int 0x80` software-interrupt gate initially
(simplest to implement and debug; `sysenter`/`sysexit` is a
straightforward later optimization on CPUs that support it, and doesn't
change the ABI shape). Syscall numbers and argument conventions are
versioned from the first userspace milestone (M8) so the ABI can evolve
without breaking old binaries silently.

### 2.10 VFS
A vnode-style abstraction (operations table per open file/inode:
`read`, `write`, `open`, `close`, `readdir`, `lookup`, …), deliberately
modeled on the well-documented 4.4BSD VFS/vnode interface design
(concept and terminology are public, decades-old OS design knowledge —
no code is copied; see §3.2 on the distinction). First concrete
filesystem is likely a minimal read-only FAT or a purpose-built simple FS
(`zkfs`?) for the initial disk image — decided at M6/M7, not now.
Above this vnode dispatch layer, VFS is further shaped by
[ADR-0003](adr/0003-plan9-namespaces-and-resource-protocol.md) (§1.5):
per-process mount/bind tables and union directories, rather than one
global mount table, are the target namespace structure once M7 begins.

### 2.11 Security boundaries
Ring 0/ring 3 hardware separation (§1.2) plus, from the first userspace
milestone, per-process address spaces enforced by paging. No further
security model (capabilities, MAC, sandboxing) is designed in this
document — premature at this stage, and explicitly deferred.

### 2.12 Hardware abstraction
Covered in §1.3.

---

## 3. BSD Integration Strategy

### 3.1 Principle
"BSD-derived where appropriate and legally compatible" means: **each
component is evaluated and justified individually.** ManiOS never imports
a BSD operating system wholesale, and never imports Linux (GPL) code at
all, under any circumstance — GPL's copyleft terms are incompatible with
the permissive-licensing posture recommended in §9, and mixing kernel
code licenses is exactly the kind of mess this section exists to prevent.

### 3.2 Two different things: "BSD-inspired" vs. "BSD-derived"
- **BSD-inspired** = ManiOS writes its own original code, informed by a
  public, decades-old design (vnode/VFS shape, BSD sockets API surface,
  `cdevsw`-style driver tables). No copyright obligation attaches to
  learning from a published design; nothing is copied. Most of ZKT (§2)
  falls in this bucket.
- **BSD-derived** = ManiOS imports actual source (or a close, substantial
  adaptation of actual source) from a BSD project (FreeBSD, NetBSD,
  OpenBSD, DragonFly BSD, or their historical CSRG/4.4BSD-Lite ancestor).
  This carries real license obligations (see 3.3) and is the exception,
  used only where reimplementing from scratch has no engineering value —
  e.g., a well-tested math/string routine, a specific device driver for
  hardware whose datasheet-level details are painstaking to get right, or
  a wire-protocol implementation (TCP state machine) where subtle bugs are
  security bugs.

### 3.3 Process for any BSD-derived import
Every file brought in from a BSD project must go through this checklist
before merge:

1. Identify the **component**, its **origin project + file path +
   revision/commit**, and its **exact license** (BSD-2-Clause,
   BSD-3-Clause, ISC, or occasionally a project-specific variant —
   FreeBSD/NetBSD/OpenBSD files sometimes differ file-by-file).
2. Record what modifications ManiOS makes (ported to ZKT's PMM/VMM
   interfaces, renamed to match ManiOS naming conventions, bugs fixed,
   etc.).
3. Preserve the **original copyright header and license text verbatim**
   at the top of the file — never trim, paraphrase, or relocate it out of
   the file.
4. Add a ManiOS attribution note directly beneath the original header
   (component, ManiOS-side modification date, nothing that could be read
   as claiming original authorship of the unmodified parts).
5. Add an entry to `third_party/THIRD_PARTY_NOTICES.md` (ledger; created
   now, empty, in this proposal — see §5) with the same information, so
   the aggregate legal picture is visible without reading every file.
6. Get the import reviewed specifically for step 1–5 compliance before
   merge — this is a standing review gate, not a one-time task.

### 3.4 What "legally compatible" rules out
- No GPL/LGPL/AGPL source, ever, in `zkt/`, `libc/`, `boot/`, or any
  kernel-adjacent component. (A future userspace *application* shipped
  as an optional package is a separate, later conversation — irrelevant
  to the OS core and out of scope for this proposal.)
- No code copied from Linux under any license header, since the founding
  prompt is explicit that ManiOS must not depend on or derive from the
  Linux kernel, and ambiguity here creates exactly the kind of
  provenance mess §3.3 exists to prevent.
- Proprietary/closed-source reference code (leaked or otherwise) is
  never used as a source to copy from, even "just to look at the
  algorithm" — taint risk is not worth it.

---

## 4. i386 Boot Strategy

### 4.1 Decision D2 (see ADR-0001, §11): start on Multiboot, add a native
ManiOS bootloader later

Two real paths exist:

**Option A — Write ManiOS's own bootloader (stage1 MBR + stage2) from
day one.** Full control and "it's ours" from the very first byte, but a
real-mode BIOS bootloader (disk I/O via `int 0x13`, A20 gate handling,
GDT setup, protected-mode transition) is a notoriously fiddly, slow-to-
debug piece of code, and getting it wrong burns milestone-one time on
problems that have nothing to do with ZKT itself.

**Option B (recommended) — Target the Multiboot 1 specification** (what
GRUB Legacy/GRUB2 and `qemu-system-i386 -kernel` both understand
natively) for the kernel binary in the first milestones. ZKT ships a
Multiboot header; QEMU or GRUB does real-mode setup, disk loading, and
the jump to 32-bit protected mode; ZKT's entry point starts already in
protected mode with a memory map handed to it in a register. This is the
same pragmatic choice used by nearly every hobby/independent kernel
project (and by real ones bootstrapping quickly) to reach a working
kernel console in days instead of weeks.

This does **not** compromise "our own bootloader" as a long-term goal:
layer 1 (§1.1) is explicitly designed as a replaceable component. A
native ZKT bootloader becomes its own milestone once the kernel above it
is stable enough that bootloader bugs aren't hiding behind kernel bugs
(and vice versa) — building both at once is the "architectural decisions
that make debugging unnecessarily hard" pattern the founding prompt
warns against implicitly.

### 4.2 Toolchain
- A dedicated **`i686-elf` cross-compiler** (binutils + GCC built with
  `--target=i686-elf`, no host libc), per the well-established OSDev
  cross-compiler practice — this is what prevents the kernel binary from
  accidentally depending on the build host's libc/ABI. Build scripts live
  under `tools/`. Despite the `i686` in its name, this compiler is only
  a freestanding x86 target; its *default* code generation is
  `-march=pentiumpro`, so the Makefile pins `-march=i386` (and the
  assembler's `-march=i386`) to keep the kernel runnable on a 386/486.
  This was found at M2, where the default emitted `CMOV`.
- Assembly in GAS (`.S` files, AT&T syntax) to keep a single assembler
  toolchain (no separate NASM dependency) — open to revisiting if NASM's
  Intel syntax proves meaningfully better for readability once real
  assembly stubs exist.
- Kernel linked as a static ELF with a custom linker script
  (`zkt/arch/i386/linker.ld`) placing it at the higher-half address
  chosen in §2.4.

### 4.3 Testing
- **Primary target: QEMU** (`qemu-system-i386`), scriptable, fast,
  serial-console-friendly, and CI-friendly (headless boot + serial log
  capture is enough to assert "reached the ZKT kernel console" in an
  automated smoke test). Tests boot on QEMU's `486` CPU model, its
  oldest, which faults on post-386 instructions such as `CMOV`.
- **Secondary target: real old x86 hardware**, tested manually/
  periodically once QEMU boot is solid — real hardware surfaces BIOS
  quirks and timing bugs QEMU won't, but is not the fast inner-loop
  target.
- **Serial console (COM1) is a first-class debugging surface**, not an
  afterthought — it is often the only way to see kernel output before a
  VGA driver exists, and remains the CI-testable channel afterward.

### 4.4 First-milestone hardware surface
BIOS boot, VGA text-mode framebuffer (`0xB8000`) for the visible console,
PS/2 keyboard (polling first, IRQ-driven once interrupts work), PIT timer
for scheduler ticks, and a minimal ATA/IDE PIO driver for storage —
deliberately the smallest, best-documented, most QEMU-faithful device set
available, per the founding prompt's "do not prematurely optimize for
modern hardware."

---

## 5. Repository Structure

```
manios/
├── README.md                    # project pitch, status, links
├── docs/
│   ├── FOUNDING-PROPOSAL.md     # this document
│   └── adr/                     # Architecture Decision Records
│       ├── 0000-template.md
│       ├── 0001-bootloader-strategy.md
│       └── 0002-kernel-architecture-style.md
├── boot/                        # bootloader (native, post-Multiboot-era)
├── zkt/                         # ZygKernel Technology
│   ├── arch/
│   │   └── i386/                # first target; only place with #ifdef/asm
│   ├── mm/                      # PMM, VMM, kernel heap
│   ├── scheduler/                # processes, threads, run queues
│   ├── ipc/                     # ports/messages
│   ├── drivers/                 # driver framework + in-tree drivers
│   ├── fs/                      # VFS + filesystem implementations
│   └── kernel/                  # init, panic, logging, syscall dispatch
├── libc/                        # ManiOS's own minimal C library
├── userland/                    # init, shell, coreutils (post-M8)
├── desktop/                     # ManiOS Desktop Environment (post-M10)
├── tools/                       # cross-toolchain build scripts, image
│                                 # builder, QEMU run scripts
├── third_party/                 # vendored BSD-derived source + the
│                                 # THIRD_PARTY_NOTICES.md ledger (§3.3)
├── tests/                       # boot smoke tests, unit tests
└── build/                       # build output (git-ignored)
```

Differences from the founding prompt's example, with rationale:
- Added `docs/adr/` for Architecture Decision Records — the concrete
  mechanism for "document every major architectural decision."
- Added `third_party/` as the explicit home for any BSD-derived source
  and its notices ledger, so provenance is structurally separated from
  ManiOS-original code rather than interleaved file-by-file.
- Kept `tools/` (not `scripts/`) matching the founding prompt's naming;
  it holds both the cross-compiler build scripts and image/QEMU tooling
  rather than splitting those into more top-level directories before
  there's a reason to.

This structure is created as directory scaffolding with short
explanatory `README.md` stubs in this same change, so it's reviewable as
a concrete shape rather than only a diagram.

---

## 6. Development Roadmap

| Milestone | Goal | Depends on |
|---|---|---|
| M0 | This proposal + repo scaffolding | — |
| M1 | **Achieved.** Multiboot kernel reaches a ZKT kernel console (serial + VGA text) | §4 toolchain, §2.1–2.2 |
| M2 | **Achieved** ([notes](milestones/M2-memory-management.md)). Physical + virtual memory management, kernel heap | M1 |
| M3 | **Achieved** ([notes](milestones/M3-interrupts-and-timer.md)). Full interrupt/exception handling, PIC remap, PIT timer IRQ | M1 |
| M4 | Cooperative then preemptive multitasking (kernel threads) | M2, M3 |
| M5 | Driver framework + keyboard, VGA, serial, PIT drivers formalized | M3, M4 |
| M6 | Storage: ATA/IDE PIO block driver | M5 |
| M7 | VFS + first filesystem, per-process namespaces & union dirs (ADR-0003) | M6 |
| M8 | Userspace: ELF loader, ring 3 processes, first syscalls, namespace ops (bind/mount) | M4, M7 |
| M9 | libc + syscall ABI stabilization, coreutils, shell | M8 |
| M10 | Networking + ZRP transport over the network (ADR-0003) | M8 |
| M11 | Graphics: linear framebuffer driver, basic 2D primitives | M8 |
| M12 | ManiOS Desktop Environment MVP (compositor, shell, launcher) | M11 |
| M13 | Cluster roles: file server / CPU server / terminal (ADR-0003) | M8, M10 |

Each milestone gets its own short design note under `docs/` when it
starts (not written speculatively now), following the ADR practice
established in this proposal.

---

## 7. First Bootable Prototype Plan (M1, in detail)

Concrete steps for the "ManiOS boots on i386 and reaches a ZKT kernel
console" milestone, once this proposal is approved:

1. `zkt/arch/i386/boot.S` — Multiboot header + `_start` entry stub;
   sets up a kernel stack, pushes the Multiboot info pointer, calls into
   C.
2. `zkt/arch/i386/linker.ld` — higher-half link layout (§2.4).
3. `zkt/arch/i386/gdt.c` — flat GDT (§2.2), loaded before anything else.
4. `zkt/drivers/serial.c` — COM1 driver, polling, 38400 8N1; first thing
   that can prove "we're alive" before VGA exists.
5. `zkt/drivers/vga_text.c` — `0xB8000` text-mode console with scrolling.
6. `zkt/arch/i386/idt.c` + `zkt/arch/i386/isr.S` — IDT with the 32 CPU
   exception vectors wired to a common ring-0 fault handler that logs to
   serial + VGA and halts (`cli; hlt` loop) — this alone catches most
   early bring-up bugs immediately instead of triple-faulting silently.
7. `zkt/arch/i386/pic.c` — 8259 PIC remap to vectors 0x20–0x2F.
8. `zkt/kernel/main.c` — `kernel_main()`: init GDT → IDT → PIC → serial →
   VGA → print the ManiOS/ZKT banner → idle loop (`sti; hlt` forever).
9. `tools/toolchain/` — cross-compiler build script
   (`build-i686-elf-toolchain.sh`) and a `Makefile` wiring the above into
   a bootable ELF.
10. `tools/qemu-run.sh` — boots the kernel in `qemu-system-i386`,
    `-serial stdio` so the console is visible in a terminal.
11. `tests/boot_smoke_test.*` — headless QEMU boot with a timeout,
    asserting the expected banner string appears on the serial port; this
    becomes the first CI gate.
12. `docs/adr/0001-bootloader-strategy.md` and
    `docs/adr/0002-kernel-architecture-style.md` — recorded as **Accepted**
    once approved (currently **Proposed**, see §11).

No task in this list is started until §11's decisions are confirmed.

---

## 8. Technical Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Real/protected-mode & paging bugs are hard to debug (silent triple-faults) | Blocks M1–M2 progress | IDT with logging fault handler from step 6 of §7, before anything else is added; QEMU + GDB stub (`-s -S`) for source-level debugging |
| Toolchain maintenance burden (custom cross-compiler) | Slows every contributor's first day | Scripted, versioned toolchain build (`tools/`), documented pinned GCC/binutils versions |
| BSD-derived license compliance mistakes (dropped notices, accidental GPL contamination via copy-paste from a mixed-license reference) | Legal exposure, forces rewrites | Mandatory §3.3 checklist as a review gate; `THIRD_PARTY_NOTICES.md` kept in sync with every import in the same commit |
| Scope creep toward the desktop environment before the kernel is stable | Never reaches a usable kernel | Roadmap (§6) hard-orders desktop work after M11 (graphics) and M8 (userspace); no desktop code before then |
| Old x86 hardware diversity (BIOS quirks, missing ACPI, unusual chipsets) | Real-hardware milestone slips or silently narrows to "hardware we happen to own" | QEMU stays the inner-loop target; real-hardware testing tracked as a checklist of specific machines, not a vague goal |
| HAL boundary erosion (arch-specific code leaking into generic ZKT code) | Future x86-64 port becomes a rewrite instead of a port | The rule in §1.3 enforced by code review; revisit as a lint/CI check once there's a second arch to test the boundary against |
| Small, mostly-solo-or-small-team bandwidth vs. whole-OS ambition | Burnout, abandoned milestones | Roadmap is intentionally milestone-gated and sequential, not parallel; each milestone ships something demonstrable (§6) |
| Kernel-mode bugs = full-system compromise (no isolation yet) | Data loss during development, unsafe to dogfood early | All storage/filesystem development targets disposable disk images, never a developer's real data, until M7+ is trustworthy |
| Silent behavioral drift when adapting BSD-derived code to ZKT's own PMM/VMM/threading model | Subtle correctness bugs that look like upstream bugs but aren't | §3.3 step 2 (recorded modifications) plus tests written against the adapted version, not assumed-inherited from upstream |

---

## 9. Licensing Considerations

### 9.1 License for ManiOS's own (original) code — Decision D3, flagged for approval
Given the explicit BSD-integration goal (§3) and the explicit rejection
of Linux/GPL dependence, a **permissive license is the only license
family that is philosophically and legally consistent** with the
project's stated goals. Within that family:

| Option | Fit |
|---|---|
| **BSD-2-Clause** (recommended) | Shortest permissive license, maximal compatibility with importing BSD-2/3-Clause and ISC code from FreeBSD/NetBSD/OpenBSD without friction; matches the project's own name/philosophy |
| BSD-3-Clause | Same as above plus a non-endorsement clause; also fine, marginally more legal text |
| MIT | Equivalent permissiveness, different wording; no particular advantage over BSD-2-Clause here given the BSD-integration theme |
| MPL-2.0 | File-level copyleft (protects ManiOS's own modifications from being closed-sourced downstream while staying compatible with permissive and even proprietary combination) — worth considering only if the project wants some copyleft protection; adds complexity the others don't have |

**Recommendation: BSD-2-Clause** for all ManiOS-original code
(`zkt/`, `boot/`, `libc/`, `userland/`, `desktop/`, `tools/`). This is a
decision for the repository owner, not something this proposal commits
to unilaterally — **no `LICENSE` file is added in this change**; it
should be added in the same change that this decision is confirmed.

### 9.2 Third-party (BSD-derived) code
Retains its original upstream license unconditionally (§3.3) regardless
of what ManiOS picks for its own code — BSD-2/3-Clause and ISC are all
compatible with a BSD-2-Clause or MIT project license for ManiOS's own
parts, which is part of why §9.1 recommends one of those.

### 9.3 Explicit exclusions
No GPL/LGPL/AGPL code anywhere in the tree (§3.4). No CC-licensed code
(content licenses, not software licenses — wrong tool even if
permissively-licensed, e.g. CC-BY, and a frequent source of accidental
incompatibility when people reach for "permissive-sounding" licenses
without checking the family).

### 9.4 Naming
"ManiOS" and "ZKT / ZygKernel Technology" are project names used
throughout this and future documents. No trademark filing is proposed
here (out of scope for an engineering proposal) — flagged only so it
isn't forgotten if/when the project seeks any formal registration or
publishes a brand-usage policy for downstream forks.

### 9.5 Notices ledger
`third_party/THIRD_PARTY_NOTICES.md` is created now (empty, with the
required structure) so the very first BSD-derived import has nowhere to
go but into it.

---

## 10. Exact First Implementation Tasks

*(Not started in this change — this is the queued list for the next
session, after §11's decisions are confirmed.)*

1. Add `LICENSE` (BSD-2-Clause or whatever is confirmed in §11/D3).
2. Record ADR-0001 and ADR-0002 as **Accepted**.
3. `tools/toolchain/build-i686-elf-toolchain.sh` — reproducible
   `i686-elf` GCC/binutils cross-compiler build.
4. `zkt/arch/i386/boot.S` — Multiboot header + entry stub.
5. `zkt/arch/i386/linker.ld` — higher-half link script.
6. `zkt/arch/i386/gdt.c` + `gdt_flush.S` — flat GDT.
7. `zkt/drivers/serial.c` — COM1 polling driver.
8. `zkt/drivers/vga_text.c` — `0xB8000` text console with scroll.
9. `zkt/arch/i386/idt.c` + `isr.S` + `pic.c` — IDT, exception handlers,
   PIC remap.
10. `zkt/kernel/main.c` — `kernel_main()` wiring 6–9 together, printing
    the ZKT console banner, idle loop.
11. Root `Makefile` + `tools/qemu-run.sh`.
12. `tests/boot_smoke_test` — automated QEMU headless boot check
    (serial-port banner assertion), wired into CI.

This list **is** the answer to "the exact first implementation tasks" —
it is deliberately not executed in this change.

---

## 11. Open Decisions Requiring Approval

**D1, D2, and D3 were confirmed as recommended on 2026-09-25.** D4 was
added the same day at the repository owner's explicit direction. Status
below reflects that.

- **D1 — Kernel style** (§1.2): hybrid kernel (monolithic-ish
  performance, microkernel-*shaped* internal boundaries), accepted over
  a pure microkernel or a traditional fully-monolithic design.
  `docs/adr/0002-kernel-architecture-style.md` — **Accepted**.
- **D2 — Boot strategy** (§4.1): start on Multiboot (GRUB/QEMU-provided
  protected-mode handoff), defer a native ManiOS bootloader to a later,
  separate milestone. `docs/adr/0001-bootloader-strategy.md` —
  **Accepted**.
- **D3 — Project license** (§9.1): BSD-2-Clause for ManiOS-original
  code. `LICENSE` added in the same change as this update.
- **D4 — Plan 9-inspired cluster model** (§1.5): ZRP (ZygKernel Resource
  Protocol) as a uniform local/remote resource protocol, per-process
  namespaces with union directories, and file-server/CPU-server/terminal
  cluster roles as milestone M13.
  `docs/adr/0003-plan9-namespaces-and-resource-protocol.md` —
  **Accepted** (direction); wire format and syscalls remain open until
  M7/M8/M10.
- Anything else in this document marked "recommended" remains a default
  this proposal is prepared to act on, not a decision already made on
  the repository owner's behalf, and should be raised explicitly if it
  needs to change.

With D1–D4 confirmed, work proceeded to §10's task list, starting with M1
(§7) — see §12 for its outcome.

---

## 12. M1 Status: Achieved

§7's steps and §10's task list are implemented and verified, with one
exception: the smoke test is **not yet wired into CI**. §10 task 12
called for that, and an earlier version of this section wrongly said
everything was done. There is no GitHub Actions workflow yet; the test
runs locally via `make test`. Everything else:

- `zkt/arch/i386/boot.S`, `linker.ld`, `gdt.c`/`gdt_flush.S`,
  `idt.c`/`idt_flush.S`, `zkt/arch/i386/isr.S` (exception stubs) +
  `exception.c` (dispatch to `panic_dump`), `pic.c`,
  `zkt/drivers/serial.c`, `zkt/drivers/vga_text.c`,
  `zkt/kernel/kconsole.c`, `zkt/kernel/panic.c`, `zkt/kernel/main.c`.
- `tools/toolchain/build-i686-elf-toolchain.sh` (binutils 2.42 + GCC
  13.2.0, C-only, `--without-headers`), root `Makefile`,
  `tools/qemu-run.sh`, `tests/boot_smoke_test.sh`.
- `make test` boots the kernel headlessly in QEMU and asserts the
  banner on the serial port — **PASS**.
- The fault path was verified manually (a temporary `int $0x00`
  triggered in `kernel_main`, then reverted before commit): the
  divide-by-zero stub fired, `isr_handler` correctly resolved
  vector 0 to `"Divide-by-zero"`, and `panic_dump` printed a full
  register dump before halting — confirming the IDT, the 32 exception
  stubs, and the panic handler all work end-to-end, not just the
  happy-path boot banner.

One bug was found and fixed during bring-up, worth recording since it's
a generically useful gotcha: **`isr.c` and `isr.S` shared the file stem
`isr`**, so the Makefile's `%.o: %.c` and `%.o: %.S` pattern rules both
resolved to `build/zkt/arch/i386/isr.o`. Only the `.c` rule ever fired
(pattern-rule precedence), so the assembly exception stubs were never
compiled, and the object list still listed the same `.o` twice — which
`ld` rejected as `multiple definition of isr_handler` while also
reporting every `isrN` symbol undefined. Fixed by renaming the C
dispatcher to `exception.c`; no two source files under `zkt/` should
share a stem across `.c`/`.S` going forward.

A second bug: GAS gives a **non-standard section name no flags by
default** on this binutils version (2.42). `.section .multiboot`
without an explicit `"a"` (alloc) flag produced a `.multiboot` section
with no `SHF_ALLOC`, so the linker did not place it in a loadable
segment — it landed at file offset 9216, past the 8 KiB window a
Multiboot loader scans for the header, and QEMU refused to boot the
image (`Error loading uncompressed kernel without PVH ELF Note`). Fixed
by declaring `.section .multiboot, "a"` explicitly in `boot.S`.

M2 (memory management) followed; it has its own milestone note,
[docs/milestones/M2-memory-management.md](milestones/M2-memory-management.md),
per the one-design-note-per-milestone practice in §6.
