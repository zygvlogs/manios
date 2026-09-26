/* Keys and the mouse (DESIGN.md §3). ManiDE keeps Alt with these keys,
 * and F1; everything else goes to the focused window:
 *
 *   Alt+Enter   a terminal              Alt+d        the run prompt
 *   Alt+1..9    show a workspace        Alt+Shift+N  move the window there
 *   Alt+j, Tab  the next window         Alt+k        the previous one
 *   Alt+J, K    move the window along   Alt+h, l     narrow, widen (tall)
 *   Alt+Space   the next layout         Alt+q        close the window
 *   Alt+Shift+Q exit ManiDE             F1           the menu
 *
 * The mouse focuses the pane it clicks, closes one from its strip's x,
 * and picks a workspace, or the menu, from the bar. */
#include "manide.h"
#include <manios.h>
#include <stdio.h>
#include <string.h>

#define KEY_ESCAPE 0x1B
#define KEY_TAB '\t'
/* Alt+Shift+1..9 on a US keyboard. */
static const char SHIFTED_DIGITS[] = "!@#$%^&*(";

static void set_menu(bool open)
{
	if (D.menu_open != open) {
		D.menu_open = open;
		D.menu_sel = 0;
		damage(menu_rect());
		damage(bar_logo_rect());
	}
}

static void activate(int i)
{
	set_menu(false);
	if (!MENU[i].label) {
		return;
	}
	if (MENU[i].command) {
		launch(MENU[i].command);
	} else {
		D.quit = true;
	}
}

static void menu_step(int delta)
{
	int i = D.menu_sel;
	do {
		i = (i + delta + MENU_COUNT) % MENU_COUNT;
	} while (!MENU[i].label);
	damage(menu_item_rect(D.menu_sel));
	D.menu_sel = i;
	damage(menu_item_rect(i));
}

/* --- the run prompt --- */

/* The programs in /bin whose names start with prefix: how many, the
 * longest start they share (into common), and a list for the hint. */
static int complete(const char *prefix, char *common, size_t common_size, char *list,
                    size_t list_size)
{
	int fd = open("/bin", OREAD), n = 0;
	size_t plen = strlen(prefix);
	struct zkt_dirent e;
	list[0] = common[0] = '\0';
	while (fd >= 0 && read(fd, &e, sizeof(e)) == sizeof(e)) {
		if (strncmp(e.name, prefix, plen)) {
			continue;
		}
		if (n++ == 0) {
			strlcpy(common, e.name, common_size);
		} else {
			size_t k = 0;
			while (common[k] && common[k] == e.name[k]) {
				k++;
			}
			common[k] = '\0';
		}
		if (strlen(list) + strlen(e.name) + 2 < list_size) {
			if (list[0]) {
				strlcat(list, " ", list_size);
			}
			strlcat(list, e.name, list_size);
		}
	}
	if (fd >= 0) {
		close(fd);
	}
	return n;
}

static void update_hint(void)
{
	char common[PROMPT_MAX + 1];
	D.hint[0] = '\0';
	if (D.prompt[0] && !strchr(D.prompt, ' ')) {
		complete(D.prompt, common, sizeof(common), D.hint, sizeof(D.hint));
	}
	damage_bar();
}

void open_prompt(void)
{
	set_menu(false);
	D.prompt_open = true;
	D.prompt[0] = '\0';
	update_hint();
}

static void close_prompt(void)
{
	D.prompt_open = false;
	D.hint[0] = '\0';
	damage_bar();
}

static void prompt_key(int key)
{
	size_t len = strlen(D.prompt);
	if (key == KEY_ESCAPE) {
		close_prompt();
	} else if (key == '\n' || key == '\r') {
		char command[PROMPT_MAX + 1];
		strlcpy(command, D.prompt, sizeof(command));
		close_prompt();
		if (command[0]) {
			launch(command);
		}
	} else if (key == '\b' || key == 0x7F) {
		if (len) {
			D.prompt[len - 1] = '\0';
		}
	} else if (key == 0x15) { /* ^U */
		D.prompt[0] = '\0';
	} else if (key == KEY_TAB) {
		char common[PROMPT_MAX + 1], list[8];
		if (D.prompt[0] && !strchr(D.prompt, ' ')) {
			int n = complete(D.prompt, common, sizeof(common), list, sizeof(list));
			if (n >= 1) {
				strlcpy(D.prompt, common, sizeof(D.prompt));
			}
			if (n == 1 && strlen(D.prompt) < PROMPT_MAX) {
				strlcat(D.prompt, " ", sizeof(D.prompt));
			}
		}
	} else if (key >= ' ' && key <= '~' && len < PROMPT_MAX) {
		D.prompt[len] = (char)key;
		D.prompt[len + 1] = '\0';
	}
	if (D.prompt_open) {
		update_hint();
	}
}

