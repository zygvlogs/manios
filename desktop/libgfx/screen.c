/* The screen: /dev/fb and /dev/fbctl (zkt/drivers/fb.c). */
#include "gfx.h"
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_MAX 4096

int gfx_screen_open(struct gfx_screen *s, int width, int height)
{
	char cmd[32], desc[64];
	s->fb = -1;
	s->ctl = open("/dev/fbctl", ORDWR);
	if (s->ctl < 0) {
		return -1;
	}
	if (width) {
		snprintf(cmd, sizeof(cmd), "mode %d %d", width, height);
	} else {
		strcpy(cmd, "mode vga");
	}
	long n = -1;
	if (write(s->ctl, cmd, strlen(cmd)) >= 0 && lseek(s->ctl, 0, SEEK_SET) == 0) {
		n = read(s->ctl, desc, sizeof(desc) - 1);
	}
	if (n <= 0) {
		int err = n < 0 ? errno : EIO;
		close(s->ctl);
		errno = err;
		return -1;
	}
	desc[n] = '\0';
	/* "WIDTH HEIGHT DEPTH PITCH FORMAT DRIVER" */
	char *p = desc;
	s->width = (int)strtol(p, &p, 10);
	s->height = (int)strtol(p, &p, 10);
	s->depth = (int)strtol(p, &p, 10);
	s->pitch = (int)strtol(p, &p, 10);
	s->rgb332 = strstr(p, "rgb332") != NULL;
	s->fb = open("/dev/fb", ORDWR);
	if (s->fb < 0 || s->width <= 0 || s->height <= 0 || s->width > ROW_MAX) {
		int err = s->fb < 0 ? errno : EIO;
		gfx_screen_close(s);
		errno = err;
		return -1;
	}
	return 0;
}

void gfx_screen_close(struct gfx_screen *s)
{
	if (s->ctl >= 0) {
		write(s->ctl, "text", 4);
		close(s->ctl);
		s->ctl = -1;
	}
	if (s->fb >= 0) {
		close(s->fb);
		s->fb = -1;
	}
}

void gfx_present(struct gfx_screen *s, const struct gfx_canvas *c, struct gfx_rect r)
{
	static uint8_t row8[ROW_MAX];
	r = gfx_intersect(r, (struct gfx_rect){ 0, 0, c->width, c->height });
	r = gfx_intersect(r, (struct gfx_rect){ 0, 0, s->width, s->height });
	for (int y = r.y; y < r.y + r.h; y++) {
		const uint32_t *in = &c->pixels[y * c->width + r.x];
		if (s->rgb332) {
			/* 4x4 ordered dithering: 256 colours show a gradient as a
			 * texture rather than bands. The offset stays below one
			 * step, so pure colours (0 or 255) come out exact. */
			static const uint8_t bayer[4][4] = {
				{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
			};
			for (int x = 0; x < r.w; x++) {
				uint32_t px = in[x], t = bayer[y & 3][(r.x + x) & 3];
				uint32_t red = GFX_R(px) + 2 * t, green = GFX_G(px) + 2 * t, blue = GFX_B(px) + 4 * t;
				red = red > 255 ? 255 : red;
				green = green > 255 ? 255 : green;
				blue = blue > 255 ? 255 : blue;
				row8[x] = (uint8_t)((red >> 5) << 5 | (green >> 5) << 2 | blue >> 6);
			}
			lseek(s->fb, (long)y * s->pitch + r.x, SEEK_SET);
			write(s->fb, row8, (size_t)r.w);
		} else {
			lseek(s->fb, (long)y * s->pitch + (long)r.x * 4, SEEK_SET);
			write(s->fb, in, (size_t)r.w * 4);
		}
	}
}
