/* Run two copies at once: both use the same addresses, so each seeing
 * only its own values shows the address spaces are separate. Also
 * checks the loader's work: initialised data, zeroed bss. */
#include <manios.h>

static volatile unsigned initialised = 0x1234ABCDu;
static volatile unsigned char zeroed[8192];
static volatile unsigned mine;

int main(void)
{
	if (initialised != 0x1234ABCDu) {
		return 2;
	}
	for (unsigned i = 0; i < sizeof(zeroed); i++) {
		if (zeroed[i]) {
			return 3;
		}
	}
	unsigned value = (unsigned)getpid() * 2654435761u;
	mine = value;
	zeroed[4096] = (unsigned char)value;
	for (int i = 0; i < 5; i++) {
		sleep_ms(10); /* let the other copy run and write its own values */
		if (mine != value || zeroed[4096] != (unsigned char)value) {
			return 4;
		}
	}
	return 0;
}
