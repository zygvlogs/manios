/* desktop [WIDTH HEIGHT] -- the ManiOS desktop (DESIGN.md). 800x600 by
 * default; needs a Bochs VBE display of at least 640x480. Exits from the
 * panel's menu, restoring the text console.
 *
 * `desktop -m` is the helper that mounts the window system: the desktop
 * can't mount its own pipe, because mounting waits for the server to
 * answer -- and the server is the desktop. */
#include "desktop.h"
#include <errno.h>
#include <manios.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS 250
#define SHUTDOWN_MS 3000

struct desktop D;

const struct menu_item MENU[] = {
	{ "Terminal", "/bin/term" },
	{ "Clock", "/bin/clock" },
	{ "About ManiOS", "/bin/about" },
	{ NULL, NULL },
	{ "Exit desktop", NULL },
};
const int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

/* Progress goes to the console (and the serial line, which tests read). */
void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("desktop: ", stdout);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

void launch(const char *program)
{
	const char *name = strrchr(program, '/');
	char *argv[] = { (char *)(name ? name + 1 : program), NULL };
	int pid = spawn(program, argv);
	if (pid < 0) {
		say("%s: %s", program, strerror(errno));
	} else {
		say("started %s (pid %d)", program, pid);
	}
}

/* Logs children that exited; -1 once there are none left. */
static int reap_children(void)
{
	int status, pid;
	while ((pid = reap(&status)) > 0) {
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
		handle_key(buf[i]);
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
		/* A burst of requests (an image arriving in pieces) is handled
		 * before redrawing. */
		for (int i = 0; pf[0].ready && i < 64; i++) {
			zsrv_handle(D.srv);
			poll(pf, 1, 0);
		}
		if (kbd >= 0 && pf[kbd].ready) {
			read_keys();
		}
		if (mouse >= 0 && pf[mouse].ready) {
			read_mouse();
		}
	}
	update_clock();
	reap_children();
	compose();
}

/* Serves the helper's mount until it has finished. */
static int mount_wsys(int client_end)
{
	int saved = dup(0);
	char *argv[] = { "desktop", "-m", NULL };
	dup2(client_end, 0);
	int helper = spawn("/bin/desktop", argv);
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
		perror("desktop: mounting /dev/wsys");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-m")) {
		return mount_helper();
	}
	int width = 800, height = 600;
	if (argc == 3) {
		width = atoi(argv[1]);
		height = atoi(argv[2]);
	} else if (argc != 1) {
		fprintf(stderr, "usage: desktop [WIDTH HEIGHT]\n");
		return 2;
	}
	if (width < 640 || height < 480) {
		fprintf(stderr, "desktop: the screen must be at least 640x480\n");
		return 2;
	}

	/* Our own namespace: /dev/wsys is for us and what we start. */
	int probe = open("/dev/wsys", OREAD);
	if (probe >= 0) {
		close(probe);
		fprintf(stderr, "desktop: already running (there is a /dev/wsys)\n");
		return 1;
	}
	int fds[2];
	if (nsfork() < 0 || pipe(fds) < 0 || wsys_init(fds[0]) < 0 || mount_wsys(fds[1]) < 0) {
		fprintf(stderr, "desktop: cannot serve /dev/wsys\n");
		return 1;
	}

	if (gfx_screen_open(&D.screen, width, height) < 0) {
		fprintf(stderr, "desktop: cannot set a %dx%d mode: %s (a Bochs VBE display is needed)\n",
		        width, height, strerror(errno));
		return 1;
	}
	D.width = D.screen.width;
	D.height = D.screen.height;
	D.back = gfx_canvas_new(D.width, D.height);
	D.kbd = open("/dev/kbd", OREAD);
	D.mouse = open("/dev/mouse", OREAD);
	D.time = open("/dev/time", OREAD);
	if (!D.back || D.kbd < 0) {
		gfx_screen_close(&D.screen);
		fprintf(stderr, "desktop: out of memory, or no keyboard\n");
		return 1;
	}
	D.px = D.width / 2;
	D.py = D.height / 2;
	D.next_id = 1;
	update_clock();
	damage_all();
	compose();
	say("%dx%d, serving /dev/wsys; F1 opens the menu%s", D.width, D.height,
	    D.mouse < 0 ? " (no mouse)" : "");

	while (!D.quit) {
		step(TICK_MS);
	}

	/* Close every window; the applications see end of file on their
	 * events and exit, and their last requests still need answers. */
	say("exiting");
	while (D.count) {
		window_destroy(D.stack[D.count - 1]);
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
