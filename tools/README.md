# tools/

Development tooling that is not part of the shipped OS:

- `toolchain/` — scripts to build the pinned `i686-elf` cross-compiler
  (binutils + GCC, no host libc linkage). See
  [`docs/FOUNDING-PROPOSAL.md` §4.2](../docs/FOUNDING-PROPOSAL.md#42-toolchain).
- `qemu-run.sh` — boots the built kernel in `qemu-system-i386` with a
  serial console attached to the terminal.
- Future: disk/ISO image builder scripts once storage (M6) exists.

Empty until [M1 implementation tasks](../docs/FOUNDING-PROPOSAL.md#10-exact-first-implementation-tasks)
begin.
