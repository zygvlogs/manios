/* The window system's files (DESIGN.md §4):
 *
 *   /dev/wsys/new       clone: write "W H TITLE", read the number; the
 *                       window closes when this file does
 *   /dev/wsys/N/ctl     read the state; write "title T", "top", "size W H",
 *                       "ws N"
 *   /dev/wsys/N/image   pixels, 4 bytes each, row-major, at the size the
 *                       program chose
 *   /dev/wsys/N/event   text lines, read blocking; end of file when gone
 */
#include "manide.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct zsrv_node *wsys_dir, *new_node;

static bool is(const struct zsrv_node *n, const char *name)
{
	return !strcmp(n->name, name);
}

/* Copies text s from offset, as a read of a file holding s. */
static long text(const struct zsrv_req *req, void *buf, const char *s)
{
	uint32_t len = (uint32_t)strlen(s);
	if (req->offset >= len) {
		return 0;
	}
	uint32_t n = len - req->offset;
	n = n < req->count ? n : req->count;
	memcpy(buf, s + req->offset, n);
	return n;
}

/* --- events --- */

/* The whole lines at the front of the queue that fit in count bytes. */
static int lines_fitting(const struct window *w, uint32_t count)
{
	int n = 0;
	for (int i = 0; i < w->nevents && (uint32_t)i < count; i++) {
		if (w->events[i] == '\n') {
			n = i + 1;
		}
	}
	return n;
}

static void take_events(struct window *w, int n)
{
	memmove(w->events, w->events + n, (size_t)(w->nevents - n));
	w->nevents -= n;
}

/* Answers a read waiting on the window's event file, if there is one
 * and there is something to say. */
static void deliver(struct window *w)
{
	for (struct zsrv_req *r = D.srv->deferred; r && w->nevents; r = r->next) {
		if (r->fid->node == w->event_node) {
			int n = lines_fitting(w, r->count);
			if (n) {
				zsrv_respond(D.srv, r, w->events, (uint32_t)n);
				take_events(w, n);
			}
			return;
		}
	}
}

void post_event(struct window *w, const char *fmt, ...)
{
	char line[64];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
	va_end(ap);
	if (n <= 0 || n >= (int)sizeof(line) - 1) {
		return;
	}
	line[n++] = '\n';
	/* A pointer move replaces a queued one with the same buttons. */
	if (line[0] == 'm' && w->nevents) {
		int last = w->nevents - 1;
		while (last > 0 && w->events[last - 1] != '\n') {
			last--;
		}
		const char *b1 = strrchr(line, ' ');
		char prev[64];
		int plen = w->nevents - last;
		if (w->events[last] == 'm' && plen < (int)sizeof(prev)) {
			memcpy(prev, w->events + last, (size_t)plen);
			prev[plen] = '\0';
			const char *b2 = strrchr(prev, ' ');
			if (b1 && b2 && !strcmp(b1, b2)) {
				w->nevents = last;
			}
		}
	}
	if (w->nevents + n > EVENT_QUEUE) {
		return; /* the application isn't reading: drop it */
	}
	memcpy(w->events + w->nevents, line, (size_t)n);
	w->nevents += n;
	deliver(w);
}

/* --- windows --- */

static void clean_title(char *dst, const char *src)
{
	strlcpy(dst, *src ? src : "untitled", TITLE_MAX + 1);
	for (char *c = dst; *c; c++) {
		if (*c < ' ' || *c > '~') {
			*c = ' ';
		}
	}
}

/* Parses "W H" at the start of spec: 0, or -1 unless both are sizes a
 * window can have. */
static int parse_size(const char *spec, int *width, int *height, char **rest)
{
	char *p, *q;
	long w = strtol(spec, &p, 10);
	long h = strtol(p, &q, 10);
	if (p == spec || q == p || w < MIN_SIZE || h < MIN_SIZE || w > D.width || h > D.height) {
		return -1;
	}
	*width = (int)w;
	*height = (int)h;
	*rest = q;
	return 0;
}

