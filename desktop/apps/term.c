/* term [COMMAND ...] -- a terminal window running sh (DESIGN.md §5).
 *
 * As many cells of the ManiOS font as its pane holds; when ManiDE
 * resizes the pane, the text stays where it was and lines that no
 * longer fit go into the scroll-back. It understands the ANSI escape
 * sequences programs such as fetch and top use: SGR colours (16, and
 * bold as bright), cursor position and movement, erasing, hiding the
 * cursor, and two queries it answers on the program's standard input:
 * the cursor's position (ESC [ 6 n) and the size in cells (ESC [ 18 t,
 * answered ESC [ 8 ; ROWS ; COLS t).
 *
 * The shell's standard input and output are pipes; lines are edited
 * here (Backspace, ^U) and sent whole, as the console's line discipline
 * does for programs on /dev/cons. ^D on an empty line sends end of
 * file. PgUp and PgDn look back over the last HISTORY lines that
 * scrolled off the top; typing, or more output, comes back to the live
 * screen. With a COMMAND, term types it into the shell once the shell
 * is ready (`term top`). The window closes when the shell exits, or when
 * ManiDE asks (Alt+q). */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <win.h>

#define MAX_COLS 160
#define MAX_ROWS 64
#define MARGIN 4
#define LINE_MAX 255
#define HISTORY 256
#define PARAMS 8

#define BG     GFX_RGB(0x0F, 0x13, 0x19)
#define FG     GFX_RGB(0xD8, 0xDE, 0xE9)
#define CURSOR GFX_RGB(0xE0, 0x7A, 0x2E)

/* ANSI's sixteen colours: black red green yellow blue magenta cyan
 * white, then their bright forms. */
static const gfx_color PALETTE[16] = {
	GFX_RGB(0x2A, 0x30, 0x3A), GFX_RGB(0xE0, 0x6C, 0x75), GFX_RGB(0x98, 0xC3, 0x79),
	GFX_RGB(0xE5, 0xC0, 0x7B), GFX_RGB(0x61, 0xAF, 0xEF), GFX_RGB(0xC6, 0x78, 0xDD),
	GFX_RGB(0x56, 0xB6, 0xC2), GFX_RGB(0xAB, 0xB2, 0xBF), GFX_RGB(0x5C, 0x63, 0x70),
	GFX_RGB(0xFF, 0x7B, 0x86), GFX_RGB(0xB5, 0xE8, 0x90), GFX_RGB(0xF2, 0xA6, 0x4B),
	GFX_RGB(0x82, 0xC4, 0xFF), GFX_RGB(0xDD, 0xA0, 0xF0), GFX_RGB(0x7F, 0xDC, 0xE6),
	GFX_RGB(0xFF, 0xFF, 0xFF),
};
#define DEFAULT 16 /* FG or BG */

struct cell {
	char ch;       /* 0: never written */
	uint8_t fg, bg; /* PALETTE index, or DEFAULT */
};

static struct win *w;
static int cols, rows;
static struct cell cells[MAX_ROWS][MAX_COLS];
static struct cell history[HISTORY][MAX_COLS]; /* lines scrolled off the top: a ring */
static int history_count, history_next;
static int back;                    /* how far the view is scrolled back */
static int row, col;
static uint8_t fg = DEFAULT, bg = DEFAULT;
static bool bold, cursor_shown = true;
static bool redraw_all;
static int dirty_first = MAX_ROWS, dirty_last = -1;
static int to_sh = -1, from_sh = -1, sh_pid;
static char line[LINE_MAX + 1];
static int line_len;
static int focused = 1;
static char pending[LINE_MAX + 1];  /* the command to type once sh is ready */

static enum { TEXT, ESCAPE, CSI } esc;
static int params[PARAMS], nparams;
static bool private_mode;

static void dirty(int r)
{
	dirty_first = r < dirty_first ? r : dirty_first;
	dirty_last = r > dirty_last ? r : dirty_last;
}

static void dirty_all(void)
{
	dirty(0);
	dirty(rows - 1);
}

/* What row r of the window shows: a line from the history while the
 * view is scrolled back that far, otherwise one of the screen's. */
static const struct cell *view_row(int r)
{
	if (r < back) {
		int oldest = (history_next - history_count + HISTORY) % HISTORY;
		return history[(oldest + history_count - back + r) % HISTORY];
	}
	return cells[r - back];
}

static gfx_color colour(uint8_t index, bool is_bg)
{
	return index == DEFAULT ? (is_bg ? BG : FG) : PALETTE[index];
}

