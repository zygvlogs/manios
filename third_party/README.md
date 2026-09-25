# third_party/

Vendored source imported from BSD projects (FreeBSD, NetBSD, OpenBSD,
DragonFly BSD, or 4.4BSD-Lite), used only where reimplementing from
scratch has no engineering value. This directory structurally separates
BSD-derived code from ManiOS-original code (which lives under `zkt/`,
`libc/`, `boot/`, etc., and may be *BSD-inspired* by public design
knowledge without being *BSD-derived* source — see the distinction in
[`docs/FOUNDING-PROPOSAL.md` §3.2](../docs/FOUNDING-PROPOSAL.md#32-two-different-things-bsd-inspired-vs-bsd-derived)).

Every import here must follow the checklist in
[§3.3](../docs/FOUNDING-PROPOSAL.md#33-process-for-any-bsd-derived-import) and
have a corresponding entry in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

No GPL/LGPL/AGPL code, and no Linux-derived code, is ever placed here or
anywhere else in this repository — see
[§3.4](../docs/FOUNDING-PROPOSAL.md#34-what-legally-compatible-rules-out).

Empty; nothing has been imported yet.
