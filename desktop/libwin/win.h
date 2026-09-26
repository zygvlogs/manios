/* libwin: a window on ManiDE, the ManiOS desktop (M12, M18). The window
 * system is a file server, /dev/wsys (docs/desktop/DESIGN.md §4); this
 * library is a thin layer over its files:
 *
 *   w = win_open("Title", width, height);   draw into w->canvas (libgfx),
 *   win_flush(w, y, h);                     then show rows [y, y+h);
 *   win_next(w, &e, timeout_ms);            wait for an event.
 *
 * ManiDE tiles its windows, so a window's size is ManiDE's to choose:
 * width and height are only where it starts. When ManiDE gives the
 * window another size, win_next makes w->canvas that size (cleared to
 * black) and returns WIN_RESIZE; draw everything again and flush.
 *
 * Events come from a helper process (`cat /dev/wsys/N/event`) through a
 * pipe, so a program waiting for other things too can poll() win_fd()
 * along with its other descriptors. */
#ifndef MANIOS_WIN_H
#define MANIOS_WIN_H

#include <gfx.h>
#include <stdbool.h>

struct win {
	int id;
	int width, height;
	struct gfx_canvas *canvas;
	/* private */
	int ctl;     /* /dev/wsys/new, open for the window's lifetime */
	int ctlfile; /* /dev/wsys/N/ctl, opened when first needed */
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
	WIN_CLOSE = 'c', /* asked to close (Alt+q, the close box) */
	WIN_RESIZE = 'r', /* width, height: the canvas has a new size */
};

struct win_event {
	enum win_event_type type;
	int key;
	bool alt; /* WIN_KEY: pressed with Alt (keys ManiDE doesn't keep) */
	int x, y, buttons;
	int focused;
	int width, height;
};

/* Opens a window with a content area of width x height pixels, cleared
 * to black; NULL with errno set (ENOENT: no desktop is running). */
struct win *win_open(const char *title, int width, int height);
/* Shows rows [y, y + h) of the canvas. */
int win_flush(struct win *w, int y, int h);
/* The next event: 1, 0 if timeout_ms passed first (-1: wait forever),
 * or -1 once the window is gone (the desktop closed it or exited).
 * Before returning WIN_RESIZE it has resized w->canvas, w->width and
 * w->height, and told the desktop. */
int win_next(struct win *w, struct win_event *e, int timeout_ms);
/* The descriptor to poll() for events. */
int win_fd(const struct win *w);
int win_set_title(struct win *w, const char *title);
/* Asks for a canvas of another size, as WIN_RESIZE does; 0 or -1. */
int win_resize(struct win *w, int width, int height);
/* Closes the window and frees everything. */
void win_close(struct win *w);

#endif
