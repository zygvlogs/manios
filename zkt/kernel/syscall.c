#include "syscall.h"
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "namespace.h"
#include "pipe.h"
#include "poll.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "usercopy.h"
#include "vfs.h"
#include "vmm.h"
#include "zkt_abi.h"
#include "zrp.h"

#define IO_CHUNK 512
#define IO_BIG 16384 /* ZRP_CHANNEL_MSIZE: a big write to a user file server is one message */

#define MESSAGE_MAX (256 * 1024)

/* A pipe moves whole messages: one bounce buffer, one transfer. */
static long read_message(struct file *f, uint8_t *ubuf, uint32_t len)
{
	if (!vmm_user_range_ok((uintptr_t)ubuf, len, true)) {
		return -EFAULT;
	}
	size_t n = len < MESSAGE_MAX ? len : MESSAGE_MAX;
	uint8_t *buf = kmalloc(n ? n : 1);
	if (!buf) {
		return -ENOMEM;
	}
	long got = vfs_read(f, buf, n);
	if (got > 0 && copy_to_user(ubuf, buf, (size_t)got)) {
		got = -EFAULT;
	}
	kfree(buf);
	return got;
}

static long write_message(struct file *f, const uint8_t *ubuf, uint32_t len)
{
	if (len > MESSAGE_MAX) {
		return -EINVAL;
	}
	uint8_t *buf = kmalloc(len ? len : 1);
	if (!buf) {
		return -ENOMEM;
	}
	long rc = copy_from_user(buf, ubuf, len) ? -EFAULT : vfs_write(f, buf, len);
	kfree(buf);
	return rc;
}

/* The kernel's copy of a transfer's bytes: the caller's IO_CHUNK stack
 * buffer for a small one, a heap buffer of IO_BIG for a bigger one (if
 * there is memory), so a big transfer to a file server -- a window's
 * image -- goes as a few big messages rather than many small ones.
 * Returns the buffer's size; the caller frees *buf unless it is small. */
static uint32_t io_buffer(uint32_t len, uint8_t *small, uint8_t **buf)
{
	if (len > IO_CHUNK) {
		*buf = kmalloc(IO_BIG);
		if (*buf) {
			return IO_BIG;
		}
	}
	*buf = small;
	return IO_CHUNK;
}

static long sys_read(struct process *p, int fd, uint8_t *ubuf, uint32_t len)
{
	struct file *f = process_fd(p, fd);
	if (!f) {
		return -EBADF;
	}
	if (vfs_type(f) == VNODE_PIPE) {
		return read_message(f, ubuf, len);
	}
	if (!len) {
		return 0;
	}

	if (vfs_type(f) == VNODE_DIR) {
		uint32_t done = 0;
		while (len - done >= sizeof(struct zkt_dirent)) {
			struct dirent d;
			int rc = vfs_readdir(f, &d);
			if (rc <= 0) {
				return done ? (long)done : rc;
			}
			struct zkt_dirent z;
			memset(&z, 0, sizeof(z));
			strlcpy(z.name, d.name, sizeof(z.name));
			z.type = d.type;
			z.size = d.size;
			if (copy_to_user(ubuf + done, &z, sizeof(z))) {
				return -EFAULT;
			}
			done += sizeof(z);
		}
		return done ? (long)done : -EINVAL; /* buffer smaller than one record */
	}

	/* Validate up front, so a bad buffer can't cost a byte of input. */
	if (!vmm_user_range_ok((uintptr_t)ubuf, len, true)) {
		return -EFAULT;
	}
	uint8_t small[IO_CHUNK], *chunk;
	uint32_t size = io_buffer(len, small, &chunk);
	long rc = 0;
	uint32_t done = 0;
	while (done < len) {
		uint32_t want = len - done < size ? len - done : size;
		long n = vfs_read(f, chunk, want);
		if (n < 0) {
			rc = done ? (long)done : n;
			break;
		}
		if (n == 0) {
			break;
		}
		if (copy_to_user(ubuf + done, chunk, (size_t)n)) {
			rc = -EFAULT;
			break;
		}
		done += (uint32_t)n;
		rc = (long)done;
		/* A device returns what it has (a console: one line's worth);
		 * don't block for more. */
		if ((uint32_t)n < want || vfs_type(f) == VNODE_DEVICE) {
			break;
		}
	}
	if (chunk != small) {
		kfree(chunk);
	}
	return rc;
}

