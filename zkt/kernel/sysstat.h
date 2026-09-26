#ifndef ZKT_KERNEL_SYSSTAT_H
#define ZKT_KERNEL_SYSSTAT_H

/* Registers two text devices about this machine, for programs such as
 * fetch, top and ManiDE's status bar. Times are milliseconds and sizes
 * KiB, in 32 bits (times wrap after 49 days: take differences).
 *
 * "sysstat": one "name value" per line --
 *     version 0.18.0          ManiOS's version
 *     uptime MS               since boot
 *     idle MS                 of that, time the CPU had nothing to run
 *     memory TOTAL FREE       KiB of RAM, and how much of it is free
 *     processes N
 *     cpu DESCRIPTION         (cpuinfo.h)
 * The CPU is busy (idle2 - idle1) / (uptime2 - uptime1) of the time
 * between two reads, taken away from one.
 *
 * "ps": one line per process, oldest first --
 *     PID PPID STATE CPU_MS MEM_KIB NAME
 * STATE is "running", "ready", "sleeping", "blocked", "new" or
 * "exited" (not yet collected by its parent). CPU time is sampled: each
 * timer tick (TIMER_HZ a second) goes to whoever it interrupted.
 *
 * Each is made afresh when read from offset 0; reads at later offsets
 * continue that snapshot. */
void sysstat_register(void);

#endif
