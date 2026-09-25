/* desktop: the ManiOS compositor, window manager, panel and launcher
 * (M12). Design: docs/desktop/DESIGN.md. One process:
 *
 *   main.c   start-up, the main loop, launching programs, exit
 *   wsys.c   the /dev/wsys file server (zrpsrv.h): windows as files
 *   draw.c   composition: background, windows, panel, menu, pointer
 *   input.c  keyboard and mouse: focus, raise, move, close, the menu
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include <gfx.h>
#include <stdbool.h>
#include <zrpsrv.h>

#define PANEL_H 22
#define TITLE_H 18
#define BORDER 1
#define CLOSE_BOX 12
#define MAX_WINDOWS 16
#define TITLE_MAX 48
#define EVENT_QUEUE 1024
#define MIN_SIZE 16

/* The palette (DESIGN.md §6). */
#define C_BG_TOP     GFX_RGB(0x1B, 0x3A, 0x4B)
#define C_BG_BOTTOM  GFX_RGB(0x0E, 0x1A, 0x24)
#define C_PANEL      GFX_RGB(0x1E, 0x24, 0x30)
#define C_ACCENT     GFX_RGB(0xE0, 0x7A, 0x2E)
#define C_INACTIVE   GFX_RGB(0x5A, 0x62, 0x70)
#define C_FRAME      GFX_RGB(0x0B, 0x0F, 0x14)
#define C_MENU       GFX_RGB(0x26, 0x2D, 0x3A)
#define C_BUTTON     GFX_RGB(0x32, 0x3A, 0x4A)
#define C_DARK_TEXT  GFX_RGB(0x1A, 0x1A, 0x1A)
#define C_LIGHT_TEXT GFX_RGB(0xE8, 0xE8, 0xE8)

struct window {
	int id;
	char title[TITLE_MAX + 1];
	struct gfx_rect content; /* where the image is on screen */
	struct gfx_canvas *image;
	struct zsrv_node *dir, *event_node;
	struct zsrv_fid *owner;  /* the fid on `new` that made it */
	char events[EVENT_QUEUE];
	int nevents;             /* bytes queued: whole lines */
};

struct menu_item {
	const char *label;
	const char *program; /* NULL: a separator, or "Exit desktop" if label is set */
};

struct desktop {
	int width, height;
	struct gfx_screen screen;
	struct gfx_canvas *back;
	struct zsrv *srv;
	int kbd, mouse, time;

	struct window *stack[MAX_WINDOWS]; /* bottom first */
	int count;
	struct window *focus;
	int next_id, placed;

	struct gfx_rect damage;

	int px, py, buttons;           /* the pointer */
	struct window *drag;           /* moved by its title bar */
	int drag_dx, drag_dy;
	struct window *grab;           /* gets the pointer while a button is held */

	bool menu_open;
	int menu_sel;
	char clock[16];

	bool quit;
};

extern struct desktop D;
extern const struct menu_item MENU[];
extern const int MENU_COUNT;

/* main.c */
void launch(const char *program);
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* wsys.c */
int wsys_init(int fd);
void window_destroy(struct window *w);
void post_event(struct window *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void set_focus(struct window *w);
void raise_window(struct window *w);
void move_window(struct window *w, int x, int y);

/* draw.c */
void damage(struct gfx_rect r);
void damage_all(void);
void compose(void);
struct gfx_rect frame_rect(const struct window *w);
struct gfx_rect title_rect(const struct window *w);
struct gfx_rect close_rect(const struct window *w);
struct gfx_rect cursor_rect(void);
struct gfx_rect menu_button_rect(void);
struct gfx_rect menu_item_rect(int i);
struct gfx_rect menu_rect(void);
/* The panel's button for the i-th window by number; count through *n. */
struct gfx_rect window_button_rect(int i, int n);
/* The windows in order of their numbers (the panel's order). */
int windows_by_id(struct window **out);
void update_clock(void);

/* input.c */
void handle_key(int key);
void handle_mouse(int dx, int dy, int buttons);

#endif
