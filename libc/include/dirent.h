/* Reading directories, as POSIX has it, over ManiOS's directory reads
 * (whole struct zkt_dirent records). */
#ifndef MANIOS_DIRENT_H
#define MANIOS_DIRENT_H

#include <sys/types.h>
#include <zkt_abi.h>

#define MAXNAMLEN ZKT_NAME_MAX

struct dirent {
	ino_t d_fileno;
	off_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	uint8_t d_namlen;
	char d_name[MAXNAMLEN + 1];
};
#define d_ino d_fileno

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_REG     8

typedef struct manios_dir DIR;

DIR *opendir(const char *path);
DIR *fdopendir(int fd);
struct dirent *readdir(DIR *dir);
void rewinddir(DIR *dir);
int closedir(DIR *dir);
int dirfd(DIR *dir);

#endif
