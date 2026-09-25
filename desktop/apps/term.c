/* term -- a terminal window running sh (DESIGN.md §5).
 *
 * 80x24 cells of the ManiOS font. The shell's standard input and output
 * are pipes; lines are edited here (Backspace, ^U) and sent whole, as
 * the console's line discipline does for programs on /dev/cons. ^D on
 * an empty line sends end of file. PgUp and PgDn look back over the
 * last HISTORY lines that scrolled off the top; typing, or more output,
 * comes back to the live screen. The window closes when the shell
 * exits, or from its close box. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <win.h>

#define COLS 80
#define ROWS 24
#define MARGIN 4
#define LINE_MAX 255
#define HISTORY 256

#define BG     GFX_RGB(0x10, 0x15, 0x1C)
#define FG     GFX_RGB(0xD8, 0xDE, 0xE9)
#define CURSOR GFX_RGB(0xE0, 0x7A, 0x2E)

static struct win *w;
static char cells[ROWS][COLS];
static char history[HISTORY][COLS]; /* lines scrolled off the top: a ring */
static int history_count, history_next;
static int back;                    /* how far the view is scrolled back */
static int row, col;
static int dirty_first = ROWS, dirty_last = -1;
static int to_sh = -1, from_sh = -1, sh_pid;
static char line[LINE_MAX + 1];
static int line_len;
static int focused = 1;

static void dirty(int r)
{
	dirty_first = r < dirty_first ? r : dirty_first;
	dirty_last = r > dirty_last ? r : dirty_last;
}

/* What row r of the window shows: a line from the history while the
 * view is scrolled back that far, otherwise one of the screen's. */
static const char *view_row(int r)
{
	if (r < back) {
		int oldest = (history_next - history_count + HISTORY) % HISTORY;
		return history[(oldest + history_count - back + r) % HISTORY];
	}
	return cells[r - back];
}

static void draw_row(int r)
{
	struct gfx_canvas *c = w->canvas;
	int y = MARGIN + r * GFX_CELL_H;
	gfx_fill(c, (struct gfx_rect){ 0, y, c->width, GFX_CELL_H }, BG);
	char text[COLS + 1];
	memcpy(text, view_row(r), COLS);
	text[COLS] = '\0';
	for (int i = 0; i < COLS; i++) {
		if (!text[i]) {
			text[i] = ' ';
		}
	}
	gfx_text(c, MARGIN, y, text, FG, 1);
	if (back && r == 0) {
		char mark[48];
		snprintf(mark, sizeof(mark), " %d lines back (PgDn) ", back);
		int width = gfx_text_width(mark, 1), x = c->width - MARGIN - width;
		gfx_fill(c, (struct gfx_rect){ x, y, width, GFX_CELL_H }, CURSOR);
		gfx_text(c, x, y, mark, BG, 1);
	}
	if (!back && r == row && col < COLS) {
		struct gfx_rect cur = { MARGIN + col * GFX_CELL_W, y + GFX_CELL_H - 2, GFX_CELL_W, 2 };
		if (!focused) {
			gfx_outline(c, (struct gfx_rect){ cur.x, y, GFX_CELL_W, GFX_CELL_H }, CURSOR);
		} else {
			gfx_fill(c, cur, CURSOR);
		}
	}
}

static void flush(void)
{
	if (dirty_last < 0) {
		return;
	}
	for (int r = dirty_first; r <= dirty_last; r++) {
		draw_row(r);
	}
	win_flush(w, MARGIN + dirty_first * GFX_CELL_H, (dirty_last - dirty_first + 1) * GFX_CELL_H);
	dirty_first = ROWS;
	dirty_last = -1;
}

static void newline(void)
{
	col = 0;
	if (row < ROWS - 1) {
		row++;
		return;
	}
	memcpy(history[history_next], cells[0], COLS);
	history_next = (history_next + 1) % HISTORY;
	if (history_count < HISTORY) {
		history_count++;
	}
	memmove(cells[0], cells[1], sizeof(cells[0]) * (ROWS - 1));
	memset(cells[ROWS - 1], 0, sizeof(cells[0]));
	dirty(0);
	dirty(ROWS - 1);
}

/* Scrolls the view to `lines` back (0: the live screen). */
static void scroll_to(int lines)
{
	lines = lines < 0 ? 0 : lines > history_count ? history_count : lines;
	if (lines != back) {
		back = lines;
		dirty(0);
		dirty(ROWS - 1);
	}
}

