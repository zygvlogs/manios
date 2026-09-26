/* ManiDE: the ManiOS desktop (M12, M18) -- a tiling window manager,
 * its compositor, status bar, menu and run prompt. Design:
 * docs/desktop/DESIGN.md. One process:
 *
 *   main.c   start-up, the session, the main loop, launching, exit
 *   wsys.c   the /dev/wsys file server (zrpsrv.h): windows as files
 *   tile.c   workspaces, layouts, focus
 *   bar.c    the status bar's facts: time, CPU, memory, network
 *   draw.c   composition: bar, panes, menu, prompt, pointer
 *   input.c  keys (Alt bindings) and the mouse
 */
#ifndef MANIDE_H
#define MANIDE_H

#include <gfx.h>
#include <stdbool.h>
#include <zrpsrv.h>

#define BAR_H 18        /* the status bar */
#define HEAD_H 15       /* a pane's title strip */
#define GAP 4           /* between panes, and around them */
#define CLOSE_W 11      /* the close mark at a strip's right end */
#define MAX_WINDOWS 16
#define WORKSPACES 9
#define TITLE_MAX 48
#define EVENT_QUEUE 1024
#define MIN_SIZE 16
#define PROMPT_MAX 60

/* The palette (DESIGN.md §6): dark, cyan for the focus, amber, the
 * ManiOS mark, for what is current. */
#define C_DESK      GFX_RGB(0x0A, 0x0D, 0x12)
#define C_BAR       GFX_RGB(0x12, 0x16, 0x1E)
#define C_PANE      GFX_RGB(0x0F, 0x13, 0x19)
#define C_HEAD      GFX_RGB(0x17, 0x1C, 0x25)
#define C_EDGE      GFX_RGB(0x2A, 0x31, 0x3C)
#define C_FOCUS     GFX_RGB(0x3F, 0xC8, 0xD8)
#define C_ACCENT    GFX_RGB(0xE0, 0x7A, 0x2E)
#define C_TEXT      GFX_RGB(0xD8, 0xDE, 0xE9)
#define C_DIM       GFX_RGB(0x6B, 0x74, 0x83)
#define C_DARK      GFX_RGB(0x10, 0x12, 0x16)
#define C_GREEN     GFX_RGB(0x8F, 0xC8, 0x6A)
#define C_MENU      GFX_RGB(0x1A, 0x20, 0x2A)

enum layout { LAYOUT_GRID, LAYOUT_TALL, LAYOUT_MONO, LAYOUTS };

struct window {
	int id;
	char title[TITLE_MAX + 1];
	int ws;                  /* its workspace, 0 to WORKSPACES - 1 */
	bool shown;              /* on the workspace in view, and tiled */
	struct gfx_rect pane;    /* its place on screen: edge, strip, content */
	struct gfx_rect content; /* where its image goes */
	int told_w, told_h;      /* the size last offered ("r W H") */
	struct gfx_canvas *image; /* its pixels, at the size it chose */
	struct zsrv_node *dir, *image_node, *event_node;
	struct zsrv_fid *owner;  /* the fid on `new` that made it */
	char events[EVENT_QUEUE];
	int nevents;             /* bytes queued: whole lines */
};

struct workspace {
	enum layout layout;
	int master;               /* LAYOUT_TALL: the first pane's share, % */
	struct window *focus;     /* the focus when last shown */
};

struct menu_item {
	const char *label;
	const char *command; /* NULL: a separator, or "Exit ManiDE" if label is set */
};

struct desktop {
	int width, height;
	struct gfx_screen screen;
	struct gfx_canvas *back;
	struct zsrv *srv;
	int kbd, mouse;

	struct window *order[MAX_WINDOWS]; /* the tiling order */
	int count;
	struct window *focus;
	int ws;                             /* the workspace in view */
	int place_ws;                       /* where new windows go */
	struct workspace wss[WORKSPACES];
	int next_id;

	struct gfx_rect damage;

	int px, py, buttons;     /* the pointer */
	struct window *grab;     /* gets the pointer while a button is held */
	bool alt_next;           /* the next key byte came with Alt */

	bool menu_open;
	int menu_sel;
	bool prompt_open;
	char prompt[PROMPT_MAX + 1];
	char hint[128];          /* what Tab could complete */

	bool quit;
};

extern struct desktop D;
extern const struct menu_item MENU[];
extern const int MENU_COUNT;

/* main.c */
void launch(const char *command);
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* wsys.c */
int wsys_init(int fd);
void window_destroy(struct window *w);
void post_event(struct window *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* tile.c */
void retile(void);
void set_focus(struct window *w);
void focus_step(int delta);
void swap_step(int delta);
void show_workspace(int ws);
void send_to_workspace(struct window *w, int ws);
void next_layout(void);
void grow_master(int delta);
const char *layout_name(enum layout l);
struct gfx_rect tiling_area(void);
struct gfx_rect strip_rect(const struct window *w);
struct gfx_rect close_rect(const struct window *w);
struct window *window_at(int x, int y);
void add_window(struct window *w);
void remove_window(struct window *w);

/* bar.c */
void bar_init(void);
/* Refreshes the facts; true when the bar's text changed. */
bool bar_update(void);
/* The facts, and the date and time. */
const char *bar_left(void);
const char *bar_right(void);

/* draw.c */
void damage(struct gfx_rect r);
void damage_all(void);
void damage_bar(void);
void compose(void);
struct gfx_rect cursor_rect(void);
/* Where the bar shows "ManiDE" (the menu) and workspace ws. */
struct gfx_rect bar_logo_rect(void);
struct gfx_rect bar_ws_rect(int ws);
struct gfx_rect menu_item_rect(int i);
struct gfx_rect menu_rect(void);

/* input.c */
void handle_key(int key, bool alt);
void handle_mouse(int dx, int dy, int buttons);
void open_prompt(void);

#endif
