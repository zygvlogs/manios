/* Keyboard and mouse (DESIGN.md §3). The desktop keeps F1 (the menu)
 * and F2 (the next window); other keys go to the focused window. */
#include "desktop.h"
#include <manios.h>

#define KEY_ESCAPE 0x1B

static struct window *window_at(int x, int y)
{
	for (int i = D.count - 1; i >= 0; i--) {
		if (gfx_contains(frame_rect(D.stack[i]), x, y)) {
			return D.stack[i];
		}
	}
	return NULL;
}

static void set_menu(bool open)
{
	if (D.menu_open != open) {
		D.menu_open = open;
		D.menu_sel = 0;
		damage(menu_rect());
		damage(menu_button_rect());
	}
}

static void activate(int i)
{
	set_menu(false);
	if (!MENU[i].label) {
		return;
	}
	if (MENU[i].program) {
		launch(MENU[i].program);
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

void handle_key(int key)
{
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
	if (key == ZKT_KEY_F1 + 1) {
		/* F2: the bottom window comes to the top. */
		if (D.count > 1) {
			raise_window(D.stack[0]);
		}
		return;
	}
	if (D.focus) {
		post_event(D.focus, "k %d", key);
	}
}

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
	if (y < PANEL_H) {
		if (gfx_contains(menu_button_rect(), x, y)) {
			set_menu(true);
			return;
		}
		struct window *list[MAX_WINDOWS];
		int n = windows_by_id(list);
		for (int i = 0; i < n; i++) {
			if (gfx_contains(window_button_rect(i, n), x, y)) {
				raise_window(list[i]);
			}
		}
		return;
	}
	struct window *w = window_at(x, y);
	if (!w) {
		return;
	}
	raise_window(w);
	if (gfx_contains(close_rect(w), x, y)) {
		post_event(w, "c");
	} else if (gfx_contains(title_rect(w), x, y) || !gfx_contains(w->content, x, y)) {
		D.drag = w; /* the title bar, or the frame */
		D.drag_dx = x - w->content.x;
		D.drag_dy = y - w->content.y;
	} else {
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

	if (D.drag) {
		struct window *w = D.drag;
		move_window(w, x - D.drag_dx, y - D.drag_dy);
		if (released & 1) {
			D.drag = NULL;
			say("window %d moved to %d,%d", w->id, w->content.x, w->content.y);
		}
		return;
	}
	if (pressed & 1) {
		click(x, y);
	} else if (pressed && !D.grab) {
		/* Other buttons go to the window under the pointer. */
		struct window *w = window_at(x, y);
		if (w && gfx_contains(w->content, x, y)) {
			raise_window(w);
			D.grab = w;
		}
	}
	struct window *to = D.grab;
	if (!to && moved && D.focus && gfx_contains(D.focus->content, x, y)
	    && window_at(x, y) == D.focus) {
		to = D.focus; /* hovering over the focused window */
	}
	if (to && (moved || pressed || released)) {
		post_event(to, "m %d %d %d", x - to->content.x, y - to->content.y, buttons);
	}
	if (!buttons) {
		D.grab = NULL;
	}
}