/* Shows one byte of output. */
static void put(unsigned char ch)
{
	scroll_to(0);
	dirty(row);
	if (ch == '\n') {
		newline();
	} else if (ch == '\r') {
		col = 0;
	} else if (ch == '\b') {
		if (col > 0) {
			col--;
		}
	} else if (ch == '\t') {
		col = (col + 8) & ~7;
		if (col >= COLS) {
			newline();
		}
	} else if (ch >= ' ' && ch <= '~') {
		if (col == COLS) {
			newline();
		}
		cells[row][col++] = (char)ch;
	}
	dirty(row);
}

static void put_string(const char *s)
{
	while (*s) {
		put((unsigned char)*s++);
	}
}

static void key(int k)
{
	if (k == ZKT_KEY_PGUP || k == ZKT_KEY_PGDN) {
		scroll_to(back + (k == ZKT_KEY_PGUP ? ROWS / 2 : -ROWS / 2));
		return;
	}
	scroll_to(0);
	if (k == '\b' || k == 0x7F) {
		if (line_len) {
			line_len--;
			put_string("\b \b");
		}
	} else if (k == 0x15) { /* ^U */
		while (line_len) {
			line_len--;
			put_string("\b \b");
		}
	} else if (k == 0x04) { /* ^D */
		if (!line_len) {
			write(to_sh, "", 0); /* end of file */
		}
	} else if (k == '\n' || k == '\r') {
		put('\n');
		line[line_len++] = '\n';
		write(to_sh, line, (size_t)line_len);
		line_len = 0;
	} else if (k >= ' ' && k <= '~' && line_len < LINE_MAX - 1) {
		line[line_len++] = (char)k;
		put((unsigned char)k);
	}
}

/* sh with its standard input and output (and errors) on pipes to us. */
static int start_shell(void)
{
	int in[2], out[2];
	if (pipe(in) < 0) {
		return -1;
	}
	if (pipe(out) < 0) {
		close(in[0]);
		close(in[1]);
		return -1;
	}
	int saved[3] = { dup(0), dup(1), dup(2) };
	dup2(in[1], 0);
	dup2(out[1], 1);
	dup2(out[1], 2);
	char *argv[] = { "sh", NULL };
	sh_pid = spawn("/bin/sh", argv);
	for (int fd = 0; fd < 3; fd++) {
		dup2(saved[fd], fd);
		close(saved[fd]);
	}
	close(in[1]);
	close(out[1]);
	to_sh = in[0];
	from_sh = out[0];
	return sh_pid < 0 ? -1 : 0;
}

int main(void)
{
	w = win_open("Terminal", COLS * GFX_CELL_W + 2 * MARGIN, ROWS * GFX_CELL_H + 2 * MARGIN);
	if (!w) {
		fprintf(stderr, "term: no window: %s\n", strerror(errno));
		return 1;
	}
	gfx_fill(w->canvas, (struct gfx_rect){ 0, 0, w->width, w->height }, BG);
	win_flush(w, 0, w->height);
	if (start_shell() < 0) {
		put_string("term: cannot start /bin/sh\n");
	}
	flush();

	bool done = false;
	while (!done) {
		struct zkt_pollfd pf[2] = { { win_fd(w), 0 }, { from_sh, 0 } };
		poll(pf, from_sh >= 0 ? 2 : 1, -1);
		if (from_sh >= 0 && pf[1].ready) {
			char buf[512];
			long n = read(from_sh, buf, sizeof(buf));
			if (n <= 0) {
				done = true; /* the shell (and all it started) is gone */
			}
			for (long i = 0; i < n; i++) {
				put((unsigned char)buf[i]);
			}
		}
		struct win_event e;
		int got;
		while (!done && (got = win_next(w, &e, 0)) != 0) {
			if (got < 0 || e.type == WIN_CLOSE) {
				done = true;
			} else if (e.type == WIN_KEY) {
				key(e.key);
			} else if (e.type == WIN_FOCUS) {
				focused = e.focused;
				dirty(row);
			}
		}
		flush();
	}
	/* The shell reads end of file and exits when it can; it is not
	 * waited for (a program it runs may take a while). */
	if (to_sh >= 0) {
		write(to_sh, "", 0);
		close(to_sh);
	}
	win_close(w);
	return 0;
}
