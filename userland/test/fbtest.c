/* The framebuffer device's file interface (fb.c), run by
 * tests/gfx_test.py: offsets, bounds, reading back, and fbctl errors. */
#include <errno.h>
#include <gfx.h>
#include <manios.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("fbtest: FAIL: %s (errno %d)\n", what, errno);
	}
}

int main(void)
{
	struct gfx_screen s;
	if (gfx_screen_open(&s, 640, 480) < 0) {
		perror("fbtest");
		return 1;
	}
	struct zkt_dirent d;
	uint32_t bytes = (uint32_t)(s.pitch * s.height);
	check(fstat(s.fb, &d) == 0 && d.type == ZKT_TYPE_DEVICE && d.size == bytes,
	      "/dev/fb's size is pitch x height");

	uint32_t pattern[4] = { 0x00112233, 0x00445566, 0x00778899, 0x00AABBCC }, back[4];
	check(lseek(s.fb, 100 * s.pitch + 40, SEEK_SET) == 100 * s.pitch + 40
	      && write(s.fb, pattern, sizeof(pattern)) == sizeof(pattern), "a write at an offset");
	lseek(s.fb, 100 * s.pitch + 40, SEEK_SET);
	check(read(s.fb, back, sizeof(back)) == sizeof(back) && !memcmp(back, pattern, sizeof(back)),
	      "reading back what was written");
	check(lseek(s.fb, (long)bytes - 8, SEEK_SET) >= 0 && write(s.fb, pattern, sizeof(pattern)) == 8,
	      "a write running off the end is cut short");
	check(read(s.fb, back, 4) == 0, "reading at the end gives end of file");
	check(write(s.fb, pattern, 4) == -1 && errno == ENXIO, "a write past the end is ENXIO");
	check(lseek(s.fb, -1, SEEK_SET) == -1 && errno == EINVAL, "a negative offset is EINVAL");

	check(write(s.ctl, "mode 99999 3", 12) == -1 && errno == EINVAL, "fbctl: an impossible mode");
	check(write(s.ctl, "blink", 5) == -1 && errno == EINVAL, "fbctl: an unknown command");
	char long_cmd[64];
	memset(long_cmd, 'x', sizeof(long_cmd));
	check(write(s.ctl, long_cmd, sizeof(long_cmd)) == -1 && errno == EINVAL,
	      "fbctl: an overlong command");

	gfx_screen_close(&s);
	int fb = open("/dev/fb", ORDWR);
	check(fb >= 0 && write(fb, pattern, 4) == -1 && errno == ENXIO, "no pixels in text mode");
	close(fb);
	if (failures) {
		printf("fbtest: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("fbtest: all %d checks passed\n", checks);
	return 0;
}
