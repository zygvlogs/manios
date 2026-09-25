/* Namespace changes made by a child, for utest to observe from the
 * parent (ADR-0003: a child shares its parent's namespace unless it
 * forks it), and directory inheritance. Usage: nstest bind |
 * fork-unbind | cwd. */
#include <manios.h>
#include <string.h>

static int exists(const char *path)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		return 0;
	}
	close(fd);
	return 1;
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";
	if (!strcmp(mode, "bind")) {
		/* Shared: the parent sees this. */
		return bind("/boot", "/n", BIND_FLAG_REPLACE) == 0 && exists("/n/bin/hello") ? 0 : 1;
	}
	if (!strcmp(mode, "fork-unbind")) {
		/* Private: the parent keeps its /bin. */
		if (nsfork() != 0 || unbind("/bin") != 0) {
			return 1;
		}
		return exists("/bin/hello") ? 1 : 0;
	}
	if (!strcmp(mode, "cwd")) {
		/* utest runs this from /boot. */
		char buf[ZKT_PATH_MAX + 1];
		return getcwd(buf, sizeof(buf)) && !strcmp(buf, "/boot") && exists("etc/motd") ? 0 : 1;
	}
	return 1;
}
