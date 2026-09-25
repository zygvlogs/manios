/* User processes: an address space, open files, and one thread (which
 * carries the namespace, ADR-0003). Design notes:
 * docs/milestones/M8-userspace.md. */
#ifndef ZKT_KERNEL_PROCESS_H
#define ZKT_KERNEL_PROCESS_H

#include <stdint.h>
#include "vfs.h"
#include "zkt_abi.h"

#define PROC_FD_MAX ZKT_FD_MAX
#define SPAWN_ARGS_MAX ZKT_ARGS_MAX
#define SPAWN_ARG_BYTES ZKT_ARG_BYTES

struct process;

/* Starts the ELF executable at `path` with argv[0..argc). The child
 * shares the caller's namespace, as in Plan 9, and inherits the calling
 * process's descriptors 0-2; when the caller is a kernel thread, 0-2
 * are /dev/cons. The caller -- a process, or the kernel when `parent`
 * is NULL -- must process_wait() for it. Returns the pid, or a negated
 * error. */
int process_spawn(const char *path, int argc, char *const argv[], struct process *parent);

/* Waits for child `pid` of `parent` to exit; stores its wait status
 * (zkt_abi.h). Returns pid, or -ECHILD. */
int process_wait(struct process *parent, int pid, int *status);

/* Ends the calling process with `status`: closes its files, frees its
 * address space, wakes its waiter. */
__attribute__((noreturn)) void process_exit(int status);

/* For a CPU exception in user mode: reports it and ends the process
 * with ZKT_WAIT_KILLED | vector. */
__attribute__((noreturn)) void process_kill_current(uint32_t vector, const char *what,
                                                    uint32_t fault_addr);

struct process *process_current(void);
uint32_t process_pid(const struct process *p);

/* The descriptor table: fd_install returns the new fd or -EMFILE. */
struct file *process_fd(struct process *p, int fd);
int process_fd_install(struct process *p, struct file *f);
int process_fd_close(struct process *p, int fd);

#endif
