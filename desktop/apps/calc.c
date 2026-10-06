/* calc -- a calculator, in a window of buttons you click or keys you type.
 *
 * Numbers are fixed-point: whole numbers of up to 11 digits and up to 6
 * decimals, in a 64-bit integer (ManiOS programs have no floating point).
 * Operators work as on a simple pocket calculator: each is done as the
 * next is typed, so 2 + 3 * 4 = is 20. = again with nothing typed uses the
 * result for both sides (2 * = is 4); % is a percentage of what is there
 * (200 + 10 % = is 220, 50 % is 0.5).
 *
 * Keys: 0-9 . + - * / (x too) = or Enter, % , Backspace, c or Escape to
 * clear, n to change the sign. `calc -t` checks the arithmetic, with no
 * window. */
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ui.h>

#define SCALE 1000000LL               /* six decimals */
#define LIMIT (100000000000LL * SCALE - 1) /* 99999999999.999999 */
#define INT_DIGITS 11
#define FRAC_DIGITS 6

struct calc {
	long long acc; /* the left operand, once an operator is pending */
	int op;        /* '+', '-', '*', '/', or 0 */
	char entry[24]; /* the number being typed */
	bool typing;
	long long shown; /* what is displayed when nothing is being typed */
	const char *error;
};

/* --- the arithmetic, on numbers scaled by SCALE --- */

static bool add(long long a, long long b, long long *out)
{
	*out = a + b; /* both within +-LIMIT: no 64-bit overflow */
	return *out <= LIMIT && *out >= -LIMIT;
}

static bool mul(long long a, long long b, long long *out)
{
	bool negative = (a < 0) != (b < 0);
	unsigned long long ua = (unsigned long long)(a < 0 ? -a : a), ub = (unsigned long long)(b < 0 ? -b : b);
	unsigned long long ai = ua / SCALE, af = ua % SCALE, bi = ub / SCALE, bf = ub % SCALE;
	/* a * b / SCALE = ai*bi*SCALE + ai*bf + af*bi + af*bf/SCALE, each
	 * part small enough for 64 bits once ai*bi is known to be. */
	if (ai && bi && ai > (unsigned long long)(LIMIT / SCALE) / bi) {
		return false;
	}
	unsigned long long r = ai * bi * SCALE + ai * bf + af * bi + (af * bf + SCALE / 2) / SCALE;
	if (r > (unsigned long long)LIMIT) {
		return false;
	}
	*out = negative ? -(long long)r : (long long)r;
	return true;
}

static bool divide(long long a, long long b, long long *out)
{
	bool negative = (a < 0) != (b < 0);
	unsigned long long ua = (unsigned long long)(a < 0 ? -a : a), ub = (unsigned long long)(b < 0 ? -b : b);
	unsigned long long q = ua / ub, r = ua % ub, frac = 0;
	if (q > (unsigned long long)(LIMIT / SCALE)) {
		return false;
	}
	for (int i = 0; i < FRAC_DIGITS; i++) { /* long division, a digit at a time */
		r *= 10;
		frac = frac * 10 + r / ub;
		r %= ub;
	}
	if (r * 10 / ub >= 5) {
		frac++; /* rounded; a carry out of the decimals just adds one */
	}
	unsigned long long v = q * SCALE + frac;
	if (v > (unsigned long long)LIMIT) {
		return false;
	}
	*out = negative ? -(long long)v : (long long)v;
	return true;
}

static void format(long long v, char *out, size_t size)
{
	unsigned long long u = (unsigned long long)(v < 0 ? -v : v);
	char text[40];
	int n = snprintf(text, sizeof(text), "%s%llu", v < 0 ? "-" : "", u / SCALE);
	if (u % SCALE) {
		n += snprintf(text + n, sizeof(text) - (size_t)n, ".%06llu", u % SCALE);
		while (text[n - 1] == '0') {
			text[--n] = '\0'; /* no trailing zeros */
		}
	}
	strlcpy(out, text, size);
}

