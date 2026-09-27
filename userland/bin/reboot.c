/* reboot -- resets the machine (through /dev/power). */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	int fd = open("/dev/power", OWRITE);
	if (fd < 0 || write(fd, "reboot", 6) != 6) {
		fprintf(stderr, "reboot: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}
