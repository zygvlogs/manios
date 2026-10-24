/* Native FAT modification timestamp, without timezone conversion. */
#ifndef MANIOS_SYS_FILETIME_H
#define MANIOS_SYS_FILETIME_H
#include <stdint.h>
/* stamp: date in bits 31..16, time in bits 15..0. set=0 reads;
 * set=1 persists. Unsupported filesystems fail instead of inventing a date. */
int filetime(int fd, uint32_t *stamp, int set);
/* Reads or persists DOS/FAT directory attributes (R/H/S/A bits). */
int fileattr(const char *path, uint8_t *attr, int set);
#endif