static struct window *create(struct zsrv_fid *owner, const char *spec)
{
	int width, height;
	char *p;
	if (parse_size(spec, &width, &height, &p) < 0) {
		errno = EINVAL;
		return NULL;
	}
	while (*p == ' ') {
		p++;
	}
	if (D.count == MAX_WINDOWS) {
		errno = EBUSY;
		return NULL;
	}
	struct window *w = calloc(1, sizeof(*w));
	if (!w || !(w->image = gfx_canvas_new(width, height))) {
		free(w);
		errno = ENOMEM;
		return NULL;
	}
	w->id = D.next_id++;
	clean_title(w->title, p);
	char name[16];
	snprintf(name, sizeof(name), "%d", w->id);
	w->dir = zsrv_add(wsys_dir, name, ZKT_TYPE_DIR, w);
	struct zsrv_node *ctl = w->dir ? zsrv_add(w->dir, "ctl", ZKT_TYPE_FILE, w) : NULL;
	w->image_node = ctl ? zsrv_add(w->dir, "image", ZKT_TYPE_FILE, w) : NULL;
	w->event_node = w->image_node ? zsrv_add(w->dir, "event", ZKT_TYPE_FILE, w) : NULL;
	if (!w->event_node) {
		if (w->dir) {
			zsrv_remove(D.srv, w->dir);
		}
		gfx_canvas_free(w->image);
		free(w);
		errno = ENOMEM;
		return NULL;
	}
	w->image_node->length = (uint32_t)(width * height * 4);
	w->owner = owner;
	w->told_w = width;
	w->told_h = height;
	add_window(w);
	say("window %d \"%s\" %dx%d on workspace %d", w->id, w->title, width, height, w->ws + 1);
	return w;
}

void window_destroy(struct window *w)
{
	if (D.grab == w) {
		D.grab = NULL;
	}
	remove_window(w);
	/* Readers of its events see end of file; then the files go. */
	for (struct zsrv_req *r = D.srv->deferred, *next; r; r = next) {
		next = r->next;
		if (r->fid->node == w->event_node) {
			zsrv_respond(D.srv, r, "", 0);
		}
	}
	zsrv_remove(D.srv, w->dir);
	if (w->owner) {
		w->owner->aux = NULL;
	}
	say("window %d closed", w->id);
	gfx_canvas_free(w->image);
	free(w);
}

/* The program's image has a new size: what fits of the old stays. */
static long resize_image(struct window *w, const char *spec)
{
	int width, height;
	char *rest;
	if (parse_size(spec, &width, &height, &rest) < 0 || *rest) {
		return -EINVAL;
	}
	if (width == w->image->width && height == w->image->height) {
		return 0;
	}
	struct gfx_canvas *c = gfx_canvas_new(width, height);
	if (!c) {
		return -ENOMEM;
	}
	gfx_blit(c, 0, 0, w->image, (struct gfx_rect){ 0, 0, w->image->width, w->image->height });
	gfx_canvas_free(w->image);
	w->image = c;
	w->image_node->length = (uint32_t)(width * height * 4);
	damage(w->content);
	return 0;
}

/* --- the file server's callbacks --- */

static long wsys_read(struct zsrv *s, struct zsrv_req *req, void *buf)
{
	(void)s;
	struct zsrv_node *n = req->fid->node;
	struct window *w = n->aux;
	char line[TITLE_MAX + 64];
	if (n == new_node) {
		w = req->fid->aux;
		if (!w) {
			return -EINVAL; /* write the size first */
		}
		snprintf(line, sizeof(line), "%d\n", w->id);
		return text(req, buf, line);
	}
	if (is(n, "ctl")) {
		snprintf(line, sizeof(line), "%d %d %d %d %d %d %s\n", w->id, w->content.x, w->content.y,
		         w->image->width, w->image->height, D.focus == w, w->title);
		return text(req, buf, line);
	}
	if (is(n, "image")) {
		uint32_t size = w->image_node->length;
		if (req->offset >= size) {
			return 0;
		}
		uint32_t k = size - req->offset < req->count ? size - req->offset : req->count;
		memcpy(buf, (const uint8_t *)w->image->pixels + req->offset, k);
		return k;
	}
	/* event */
	int k = lines_fitting(w, req->count);
	if (!k) {
		return w->nevents ? -EINVAL : ZSRV_DEFER; /* EINVAL: too small for a line */
	}
	memcpy(buf, w->events, (size_t)k);
	take_events(w, k);
	return k;
}