static long sys_write(struct process *p, int fd, const uint8_t *ubuf, uint32_t len)
{
	struct file *f = process_fd(p, fd);
	if (!f) {
		return -EBADF;
	}
	if (vfs_type(f) == VNODE_PIPE) {
		return write_message(f, ubuf, len);
	}
	uint8_t small[IO_CHUNK], *chunk;
	uint32_t size = io_buffer(len, small, &chunk);
	long rc = 0;
	uint32_t done = 0;
	while (done < len) {
		uint32_t n = len - done < size ? len - done : size;
		if (copy_from_user(chunk, ubuf + done, n)) {
			rc = done ? (long)done : -EFAULT;
			break;
		}
		long w = vfs_write(f, chunk, n);
		if (w < 0) {
			rc = done ? (long)done : w;
			break;
		}
		done += (uint32_t)w;
		rc = (long)done;
		if ((uint32_t)w < n) {
			break;
		}
	}
	if (chunk != small) {
		kfree(chunk);
	}
	return rc;
}

_Static_assert(VFS_PATH_MAX == ZKT_PATH_MAX, "the ABI's path limit is the VFS's");

/* Copies a path from the process and makes it absolute: a relative one
 * is appended to the current directory (the VFS cleans the result). */
static long copy_path(struct process *p, char *dst, const char *upath)
{
	char rel[VFS_PATH_MAX + 1];
	long n = copy_string_from_user(rel, upath, sizeof(rel));
	if (n < 0) {
		return n;
	}
	if (rel[0] == '/') {
		memcpy(dst, rel, (size_t)n + 1);
		return 0;
	}
	int len = ksnprintf(dst, VFS_PATH_MAX + 1, "%s/%s", process_cwd(p), rel);
	return len > VFS_PATH_MAX ? -ENAMETOOLONG : 0;
}

static long sys_open(struct process *p, const char *upath, int mode)
{
	char path[VFS_PATH_MAX + 1];
	long rc = copy_path(p, path, upath);
	if (rc) {
		return rc;
	}
	struct file *f;
	rc = vfs_open(path, mode, &f);
	if (rc) {
		return rc;
	}
	int fd = process_fd_install(p, f);
	if (fd < 0) {
		vfs_close(f);
	}
	return fd;
}

struct spawn_args {
	char path[VFS_PATH_MAX + 1];
	char *argv[SPAWN_ARGS_MAX + 1];
	char strings[SPAWN_ARG_BYTES];
};

static long sys_spawn(struct process *p, const char *upath, char *const *uargv)
{
	struct spawn_args *a = kmalloc(sizeof(*a));
	if (!a) {
		return -ENOMEM;
	}
	long rc = copy_path(p, a->path, upath);
	int argc = 0;
	size_t used = 0;
	while (rc == 0) {
		char *uarg;
		if (copy_from_user(&uarg, uargv + argc, sizeof(uarg))) {
			rc = -EFAULT;
			break;
		}
		if (!uarg) {
			break;
		}
		if (argc == SPAWN_ARGS_MAX || used >= SPAWN_ARG_BYTES) {
			rc = -E2BIG;
			break;
		}
		long n = copy_string_from_user(a->strings + used, uarg, SPAWN_ARG_BYTES - used);
		if (n < 0) {
			rc = n == -ENAMETOOLONG ? -E2BIG : n;
			break;
		}
		a->argv[argc++] = a->strings + used;
		used += (size_t)n + 1;
	}
	if (rc == 0) {
		a->argv[argc] = 0;
		rc = process_spawn(a->path, argc, a->argv, p);
	}
	kfree(a);
	return rc;
}

static long sys_wait(struct process *p, int pid, int *ustatus)
{
	int status;
	int rc = process_wait(p, pid, &status);
	if (rc >= 0 && ustatus && copy_to_user(ustatus, &status, sizeof(status))) {
		return -EFAULT; /* the child is reaped either way */
	}
	return rc;
}

static int bind_flag(int flag, enum bind_flag *out)
{
	switch (flag) {
	case BIND_FLAG_REPLACE: *out = BIND_REPLACE; return 0;
	case BIND_FLAG_BEFORE:  *out = BIND_BEFORE; return 0;
	case BIND_FLAG_AFTER:   *out = BIND_AFTER; return 0;
	default:                return -EINVAL;
	}
}

static long sys_bind(struct process *p, const char *unew, const char *uold, int flag)
{
	char new_path[VFS_PATH_MAX + 1], old_path[VFS_PATH_MAX + 1];
	long rc = copy_path(p, new_path, unew);
	if (!rc) {
		rc = copy_path(p, old_path, uold);
	}
	if (rc) {
		return rc;
	}
	enum bind_flag how;
	rc = bind_flag(flag, &how);
	return rc ? rc : vfs_bind(new_path, old_path, how);
}

