#include "kerrno.h"

const char *kstrerror(int err)
{
	switch (err < 0 ? -err : err) {
	case 0:            return "success";
	case EPERM:        return "operation not permitted";
	case ENOENT:       return "no such file or directory";
	case EIO:          return "I/O error";
	case ENXIO:        return "beyond end of device";
	case EBADF:        return "bad file handle";
	case ENOMEM:       return "out of memory";
	case EACCES:       return "permission denied";
	case EBUSY:        return "busy";
	case EEXIST:       return "already exists";
	case ENODEV:       return "operation not supported by device";
	case ENOTDIR:      return "not a directory";
	case EISDIR:       return "is a directory";
	case EINVAL:       return "invalid argument";
	case EROFS:        return "read-only file system";
	case ERANGE:       return "result too large";
	case ENAMETOOLONG: return "name too long";
	case E2BIG:        return "argument list too long";
	case ENOEXEC:      return "not an executable";
	case ECHILD:       return "no such child process";
	case EFAULT:       return "bad address";
	case EMFILE:       return "too many open files";
	case ENOSYS:       return "no such system call";
	case EPIPE:        return "broken pipe";
	case EPROTO:       return "protocol error";
	case EADDRINUSE:   return "address in use";
	case ENETUNREACH:  return "network unreachable";
	case ETIMEDOUT:    return "timed out";
	case EHOSTUNREACH: return "host unreachable";
	default:           return "unknown error";
	}
}
