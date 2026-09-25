/* sleep SECONDS -- wait; SECONDS may have a fraction (0.25). */
#include <ctype.h>
#include <manios.h>
#include <stdio.h>

#define MAX_SECONDS 4000000u /* keeps the milliseconds within 32 bits */

int main(int argc, char **argv)
{
	const char *p = argc == 2 ? argv[1] : "";
	uint32_t seconds = 0, ms = 0;
	int digits = 0;
	for (; isdigit((unsigned char)*p); p++, digits++) {
		seconds = seconds * 10 + (uint32_t)(*p - '0');
		if (seconds > MAX_SECONDS) {
			break;
		}
	}
	if (*p == '.') {
		uint32_t scale = 100;
		for (p++; isdigit((unsigned char)*p); p++, digits++) {
			ms += (uint32_t)(*p - '0') * scale;
			scale /= 10;
		}
	}
	if (!digits || *p) {
		fprintf(stderr, "usage: sleep SECONDS (at most %u)\n", MAX_SECONDS);
		return 1;
	}
	return sleep_ms(seconds * 1000 + ms) < 0;
}
