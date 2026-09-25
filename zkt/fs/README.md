# zkt/fs/

The virtual filesystem: a vnode-style abstraction (operations table per
open file/inode — `read`, `write`, `open`, `close`, `readdir`,
`lookup`, …), deliberately modeled on the well-documented 4.4BSD
VFS/vnode design (original ManiOS code informed by a public design — see
the BSD-inspired vs. BSD-derived distinction in the founding proposal).
Concrete filesystem implementations live here too once chosen.

See [`docs/FOUNDING-PROPOSAL.md` §2.10](../../docs/FOUNDING-PROPOSAL.md#210-vfs)
and [§3.2](../../docs/FOUNDING-PROPOSAL.md#32-two-different-things-bsd-inspired-vs-bsd-derived).
Targeted at milestone M7.
