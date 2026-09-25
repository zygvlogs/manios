/* pwd -- print the current directory. */
#include <manios.h>
#include <stdio.h>

int main(void)
{
	char buf[ZKT_PATH_MAX + 1];
	if (!getcwd(buf, sizeof(buf))) {
		perror("pwd");
		return 1;
	}
	puts(buf);
	return 0;
}
