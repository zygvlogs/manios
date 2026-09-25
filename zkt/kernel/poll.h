/* Waiting for any of several files to become readable (SYS_POLL). */
#ifndef ZKT_KERNEL_POLL_H
#define ZKT_KERNEL_POLL_H

#include <stdint.h>

/* Anything a poller might wait for -- data in a pipe, a key, a mouse
 * move -- calls this after the change. Wakes every poller to look
 * again: simple, and cheap with few processes. Safe from IRQ handlers. */
void poll_notify(void);

struct process;
/* SYS_POLL for process p: fills each entry's ready flag; returns how
 * many are ready (0 on timeout), or a negated error. */
long poll_files(struct process *p, void *ufds, uint32_t count, int32_t timeout_ms);

#endif
