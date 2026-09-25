/* clock -- the time (UTC) and date, from /dev/time, in a window. Closes
 * from its close box or with Escape or q. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>
#include <win.h>

#define WIDTH 200
#define HEIGHT 70
#define BG     GFX_RGB(0x1E, 0x24, 0x30)
#define AMBER  GFX_RGB(0xE0, 0x7A, 0x2E)
#define LIGHT  GFX_RGB(0xC8, 0xCE, 0xD8)

static long now(int fd)
{
	char buf[24];
	lseek(fd, 0, SEEK_SET);
	long n = read(fd, buf, sizeof(buf) - 1);
	long t = 0;
	for (long i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++) {
		t = t * 10 + (buf[i] - '0');
	}
	return n > 0 ? t : -1;
}

/* Days since 1970-01-01 to a civil date (proleptic Gregorian), with
 * integers only: the algorithm of H. Hinnant's "chrono-compatible
 * low-level date algorithms", restated. */
static void civil(long days, int *year, int *month, int *day)
{
	long z = days + 719468;
	long era = z / 146097;
	long doe = z - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;
	*day = (int)(doy - (153 * mp + 2) / 5 + 1);
	*month = (int)(mp < 10 ? mp + 3 : mp - 9);
	*year = (int)(yoe + era * 400 + (*month <= 2));
}

static void draw(struct win *w, long t)
{
	char time_text[16], date_text[24];
	struct gfx_canvas *c = w->canvas;
	gfx_fill(c, (struct gfx_rect){ 0, 0, WIDTH, HEIGHT }, BG);
	if (t < 0) {
		gfx_text(c, 10, 10, "no /dev/time", LIGHT, 1);
	} else {
		long s = t % 86400;
		int year, month, day;
		civil(t / 86400, &year, &month, &day);
		snprintf(time_text, sizeof(time_text), "%02ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
		snprintf(date_text, sizeof(date_text), "%04d-%02d-%02d UTC", year, month, day);
		gfx_text(c, (WIDTH - gfx_text_width(time_text, 3)) / 2, 10, time_text, AMBER, 3);
		gfx_text(c, (WIDTH - gfx_text_width(date_text, 1)) / 2, 50, date_text, LIGHT, 1);
	}
	win_flush(w, 0, HEIGHT);
}

int main(void)
{
	struct win *w = win_open("Clock", WIDTH, HEIGHT);
	if (!w) {
		fprintf(stderr, "clock: no window: %s\n", strerror(errno));
		return 1;
	}
	int fd = open("/dev/time", OREAD);
	long shown = -2;
	for (;;) {
		long t = fd >= 0 ? now(fd) : -1;
		if (t != shown) {
			draw(w, t);
			shown = t;
		}
		struct win_event e;
		int got = win_next(w, &e, 200);
		if (got < 0 || (got && (e.type == WIN_CLOSE
		                        || (e.type == WIN_KEY && (e.key == 0x1B || e.key == 'q'))))) {
			break;
		}
	}
	win_close(w);
	return 0;
}
