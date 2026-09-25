/* cpud -- the CPU server's service (M13, ADR-0005).
 *
 *   cpud        starts the service in the background and returns
 *   cpud -i     the same on a machine without a cluster key (insecure:
 *               anyone who can reach the machine can run programs)
 *
 * The service is a file tree, exported over ZRP as attach name "cpu":
 *
 *   new         clone: write a job, one field per line --
 *                 DIAL        where the terminal is ("udp!10.0.0.3")
 *                 ANAME       the name it exported its namespace as
 *                 CWD         its current directory
 *                 COMMAND     the program, then its arguments
 *               -- then read the job's number
 *   N/wait      read: blocks until the job ends: "exit CODE",
 *               "killed VECTOR", or "error WHY" if the job never
 *               started (the terminal couldn't be reached, say)
 *
 * A job runs in a namespace of its own with the terminal's namespace
 * mounted at /mnt/term, the terminal's /dev/cons over /dev/cons, and the
 * terminal's other devices after /dev -- so its standard input and
 * output are the terminal's console, and on a terminal running the
 * desktop, /dev/wsys is the terminal's window system. It starts in
 * /mnt/term/CWD. (Internal modes: -s the service, -m its mount helper,
 * -x a job.) */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zrpsrv.h>

#define FIELDS_MAX (4 + ZKT_ARGS_MAX)
#define VERDICT_MAX 192 /* "error " WHY "\n" */

struct job {
	int id, pid;
	bool done;
	int status;
	int errors;      /* the runner's standard error until it starts the program */
	char why[160];   /* what it said, if it gave up */
	struct zsrv_node *dir, *wait;
	struct zsrv_fid *owner;
	struct job *next;
};

static struct job *jobs;
static struct zsrv *srv;
static int next_id = 1;

/* --- a job (-x DIAL ANAME CWD COMMAND ARGS...) --- */

static int job_main(int argc, char **argv)
{
	const char *dial = argv[2], *aname = argv[3], *cwd = argv[4];
	char **command = &argv[5];
	/* Until the program starts, standard error is a pipe to cpud, which
	 * passes what goes wrong here to the terminal (N/wait: "error ..."). */
	if (argc < 6 || nsfork() < 0) {
		fprintf(stderr, "cannot make a namespace for the job\n");
		return 125;
	}
	if (mount(dial, "/mnt/term", BIND_FLAG_REPLACE, aname) < 0) {
		fprintf(stderr, "cannot reach the terminal's namespace (%s): %s\n", dial, strerror(errno));
		return 125;
	}
	if (bind("/mnt/term/dev/cons", "/dev/cons", BIND_FLAG_REPLACE) < 0
	    || bind("/mnt/term/dev", "/dev", BIND_FLAG_AFTER) < 0) {
		fprintf(stderr, "cannot bind the terminal's devices: %s\n", strerror(errno));
		return 125;
	}
	int cons = open("/dev/cons", ORDWR);
	if (cons < 0) {
		fprintf(stderr, "cannot open the terminal's console: %s\n", strerror(errno));
		return 125;
	}
	for (int fd = 0; fd < 3; fd++) {
		dup2(cons, fd);
	}
	close(cons);
	char dir[ZKT_PATH_MAX + 1];
	snprintf(dir, sizeof(dir), "/mnt/term%s", cwd);
	if (chdir(dir) < 0) {
		chdir("/");
	}
	int pid = spawn(command[0], command);
	if (pid < 0) {
		fprintf(stderr, "cpu: %s: %s\n", command[0],
		        errno == ENOENT ? "not found" : strerror(errno));
		return 127;
	}
	int status;
	if (wait(pid, &status) != pid) {
		return 125;
	}
	if (status & ZKT_WAIT_KILLED) {
		/* Told where the user is, as the shell would tell it. */
		fprintf(stderr, "cpu: %s: killed (vector %d)\n", command[0], ZKT_WAIT_VECTOR(status));
		return 128 + ZKT_WAIT_VECTOR(status);
	}
	return ZKT_WAIT_CODE(status);
}

/* --- the service --- */

static struct job *find_job(struct zsrv_node *n)
{
	for (struct job *j = jobs; j; j = j->next) {
		if (j->dir == n || j->wait == n) {
			return j;
		}
	}
	return 0;
}