/* --- keys --- */

static bool alt_binding(int key)
{
	const char *shifted = key ? strchr(SHIFTED_DIGITS, key) : NULL;
	if (key >= '1' && key <= '9') {
		show_workspace(key - '1');
	} else if (shifted) {
		if (D.focus) {
			send_to_workspace(D.focus, (int)(shifted - SHIFTED_DIGITS));
		}
	} else if (key == '\n' || key == '\r') {
		launch("term");
	} else if (key == 'd') {
		open_prompt();
	} else if (key == 'j' || key == KEY_TAB) {
		focus_step(1);
	} else if (key == 'k') {
		focus_step(-1);
	} else if (key == 'J') {
		swap_step(1);
	} else if (key == 'K') {
		swap_step(-1);
	} else if (key == 'h') {
		grow_master(-5);
	} else if (key == 'l') {
		grow_master(5);
	} else if (key == ' ') {
		next_layout();
	} else if (key == 'q') {
		if (D.focus) {
			post_event(D.focus, "c");
		}
	} else if (key == 'Q') {
		D.quit = true;
	} else {
		return false;
	}
	return true;
}

void handle_key(int key, bool alt)
{
	if (D.prompt_open) {
		prompt_key(key);
		return;
	}
	if (key == ZKT_KEY_F1) {
		set_menu(!D.menu_open);
		return;
	}
	if (D.menu_open) {
		if (key == ZKT_KEY_DOWN) {
			menu_step(1);
		} else if (key == ZKT_KEY_UP) {
			menu_step(-1);
		} else if (key == '\n' || key == '\r') {
			activate(D.menu_sel);
		} else if (key == KEY_ESCAPE) {
			set_menu(false);
		}
		return;
	}
	if (alt && alt_binding(key)) {
		return;
	}
	if (D.focus) {
		post_event(D.focus, alt ? "k %d a" : "k %d", key);
	}
}

/* --- the mouse --- */

/* A left click at (x, y). */
static void click(int x, int y)
{
	if (D.menu_open) {
		for (int i = 0; i < MENU_COUNT; i++) {
			if (gfx_contains(menu_item_rect(i), x, y)) {
				activate(i);
				return;
			}
		}
		set_menu(false); /* a click anywhere else only closes it */
		return;
	}
	if (y < BAR_H) {
		if (gfx_contains(bar_logo_rect(), x, y)) {
			set_menu(true);
		}
		for (int i = 0; i < WORKSPACES; i++) {
			if (gfx_contains(bar_ws_rect(i), x, y)) {
				show_workspace(i);
			}
		}
		return;
	}
	struct window *w = window_at(x, y);
	if (!w) {
		return;
	}
	set_focus(w);
	if (gfx_contains(close_rect(w), x, y)) {
		post_event(w, "c");
	} else if (gfx_contains(w->content, x, y)) {
		D.grab = w;
	}
}

void handle_mouse(int dx, int dy, int buttons)
{
	int x = D.px + dx, y = D.py + dy;
	x = x < 0 ? 0 : x >= D.width ? D.width - 1 : x;
	y = y < 0 ? 0 : y >= D.height ? D.height - 1 : y;
	bool moved = x != D.px || y != D.py;
	if (moved) {
		damage(cursor_rect());
		D.px = x;
		D.py = y;
		damage(cursor_rect());
	}
	int pressed = buttons & ~D.buttons, released = D.buttons & ~buttons;
	D.buttons = buttons;

	if (pressed & 1) {
		click(x, y);
	} else if (pressed && !D.grab) {
		/* Other buttons go to the window under the pointer. */
		struct window *w = window_at(x, y);
		if (w && gfx_contains(w->content, x, y)) {
			set_focus(w);
			D.grab = w;
		}
	}
	struct window *to = D.grab;
	if (!to && moved && D.focus && D.focus->shown && gfx_contains(D.focus->content, x, y)) {
		to = D.focus; /* hovering over the focused window */
	}
	if (to && (moved || pressed || released)) {
		post_event(to, "m %d %d %d", x - to->content.x, y - to->content.y, buttons);
	}
	if (!buttons) {
		D.grab = NULL;
	}
}