static long sys_unbind(struct process *p, const char *uold)
{
	char path[VFS_PATH_MAX + 1];
	long rc = copy_path(p, path, uold);
	return rc ? rc : vfs_unbind(path);
}

static long sys_mount(struct process *p, const char *udial, const char *uold, int flag,
                      const char *uaname)
{
	char dial[64], aname[VFS_NAME_MAX + 1] = "", old_path[VFS_PATH_MAX + 1], label[40];
	enum bind_flag how;
	long rc = copy_string_from_user(dial, udial, sizeof(dial));
	if (rc >= 0) {
		rc = copy_path(p, old_path, uold);
	}
	if (rc >= 0 && uaname) {
		rc = copy_string_from_user(aname, uaname, sizeof(aname));
	}
	if (rc >= 0) {
		rc = bind_flag(flag, &how);
	}
	if (rc < 0) {
		return rc;
	}
	struct vnode *root;
	rc = zrp_mount(dial, aname, &root, label, sizeof(label));
	if (rc) {
		return rc;
	}
	rc = vfs_mount(root, label, old_path, how);
	vnode_unref(root); /* the mount holds its own reference */
	return rc;
}

static long sys_pipe(struct process *p, int *ufds)
{
	struct vnode *ends[2];
	int rc = pipe_create(ends);
	if (rc) {
		return rc;
	}
	/* Each end's reference passes to its file; a file that can't be
	 * made leaves its reference with us to drop. */
	struct file *files[2] = { 0, 0 };
	for (int i = 0; i < 2; i++) {
		if (vfs_open_vnode(ends[i], ORDWR, "pipe", &files[i]) != 0) {
			files[i] = 0;
			vnode_unref(ends[i]);
			rc = -ENOMEM;
		}
	}
	int fds[2] = { -1, -1 };
	if (rc == 0) {
		fds[0] = process_fd_install(p, files[0]);
		fds[1] = fds[0] < 0 ? -1 : process_fd_install(p, files[1]);
		rc = fds[0] < 0 ? fds[0] : fds[1] < 0 ? fds[1] : 0;
	}
	if (rc == 0 && copy_to_user(ufds, fds, sizeof(fds))) {
		rc = -EFAULT;
	}
	if (rc) {
		for (int i = 0; i < 2; i++) {
			if (fds[i] >= 0) {
				process_fd_close(p, fds[i]);
			} else if (files[i]) {
				vfs_close(files[i]);
			}
		}
	}
	return rc;
}

static long sys_dup2(struct process *p, int fd, int newfd)
{
	struct file *f = process_fd(p, fd);
	if (!f || newfd < 0 || newfd >= PROC_FD_MAX) {
		return -EBADF;
	}
	if (fd == newfd) {
		return newfd;
	}
	return process_fd_install_at(p, newfd, vfs_dup(f));
}

static long sys_reap(struct process *p, int *ustatus)
{
	int status;
	int pid = process_reap(p, &status);
	if (pid > 0 && ustatus && copy_to_user(ustatus, &status, sizeof(status))) {
		return -EFAULT;
	}
	return pid;
}

static long sys_mountfd(struct process *p, int fd, const char *uold, int flag, const char *uaname)
{
	char aname[VFS_NAME_MAX + 1] = "", old_path[VFS_PATH_MAX + 1], label[40];
	enum bind_flag how;
	struct file *f = process_fd(p, fd);
	if (!f) {
		return -EBADF;
	}
	if (vfs_type(f) != VNODE_PIPE) {
		return -EINVAL;
	}
	long rc = copy_path(p, old_path, uold);
	if (rc >= 0 && uaname) {
		rc = copy_string_from_user(aname, uaname, sizeof(aname));
	}
	if (rc >= 0) {
		rc = bind_flag(flag, &how);
	}
	if (rc < 0) {
		return rc;
	}
	struct vnode *root;
	rc = zrp_mount_channel(vfs_vnode(f), aname, &root, label, sizeof(label));
	if (rc) {
		return rc;
	}
	rc = vfs_mount(root, label, old_path, how);
	vnode_unref(root);
	return rc;
}

static long sys_export(struct process *p, const char *upath, const char *uname)
{
	char name[VFS_NAME_MAX + 1], path[VFS_PATH_MAX + 1];
	long rc = copy_string_from_user(name, uname, sizeof(name));
	if (rc >= 0 && upath) {
		rc = copy_path(p, path, upath);
	}
	if (rc < 0) {
		return rc;
	}
	struct zrp_server *srv = zrp_main_server();
	if (!srv) {
		return -ENODEV;
	}
	if (!name[0]) {
		return -EINVAL; /* the default export is the kernel's (export=) */
	}
	return upath ? zrp_export(srv, name, path, p) : zrp_unexport(srv, name, p);
}