static long text(const struct zsrv_req *req, void *buf, const char *s)
{
	uint32_t len = (uint32_t)strlen(s);
	if (req->offset >= len) {
		return 0;
	}
	uint32_t n = len - req->offset < req->count ? len - req->offset : req->count;
	memcpy(buf, s + req->offset, n);
	return n;
}

static void verdict(const struct job *j, char *out, size_t size)
{
	if (j->why[0]) {
		snprintf(out, size, "error %s\n", j->why);
	} else if (j->status & ZKT_WAIT_KILLED) {
		snprintf(out, size, "killed %d\n", ZKT_WAIT_VECTOR(j->status));
	} else {
		snprintf(out, size, "exit %d\n", ZKT_WAIT_CODE(j->status));
	}
}

static long serve_read(struct zsrv *s, struct zsrv_req *req, void *buf)
{
	(void)s;
	char line[VERDICT_MAX];
	struct zsrv_node *n = req->fid->node;
	if (!strcmp(n->name, "new")) {
		struct job *j = req->fid->aux;
		if (!j) {
			return -EINVAL; /* write a job first */
		}
		snprintf(line, sizeof(line), "%d\n", j->id);
		return text(req, buf, line);
	}
	struct job *j = find_job(n);
	if (!j->done) {
		return ZSRV_DEFER;
	}
	verdict(j, line, sizeof(line));
	return text(req, buf, line);
}

/* Starts a job from its fields; the job, or NULL with errno set. */
static struct job *start(char **fields, int count)
{
	if (count < 4 || fields[0][0] == '\0' || fields[3][0] == '\0') {
		errno = EINVAL;
		return 0;
	}
	char *argv[FIELDS_MAX + 3];
	argv[0] = "cpud";
	argv[1] = "-x";
	for (int i = 0; i < count; i++) {
		argv[2 + i] = fields[i];
	}
	argv[2 + count] = NULL;
	struct job *j = calloc(1, sizeof(*j));
	int errors[2];
	if (!j || pipe(errors) < 0) {
		free(j);
		errno = ENOMEM;
		return 0;
	}
	/* The runner's standard error is one end of the pipe. */
	int saved = dup(2);
	dup2(errors[1], 2);
	int pid = spawn("/bin/cpud", argv);
	int err = errno;
	dup2(saved, 2);
	close(saved);
	close(errors[1]);
	if (pid < 0) {
		close(errors[0]);
		free(j);
		errno = err;
		return 0;
	}
	j->errors = errors[0];
	char name[16];
	j->id = next_id++;
	j->pid = pid;
	snprintf(name, sizeof(name), "%d", j->id);
	j->dir = zsrv_add(&srv->root, name, ZKT_TYPE_DIR, j);
	j->wait = j->dir ? zsrv_add(j->dir, "wait", ZKT_TYPE_FILE, j) : 0;
	j->next = jobs;
	jobs = j;
	printf("cpud: job %d: %s for %s (pid %d)\n", j->id, fields[3], fields[0], pid);
	fflush(stdout);
	return j;
}

static long serve_write(struct zsrv *s, struct zsrv_fid *f, uint32_t offset, const void *buf,
                        uint32_t count)
{
	(void)s;
	(void)offset;
	static char copy[ZSRV_MSIZE];
	if (strcmp(f->node->name, "new")) {
		return -EROFS;
	}
	if (f->aux) {
		return -EBUSY; /* one job per open */
	}
	if (count >= sizeof(copy)) {
		return -E2BIG;
	}
	memcpy(copy, buf, count);
	copy[count] = '\0';
	char *fields[FIELDS_MAX];
	int n = 0;
	for (char *p = copy; *p && n < FIELDS_MAX;) {
		fields[n++] = p;
		char *nl = strchr(p, '\n');
		if (!nl) {
			break;
		}
		*nl = '\0';
		p = nl + 1;
	}
	struct job *j = start(fields, n);
	if (!j) {
		return -errno;
	}
	j->owner = f;
	f->aux = j;
	return count;
}

static void forget(struct job *j)
{
	for (struct job **link = &jobs; *link; link = &(*link)->next) {
		if (*link == j) {
			*link = j->next;
			break;
		}
	}
	if (j->dir) {
		zsrv_remove(srv, j->dir);
	}
	free(j);
}

/* A job's files go once it has ended and its terminal has let go. */
static void serve_clunk(struct zsrv *s, struct zsrv_fid *f)
{
	(void)s;
	struct job *j = f->aux;
	if (j) {
		j->owner = 0;
		if (j->done) {
			forget(j);
		}
	}
}

