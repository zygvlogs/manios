/* libwin: a window on the ManiOS desktop (M12). The window system is a
 * file server, /dev/wsys (docs/desktop/DESIGN.md §4); this library is a
 * thin layer over its files:
 *
 *   w = win_open("Title", width, height);   draw into w->canvas (libgfx),
 *   win_flush(w, y, h);                     then show rows [y, y+h);
 *   win_next(w, &e, timeout_ms);            wait for an event.
 *
 * Events come from a helper process (`cat /dev/wsys/N/event`) through a
 * pipe, so a program waiting for other things too can poll() win_fd()
 * along with its other descriptors. */
#ifndef MANIOS_WIN_H
#define MANIOS_WIN_H

#include <gfx.h>

struct win {
	int id;
	int width, height;
	struct gfx_canvas *canvas;
	/* private */
	int ctl;     /* /dev/wsys/new, open for the window's lifetime */
	int image;   /* /dev/wsys/N/image */
	int events;  /* the read end of the event helper's pipe */
	int pump;    /* the helper's pid */
	char buf[512];
	int len;
};

enum win_event_type {
	WIN_KEY = 'k',   /* key: a /dev/kbd byte (ASCII or ZKT_KEY_*) */
	WIN_MOUSE = 'm', /* x, y (window coordinates), buttons */
	WIN_FOCUS = 'f', /* focused: 1 gained, 0 lost */
	WIN_CLOSE = 'c', /* the close box was pressed */
};

struct win_event {
	enum win_event_type type;
	int key;
	int x, y, buttons;
	int focused;
};

/* Opens a window with a content area of width x height pixels, cleared
 * to black; NULL with errno set (ENOENT: no desktop is running). */
struct win *win_open(const char *title, int width, int height);
/* Shows rows [y, y + h) of the canvas. */
int win_flush(struct win *w, int y, int h);
/* The next event: 1, 0 if timeout_ms passed first (-1: wait forever),
 * or -1 once the window is gone (the desktop closed it or exited). */
int win_next(struct win *w, struct win_event *e, int timeout_ms);
/* The descriptor to poll() for events. */
int win_fd(const struct win *w);
int win_set_title(struct win *w, const char *title);
/* Closes the window and frees everything. */
void win_close(struct win *w);

#endif