static long ctl_write(struct window *w, const char *cmd)
{
	if (!strncmp(cmd, "title ", 6)) {
		clean_title(w->title, cmd + 6);
		if (w->shown) {
			damage(strip_rect(w));
		}
		return 0;
	}
	if (!strcmp(cmd, "top")) {
		/* The window comes into view, with the focus. */
		show_workspace(w->ws);
		set_focus(w);
		return 0;
	}
	if (!strncmp(cmd, "size ", 5)) {
		return resize_image(w, cmd + 5);
	}
	if (!strncmp(cmd, "ws ", 3)) {
		char *end;
		long ws = strtol(cmd + 3, &end, 10);
		if (end == cmd + 3 || *end || ws < 1 || ws > WORKSPACES) {
			return -EINVAL;
		}
		send_to_workspace(w, (int)ws - 1);
		return 0;
	}
	return -EINVAL;
}

static long wsys_write(struct zsrv *s, struct zsrv_fid *f, uint32_t offset, const void *buf,
                       uint32_t count)
{
	(void)s;
	struct zsrv_node *n = f->node;
	char cmd[TITLE_MAX + 16];
	if (n == new_node || is(n, "ctl")) {
		if (count >= sizeof(cmd)) {
			return -EINVAL;
		}
		memcpy(cmd, buf, count);
		cmd[count] = '\0';
		if (count && cmd[count - 1] == '\n') {
			cmd[count - 1] = '\0';
		}
		if (n == new_node) {
			if (f->aux) {
				return -EBUSY; /* one window per open */
			}
			f->aux = create(f, cmd);
			return f->aux ? (long)count : -errno;
		}
		long rc = ctl_write(n->aux, cmd);
		return rc < 0 ? rc : (long)count;
	}
	if (is(n, "image")) {
		struct window *w = n->aux;
		uint32_t size = w->image_node->length;
		if (offset >= size) {
			return -EINVAL;
		}
		if (count > size - offset) {
			count = size - offset;
		}
		memcpy((uint8_t *)w->image->pixels + offset, buf, count);
		if (w->shown) {
			int row_bytes = w->image->width * 4;
			int first = (int)(offset / (uint32_t)row_bytes);
			int last = (int)((offset + count - 1) / (uint32_t)row_bytes);
			damage(gfx_intersect(w->content, (struct gfx_rect){ w->content.x, w->content.y + first,
			                                                    w->image->width, last - first + 1 }));
		}
		return count;
	}
	return -EINVAL; /* event */
}

static void wsys_clunk(struct zsrv *s, struct zsrv_fid *f)
{
	(void)s;
	if (f->node == new_node && f->aux) {
		window_destroy(f->aux);
	}
}

static const struct zsrv_ops ops = { wsys_read, wsys_write, wsys_clunk };

int wsys_init(int fd)
{
	D.srv = zsrv_new(fd, &ops, NULL);
	if (!D.srv) {
		return -1;
	}
	/* Mounted after /dev, the tree adds one directory to it. */
	wsys_dir = zsrv_add(&D.srv->root, "wsys", ZKT_TYPE_DIR, NULL);
	new_node = wsys_dir ? zsrv_add(wsys_dir, "new", ZKT_TYPE_FILE, NULL) : NULL;
	return new_node ? 0 : -1;
}
