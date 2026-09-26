/* manide [-n | -s SESSION] [WIDTH HEIGHT] -- ManiDE, the ManiOS desktop
 * (DESIGN.md). 800x600 by default; needs a Bochs VBE display of at
 * least 640x480. It starts the programs its session file lists
 * (/boot/etc/manide, or SESSION; -n: none), and exits with Alt+Shift+Q
 * or from its menu, restoring the text console.
 *
 * `manide -m` is the helper that mounts the window system: ManiDE can't
 * mount its own pipe, because mounting waits for the server to answer
 * -- and the server is ManiDE. */
#include "manide.h"
#include <errno.h>
#include <manios.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS 250
#define BURST_MS 5
#define SHUTDOWN_MS 3000
#define SESSION "/boot/etc/manide"
#define SESSION_WAIT_MS 3000 /* for each program's window, at most */
#define ARGS_MAX 15

struct desktop D;

const struct menu_item MENU[] = {
	{ "Terminal", "term" },
	{ "System info", "term fetch" },
	{ "Processes", "term top" },
	{ "Welcome", "welcome" },
	{ "Clock", "clock" },
	{ "About ManiOS", "about" },
	{ NULL, NULL },
	{ "Exit ManiDE", NULL },
};
const int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

/* Progress goes to the console (and the serial line, which tests read). */
void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("manide: ", stdout);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

/* Starts a command: words split at spaces, the first a program in
 * /bin unless it has a slash. The pid, or -1. */
static int start(const char *command)
{
	char copy[128], path[160], *argv[ARGS_MAX + 1];
	int argc = 0;
	strlcpy(copy, command, sizeof(copy));
	for (char *p = strtok(copy, " \t"); p && argc < ARGS_MAX; p = strtok(NULL, " \t")) {
		argv[argc++] = p;
	}
	argv[argc] = NULL;
	if (!argc) {
		return -1;
	}
	snprintf(path, sizeof(path), "%s%s", strchr(argv[0], '/') ? "" : "/bin/", argv[0]);
	int pid = spawn(path, argv);
	if (pid < 0) {
		say("%s: %s", path, strerror(errno));
		return -1;
	}
	char words[128] = "";
	for (int i = 0; i < argc; i++) {
		if (i) {
			strlcat(words, " ", sizeof(words));
		}
		strlcat(words, argv[i], sizeof(words));
	}
	say("started %s (pid %d)", words, pid);
	return pid;
}

void launch(const char *command)
{
	start(command);
}

static int session_pid; /* the session program being waited for */

/* Logs children that exited; -1 once there are none left. */
static int reap_children(void)
{
	int status, pid;
	while ((pid = reap(&status)) > 0) {
		if (pid == session_pid) {
			session_pid = -1; /* gone without a window: on with the session */
		}
		if (status & ZKT_WAIT_KILLED) {
			say("pid %d killed (vector %d)", pid, ZKT_WAIT_VECTOR(status));
		} else {
			say("pid %d exited (status %d)", pid, ZKT_WAIT_CODE(status));
		}
	}
	return pid;
}

static void read_keys(void)
{
	unsigned char buf[64];
	long n = read(D.kbd, buf, sizeof(buf));
	for (long i = 0; i < n; i++) {
		if (buf[i] == ZKT_KEY_ALT) {
			D.alt_next = true; /* the key comes next, maybe in the next read */
			continue;
		}
		bool alt = D.alt_next;
		D.alt_next = false;
		handle_key(buf[i], alt);
	}
}

static void read_mouse(void)
{
	char buf[512];
	long n = read(D.mouse, buf, sizeof(buf) - 1);
	if (n <= 0) {
		return;
	}
	buf[n] = '\0';
	for (char *p = buf; *p == 'm';) {
		int dx = (int)strtol(p + 1, &p, 10);
		int dy = (int)strtol(p, &p, 10);
		int buttons = (int)strtol(p, &p, 10);
		handle_mouse(dx, dy, buttons);
		while (*p == '\n') {
			p++;
		}
	}
}

