/* Memory-mapped files: ManiOS has none. mmap() fails (ENODEV), which
 * programs that map files for speed (grep, cmp) take as a cue to read
 * them instead. */
#ifndef MANIOS_SYS_MMAN_H
#define MANIOS_SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_SHARED  0x0001
#define MAP_PRIVATE 0x0002
#define MAP_FIXED   0x0010
#define MAP_ANON    0x1000
#define MAP_FAILED ((void *)-1)
#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t len);
int madvise(void *addr, size_t len, int advice);

#endif
