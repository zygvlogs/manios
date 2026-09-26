/* clock -- the time (UTC) and date, from /dev/time, in a window: as
 * large as its pane allows. Closes when asked (Alt+q, the close box) or
 * with Escape or q. */
#include <errno.h>
#include <manios.h>
#include <stdbool.h>
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

/* Draws the time; sends the whole window when `all`, else only the
 * rows the time and date are on (the rest stays as it was). */
static void draw(struct win *w, long t, bool all)
{
	char time_text[16], date_text[24];
	struct gfx_canvas *c = w->canvas;
	int width = w->width, height = w->height;
	gfx_fill(c, (struct gfx_rect){ 0, 0, width, height }, BG);
	if (t < 0) {
		gfx_text(c, 10, 10, "no /dev/time", LIGHT, 1);
	} else {
		long s = t % 86400;
		int year, month, day;
		civil(t / 86400, &year, &month, &day);
		snprintf(time_text, sizeof(time_text), "%02ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
		snprintf(date_text, sizeof(date_text), "%04d-%02d-%02d UTC", year, month, day);
		/* The largest time that fits, with the date under it. */
		int scale = 1;
		while (gfx_text_width(time_text, scale + 1) + 20 <= width
		       && GFX_CELL_H * (scale + 1) + 30 <= height && scale < 12) {
			scale++;
		}
		int small = scale >= 6 ? 2 : 1;
		int block = GFX_CELL_H * scale + 8 + GFX_CELL_H * small;
		int y = (height - block) / 2;
		gfx_text(c, (width - gfx_text_width(time_text, scale)) / 2, y, time_text, AMBER, scale);
		gfx_text(c, (width - gfx_text_width(date_text, small)) / 2, y + GFX_CELL_H * scale + 8,
		         date_text, LIGHT, small);
		if (!all) {
			win_flush(w, y, block);
			return;
		}
	}
	win_flush(w, 0, height);
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
			draw(w, t, shown == -2);
			shown = t;
		}
		struct win_event e;
		int got = win_next(w, &e, 200);
		if (got < 0 || (got && (e.type == WIN_CLOSE
		                        || (e.type == WIN_KEY && !e.alt && (e.key == 0x1B || e.key == 'q'))))) {
			break;
		}
		if (got && e.type == WIN_RESIZE) {
			shown = -2; /* draw again */
		}
	}
	win_close(w);
	return 0;
}
