/* The window system's conformance test (M12, docs/desktop/DESIGN.md §4).
 * Run it inside the desktop -- from a terminal window -- and it checks
 * /dev/wsys through plain file operations: making windows, ctl, image
 * (read back from the screen itself, /dev/fb), events, errors, limits,
 * and that a window goes when its program dies. The summary goes to
 * standard output and to /dev/cons (the serial line, for tests).
 *
 *   wintest          the test
 *   wintest crash    opens a window, then faults   } used by
 *   wintest hold     keeps some windows open       } the test */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 40
#define H 30
#define TOP    0x123456u
#define BOTTOM 0x654321u
#define EDGE   0xABCDEFu

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("wintest: FAIL: %s (errno %d)\n", what, errno);
	}
}

static void check_err(long r, int err, const char *what)
{
	check(r == -1 && errno == err, what);
}

static long get(const char *path, char *buf, long size)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		return -1;
	}
	long n = read(fd, buf, (size_t)size - 1);
	close(fd);
	buf[n > 0 ? n : 0] = '\0';
	return n;
}

static long put(const char *path, const char *s)
{
	int fd = open(path, OWRITE);
	if (fd < 0) {
		return -1;
	}
	long n = write(fd, s, strlen(s));
	close(fd);
	return n;
}

static int exists(const char *path)
{
	int fd = open(path, OREAD);
	if (fd >= 0) {
		close(fd);
	}
	return fd >= 0;
}

static int entries(const char *path)
{
	struct zkt_dirent e;
	int fd = open(path, OREAD), n = 0;
	while (fd >= 0 && read(fd, &e, sizeof(e)) == sizeof(e)) {
		n++;
	}
	close(fd);
	return fd < 0 ? -1 : n;
}

/* Makes a window; its number, or -1. *ctl is the clone file. */
static int make(const char *spec, int *ctl)
{
	char buf[16];
	*ctl = open("/dev/wsys/new", ORDWR);
	if (*ctl < 0) {
		return -1;
	}
	if (write(*ctl, spec, strlen(spec)) < 0 || lseek(*ctl, 0, SEEK_SET) != 0
	    || read(*ctl, buf, sizeof(buf) - 1) <= 0) {
		int err = errno;
		close(*ctl);
		errno = err;
		return -1;
	}
	return atoi(buf);
}

struct state {
	int id, x, y, w, h, focused;
	char title[64];
};

/* Parses N/ctl: "N X Y WIDTH HEIGHT FOCUSED TITLE". */
static int state(int id, struct state *s)
{
	char path[48], buf[128];
	snprintf(path, sizeof(path), "/dev/wsys/%d/ctl", id);
	if (get(path, buf, sizeof(buf)) <= 0) {
		return -1;
	}
	char *p = buf;
	s->id = (int)strtol(p, &p, 10);
	s->x = (int)strtol(p, &p, 10);
	s->y = (int)strtol(p, &p, 10);
	s->w = (int)strtol(p, &p, 10);
	s->h = (int)strtol(p, &p, 10);
	s->focused = (int)strtol(p, &p, 10);
	if (*p == ' ') {
		p++;
	}
	strlcpy(s->title, p, sizeof(s->title));
	char *nl = strchr(s->title, '\n');
	if (nl) {
		*nl = '\0';
	}
	return 0;
}

static long ctl(int id, const char *cmd)
{
	char path[48];
	snprintf(path, sizeof(path), "/dev/wsys/%d/ctl", id);
	return put(path, cmd);
}

/* Waits (up to 2 s) for the screen to show `color` at (x, y): the
 * compositor draws after answering the write. 32-bit modes only. */
static int screen_shows(int x, int y, uint32_t color)
{
	char desc[64];
	int pitch = 0;
	if (get("/dev/fbctl", desc, sizeof(desc)) > 0) {
		char *p = desc;
		for (int i = 0; i < 4; i++) {
			pitch = (int)strtol(p, &p, 10); /* width height depth pitch */
		}
	}
	int fb = open("/dev/fb", OREAD);
	uint32_t until = uptime_ms() + 2000, px = 0;
	while (fb >= 0 && pitch > 0) {
		lseek(fb, (long)y * pitch + (long)x * 4, SEEK_SET);
		if (read(fb, &px, 4) != 4 || (px & 0xFFFFFF) == color || uptime_ms() > until) {
			break;
		}
		sleep_ms(20);
	}
	close(fb);
	return (px & 0xFFFFFF) == color;
}