/* Waits up to timeout_ms for requests and input, handles them, redraws. */
static void step(int timeout_ms)
{
	struct zkt_pollfd pf[3];
	int n = 0;
	pf[n++] = (struct zkt_pollfd){ D.srv->fd, 0 };
	int kbd = D.kbd >= 0 ? n++ : -1, mouse = D.mouse >= 0 ? n++ : -1;
	if (kbd >= 0) {
		pf[kbd] = (struct zkt_pollfd){ D.kbd, 0 };
	}
	if (mouse >= 0) {
		pf[mouse] = (struct zkt_pollfd){ D.mouse, 0 };
	}
	if (poll(pf, n, timeout_ms) > 0) {
		/* A burst of requests is handled before redrawing: a program
		 * sends its image in pieces, one after another, so wait a
		 * moment for the next before composing. */
		for (int i = 0; pf[0].ready && i < 64; i++) {
			zsrv_handle(D.srv);
			poll(pf, 1, BURST_MS);
		}
		if (kbd >= 0 && pf[kbd].ready) {
			read_keys();
		}
		if (mouse >= 0 && pf[mouse].ready) {
			read_mouse();
		}
	}
	if (bar_update()) {
		damage_bar();
	}
	reap_children();
	compose();
}

/* --- the session --- */

static int session_fd = -1;
static char session_buf[1024];
static int session_len;
static int session_count;
static uint32_t session_since;

/* The next line of the session file, without its newline; 0 at the end. */
static int session_line(char *line, size_t size)
{
	for (;;) {
		char *nl = memchr(session_buf, '\n', (size_t)session_len);
		if (nl || (session_fd < 0 && session_len)) {
			int len = nl ? (int)(nl - session_buf) : session_len;
			int copy = len < (int)size - 1 ? len : (int)size - 1;
			memcpy(line, session_buf, (size_t)copy);
			line[copy] = '\0';
			int used = nl ? len + 1 : len;
			memmove(session_buf, session_buf + used, (size_t)(session_len - used));
			session_len -= used;
			return 1;
		}
		if (session_fd < 0) {
			return 0;
		}
		long n = read(session_fd, session_buf + session_len, sizeof(session_buf) - (size_t)session_len);
		if (n <= 0 || session_len + n == (long)sizeof(session_buf)) {
			close(session_fd);
			session_fd = -1;
		}
		session_len += n > 0 ? (int)n : 0;
	}
}

/* Runs the session one program at a time, each once the one before has
 * its window (or has exited, or had SESSION_WAIT_MS), so the panes come
 * in the file's order. Called from the main loop. */
static void session_step(void)
{
	if (session_pid) {
		bool waited = uptime_ms() - session_since >= SESSION_WAIT_MS;
		if (session_pid > 0 && D.count <= session_count && !waited) {
			return;
		}
		session_pid = 0;
	}
	char line[128];
	while (session_line(line, sizeof(line))) {
		char *p = line + strspn(line, " \t");
		if (!*p || *p == '#') {
			continue;
		}
		if (!strncmp(p, "workspace ", 10)) {
			int ws = atoi(p + 10);
			if (ws >= 1 && ws <= WORKSPACES) {
				D.place_ws = ws - 1;
			}
			continue;
		}
		session_count = D.count;
		session_since = uptime_ms();
		int pid = start(p);
		if (pid > 0) {
			session_pid = pid;
			return;
		}
	}
	D.place_ws = D.ws; /* the session is over */
}

static void session_open(const char *path)
{
	session_fd = open(path, OREAD);
	if (session_fd < 0) {
		say("no session: %s: %s", path, strerror(errno));
	}
}