static long long parse(const char *s)
{
	bool negative = *s == '-';
	long long whole = 0, frac = 0, scale = SCALE / 10;
	s += negative;
	for (; *s >= '0' && *s <= '9'; s++) {
		whole = whole * 10 + (*s - '0');
	}
	if (*s == '.') {
		for (s++; *s >= '0' && *s <= '9' && scale; s++, scale /= 10) {
			frac += (*s - '0') * scale;
		}
	}
	long long v = whole * SCALE + frac;
	return negative ? -v : v;
}

/* --- the calculator --- */

static void clear(struct calc *c)
{
	memset(c, 0, sizeof(*c));
}

static long long current(const struct calc *c)
{
	return c->typing ? parse(c->entry) : c->shown;
}

static void fail(struct calc *c, const char *why)
{
	clear(c);
	c->error = why;
}

/* Applies the pending operator to acc and `value`. */
static bool apply(struct calc *c, long long value)
{
	long long r = value;
	bool ok = true;
	switch (c->op) {
	case '+':
		ok = add(c->acc, value, &r);
		break;
	case '-':
		ok = add(c->acc, -value, &r);
		break;
	case '*':
		ok = mul(c->acc, value, &r);
		break;
	case '/':
		if (value == 0) {
			fail(c, "Divide by 0");
			return false;
		}
		ok = divide(c->acc, value, &r);
		break;
	}
	if (!ok) {
		fail(c, "Overflow");
		return false;
	}
	c->acc = c->shown = r;
	return true;
}

static void digit(struct calc *c, int d)
{
	if (!c->typing) {
		if (!c->op) {
			c->acc = 0; /* a new number after a result */
		}
		c->entry[0] = '\0';
		c->typing = true;
	}
	char *dot = strchr(c->entry, '.');
	size_t len = strlen(c->entry);
	size_t whole = (size_t)(dot ? dot - c->entry : (long)len) - (c->entry[0] == '-');
	if ((dot && len - (size_t)(dot - c->entry) - 1 >= FRAC_DIGITS) || (!dot && whole >= INT_DIGITS)) {
		return;
	}
	if (!strcmp(c->entry, "0") || !strcmp(c->entry, "-0")) {
		len--; /* no leading zeros */
	}
	c->entry[len] = (char)d;
	c->entry[len + 1] = '\0';
}

static void point(struct calc *c)
{
	if (!c->typing) {
		if (!c->op) {
			c->acc = 0;
		}
		strlcpy(c->entry, "0", sizeof(c->entry));
		c->typing = true;
	}
	if (!strchr(c->entry, '.') && strlen(c->entry) < sizeof(c->entry) - 2) {
		strlcat(c->entry, ".", sizeof(c->entry));
	}
}

static void operator(struct calc *c, int op)
{
	if (c->typing || !c->op) {
		/* What is shown is the first operand, or the second to the pending
		 * operator, which is done now. (An operator typed straight after
		 * another only replaces it.) */
		long long v = current(c);
		if (c->op) {
			if (!apply(c, v)) {
				return;
			}
		} else {
			c->acc = c->shown = v;
		}
		c->typing = false;
	}
	c->op = op;
}

static void equals(struct calc *c)
{
	if (!c->op) {
		c->shown = current(c);
		c->acc = c->shown;
		c->typing = false;
		return;
	}
	if (apply(c, current(c))) {
		c->op = 0;
		c->typing = false;
	}
}

static void percent(struct calc *c)
{
	long long v = current(c), r = v / 100;
	if (c->op == '+' || c->op == '-') {
		if (!mul(c->acc, v, &r)) {
			fail(c, "Overflow");
			return;
		}
		r /= 100;
	}
	c->shown = r;
	format(r, c->entry, sizeof(c->entry));
	c->typing = true;
}

static void negate(struct calc *c)
{
	if (c->typing) {
		if (strcmp(c->entry, "0")) {
			if (c->entry[0] == '-') {
				memmove(c->entry, c->entry + 1, strlen(c->entry));
			} else if (strlen(c->entry) < sizeof(c->entry) - 2) {
				memmove(c->entry + 1, c->entry, strlen(c->entry) + 1);
				c->entry[0] = '-';
			}
		}
	} else {
		c->shown = -c->shown;
		if (!c->op) {
			c->acc = c->shown;
		}
	}
}

