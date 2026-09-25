# zkt/drivers/

The driver framework (a small vtable-style interface per device class —
block, character, network, input, display) and in-tree drivers built
against it. Drivers run in-kernel in the first milestones but are
written to the framework interface from the start so individual drivers
can move to userspace servers later without a rewrite.

First drivers (M1/M5): serial (COM1), VGA text console, PS/2 keyboard,
PIT timer, ATA/IDE PIO block driver.

See [`docs/FOUNDING-PROPOSAL.md` §2.7](../../docs/FOUNDING-PROPOSAL.md#27-driver-framework).