static void test_new(int *a, int *a_ctl)
{
	char buf[64];
	int fd = open("/dev/wsys/new", ORDWR);
	check_err(read(fd, buf, sizeof(buf)), EINVAL, "reading new before making a window");
	check_err(write(fd, "0 0 x", 5), EINVAL, "a window too small");
	check_err(write(fd, "5000 20 x", 9), EINVAL, "a window wider than the screen");
	check_err(write(fd, "nonsense", 8), EINVAL, "a size that isn't numbers");
	close(fd);

	*a = make("40 30 wintest", a_ctl);
	check(*a > 0, "making a window");
	check_err(write(*a_ctl, "50 50 again", 11), EBUSY, "one window per open of new");
	struct state s;
	check(state(*a, &s) == 0 && s.id == *a && s.w == W && s.h == H && s.focused == 1
	      && !strcmp(s.title, "wintest"), "ctl: a new window has its size, title and the focus");
	snprintf(buf, sizeof(buf), "/dev/wsys/%d", *a);
	check(entries(buf) == 3, "a window's directory: ctl, image, event");
}

static void test_ctl(int a)
{
	struct state s;
	check(ctl(a, "title renamed") > 0 && state(a, &s) == 0 && !strcmp(s.title, "renamed"),
	      "ctl: title");
	check(ctl(a, "move 300 200") > 0 && state(a, &s) == 0 && s.x == 300 && s.y == 200, "ctl: move");
	/* Some of the title bar always stays on screen, below the panel. */
	check(ctl(a, "move -1000 -1000") > 0 && state(a, &s) == 0 && s.x == -1 && s.y == 41,
	      "ctl: a move off screen is limited");
	ctl(a, "move 300 200");
	check_err(ctl(a, "move 1"), EINVAL, "ctl: move needs two numbers");
	check_err(ctl(a, "move 1 2 3"), EINVAL, "ctl: move takes two numbers");
	check_err(ctl(a, "jump"), EINVAL, "ctl: an unknown command");
}

static void test_image(int a)
{
	static uint32_t pixels[W * H], back[W * H];
	char path[48];
	for (int i = 0; i < W * H; i++) {
		pixels[i] = i < W * H / 2 ? TOP : BOTTOM;
	}
	snprintf(path, sizeof(path), "/dev/wsys/%d/image", a);
	int fd = open(path, ORDWR);
	check(fd >= 0 && write(fd, pixels, sizeof(pixels)) == sizeof(pixels), "writing the image");
	check(lseek(fd, 0, SEEK_SET) == 0 && read(fd, back, sizeof(back)) == sizeof(back)
	      && !memcmp(pixels, back, sizeof(back)), "the image reads back");
	check(screen_shows(305, 205, TOP) && screen_shows(305, 225, BOTTOM),
	      "the screen shows the image where the window is");
	uint32_t edge[W];
	for (int i = 0; i < W; i++) {
		edge[i] = EDGE;
	}
	check(lseek(fd, (H - 1) * W * 4, SEEK_SET) >= 0 && write(fd, edge, sizeof(edge)) == sizeof(edge)
	      && screen_shows(339, 229, EDGE), "a partial write shows at once");
	check(lseek(fd, W * H * 4 - 4, SEEK_SET) >= 0 && write(fd, edge, 8) == 4,
	      "a write past the end is cut short");
	check_err(lseek(fd, W * H * 4, SEEK_SET) >= 0 ? write(fd, edge, 4) : 0, EINVAL,
	          "a write at the end is EINVAL");
	close(fd);
}

static void test_events(int a, int *b, int *b_ctl)
{
	char path[48], buf[64];
	snprintf(path, sizeof(path), "/dev/wsys/%d/event", a);
	int ev = open(path, ORDWR);
	check(ev >= 0 && read(ev, buf, sizeof(buf)) == 4 && !memcmp(buf, "f 1\n", 4),
	      "events: a new window is told it has the focus");
	*b = make("30 20 second", b_ctl);
	check(*b > a, "a second window gets a new number");
	struct state s;
	check(state(a, &s) == 0 && !s.focused && state(*b, &s) == 0 && s.focused,
	      "the newest window has the focus");
	check_err(read(ev, buf, 2), EINVAL, "events: a buffer too small for a line");
	check(read(ev, buf, sizeof(buf)) == 4 && !memcmp(buf, "f 0\n", 4),
	      "events: losing the focus");
	check_err(write(ev, "k 1\n", 4), EINVAL, "events can't be written");
	check(ctl(a, "top") > 0 && read(ev, buf, sizeof(buf)) == 4 && !memcmp(buf, "f 1\n", 4),
	      "ctl: top gives the focus back");
	close(ev);
}