static void backspace(struct calc *c)
{
	size_t len = strlen(c->entry);
	if (c->typing && len) {
		c->entry[len - 1] = '\0';
		if (!strcmp(c->entry, "-")) {
			c->entry[0] = '\0';
		}
		if (!c->entry[0]) {
			strlcpy(c->entry, "0", sizeof(c->entry));
		}
	}
}

/* A key; false if it means nothing to a calculator. */
static bool calc_key(struct calc *c, int key)
{
	bool known = (key >= '0' && key <= '9') || strchr(".,+-*/xX=\n\r%cC\x1b\b\x7fnN", key);
	if (!known || key == 0) {
		return false;
	}
	if (c->error) {
		/* Any key clears an error; a digit goes on to start a number. */
		c->error = NULL;
		if (!(key >= '0' && key <= '9') && key != '.' && key != ',') {
			return true;
		}
	}
	if (key >= '0' && key <= '9') {
		digit(c, key);
	} else if (key == '.' || key == ',') {
		point(c);
	} else if (strchr("+-*/", key)) {
		operator(c, key);
	} else if (key == 'x' || key == 'X') {
		operator(c, '*');
	} else if (key == '=' || key == '\n' || key == '\r') {
		equals(c);
	} else if (key == '%') {
		percent(c);
	} else if (key == 'c' || key == 'C' || key == 0x1B) {
		clear(c);
	} else if (key == '\b' || key == 0x7F) {
		backspace(c);
	} else {
		negate(c);
	}
	return true;
}

static void calc_text(const struct calc *c, char *out, size_t size)
{
	if (c->error) {
		strlcpy(out, c->error, size);
	} else if (c->typing) {
		strlcpy(out, c->entry, size);
	} else {
		format(c->shown, out, size);
	}
}

/* What is waiting for its second operand: "12 +". */
static void calc_pending(const struct calc *c, char *out, size_t size)
{
	if (c->op && !c->error) {
		char acc[32];
		format(c->acc, acc, sizeof(acc));
		snprintf(out, size, "%s %c", acc, c->op);
	} else {
		strlcpy(out, "", size);
	}
}

/* --- the check --- */

static int checks, failures;

static void expect(const char *keys, const char *shown)
{
	struct calc c;
	char text[40];
	clear(&c);
	for (const char *k = keys; *k; k++) {
		calc_key(&c, *k);
	}
	calc_text(&c, text, sizeof(text));
	checks++;
	if (strcmp(text, shown)) {
		failures++;
		printf("calc: FAIL: %s shows %s, not %s\n", keys, text, shown);
	}
}

