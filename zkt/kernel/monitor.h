#ifndef ZKT_KERNEL_MONITOR_H
#define ZKT_KERNEL_MONITOR_H

/* The ZKT kernel monitor: a line-oriented command prompt on device
 * "cons", for inspecting the running kernel. It is a kernel debugging
 * console, not the ManiOS shell, which is userspace (M9). Run it as a
 * thread; it never returns. */
void monitor_main(void *unused);

#endif