/* Serves the helper's mount until it has finished. */
static int mount_wsys(int client_end)
{
	int saved = dup(0);
	char *argv[] = { "manide", "-m", NULL };
	dup2(client_end, 0);
	int helper = spawn("/bin/manide", argv);
	dup2(saved, 0);
	close(saved);
	close(client_end);
	if (helper < 0) {
		return -1;
	}
	uint32_t deadline = uptime_ms() + 5000;
	while (uptime_ms() < deadline) {
		struct zkt_pollfd pf = { D.srv->fd, 0 };
		if (poll(&pf, 1, 50) > 0) {
			zsrv_handle(D.srv);
		}
		int status;
		if (reap(&status) == helper) {
			return status == 0 ? 0 : -1;
		}
	}
	return -1;
}

static int mount_helper(void)
{
	if (mountfd(0, "/dev", BIND_FLAG_AFTER, NULL) < 0) {
		perror("manide: mounting /dev/wsys");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-m")) {
		return mount_helper();
	}
	int width = 800, height = 600, arg = 1;
	const char *session = SESSION;
	if (arg < argc && !strcmp(argv[arg], "-n")) {
		session = NULL;
		arg++;
	} else if (arg + 1 < argc && !strcmp(argv[arg], "-s")) {
		session = argv[arg + 1];
		arg += 2;
	}
	if (argc - arg == 2) {
		width = atoi(argv[arg]);
		height = atoi(argv[arg + 1]);
	} else if (argc != arg) {
		fprintf(stderr, "usage: manide [-n | -s SESSION] [WIDTH HEIGHT]\n");
		return 2;
	}
	if (width < 640 || height < 480) {
		fprintf(stderr, "manide: the screen must be at least 640x480\n");
		return 2;
	}

	/* Our own namespace: /dev/wsys is for us and what we start. */
	int probe = open("/dev/wsys", OREAD);
	if (probe >= 0) {
		close(probe);
		fprintf(stderr, "manide: already running (there is a /dev/wsys)\n");
		return 1;
	}
	int fds[2];
	if (nsfork() < 0 || pipe(fds) < 0 || wsys_init(fds[0]) < 0 || mount_wsys(fds[1]) < 0) {
		fprintf(stderr, "manide: cannot serve /dev/wsys\n");
		return 1;
	}

	if (gfx_screen_open(&D.screen, width, height) < 0) {
		fprintf(stderr, "manide: cannot set a %dx%d mode: %s (a Bochs VBE display is needed)\n",
		        width, height, strerror(errno));
		return 1;
	}
	D.width = D.screen.width;
	D.height = D.screen.height;
	D.back = gfx_canvas_new(D.width, D.height);
	D.kbd = open("/dev/kbd", OREAD);
	D.mouse = open("/dev/mouse", OREAD);
	if (!D.back || D.kbd < 0) {
		gfx_screen_close(&D.screen);
		fprintf(stderr, "manide: out of memory, or no keyboard\n");
		return 1;
	}
	D.px = D.width / 2;
	D.py = D.height / 2;
	D.next_id = 1;
	for (int i = 0; i < WORKSPACES; i++) {
		D.wss[i] = (struct workspace){ LAYOUT_GRID, 55, NULL };
	}
	bar_init();
	damage_all();
	compose();
	say("%dx%d, serving /dev/wsys; Alt+Enter: terminal, F1: menu%s", D.width, D.height,
	    D.mouse < 0 ? " (no mouse)" : "");
	if (session) {
		session_open(session);
	}

	while (!D.quit) {
		session_step();
		step(session_pid ? 50 : TICK_MS);
	}

	/* Close every window; the applications see end of file on their
	 * events and exit, and their last requests still need answers. */
	say("exiting");
	while (D.count) {
		window_destroy(D.order[D.count - 1]);
	}
	compose();
	uint32_t deadline = uptime_ms() + SHUTDOWN_MS;
	while (reap_children() >= 0 && uptime_ms() < deadline) {
		step(50);
	}
	gfx_screen_close(&D.screen);
	close(D.kbd);
	if (D.mouse >= 0) {
		close(D.mouse);
	}
	say("bye");
	return 0;
}
