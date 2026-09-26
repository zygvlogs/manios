/* libwin (win.h): windows as files under /dev/wsys. */
#include "win.h"
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Starts `cat PATH` with its standard output on a new pipe; returns the
 * pipe's other end and stores the pid. */
static int start_pump(const char *path, int *pid)
{
	int fds[2];
	if (pipe(fds) < 0) {
		return -1;
	}
	int saved = dup(1);
	char *argv[] = { "cat", (char *)path, NULL };
	dup2(fds[1], 1);
	*pid = spawn("/bin/cat", argv);
	int err = errno;
	dup2(saved, 1);
	close(saved);
	close(fds[1]);
	if (*pid < 0) {
		close(fds[0]);
		errno = err;
		return -1;
	}
	return fds[0];
}

struct win *win_open(const char *title, int width, int height)
{
	char path[64], msg[96];
	struct win *w = calloc(1, sizeof(*w));
	if (!w) {
		return NULL;
	}
	w->ctl = w->image = w->events = w->ctlfile = -1;
	w->width = width;
	w->height = height;
	w->canvas = gfx_canvas_new(width, height);
	int err = ENOMEM;
	if (!w->canvas) {
		goto fail;
	}
	w->ctl = open("/dev/wsys/new", ORDWR);
	if (w->ctl < 0) {
		err = errno;
		goto fail;
	}
	int n = snprintf(msg, sizeof(msg), "%d %d %s", width, height, title);
	long got = -1;
	errno = 0;
	/* The write moved the file offset; the number is at the start. */
	if (write(w->ctl, msg, (size_t)n) != n || lseek(w->ctl, 0, SEEK_SET) != 0
	    || (got = read(w->ctl, path, sizeof(path) - 1)) <= 0) {
		err = errno ? errno : EIO;
		goto fail;
	}
	path[got] = '\0';
	w->id = atoi(path);
	snprintf(path, sizeof(path), "/dev/wsys/%d/image", w->id);
	w->image = open(path, OWRITE);
	snprintf(path, sizeof(path), "/dev/wsys/%d/event", w->id);
	if (w->image < 0 || (w->events = start_pump(path, &w->pump)) < 0) {
		err = errno;
		goto fail;
	}
	win_flush(w, 0, height);
	return w;
fail:
	if (w->image >= 0) {
		close(w->image);
	}
	if (w->ctl >= 0) {
		close(w->ctl);
	}
	gfx_canvas_free(w->canvas);
	free(w);
	errno = err;
	return NULL;
}

int win_flush(struct win *w, int y, int h)
{
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (y + h > w->height) {
		h = w->height - y;
	}
	if (h <= 0) {
		return 0;
	}
	/* Whole rows are contiguous in both the canvas and the image. */
	long offset = (long)y * w->width * 4, len = (long)h * w->width * 4;
	if (lseek(w->image, offset, SEEK_SET) < 0) {
		return -1;
	}
	const char *p = (const char *)&w->canvas->pixels[y * w->width];
	while (len > 0) {
		long n = write(w->image, p, (size_t)len);
		if (n <= 0) {
			return -1;
		}
		p += n;
		len -= n;
	}
	return 0;
}

/* Writes a command to the window's ctl file. */
static int ctl(struct win *w, const char *cmd)
{
	if (w->ctlfile < 0) {
		char path[64];
		snprintf(path, sizeof(path), "/dev/wsys/%d/ctl", w->id);
		w->ctlfile = open(path, OWRITE);
		if (w->ctlfile < 0) {
			return -1;
		}
	}
	long n = (long)strlen(cmd);
	return write(w->ctlfile, cmd, (size_t)n) == n ? 0 : -1;
}

int win_resize(struct win *w, int width, int height)
{
	if (width == w->width && height == w->height) {
		return 0;
	}
	struct gfx_canvas *c = gfx_canvas_new(width, height);
	if (!c) {
		return -1;
	}
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "size %d %d", width, height);
	if (ctl(w, cmd) < 0) {
		gfx_canvas_free(c);
		return -1;
	}
	gfx_canvas_free(w->canvas);
	w->canvas = c;
	w->width = width;
	w->height = height;
	return 0;
}

/* Parses one line ("k 97", "k 97 a", "m 10 20 1", "f 1", "c", "r 300
 * 200"); 0 if unknown. */
static int parse(const char *line, struct win_event *e)
{
	memset(e, 0, sizeof(*e));
	char *p = (char *)line + 1;
	switch (line[0]) {
	case 'k':
		e->key = (int)strtol(p, &p, 10);
		e->alt = !strcmp(p, " a");
		break;
	case 'r':
		e->width = (int)strtol(p, &p, 10);
		e->height = (int)strtol(p, &p, 10);
		break;
	case 'm':
		e->x = (int)strtol(p, &p, 10);
		e->y = (int)strtol(p, &p, 10);
		e->buttons = (int)strtol(p, &p, 10);
		break;
	case 'f':
		e->focused = (int)strtol(p, &p, 10);
		break;
	case 'c':
		break;
	default:
		return 0;
	}
	e->type = (enum win_event_type)line[0];
	return 1;
}

int win_next(struct win *w, struct win_event *e, int timeout_ms)
{
	uint32_t start = uptime_ms();
	for (;;) {
		char *nl = memchr(w->buf, '\n', (size_t)w->len);
		while (nl) {
			*nl = '\0';
			int ok = parse(w->buf, e);
			int used = (int)(nl + 1 - w->buf);
			memmove(w->buf, nl + 1, (size_t)(w->len - used));
			w->len -= used;
			if (ok && e->type == WIN_RESIZE) {
				/* A size it can't have (no memory): it keeps its own,
				 * and the desktop shows what fits. */
				ok = win_resize(w, e->width, e->height) == 0;
			}
			if (ok) {
				return 1;
			}
			nl = memchr(w->buf, '\n', (size_t)w->len);
		}
		if (w->len == (int)sizeof(w->buf)) {
			w->len = 0; /* a line that long is not an event */
		}
		if (w->events < 0) {
			return -1;
		}
		int wait = -1;
		if (timeout_ms >= 0) {
			uint32_t spent = uptime_ms() - start;
			wait = spent >= (uint32_t)timeout_ms ? 0 : timeout_ms - (int)spent;
		}
		struct zkt_pollfd pf = { w->events, 0 };
		if (poll(&pf, 1, wait) == 0) {
			return 0;
		}
		long n = read(w->events, w->buf + w->len, sizeof(w->buf) - (size_t)w->len);
		if (n <= 0) {
			close(w->events); /* the helper saw end of file: the window is gone */
			w->events = -1;
			return -1;
		}
		w->len += (int)n;
	}
}

int win_fd(const struct win *w)
{
	return w->events;
}

int win_set_title(struct win *w, const char *title)
{
	char msg[80];
	snprintf(msg, sizeof(msg), "title %s", title);
	return ctl(w, msg);
}

void win_close(struct win *w)
{
	/* The window goes with the clone file; the helper then reads end of
	 * file and exits, before its pipe is closed under it. */
	close(w->image);
	if (w->ctlfile >= 0) {
		close(w->ctlfile);
	}
	close(w->ctl);
	int status;
	wait(w->pump, &status);
	if (w->events >= 0) {
		close(w->events);
	}
	gfx_canvas_free(w->canvas);
	free(w);
}
