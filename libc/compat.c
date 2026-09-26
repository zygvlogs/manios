/* OpenBSD's pledge() and unveil() (<unistd.h>): ManiOS has no such
 * restrictions yet, so these do nothing and succeed. */
#include <unistd.h>

int pledge(const char *promises, const char *execpromises)
{
	(void)promises;
	(void)execpromises;
	return 0;
}

int unveil(const char *path, const char *permissions)
{
	(void)path;
	(void)permissions;
	return 0;
}
