#include "process.h"
#include <stdbool.h>
#include "context.h"
#include "cpu.h"
#include "elf.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "memlayout.h"
#include "pmm.h"
#include "sched.h"
#include "vmm.h"
#include "zkt_abi.h"

#define EXEC_SIZE_MAX (1024 * 1024)

struct process {
	uint32_t pid;
	char name[32];
	struct address_space *as;
	struct file *fds[PROC_FD_MAX];
	struct process *parent; /* NULL: a kernel thread waits for it */
	bool orphaned;          /* parent exited first: nobody will wait */
	bool exited;
	bool waited_on;
	int status;
	struct waitq exited_wq;
	uintptr_t entry;
	uintptr_t user_sp;
	struct process *next;
};

static struct process *processes;
static uint32_t next_pid = 1;

struct process *process_current(void)
{
	return thread_process();
}

uint32_t process_pid(const struct process *p)
{
	return p->pid;
}

struct file *process_fd(struct process *p, int fd)
{
	return (fd >= 0 && fd < PROC_FD_MAX) ? p->fds[fd] : 0;
}

int process_fd_install(struct process *p, struct file *f)
{
	for (int fd = 0; fd < PROC_FD_MAX; fd++) {
		if (!p->fds[fd]) {
			p->fds[fd] = f;
			return fd;
		}
	}
	return -EMFILE;
}

int process_fd_close(struct process *p, int fd)
{
	struct file *f = process_fd(p, fd);
	if (!f) {
		return -EBADF;
	}
	p->fds[fd] = 0;
	vfs_close(f);
	return 0;
}

static void unlink_process(struct process *p)
{
	struct process **link = &processes;
	while (*link != p) {
		link = &(*link)->next;
	}
	*link = p->next;
}

static int read_executable(const char *path, uint8_t **image, size_t *size)
{
	struct file *f;
	int rc = vfs_open(path, OREAD, &f);
	if (rc) {
		return rc;
	}
	uint32_t len = vfs_size(f);
	enum vnode_type type = vfs_type(f);
	if (type != VNODE_FILE || len == 0 || len > EXEC_SIZE_MAX) {
		vfs_close(f);
		return type == VNODE_DIR ? -EISDIR : -ENOEXEC;
	}
	uint8_t *buf = kmalloc(len);
	if (!buf) {
		vfs_close(f);
		return -ENOMEM;
	}
	uint32_t got = 0;
	while (got < len) {
		long n = vfs_read(f, buf + got, len - got);
		if (n <= 0) {
			break;
		}
		got += (uint32_t)n;
	}
	vfs_close(f);
	if (got != len) {
		kfree(buf);
		return -EIO;
	}
	*image = buf;
	*size = len;
	return 0;
}

/* Maps the stack in the active address space and lays out the entry
 * state (zkt_abi.h): argc, then argv, pointing at copied strings. */
static int setup_stack(int argc, char *const argv[], uintptr_t *sp_out)
{
	uintptr_t bottom = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
	for (uintptr_t page = bottom; page < USER_STACK_TOP; page += PAGE_SIZE) {
		uintptr_t frame = pmm_alloc_frame();
		if (!frame || vmm_map_page(page, frame, VMM_WRITABLE | VMM_USER) != 0) {
			if (frame) {
				pmm_free_frame(frame);
			}
			return -ENOMEM;
		}
		memset((void *)page, 0, PAGE_SIZE);
	}

	uintptr_t sp = USER_STACK_TOP;
	uintptr_t user_argv[SPAWN_ARGS_MAX + 1];
	for (int i = argc - 1; i >= 0; i--) {
		size_t len = strlen(argv[i]) + 1;
		sp -= len;
		memcpy((void *)sp, argv[i], len);
		user_argv[i] = sp;
	}
	user_argv[argc] = 0;
	sp &= ~(uintptr_t)3;
	sp -= (argc + 1) * sizeof(uintptr_t);
	memcpy((void *)sp, user_argv, (argc + 1) * sizeof(uintptr_t));
	uintptr_t argv_ptr = sp;
	sp -= sizeof(uintptr_t);
	*(uintptr_t *)sp = argv_ptr;
	sp -= sizeof(uintptr_t);
	*(uintptr_t *)sp = (uintptr_t)argc;
	*sp_out = sp;
	return 0;
}

static void user_thread_start(void *arg)
{
	struct process *p = arg;
	arch_enter_user(p->entry, p->user_sp);
}

static const char *basename(const char *path)
{
	const char *base = path;
	for (const char *c = path; *c; c++) {
		if (*c == '/' && c[1]) {
			base = c + 1;
		}
	}
	return base;
}