static int self_test(void)
{
	expect("", "0");
	expect("7*8=", "56");
	expect("1/3=", "0.333333");
	expect("2/3=", "0.666667");
	expect("10/4=", "2.5");
	expect("0.1+0.2=", "0.3");
	expect("5-8=", "-3");
	expect("1/0=", "Divide by 0");
	expect("1/0=5", "5");
	expect("1/0=+", "0");
	expect("99999999999+1=", "Overflow");
	expect("99999999999*10=", "Overflow");
	expect("68719476736*68719476736=", "Overflow"); /* 2^36 squared wraps 64 bits to 0 */
	expect("99999999999/0.000001=", "Overflow");
	expect("37000000/0.000002=", "Overflow"); /* q * SCALE wraps 64 bits to a valid-looking number */
	expect("99999999999.999999+0=", "99999999999.999999");
	expect("12345679*9=", "111111111");
	expect("2*3+4=", "10");
	expect("2+3*4=", "20");
	expect("200+10%=", "220");
	expect("200-10%=", "180");
	expect("200*10%=", "20");
	expect("50%", "0.5");
	expect("5n*3=", "-15");
	expect("5n", "-5");
	expect("5=n", "-5");
	expect("123\b\b", "1");
	expect("1\b\b", "0");
	expect(".5*2=", "1");
	expect("1.5.5", "1.55");
	expect("0000", "0");
	expect("00012", "12");
	expect("3=", "3");
	expect("3+=", "6");
	expect("2*=", "4");
	expect("5+3c", "0");
	expect("5+3c7", "7");
	expect("2+2=5", "5");
	expect("2+2=5+3=", "8");
	expect("2+2=*3=", "12");
	expect("2+*3=", "6");
	expect("3.141593*2=", "6.283186");
	expect("0.000001*0.000001=", "0");
	expect("0.5*0.5=", "0.25");
	expect("99999999999", "99999999999");
	expect("123456789012", "12345678901"); /* the 12th digit is refused */
	expect("1.1234567", "1.123456");
	expect("100/8/2=", "6.25");
	expect("1-1=", "0");
	expect("0-5=", "-5");
	expect("-5+2=", "-3");
	expect("7x6=", "42");
	if (failures) {
		printf("calc: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("calc: all %d checks passed\n", checks);
	return 0;
}

/* --- the window --- */

struct app {
	struct calc calc;
	struct ui_widget *display, *pending;
};

static void show(struct app *app)
{
	char text[40], pending[40];
	calc_text(&app->calc, text, sizeof(text));
	calc_pending(&app->calc, pending, sizeof(pending));
	ui_set_text(app->display, text);
	ui_set_text(app->pending, pending);
	ui_set_text_color(app->display, app->calc.error ? ui_theme.accent : ui_theme.text);
}

static void pressed(struct ui_widget *button, void *arg)
{
	struct app *app = arg;
	calc_key(&app->calc, (int)(long)button->user);
	show(app);
}

static bool typed(struct ui *ui, int key, void *arg)
{
	struct app *app = arg;
	(void)ui;
	bool used = calc_key(&app->calc, key);
	if (used) {
		show(app);
	}
	return used;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "-t")) {
		return self_test();
	}
	if (argc > 1) {
		fprintf(stderr, "usage: calc [-t]\n");
		return 1;
	}
	static struct app app;
	struct ui *ui = ui_open("Calculator", 200, 280);
	if (!ui) {
		fprintf(stderr, "calc: no window: %s\n", strerror(errno));
		return 1;
	}
	struct ui_widget *root = ui_root(ui);
	app.pending = ui_label(root, "");
	ui_set_align(app.pending, UI_RIGHT);
	ui_set_text_color(app.pending, ui_theme.dim);
	app.display = ui_label(root, "0");
	ui_set_scale(app.display, 2);
	ui_set_align(app.display, UI_RIGHT);
	ui_set_colors(app.display, ui_theme.field, ui_theme.edge);
	ui_set_size(app.display, 0, 36);

	static const struct { const char *label; int key; } KEYS[] = {
		{ "C", 'c' }, { "Del", '\b' }, { "%", '%' }, { "/", '/' },
		{ "7", '7' }, { "8", '8' }, { "9", '9' }, { "*", '*' },
		{ "4", '4' }, { "5", '5' }, { "6", '6' }, { "-", '-' },
		{ "1", '1' }, { "2", '2' }, { "3", '3' }, { "+", '+' },
		{ "+/-", 'n' }, { "0", '0' }, { ".", '.' }, { "=", '=' },
	};
	struct ui_widget *grid = ui_grid(root, 4, 4);
	ui_set_expand(grid, 1);
	for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
		struct ui_widget *b = ui_button(grid, KEYS[i].label, pressed, &app);
		if (!b) {
			continue;
		}
		b->user = (void *)(long)KEYS[i].key;
		ui_set_scale(b, KEYS[i].label[1] ? 1 : 2);
		ui_set_focusable(b, false);
		if (strchr("/*-+", KEYS[i].key)) {
			ui_set_text_color(b, ui_theme.focus);
		}
		if (KEYS[i].key == '=') {
			ui_set_text_color(b, ui_theme.accent);
		}
	}
	clear(&app.calc);
	ui_on_key(ui, typed, &app);
	ui_run(ui);
	ui_free(ui);
	return 0;
}
