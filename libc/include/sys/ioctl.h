/* Device control: ManiOS has none of Unix's terminal ioctls, so ioctl()
 * fails with ENOTTY, and programs use their defaults (80 columns). */
#ifndef MANIOS_SYS_IOCTL_H
#define MANIOS_SYS_IOCTL_H

struct winsize {
	unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define TIOCGWINSZ 0x40087468

int ioctl(int fd, unsigned long request, ...);

#endif