static long sys_chdir(struct process *p, const char *upath)
{
	char path[VFS_PATH_MAX + 1];
	long rc = copy_path(p, path, upath);
	return rc ? rc : process_chdir(p, path);
}

static long sys_getcwd(struct process *p, char *ubuf, uint32_t len)
{
	const char *cwd = process_cwd(p);
	size_t n = strlen(cwd);
	if (len < n + 1) {
		return -ERANGE;
	}
	return copy_to_user(ubuf, cwd, n + 1) ? -EFAULT : (long)n;
}

static long sys_nsfork(void)
{
	struct namespace *ns = ns_fork(thread_namespace());
	if (!ns) {
		return -ENOMEM;
	}
	thread_set_namespace(ns);
	ns_unref(ns);
	return 0;
}

static long sys_fstat(struct process *p, int fd, struct zkt_dirent *uout)
{
	struct file *f = process_fd(p, fd);
	if (!f) {
		return -EBADF;
	}
	struct zkt_dirent z;
	memset(&z, 0, sizeof(z));
	strlcpy(z.name, vfs_name(f), sizeof(z.name));
	z.type = vfs_type(f);
	z.size = vfs_size(f);
	return copy_to_user(uout, &z, sizeof(z));
}

long syscall_dispatch(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2,
                      uint32_t a3, uint32_t a4)
{
	(void)a4;
	struct process *p = process_current();
	if (!p) {
		return -ENOSYS;
	}

	switch (num) {
	case SYS_EXIT:   process_exit((int)(a0 & 0xFF));
	case SYS_READ:   return sys_read(p, (int)a0, (uint8_t *)a1, a2);
	case SYS_WRITE:  return sys_write(p, (int)a0, (const uint8_t *)a1, a2);
	case SYS_OPEN:   return sys_open(p, (const char *)a0, (int)a1);
	case SYS_CLOSE:  return process_fd_close(p, (int)a0);
	case SYS_SPAWN:  return sys_spawn(p, (const char *)a0, (char *const *)a1);
	case SYS_WAIT:   return sys_wait(p, (int)a0, (int *)a1);
	case SYS_GETPID: return (long)process_pid(p);
	case SYS_SLEEP:  timer_sleep_ms(a0); return 0;
	case SYS_BIND:   return sys_bind(p, (const char *)a0, (const char *)a1, (int)a2);
	case SYS_UNBIND: return sys_unbind(p, (const char *)a0);
	case SYS_NSFORK: return sys_nsfork();
	case SYS_FSTAT:  return sys_fstat(p, (int)a0, (struct zkt_dirent *)a1);
	case SYS_UPTIME: return (long)((uint32_t)timer_uptime_ms() & 0x7FFFFFFF);
	case SYS_SBRK:   return process_sbrk(p, (int32_t)a0);
	case SYS_CHDIR:  return sys_chdir(p, (const char *)a0);
	case SYS_GETCWD: return sys_getcwd(p, (char *)a0, a1);
	case SYS_POLL:   return poll_files(p, (void *)a0, a1, (int32_t)a2);
	case SYS_PIPE:   return sys_pipe(p, (int *)a0);
	case SYS_DUP: {
		struct file *f = process_fd(p, (int)a0);
		if (!f) {
			return -EBADF;
		}
		int fd = process_fd_install(p, vfs_dup(f));
		if (fd < 0) {
			vfs_close(f);
		}
		return fd;
	}
	case SYS_DUP2:   return sys_dup2(p, (int)a0, (int)a1);
	case SYS_REAP:   return sys_reap(p, (int *)a0);
	case SYS_SEEK: {
		struct file *f = process_fd(p, (int)a0);
		if (!f) {
			return -EBADF;
		}
		/* A pipe is a stream: since ABI version 2 it can't be seeked.
		 * Version-1 programs get the old answer, an offset that pipe
		 * reads ignore. */
		if (vfs_is_pipe(f) && process_abi(p) >= 2) {
			return -ESPIPE;
		}
		return vfs_seek(f, (int32_t)a1, (int)a2);
	}
	case SYS_MOUNTFD: return sys_mountfd(p, (int)a0, (const char *)a1, (int)a2, (const char *)a3);
	case SYS_EXPORT: return sys_export(p, (const char *)a0, (const char *)a1);
	case SYS_MOUNT:  return sys_mount(p, (const char *)a0, (const char *)a1, (int)a2,
	                                  (const char *)a3);
	default:         return -ENOSYS;
	}
}