int process_spawn(const char *path, int argc, char *const argv[], struct process *parent)
{
	if (argc < 1 || argc > SPAWN_ARGS_MAX) {
		return -E2BIG;
	}
	uint8_t *image;
	size_t size;
	int rc = read_executable(path, &image, &size);
	if (rc) {
		return rc;
	}

	struct process *p = kmalloc(sizeof(*p));
	if (!p) {
		kfree(image);
		return -ENOMEM;
	}
	memset(p, 0, sizeof(*p));
	p->exited_wq = (struct waitq)WAITQ_INIT;
	p->parent = parent;
	strlcpy(p->name, basename(path), sizeof(p->name));
	p->as = vmm_as_create();
	if (!p->as) {
		kfree(image);
		kfree(p);
		return -ENOMEM;
	}

	/* Fill the new address space from inside it. The spawning thread
	 * stays there across preemption, so the loader always writes into
	 * the child, never into whichever space was active before. */
	struct address_space *own = thread_address_space();
	thread_set_address_space(p->as);
	rc = elf_load(image, size, &p->entry);
	if (rc == 0) {
		rc = setup_stack(argc, argv, &p->user_sp);
	}
	if (rc) {
		vmm_as_clear_user();
	}
	thread_set_address_space(own);
	kfree(image);
	if (rc) {
		vmm_as_destroy(p->as);
		kfree(p);
		return rc;
	}

	if (parent) {
		for (int fd = 0; fd < 3; fd++) {
			if (parent->fds[fd]) {
				p->fds[fd] = vfs_dup(parent->fds[fd]);
			}
		}
	} else if (vfs_open("/dev/cons", ORDWR, &p->fds[0]) == 0) {
		p->fds[1] = vfs_dup(p->fds[0]);
		p->fds[2] = vfs_dup(p->fds[0]);
	}

	uint32_t flags = cpu_irq_save();
	p->pid = next_pid++;
	p->next = processes;
	processes = p;
	cpu_irq_restore(flags);

	if (!thread_create_in(p->name, user_thread_start, p, p->as, p)) {
		flags = cpu_irq_save();
		unlink_process(p);
		cpu_irq_restore(flags);
		for (int fd = 0; fd < PROC_FD_MAX; fd++) {
			if (p->fds[fd]) {
				vfs_close(p->fds[fd]);
			}
		}
		thread_set_address_space(p->as);
		vmm_as_clear_user();
		thread_set_address_space(own);
		vmm_as_destroy(p->as);
		kfree(p);
		return -ENOMEM;
	}
	return (int)p->pid;
}

int process_wait(struct process *parent, int pid, int *status)
{
	uint32_t flags = cpu_irq_save();
	struct process *p = processes;
	while (p && ((int)p->pid != pid || p->parent != parent || p->orphaned || p->waited_on)) {
		p = p->next;
	}
	if (!p) {
		cpu_irq_restore(flags);
		return -ECHILD;
	}
	p->waited_on = true;
	while (!p->exited) {
		waitq_sleep(&p->exited_wq);
	}
	unlink_process(p);
	cpu_irq_restore(flags);

	*status = p->status;
	kfree(p);
	return pid;
}

__attribute__((noreturn)) void process_exit(int status)
{
	struct process *p = process_current();

	for (int fd = 0; fd < PROC_FD_MAX; fd++) {
		if (p->fds[fd]) {
			vfs_close(p->fds[fd]);
			p->fds[fd] = 0;
		}
	}
	vmm_as_clear_user();
	thread_set_address_space(0);
	vmm_as_destroy(p->as);
	p->as = 0;

	uint32_t flags = cpu_irq_save();
	/* Children lose their waiter; those already finished go now. */
	for (struct process **link = &processes; *link;) {
		struct process *c = *link;
		if (c->parent == p) {
			c->parent = 0;
			c->orphaned = true;
			if (c->exited) {
				*link = c->next;
				kfree(c);
				continue;
			}
		}
		link = &c->next;
	}
	p->status = status;
	p->exited = true;
	if (p->orphaned) {
		unlink_process(p);
		kfree(p); /* nothing reads it after this: the thread only exits */
	} else {
		waitq_wake_all(&p->exited_wq);
	}
	cpu_irq_restore(flags);
	thread_exit();
}

__attribute__((noreturn)) void process_kill_current(uint32_t vector, const char *what,
                                                    uint32_t fault_addr)
{
	struct process *p = process_current();
	cpu_enable_interrupts(); /* from here it is ordinary kernel work */
	if (vector == 14) {
		kprintf("%s[%lu]: killed: %s at 0x%08lx\n", p->name, p->pid, what, fault_addr);
	} else {
		kprintf("%s[%lu]: killed: %s\n", p->name, p->pid, what);
	}
	process_exit(ZKT_WAIT_KILLED | (int)vector);
}
