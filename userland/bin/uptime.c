/* uptime -- time since boot. */
#include <manios.h>
#include <stdio.h>

int main(void)
{
	uint32_t ms = uptime_ms();
	printf("up %lu.%03lu s\n", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
	return 0;
}
