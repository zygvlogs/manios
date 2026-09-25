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
	case EBUSY:        return "busy";
	case EEXIST:       return "already exists";
	case ENODEV:       return "operation not supported by device";
	case ENOTDIR:      return "not a directory";
	case EISDIR:       return "is a directory";
	case EINVAL:       return "invalid argument";
	case EROFS:        return "read-only file system";
	case ENAMETOOLONG: return "name too long";
	default:           return "unknown error";
	}
}
