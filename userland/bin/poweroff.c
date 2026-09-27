/* poweroff -- turns the machine off (ACPI, through /dev/power). */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	int fd = open("/dev/power", OWRITE);
	if (fd < 0 || write(fd, "off", 3) != 3) {
		fprintf(stderr, "poweroff: %s\n",
		        fd >= 0 && errno == ENODEV ? "this machine can't be turned off by ManiOS (no ACPI S5)"
		                                   : strerror(errno));
		return 1;
	}
	return 0;
}
