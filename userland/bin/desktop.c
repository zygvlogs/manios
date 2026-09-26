/* desktop [ARGS] -- the name ManiOS's desktop had before ManiDE (0.18):
 * runs /bin/manide with the same arguments and exits as it does. */
#include <manios.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	char *args[ZKT_ARGS_MAX + 1] = { "manide" };
	int n = 1;
	for (int i = 1; i < argc && n < ZKT_ARGS_MAX; i++) {
		args[n++] = argv[i];
	}
	args[n] = NULL;
	int pid = spawn("/bin/manide", args), status;
	if (pid < 0 || wait(pid, &status) != pid) {
		perror("desktop: /bin/manide");
		return 1;
	}
	return status & ZKT_WAIT_KILLED ? 1 : ZKT_WAIT_CODE(status);
}
