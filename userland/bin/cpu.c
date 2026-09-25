/* cpu HOST [COMMAND [ARGS...]] -- run a command on a CPU server
 * (M13, ADR-0005), as Plan 9's cpu does.
 *
 * HOST is a dial string ("udp!10.0.0.2", or just the address). The
 * command (default /bin/sh) runs on HOST, but sees this terminal: its
 * standard input and output are this console, this namespace is at
 * /mnt/term (it starts in the same directory there), and devices this
 * machine has and HOST lacks -- /dev/wsys inside the desktop -- are in
 * its /dev. cpu waits for it and exits with its status.
 *
 * How: cpu exports its own namespace over ZRP under a fresh name,
 * mounts HOST's cpu service (cpud), and writes the job to its clone
 * file; the export goes when cpu exits. With a cluster key (key= at
 * boot) both directions are authenticated. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOB_MAX 2048

/* This machine's address, from the first interface in /dev/net that
 * isn't the loopback: "ne0 10.0.0.3/24 gateway ..." */
static int my_address(char *out, size_t size)
{
	char text[512];
	int fd = open("/dev/net", OREAD);
	long n = fd < 0 ? -1 : read(fd, text, sizeof(text) - 1);
	close(fd);
	if (n <= 0) {
		return -1;
	}
	text[n] = '\0';
	for (char *line = text; *line;) {
		char *nl = strchr(line, '\n');
		if (nl) {
			*nl = '\0';
		}
		char *addr = strchr(line, ' ');
		if (addr && strncmp(line, "lo ", 3)) {
			addr++;
			size_t len = strcspn(addr, "/ ");
			if (len && len < size && strncmp(addr, "0.0.0.0", 7)) {
				memcpy(out, addr, len);
				out[len] = '\0';
				return 0;
			}
		}
		if (!nl) {
			break;
		}
		line = nl + 1;
	}
	return -1;
}

static int fail(const char *what, const char *host)
{
	fprintf(stderr, "cpu: %s%s%s: %s\n", what, *what && host ? " " : "", host ? host : "",
	        strerror(errno));
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: cpu HOST [COMMAND [ARGS...]]\n");
		return 2;
	}
	const char *host = argv[1];
	char *shell[] = { "/bin/sh", NULL };
	char **command = argc > 2 ? &argv[2] : shell;

	char me[32], aname[32], cwd[ZKT_PATH_MAX + 1];
	if (my_address(me, sizeof(me)) < 0) {
		fprintf(stderr, "cpu: this machine has no network address\n");
		return 1;
	}
	if (!getcwd(cwd, sizeof(cwd))) {
		strcpy(cwd, "/");
	}
	/* The namespace the command will see is this one, as it is: export
	 * a copy of it, then take another copy for our own mount of HOST
	 * (which must not appear in the export). */
	if (nsfork() < 0) {
		return fail("nsfork", 0);
	}
	snprintf(aname, sizeof(aname), "cpu.%d.%lu", getpid(), (unsigned long)uptime_ms());
	if (export("/", aname) < 0) {
		return fail("exporting this namespace", 0);
	}
	if (nsfork() < 0) {
		return fail("nsfork", 0);
	}
	if (mount(host, "/n", BIND_FLAG_REPLACE, "cpu") < 0) {
		return fail("", host);
	}

	/* The job: one field per line (cpud). A program named without a
	 * '/' is one of HOST's /bin. */
	char job[JOB_MAX], path[ZKT_PATH_MAX + 1];
	if (strchr(command[0], '/')) {
		strlcpy(path, command[0], sizeof(path));
	} else {
		snprintf(path, sizeof(path), "/bin/%s", command[0]);
	}
	int n = snprintf(job, sizeof(job), "udp!%s\n%s\n%s\n%s", me, aname, cwd, path);
	for (int i = 1; command[i] && n < (int)sizeof(job); i++) {
		n += snprintf(job + n, sizeof(job) - (size_t)n, "\n%s", command[i]);
	}
	if (n >= (int)sizeof(job)) {
		fprintf(stderr, "cpu: the command is too long\n");
		return 1;
	}
	int ctl = open("/n/new", ORDWR);
	char id[16];
	long got = -1;
	if (ctl < 0 || write(ctl, job, (size_t)n) != n || lseek(ctl, 0, SEEK_SET) != 0
	    || (got = read(ctl, id, sizeof(id) - 1)) <= 0) {
		return fail("starting the job on", host);
	}
	id[got] = '\0';
	id[strcspn(id, "\n")] = '\0';

	/* Until it ends: the read waits on HOST (Rpending keeps it alive). */
	char wait_path[48], verdict[192];
	snprintf(wait_path, sizeof(wait_path), "/n/%s/wait", id);
	int fd = open(wait_path, OREAD);
	got = fd < 0 ? -1 : read(fd, verdict, sizeof(verdict) - 1);
	if (got <= 0) {
		return fail("waiting for the job on", host);
	}
	verdict[got] = '\0';
	close(fd);
	close(ctl);
	if (!strncmp(verdict, "exit ", 5)) {
		return atoi(verdict + 5);
	}
	if (!strncmp(verdict, "error ", 6)) {
		verdict[strcspn(verdict, "\n")] = '\0';
		fprintf(stderr, "cpu: %s: %s\n", host, verdict + 6);
		return 125;
	}
	if (!strncmp(verdict, "killed ", 7)) {
		fprintf(stderr, "cpu: %s: killed on %s (vector %d)\n", command[0], host, atoi(verdict + 7));
		return 128 + atoi(verdict + 7);
	}
	return 1;
}