static const struct zsrv_ops ops = { serve_read, serve_write, serve_clunk };

static void reap_jobs(void)
{
	int status, pid;
	while ((pid = reap(&status)) > 0) {
		for (struct job *j = jobs; j; j = j->next) {
			if (j->pid != pid || j->done) {
				continue;
			}
			j->done = true;
			j->status = status;
			/* The runner has exited: its pipe has what it said, then ends. */
			long n, got = 0;
			while ((n = read(j->errors, j->why + got, sizeof(j->why) - 1 - (size_t)got)) > 0) {
				got += n;
			}
			j->why[got] = '\0';
			j->why[strcspn(j->why, "\n")] = '\0';
			close(j->errors);
			if (ZKT_WAIT_CODE(status) != 125 || (status & ZKT_WAIT_KILLED)) {
				j->why[0] = '\0'; /* the program's own status, not a failure to start */
			}
			char line[VERDICT_MAX];
			verdict(j, line, sizeof(line));
			line[strlen(line) - 1] = '\0';
			printf("cpud: job %d: %s\n", j->id, line);
			fflush(stdout);
			for (struct zsrv_req *r = srv->deferred, *next; r; r = next) {
				next = r->next;
				if (r->fid->node == j->wait) {
					char out[VERDICT_MAX];
					verdict(j, out, sizeof(out));
					zsrv_respond(srv, r, out, (uint32_t)strlen(out));
				}
			}
			if (!j->owner) {
				forget(j);
			}
			break;
		}
	}
}

static int service_main(void)
{
	int fds[2];
	if (nsfork() < 0 || pipe(fds) < 0 || !(srv = zsrv_new(fds[0], &ops, NULL))
	    || !zsrv_add(&srv->root, "new", ZKT_TYPE_FILE, NULL)) {
		printf("cpud: cannot start: %s\n", strerror(errno));
		return 1;
	}
	/* The mount waits for us to answer it, so a helper makes it. */
	int saved = dup(0);
	char *argv[] = { "cpud", "-m", NULL };
	dup2(fds[1], 0);
	int helper = spawn("/bin/cpud", argv);
	dup2(saved, 0);
	close(saved);
	close(fds[1]);
	int status = -1;
	for (uint32_t until = uptime_ms() + 5000; helper > 0 && uptime_ms() < until;) {
		struct zkt_pollfd pf = { fds[0], 0 };
		if (poll(&pf, 1, 50) > 0) {
			zsrv_handle(srv);
		}
		if (reap(&status) == helper) {
			break;
		}
	}
	if (status != 0 || export("/n", "cpu") < 0) {
		printf("cpud: cannot serve the cpu service\n");
		return 1;
	}
	printf("cpud: serving attach name \"cpu\"\n");
	fflush(stdout);
	for (;;) {
		struct zkt_pollfd pf = { fds[0], 0 };
		if (poll(&pf, 1, 250) > 0 && zsrv_handle(srv) < 0) {
			return 0;
		}
		reap_jobs();
	}
}

/* Refuses a machine without a cluster key unless told otherwise. */
static int have_key(void)
{
	char buf[64];
	int fd = open("/dev/zrp", OREAD);
	long n = fd < 0 ? -1 : read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0) {
		return 0;
	}
	buf[n] = '\0';
	return !strncmp(buf, "key set", 7);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "-x")) {
		return job_main(argc, argv);
	}
	if (argc > 1 && !strcmp(argv[1], "-m")) {
		return mountfd(0, "/n", BIND_FLAG_REPLACE, NULL) < 0 ? 1 : 0;
	}
	if (argc > 1 && !strcmp(argv[1], "-s")) {
		return service_main();
	}
	bool insecure = argc > 1 && !strcmp(argv[1], "-i");
	if (argc > 2 || (argc == 2 && !insecure)) {
		fprintf(stderr, "usage: cpud [-i]\n");
		return 2;
	}
	if (!insecure && !have_key()) {
		fprintf(stderr, "cpud: this machine has no cluster key (key= at boot): anyone could run "
		                "programs here; cpud -i to allow it anyway\n");
		return 1;
	}
	/* In the background: the service outlives this process. */
	char *service[] = { "cpud", "-s", NULL };
	int pid = spawn("/bin/cpud", service);
	if (pid < 0) {
		perror("cpud");
		return 1;
	}
	return 0;
}
