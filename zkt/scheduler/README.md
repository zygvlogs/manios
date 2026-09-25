# zkt/scheduler/

Process and thread scheduling: process = address space + threads,
thread = kernel stack + saved context + scheduling state. Starts
cooperative round-robin over kernel threads (proves context switching),
then becomes preemptive off the PIT timer IRQ, then gains priorities.

See [`docs/FOUNDING-PROPOSAL.md` §2.6](../../docs/FOUNDING-PROPOSAL.md#26-scheduler--processesthreads).
Targeted at milestone M4.
