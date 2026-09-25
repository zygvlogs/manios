# zkt/arch/

Hardware abstraction layer boundary: one subdirectory per supported CPU
architecture. This is the only place `#ifdef __<arch>__` and inline/
standalone assembly are allowed anywhere in `zkt/` — generic code under
`zkt/mm`, `zkt/scheduler`, `zkt/ipc`, `zkt/fs`, `zkt/kernel` must reach
CPU-specific behavior only through the interface exposed here.

This is the rule future portability (x86-64 and beyond) depends on — see
[`docs/FOUNDING-PROPOSAL.md` §1.3](../../docs/FOUNDING-PROPOSAL.md#13-portability-discipline).

First and currently only target: `i386/`.