static void test_close(int b, int b_ctl)
{
	/* Someone waiting on the window's events sees end of file when the
	 * window goes. */
	char path[48], *argv[] = { "cat", path, NULL };
	snprintf(path, sizeof(path), "/dev/wsys/%d/event", b);
	int saved = dup(1), null = open("/dev/null", OWRITE);
	dup2(null, 1);
	int pid = spawn("/bin/cat", argv);
	dup2(saved, 1);
	close(saved);
	close(null);
	sleep_ms(200); /* until cat has read the queued events and waits */
	int status = -1, got = 0;
	check(pid > 0 && reap(&status) == 0, "a reader waits for the window's events");
	close(b_ctl);
	for (uint32_t until = uptime_ms() + 3000; pid > 0 && got != pid && uptime_ms() < until;) {
		got = reap(&status);
		sleep_ms(10);
	}
	check(got == pid && status == 0, "closing new closes the window: its reader gets end of file");
	snprintf(path, sizeof(path), "/dev/wsys/%d", b);
	check(!exists(path) && errno == ENOENT, "a closed window's directory is gone");
}

static void test_crash(void)
{
	int before = entries("/dev/wsys"), status;
	char *argv[] = { "wintest", "crash", NULL };
	int pid = spawn("/boot/test/wintest", argv);
	check(pid > 0 && wait(pid, &status) == pid && (status & ZKT_WAIT_KILLED),
	      "a program is killed with a window open");
	check(entries("/dev/wsys") == before, "its window went with it");
}

/* `wintest hold`: makes HOLD windows, says so on its standard input (a
 * pipe: both ways), and keeps them until that pipe ends. */
#define HOLD 8

static int hold(void)
{
	int fds[HOLD], n = 0;
	while (n < HOLD && make("20 20 held", &fds[n]) > 0) {
		n++;
	}
	char c = (char)('0' + n);
	write(0, &c, 1);
	while (read(0, &c, 1) > 0) {
	}
	return 0;
}

static void test_limit(void)
{
	/* A process has 16 descriptors, so a helper holds some windows. */
	int link[2], saved = dup(0), before = entries("/dev/wsys");
	char *argv[] = { "wintest", "hold", NULL }, c = 0;
	pipe(link);
	dup2(link[1], 0);
	int pid = spawn("/boot/test/wintest", argv);
	dup2(saved, 0);
	close(saved);
	close(link[1]);
	check(pid > 0 && read(link[0], &c, 1) == 1 && c == '0' + HOLD, "another program's windows");
	int fds[16], n = 0;
	while (n < 16 && make("20 20 many", &fds[n]) > 0) {
		n++;
	}
	check(n < 16 && errno == EBUSY && before - 1 + HOLD + n == 16,
	      "sixteen windows at most, then EBUSY");
	while (n > 0) {
		close(fds[--n]);
	}
	write(link[0], "", 0); /* end of file: the helper exits */
	close(link[0]);
	int status;
	check(pid > 0 && wait(pid, &status) == pid && status == 0 && entries("/dev/wsys") == before,
	      "closing them all, and exiting, closes every window");
}

static void report(void)
{
	char line[80];
	if (failures) {
		snprintf(line, sizeof(line), "wintest: %d of %d checks failed\n", failures, checks);
	} else {
		snprintf(line, sizeof(line), "wintest: all %d checks passed\n", checks);
	}
	fputs(line, stdout);
	fflush(stdout);
	int cons = open("/dev/cons", OWRITE);
	if (cons >= 0) {
		write(cons, line, strlen(line));
		close(cons);
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "hold")) {
		return hold();
	}
	if (argc > 1 && !strcmp(argv[1], "crash")) {
		int fd;
		if (make("30 30 crash", &fd) < 0) {
			return 1;
		}
		*(volatile int *)0 = 1; /* killed, with the window open */
		return 2;
	}
	if (!exists("/dev/wsys/new")) {
		printf("wintest: no /dev/wsys: run it inside the desktop\n");
		return 1;
	}
	int a, a_ctl, b, b_ctl;
	test_new(&a, &a_ctl);
	test_ctl(a);
	test_image(a);
	test_events(a, &b, &b_ctl);
	test_close(b, b_ctl);
	test_crash();
	test_limit();
	close(a_ctl);
	report();
	return failures ? 1 : 0;
}