static void draw_row(int r)
{
	struct gfx_canvas *c = w->canvas;
	int y = MARGIN + r * GFX_CELL_H;
	gfx_fill(c, (struct gfx_rect){ 0, y, c->width, GFX_CELL_H }, BG);
	const struct cell *cs = view_row(r);
	/* Runs of cells with the same colours, drawn as one string. */
	for (int i = 0; i < cols;) {
		int start = i;
		char text[MAX_COLS + 1];
		int n = 0;
		while (i < cols && cs[i].fg == cs[start].fg && cs[i].bg == cs[start].bg) {
			text[n++] = cs[i].ch ? cs[i].ch : ' ';
			i++;
		}
		text[n] = '\0';
		int x = MARGIN + start * GFX_CELL_W;
		if (cs[start].bg != DEFAULT) {
			gfx_fill(c, (struct gfx_rect){ x, y, n * GFX_CELL_W, GFX_CELL_H }, colour(cs[start].bg, true));
		}
		gfx_text(c, x, y, text, colour(cs[start].fg, false), 1);
	}
	if (back && r == 0) {
		char mark[48];
		snprintf(mark, sizeof(mark), " %d lines back (PgDn) ", back);
		int width = gfx_text_width(mark, 1), x = c->width - MARGIN - width;
		gfx_fill(c, (struct gfx_rect){ x, y, width, GFX_CELL_H }, CURSOR);
		gfx_text(c, x, y, mark, BG, 1);
	}
	if (!back && cursor_shown && r == row && col < cols) {
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
	if (redraw_all) {
		gfx_fill(w->canvas, (struct gfx_rect){ 0, 0, w->width, w->height }, BG);
		for (int r = 0; r < rows; r++) {
			draw_row(r);
		}
		win_flush(w, 0, w->height);
		redraw_all = false;
		dirty_first = MAX_ROWS;
		dirty_last = -1;
		return;
	}
	if (dirty_last < 0) {
		return;
	}
	for (int r = dirty_first; r <= dirty_last && r < rows; r++) {
		draw_row(r);
	}
	win_flush(w, MARGIN + dirty_first * GFX_CELL_H, (dirty_last - dirty_first + 1) * GFX_CELL_H);
	dirty_first = MAX_ROWS;
	dirty_last = -1;
}

static void clear_cells(struct cell *c, int n)
{
	for (int i = 0; i < n; i++) {
		c[i] = (struct cell){ 0, DEFAULT, bg };
	}
}

/* The top line goes into the history and the rest move up. */
static void scroll_up(void)
{
	memcpy(history[history_next], cells[0], sizeof(cells[0]));
	history_next = (history_next + 1) % HISTORY;
	if (history_count < HISTORY) {
		history_count++;
	}
	memmove(cells[0], cells[1], sizeof(cells[0]) * (size_t)(rows - 1));
	clear_cells(cells[rows - 1], MAX_COLS);
	dirty_all();
}

static void newline(void)
{
	col = 0;
	if (row < rows - 1) {
		row++;
	} else {
		scroll_up();
	}
}

/* Scrolls the view to `lines` back (0: the live screen). */
static void scroll_to(int lines)
{
	lines = lines < 0 ? 0 : lines > history_count ? history_count : lines;
	if (lines != back) {
		back = lines;
		dirty_all();
	}
}

/* The window has a new size: as many cells as fit. The cursor's line
 * stays on screen, the lines above it going into the history. */
static void resize(void)
{
	int new_cols = (w->width - 2 * MARGIN) / GFX_CELL_W;
	int new_rows = (w->height - 2 * MARGIN) / GFX_CELL_H;
	new_cols = new_cols < 1 ? 1 : new_cols > MAX_COLS ? MAX_COLS : new_cols;
	new_rows = new_rows < 1 ? 1 : new_rows > MAX_ROWS ? MAX_ROWS : new_rows;
	while (row >= new_rows) {
		scroll_up();
		row--;
	}
	for (int r = rows; r < new_rows; r++) {
		clear_cells(cells[r], MAX_COLS);
	}
	cols = new_cols;
	rows = new_rows;
	if (col > cols) {
		col = cols;
	}
	back = 0;
	redraw_all = true;
}

static void reply(const char *s)
{
	if (to_sh >= 0) {
		write(to_sh, s, strlen(s));
	}
}

static int param(int i, int fallback)
{
	return i < nparams && params[i] > 0 ? params[i] : fallback;
}

static void select_graphic_rendition(void)
{
	if (nparams == 0) {
		params[nparams++] = 0;
	}
	for (int i = 0; i < nparams; i++) {
		int p = params[i];
		if (p == 0) {
			fg = bg = DEFAULT;
			bold = false;
		} else if (p == 1) {
			bold = true;
		} else if (p == 22) {
			bold = false;
		} else if (p >= 30 && p <= 37) {
			fg = (uint8_t)(p - 30);
		} else if (p >= 90 && p <= 97) {
			fg = (uint8_t)(p - 90 + 8);
		} else if (p == 39) {
			fg = DEFAULT;
		} else if (p >= 40 && p <= 47) {
			bg = (uint8_t)(p - 40);
		} else if (p >= 100 && p <= 107) {
			bg = (uint8_t)(p - 100 + 8);
		} else if (p == 49) {
			bg = DEFAULT;
		}
	}
}

static void control_sequence(char final)
{
	char answer[32];
	int n = param(0, 1);
	if (private_mode) {
		if ((final == 'h' || final == 'l') && param(0, 0) == 25) {
			cursor_shown = final == 'h';
			dirty(row);
		}
		return;
	}
	switch (final) {
	case 'm':
		select_graphic_rendition();
		return;
	case 'H':
	case 'f':
		row = param(0, 1) > rows ? rows - 1 : param(0, 1) - 1;
		col = param(1, 1) > cols ? cols - 1 : param(1, 1) - 1;
		break;
	case 'A':
		row = n > row ? 0 : row - n;
		break;
	case 'B':
		row = row + n >= rows ? rows - 1 : row + n;
		break;
	case 'C':
		col = col + n >= cols ? cols - 1 : col + n;
		break;
	case 'D':
		col = n > col ? 0 : col - n;
		break;
	case 'G':
		col = n > cols ? cols - 1 : n - 1;
		break;
	case 'J': /* 0: to the end, 1: from the start, 2: all */
		if (param(0, 0) == 0) {
			clear_cells(&cells[row][col], MAX_COLS - col);
			for (int r = row + 1; r < rows; r++) {
				clear_cells(cells[r], MAX_COLS);
			}
		} else {
			int last = param(0, 0) == 1 ? row : rows - 1;
			for (int r = 0; r <= last; r++) {
				clear_cells(cells[r], r == row && param(0, 0) == 1 ? col + 1 : MAX_COLS);
			}
		}
		dirty_all();
		return;
	case 'K': /* the same, within the line */
		if (param(0, 0) == 0) {
			clear_cells(&cells[row][col], MAX_COLS - col);
		} else {
			clear_cells(cells[row], param(0, 0) == 1 ? col + 1 : MAX_COLS);
		}
		break;
	case 'n':
		if (param(0, 0) == 6) {
			snprintf(answer, sizeof(answer), "\033[%d;%dR", row + 1, col + 1);
			reply(answer);
		}
		return;
	case 't':
		if (param(0, 0) == 18) {
			snprintf(answer, sizeof(answer), "\033[8;%d;%dt", rows, cols);
			reply(answer);
		}
		return;
	default:
		return;
	}
	dirty(row);
}

/* Returns whether ch belonged to an escape sequence. */
static bool escape(unsigned char ch)
{
	if (esc == TEXT) {
		if (ch != 0x1B) {
			return false;
		}
		esc = ESCAPE;
		return true;
	}
	if (esc == ESCAPE) {
		esc = ch == '[' ? CSI : TEXT;
		nparams = 0;
		private_mode = false;
		return true;
	}
	if (ch >= '0' && ch <= '9') {
		if (nparams == 0) {
			params[nparams++] = 0;
		}
		int *p = &params[nparams - 1];
		*p = *p < 10000 ? *p * 10 + (ch - '0') : *p;
	} else if (ch == ';') {
		if (nparams == 0) {
			params[nparams++] = 0;
		}
		if (nparams < PARAMS) {
			params[nparams++] = 0;
		}
	} else if (ch == '?') {
		private_mode = true;
	} else if (ch >= 0x40 && ch <= 0x7E) {
		dirty(row);
		control_sequence((char)ch);
		dirty(row);
		esc = TEXT;
	} else if (ch < 0x20 || ch > 0x3F) {
		esc = TEXT;
	}
	return true;
}

/* Shows one byte of output. */
static void put(unsigned char ch)
{
	scroll_to(0);
	if (escape(ch)) {
		return;
	}
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
		if (col >= cols) {
			newline();
		}
	} else if (ch >= ' ' && ch <= '~') {
		if (col >= cols) {
			newline();
		}
		uint8_t f = bold && fg < 8 ? (uint8_t)(fg + 8) : fg;
		cells[row][col++] = (struct cell){ (char)ch, f, bg };
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
		scroll_to(back + (k == ZKT_KEY_PGUP ? rows / 2 : -rows / 2));
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

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (i > 1) {
			strlcat(pending, " ", sizeof(pending));
		}
		strlcat(pending, argv[i], sizeof(pending));
	}
	w = win_open(pending[0] ? pending : "Terminal", 80 * GFX_CELL_W + 2 * MARGIN,
	             24 * GFX_CELL_H + 2 * MARGIN);
	if (!w) {
		fprintf(stderr, "term: no window: %s\n", strerror(errno));
		return 1;
	}
	rows = 0;
	resize();
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
			if (n > 0 && pending[0]) {
				/* The shell has prompted: type the command. */
				for (char *p = pending; *p; p++) {
					key((unsigned char)*p);
				}
				key('\n');
				pending[0] = '\0';
			}
		}
		struct win_event e;
		int got;
		while (!done && (got = win_next(w, &e, 0)) != 0) {
			if (got < 0 || e.type == WIN_CLOSE) {
				done = true;
			} else if (e.type == WIN_KEY && !e.alt) {
				key(e.key);
			} else if (e.type == WIN_FOCUS) {
				focused = e.focused;
				dirty(row);
			} else if (e.type == WIN_RESIZE) {
				resize();
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
