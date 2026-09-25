# zkt/ipc/

Inter-process communication. First primitive: synchronous message-passing
ports (send/receive/reply, bounded message size, kernel-mediated).
Designed and implemented before it has external (userspace) consumers,
so its shape isn't retrofitted around one caller.

See [`docs/FOUNDING-PROPOSAL.md` §2.8](../../docs/FOUNDING-PROPOSAL.md#28-ipc)
and [ADR-0002](../../docs/adr/0002-kernel-architecture-style.md).
