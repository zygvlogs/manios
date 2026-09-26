/* File status, as POSIX has it, built from what ManiOS knows about a file
 * (struct zkt_dirent: its type and size). ManiOS has no owners,
 * permissions or times: files read as mode 0644 and directories 0755,
 * owned by 0, with no times; st_ino is made from the file's name and
 * type, the same for stat() and fstat(). ManiOS's own fstat() (<manios.h>) fills a
 * struct zkt_dirent; this one is the POSIX function, under another
 * link name. */
#ifndef MANIOS_SYS_STAT_H
#define MANIOS_SYS_STAT_H

#include <sys/types.h>
#include <time.h>

struct stat {
	dev_t st_dev;
	ino_t st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	off_t st_size;
	struct timespec st_atim, st_mtim, st_ctim;
	blksize_t st_blksize;
	blkcnt_t st_blocks;
};
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define ACCESSPERMS 0777
#define ALLPERMS 07777
#define DEFFILEMODE 0666

int stat(const char *path, struct stat *sb);
int lstat(const char *path, struct stat *sb); /* no links: stat() */
int fstat(int fd, struct stat *sb) __asm__("__posix_fstat");
int fstatat(int dirfd, const char *path, struct stat *sb, int flag);

/* ManiOS can't change permissions or create directories: these fail
 * with EROFS. umask() only remembers. */
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int mkdir(const char *path, mode_t mode);
mode_t umask(mode_t mask);

#endif
