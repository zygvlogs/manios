# tools/

Development tooling that is not part of the shipped OS:

- `toolchain/build-i686-elf-toolchain.sh` — builds the pinned `i686-elf`
  cross-compiler (binutils 2.42 + GCC 13.2.0, C only, no host libc) into
  `toolchain/i686-elf/` (git-ignored). Run it with `make toolchain`. See
  [`docs/FOUNDING-PROPOSAL.md` §4.2](../docs/FOUNDING-PROPOSAL.md#42-toolchain).
- `qemu-run.sh` — boots the built kernel in `qemu-system-i386` on a 486
  CPU model, with the serial console attached to the terminal (`make run`).
- Future: disk/ISO image builder scripts once storage (M6) exists.
